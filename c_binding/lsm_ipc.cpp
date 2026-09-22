/*
 * SPDX-License-Identifier: LGPL-2.1-or-later
 *
 * Copyright (C) 2011-2023 Red Hat, Inc.
 *
 * Author: Tony Asleson <tasleson@redhat.com>
 */

#include "lsm_ipc.hpp"

#include "libstoragemgmt/libstoragemgmt_plug_interface.h"

#include <algorithm>
#include <errno.h>
#include <iomanip>
#include <iostream>
#include <limits.h>
#include <list>
#include <poll.h>
#include <sstream>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <time.h>
#include <unistd.h>

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include "lsm_value_jsmn.hpp"

static std::string zero_pad_num(unsigned int num) {
    std::ostringstream ss;
    ss << std::setw(Transport::HDR_LEN) << std::setfill('0') << num;
    return ss.str();
}

Transport::Transport() : s(-1), io_deadline_active(false) {
    memset(&io_deadline_ts, 0, sizeof(io_deadline_ts));
}

Transport::Transport(int socket_desc)
    : s(socket_desc), io_deadline_active(false) {
    memset(&io_deadline_ts, 0, sizeof(io_deadline_ts));
}

// Largest value a signed time_t can hold. Linux time_t is signed; keeping
// io_deadline() below this avoids signed overflow when a large timeout is
// added to the current time.
static const time_t TIME_T_MAX =
    (time_t)(((uintmax_t)1 << (sizeof(time_t) * 8 - 1)) - 1);

// How much of 'deadline' is left, in milliseconds suitable for poll().
// Returns false with error_code set to EAGAIN once the deadline has passed,
// or to errno if the clock could not be read. A clock failure is not a
// timeout: reporting it as one makes a plug-in exit as though its client had
// never registered, and hides the real error. A zeroed remainder is rounded
// up to 1ms, since poll() treats a 0 timeout as "don't wait at all" rather
// than "the last sliver of time". A remainder too large for an int is capped
// at INT_MAX: narrowing it would otherwise go negative, and a negative
// timeout means "block forever" to poll().
static bool time_remaining_ms(const struct timespec &deadline, int &ms,
                              int &error_code) {
    struct timespec now;

    if (-1 == clock_gettime(CLOCK_MONOTONIC, &now)) {
        error_code = errno;
        return false;
    }

    time_t secs = deadline.tv_sec - now.tv_sec;
    long nsecs = deadline.tv_nsec - now.tv_nsec;
    if (nsecs < 0) {
        secs -= 1;
        nsecs += 1000000000L;
    }

    if (secs < 0) {
        error_code = EAGAIN; // Mapped to TimeoutException by callers.
        return false;
    }

    if (secs >= INT_MAX / 1000) {
        ms = INT_MAX;
        return true;
    }
    int64_t millis = (int64_t)secs * 1000 + (nsecs + 999999) / 1000000;
    ms = millis > INT_MAX ? INT_MAX : (millis > 0 ? (int)millis : 1);

    return true;
}

// Waits up to 'deadline' (an absolute CLOCK_MONOTONIC time) for 'fd' to
// become ready for 'events' (POLLIN or POLLOUT). nullptr leaves the wait
// unbounded.
//
// Returns true when the fd is ready. That includes a pending error or hangup
// (POLLERR/POLLHUP), which the following recv()/send() will report as an
// errno or EOF. Otherwise returns false with error_code set to:
//   EAGAIN - the deadline expired, before or during the wait
//   EBADF  - 'fd' is not an open descriptor (POLLNVAL)
//   EINTR  - poll() was interrupted by a signal. It is not retried here; the
//            caller should loop. Because the deadline is absolute, each retry
//            recomputes the remaining time, so retries never extend the wait.
//   other  - errno from poll() or from reading the clock
// error_code is set to 0 on success.
//
// Why poll() rather than SO_RCVTIMEO/SO_SNDTIMEO: the plug-in runs in the
// lsmd_plugin_t SELinux domain after exec() and is not permitted "setopt" on
// a socket it did not create, so arming a timeout with setsockopt() on the
// client fd would be denied. poll() only needs to read/write the fd it is
// already handed, so it works regardless of which domain created the socket.
//
// Readiness alone does not bound the I/O call. On a blocking stream socket,
// POLLOUT means only that *some* buffer space is free, and a large send()
// would still block until everything is queued. Callers must therefore:
//   - pass MSG_DONTWAIT to recv()/send(). It is a per-call flag, so it needs
//     neither setsockopt() nor fcntl(O_NONBLOCK) on the shared open file
//     description;
//   - treat EAGAIN/EWOULDBLOCK from the I/O call as a cue to call wait_ready()
//     again, and loop on partial sends;
//   - pass MSG_NOSIGNAL to send(), so a hung-up peer yields EPIPE instead of
//     a SIGPIPE that would kill the plug-in.
//
// Requires time_remaining_ms() to round up to whole milliseconds (so a sub-ms
// remainder doesn't expire early), clamp to INT_MAX (a negative timeout means
// "forever" to poll()), and read CLOCK_MONOTONIC.
static bool wait_ready(int fd, short events, const struct timespec *deadline,
                       int &error_code) {
    int timeout_ms = -1; // No deadline: block indefinitely, as poll() allows.

    if (deadline != nullptr &&
        !time_remaining_ms(*deadline, timeout_ms, error_code)) {
        return false; // EAGAIN on expiry, else the clock's errno.
    }

    struct pollfd pfd = {}; // POSIX doesn't fix member order; assign by name.
    pfd.fd = fd;
    pfd.events = events;

    int rc = poll(&pfd, 1, timeout_ms);
    if (rc == 0) {
        error_code = EAGAIN; // Deadline expired waiting for the fd.
        return false;
    }
    if (rc < 0) {
        error_code = errno; // Includes EINTR; see above.
        return false;
    }
    if (pfd.revents & POLLNVAL) {
        error_code = EBADF;
        return false;
    }

    // Ready for 'events', or POLLERR/POLLHUP. Either way the I/O call is the
    // right place to learn the outcome.
    error_code = 0;
    return true;
}

// A blocking send() on a peer that has stopped reading our replies is just
// as effective a way to pin a worker as a stalled recv(), so the caller's
// deadline has to cover this loop too. Each send() is preceded by a
// wait_ready() for whatever is left of it, mirroring string_read(); NULL
// leaves the write unbounded.
//
// send() also passes MSG_DONTWAIT. POLLOUT only guarantees SO_SNDLOWAT
// (typically one byte) of space, so a blocking send() of a message larger
// than the free send buffer would block inside the kernel, past the
// deadline, until the peer reads - exactly the pin this deadline exists to
// prevent. MSG_DONTWAIT, like poll(), is a per-call flag needing no
// setsockopt(), so it works in the plug-in's SELinux domain. send() queues
// what it can and returns the count; a short write leaves the loop to
// wait_ready() again, and an EAGAIN with the deadline expired ends the
// message as a timeout.
//
// A timeout, or any other failure, after a partial write leaves the
// connection unusable, so giving up here is the only sane thing to do;
// callers map EAGAIN to TimeoutException.
int Transport::msg_send(const std::string &msg, int &error_code) {
    error_code = 0;
    if (msg.empty()) {
        error_code = EINVAL;
        return -1;
    }

    const std::string data = zero_pad_num(msg.size()) + msg;
    const size_t total = data.size();
    size_t written = 0;
    const struct timespec *deadline =
        io_deadline_active ? &io_deadline_ts : nullptr;

    while (written < total) {
        ssize_t wrote = send(s, data.data() + written, total - written,
                             MSG_NOSIGNAL | MSG_DONTWAIT);
        if (wrote > 0) {
            written += static_cast<size_t>(wrote);
            continue;
        }
        if (wrote < 0 && errno != EAGAIN && errno != EWOULDBLOCK &&
            errno != EINTR) {
            error_code = errno;
            break;
        }

        // Nothing sent: wait for writability (re-checks the deadline).
        int wait_err = 0;
        if (!wait_ready(s, POLLOUT, deadline, wait_err)) {
            if (wait_err == EINTR) {
                continue;
            }
            error_code = wait_err ? wait_err : EIO;
            break;
        }
    }

    if (written == total) {
        return 0;
    }
    // A partial frame may be on the wire; the connection is no longer usable.
    // Consider closing/marking the socket broken here.
    return -1;
}

// A wait_ready() bounds a single recv() call, not the time spent reading a
// whole message, and not the time spent across messages either. A peer that
// trickles data in, always inside the timeout but slower than we would like,
// would otherwise keep this loop making "forward progress" forever. Each
// read is instead given whatever is left of the caller's deadline; NULL
// leaves the read unbounded.
static std::string string_read(int fd, ssize_t count, int &error_code,
                               const struct timespec *deadline) {
    char buff[4096];
    ssize_t amount_read = 0;
    std::string rc = "";

    error_code = 0;

    while (amount_read < count) {
        if (!wait_ready(fd, POLLIN, deadline, error_code)) {
            if (error_code == EINTR) {
                continue;
            }
            break;
        }

        ssize_t rd = recv(
            fd, buff, std::min((ssize_t)(sizeof(buff)), (count - amount_read)),
            MSG_DONTWAIT);
        if (rd > 0) {
            ssize_t t = amount_read;
            if (__builtin_add_overflow(t, rd, &amount_read)) {
                error_code = EOVERFLOW;
                break;
            }
            rc += std::string(buff, rd);
        } else if (rd == 0) {
            throw EOFException("");
        } else if (errno == EAGAIN || errno == EWOULDBLOCK || errno == EINTR) {
            continue; // Spurious wakeup; wait_ready() re-checks the deadline.
        } else {
            error_code = errno;
            break;
        }
    }

    return rc;
}

std::string Transport::msg_recv(int &error_code) {
    std::string msg;
    error_code = 0;
    unsigned long int payload_len = 0;

    // One absolute deadline spans the header, the payload and every message
    // that follows; re-arming it here would hand a slow or chatty peer the
    // configured timeout over and over again.
    const struct timespec *dl = io_deadline_active ? &io_deadline_ts : NULL;

    std::string len = string_read(s, HDR_LEN, error_code, dl); // Read length
    if (len.size() && error_code == 0) {
        payload_len = strtoul(len.c_str(), NULL, 10);
        if (payload_len < 0x80000000) { /* Should be big enough */
            ssize_t len = payload_len;
            msg = string_read(s, len, error_code, dl);
        } else {
            error_code = EOVERFLOW;
        }
        // fprintf(stderr, "<<< %s\n", msg.c_str());
    }
    return msg;
}

int Transport::socket_get(const std::string &path, int &error_code) {
    int sfd = socket(AF_UNIX, SOCK_STREAM, 0);
    int rc = -1;
    error_code = 0;

    if (sfd != -1) {
        struct sockaddr_un addr;

        memset(&addr, 0, sizeof(addr));
        addr.sun_family = AF_UNIX;
        strncpy(addr.sun_path, path.c_str(), sizeof(addr.sun_path) - 1);

        // Connect
        rc = connect(sfd, (struct sockaddr *)&addr, sizeof(addr));
        if (rc != 0) {
            error_code = errno;
            rc = -1; // Redundant, connect should set to -1 on error
            ::close(sfd);
        } else {
            rc = sfd; // We are good to go.
        }
    }
    return rc;
}

int Transport::io_deadline(int seconds) {
    if (seconds > 0) {
        if (-1 == clock_gettime(CLOCK_MONOTONIC, &io_deadline_ts)) {
            return errno;
        }
        if ((time_t)seconds > TIME_T_MAX - io_deadline_ts.tv_sec) {
            io_deadline_ts.tv_sec = TIME_T_MAX;
        } else {
            io_deadline_ts.tv_sec += seconds;
        }
        io_deadline_active = true;
        // No socket option is touched here; string_read()/msg_send() poll()
        // for whatever is left of the deadline before each read and write.
        return 0;
    }

    // Nothing to undo: string_read()/msg_send() only ever consult
    // io_deadline_active and io_deadline_ts, never SO_RCVTIMEO/SO_SNDTIMEO,
    // so there is no kernel-level state of ours to clear. Even if this
    // descriptor carried a pre-existing socket-level timeout from whoever
    // handed it to us, that would be harmless: a blocking recv()/send()
    // would honor it, yet we never issue one - every read and write here
    // waits on the fd with poll() first, which ignores it.
    io_deadline_active = false;
    return 0;
}

Transport::~Transport() { close(); }

void Transport::close() {
    if (s >= 0) {
        ::close(s);
        // Regardless, clear out socket
        s = -1;
    }
}

EOFException::EOFException(std::string m) : std::runtime_error(m) {}

TimeoutException::TimeoutException(std::string m) : std::runtime_error(m) {}

ValueException::ValueException(std::string m) : std::runtime_error(m) {}

LsmException::LsmException(int code, std::string &msg)
    : std::runtime_error(msg), error_code(code) {}

LsmException::LsmException(int code, std::string &msg,
                           const std::string &debug_addl)
    : std::runtime_error(msg), error_code(code), debug(debug_addl) {}

LsmException::~LsmException() throw() {}

LsmException::LsmException(int code, std::string &msg,
                           const std::string &debug_addl,
                           const std::string &debug_data_addl)
    : std::runtime_error(msg), error_code(code), debug(debug_addl),
      debug_data(debug_data_addl) {}

Ipc::Ipc() {}

Ipc::Ipc(int fd) : t(fd) {}

Ipc::~Ipc() { t.close(); }

// A send that hits the deadline has to end a plug-in's session the same way
// a read that hits it does, so it surfaces as TimeoutException rather than
// as a generic transport failure.
static void throw_send_failure(int error_code, const std::string &what) {
    if (error_code == EAGAIN || error_code == EWOULDBLOCK) {
        throw TimeoutException(std::string("Timed out ") + what);
    }

    std::string em =
        std::string("Error ") + what + ": errno " + ::to_string(error_code);
    throw LsmException((int)LSM_ERR_TRANSPORT_COMMUNICATION, em);
}

void Ipc::requestSend(const std::string request, const Value &params,
                      int32_t id) {
    int rc = 0;
    int ec = 0;
    std::map<std::string, Value> v;

    v["method"] = Value(request);
    v["id"] = Value(id);
    v["params"] = params;

    Value req(v);
    rc = t.msg_send(Payload::serialize(req), ec);

    if (rc != 0) {
        throw_send_failure(ec, "sending message");
    }
}

void Ipc::errorSend(int error_code, std::string msg, std::string debug,
                    uint32_t id) {
    int ec = 0;
    int rc = 0;
    std::map<std::string, Value> v;
    std::map<std::string, Value> error_data;

    error_data["code"] = Value(error_code);
    error_data["message"] = Value(msg);
    error_data["data"] = Value(debug);

    v["error"] = Value(error_data);
    v["id"] = Value(id);

    Value e(v);
    rc = t.msg_send(Payload::serialize(e), ec);

    if (rc != 0) {
        throw_send_failure(ec, "sending error message");
    }
}

int Ipc::io_deadline(int seconds) { return t.io_deadline(seconds); }

Value Ipc::readRequest(void) {
    int ec;
    std::string resp = t.msg_recv(ec);
    if (ec != 0) {
        if (ec == EAGAIN || ec == EWOULDBLOCK) {
            throw TimeoutException("Timed out reading message");
        }
        std::string em =
            std::string("Error reading message: errno ") + ::to_string(ec);
        throw LsmException((int)LSM_ERR_TRANSPORT_COMMUNICATION, em);
    }
    return Payload::deserialize(resp);
}

void Ipc::responseSend(const Value &response, uint32_t id) {
    int rc;
    int ec;
    std::map<std::string, Value> v;

    v["id"] = id;
    v["result"] = response;

    Value resp(v);
    rc = t.msg_send(Payload::serialize(resp), ec);

    if (rc != 0) {
        throw_send_failure(ec, "sending response");
    }
}

Value Ipc::responseRead() {
    Value r = readRequest();
    if (r.hasKey(std::string("result"))) {
        return r.getValue("result");
    } else {
        std::map<std::string, Value> rp = r.asObject();
        std::map<std::string, Value> error = rp["error"].asObject();

        std::string msg = error["message"].asString();
        std::string data = error["data"].asString();
        throw LsmException((int)(error["code"].asInt32_t()), msg, data);
    }
}

Value Ipc::rpc(const std::string &request, const Value &params, int32_t id) {
    requestSend(request, params, id);
    return responseRead();
}
