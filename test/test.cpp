/*
 * SPDX-License-Identifier: LGPL-2.1-or-later
 *
 * Copyright (C) 2011-2023 Red Hat, Inc.
 */

#include "../c_binding/lsm_ipc.hpp"
#include "libstoragemgmt/libstoragemgmt_error.h"

#include <csignal>
#include <cstdint>
#include <cstring>
#include <ctime>
#include <iomanip>
#include <sstream>
#include <sys/socket.h>
#include <thread>
#include <unistd.h>

/* check.h's "fail" macro clashes with std::basic_ios::fail(), so it must be
 * included only after all standard library headers are pulled in. */
#include <check.h>

/*
 * Milliseconds on CLOCK_MONOTONIC, for elapsed time assertions.
 */
static int64_t monotonic_ms(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (int64_t)ts.tv_sec * 1000 + ts.tv_nsec / 1000000;
}

/*
 * Writes a raw, zero-padded header declaring a payload of 'declared_len'
 * bytes, without ever writing a matching payload.  Used to exercise the
 * oversized message guard in Transport::msg_recv() / Ipc::readRequest().
 */
static void send_oversized_header(int fd, unsigned long declared_len) {
    std::ostringstream ss;
    ss << std::setw(Transport::HDR_LEN) << std::setfill('0') << declared_len;
    std::string hdr = ss.str();
    ssize_t written = write(fd, hdr.c_str(), hdr.size());
    ck_assert_int_eq(written, (ssize_t)hdr.size());
}

START_TEST(test_oversized_message_rejected) {
    int fds[2];
    ck_assert_int_eq(socketpair(AF_UNIX, SOCK_STREAM, 0, fds), 0);

    /* Declare a payload at the overflow guard boundary (matches the
     * 0x80000000 check in Transport::msg_recv). */
    send_oversized_header(fds[1], 0x80000000UL);
    close(fds[1]);

    Ipc ipc(fds[0]);
    bool caught = false;

    try {
        ipc.readRequest();
    } catch (const LsmException &e) {
        caught = true;
        ck_assert_int_eq(e.error_code, (int)LSM_ERR_TRANSPORT_COMMUNICATION);
    }

    ck_assert_msg(caught,
                  "Expected LsmException(LSM_ERR_TRANSPORT_COMMUNICATION) "
                  "for an oversized message");
}
END_TEST

START_TEST(test_undersized_message_accepted) {
    int fds[2];
    ck_assert_int_eq(socketpair(AF_UNIX, SOCK_STREAM, 0, fds), 0);

    /* One byte under the overflow guard boundary should still attempt to
     * read the (non-existent) payload rather than being rejected outright,
     * so closing the peer results in an EOF, not a size related error. */
    send_oversized_header(fds[1], 0x80000000UL - 1);
    close(fds[1]);

    Ipc ipc(fds[0]);
    bool caught_eof = false;

    try {
        ipc.readRequest();
    } catch (const EOFException &) {
        caught_eof = true;
    } catch (const LsmException &e) {
        ck_assert_msg(false,
                      "Unexpected LsmException(%d) for a message just under "
                      "the size limit",
                      e.error_code);
    }

    ck_assert_msg(caught_eof, "Expected EOFException reading a truncated, "
                              "but not oversized, message");
}
END_TEST

START_TEST(test_io_deadline_fires) {
    int fds[2];
    ck_assert_int_eq(socketpair(AF_UNIX, SOCK_STREAM, 0, fds), 0);

    Ipc ipc(fds[0]);
    ck_assert_int_eq(ipc.io_deadline(1), 0);

    /* Peer stays connected but never writes; readRequest() must time out
     * rather than block forever (the DoS this guards against). */
    bool caught_timeout = false;
    time_t start = time(NULL);
    try {
        ipc.readRequest();
    } catch (const TimeoutException &) {
        caught_timeout = true;
    }
    time_t elapsed = time(NULL) - start;

    close(fds[1]);

    ck_assert_msg(caught_timeout,
                  "Expected TimeoutException when the peer sends nothing");
    ck_assert_msg(elapsed < 5,
                  "io_deadline did not fire promptly (elapsed %ld s)",
                  (long)elapsed);
}
END_TEST

START_TEST(test_io_deadline_partial_header) {
    int fds[2];
    ck_assert_int_eq(socketpair(AF_UNIX, SOCK_STREAM, 0, fds), 0);

    /* Write fewer bytes than a full header, then stall.  This exercises the
     * MSG_WAITALL partial-read-then-EAGAIN path: it must map to a timeout,
     * not a hang or a misframed message. */
    const char partial[] = "00";
    ssize_t written = write(fds[1], partial, sizeof(partial) - 1);
    ck_assert_int_eq(written, (ssize_t)(sizeof(partial) - 1));
    ck_assert_int_lt((int)(sizeof(partial) - 1), Transport::HDR_LEN);

    Ipc ipc(fds[0]);
    ck_assert_int_eq(ipc.io_deadline(1), 0);

    bool caught_timeout = false;
    try {
        ipc.readRequest();
    } catch (const TimeoutException &) {
        caught_timeout = true;
    }

    close(fds[1]);

    ck_assert_msg(caught_timeout,
                  "Expected TimeoutException on a stalled partial header");
}
END_TEST

START_TEST(test_io_deadline_cleared) {
    int fds[2];
    ck_assert_int_eq(socketpair(AF_UNIX, SOCK_STREAM, 0, fds), 0);

    /* A complete, valid request is buffered by the socketpair, so a
     * single-threaded write-then-read is sufficient. */
    Ipc peer(fds[1]);
    peer.requestSend("systems", Value());

    Ipc ipc(fds[0]);
    /* Arm then clear the deadline; a cleared deadline must not spuriously
     * fire on a message that is actually available. */
    ck_assert_int_eq(ipc.io_deadline(1), 0);
    ck_assert_int_eq(ipc.io_deadline(0), 0);

    bool caught = false;
    std::string method;
    try {
        Value req = ipc.readRequest();
        ck_assert_msg(req.isValidRequest(), "Expected a valid request");
        method = req["method"].asString();
    } catch (...) {
        caught = true;
    }

    ck_assert_msg(!caught, "readRequest threw after the deadline was cleared");
    ck_assert_str_eq(method.c_str(), "systems");
}
END_TEST

START_TEST(test_io_deadline_drip) {
    int fds[2];
    ck_assert_int_eq(socketpair(AF_UNIX, SOCK_STREAM, 0, fds), 0);

    Ipc ipc(fds[0]);
    ck_assert_int_eq(ipc.io_deadline(1), 0);

    /* Peer declares a 50 byte payload, then keeps sending a trickle of
     * payload bytes one at a time, each arriving well inside the configured
     * timeout, but never enough to complete the declared payload. The
     * timeout must bound the whole read, not just each individual recv()
     * call, otherwise this drip defeats it and readRequest() never returns. */
    std::thread dripper([&]() {
        const char hdr[] = "0000000050";
        if (write(fds[1], hdr, sizeof(hdr) - 1) != (ssize_t)sizeof(hdr) - 1)
            return;
        for (int i = 0; i < 8; i++) {
            if (write(fds[1], "x", 1) != 1)
                break;
            usleep(150000); /* 150ms, well under the 1s deadline */
        }
    });

    bool caught_timeout = false;
    time_t start = time(NULL);
    try {
        ipc.readRequest();
    } catch (const TimeoutException &) {
        caught_timeout = true;
    }
    time_t elapsed = time(NULL) - start;

    dripper.join();
    close(fds[1]);

    ck_assert_msg(caught_timeout,
                  "Expected TimeoutException despite steady forward progress");
    ck_assert_msg(elapsed < 3,
                  "deadline did not bound the overall read (elapsed %ld s)",
                  (long)elapsed);
}
END_TEST

START_TEST(test_io_deadline_spans_messages) {
    int fds[2];
    ck_assert_int_eq(socketpair(AF_UNIX, SOCK_STREAM, 0, fds), 0);

    /* A deadline that restarted on every message would let an un-registered
     * peer hold a plug-in forever by sending one junk-but-parseable request
     * just inside each window.  Reading a complete message must not buy any
     * more time, so the second read has to expire at the original deadline. */
    Ipc peer(fds[1]);
    Ipc ipc(fds[0]);
    ck_assert_int_eq(ipc.io_deadline(1), 0);

    int64_t start = monotonic_ms();

    /* The request arrives late in the deadline (and is buffered by the
     * socketpair), so a deadline that restarted here would buy the peer a
     * whole extra window. */
    usleep(800000);
    peer.requestSend("systems", Value());
    Value req = ipc.readRequest();
    ck_assert_msg(req.isValidRequest(), "Expected a valid first request");

    bool caught_timeout = false;
    try {
        ipc.readRequest(); /* Peer has gone quiet. */
    } catch (const TimeoutException &) {
        caught_timeout = true;
    }
    int64_t elapsed = monotonic_ms() - start;

    ck_assert_msg(caught_timeout,
                  "Expected TimeoutException on the read after a successful "
                  "one");
    ck_assert_msg(elapsed < 1500,
                  "deadline restarted after a successful read (elapsed %lld "
                  "ms)",
                  (long long)elapsed);
}
END_TEST

START_TEST(test_io_deadline_spans_header_and_payload) {
    int fds[2];
    ck_assert_int_eq(socketpair(AF_UNIX, SOCK_STREAM, 0, fds), 0);

    Ipc ipc(fds[0]);
    ck_assert_int_eq(ipc.io_deadline(2), 0);

    /* The header lands just inside the deadline and the peer then stalls.
     * The payload read must inherit what is left of the deadline rather than
     * getting a fresh SO_RCVTIMEO of its own, which would stretch a single
     * message to roughly twice the configured bound. */
    std::thread late_header([&]() {
        usleep(1700000); /* 1.7s, inside the 2s deadline */
        const char hdr[] = "0000000050";
        if (write(fds[1], hdr, sizeof(hdr) - 1) != (ssize_t)sizeof(hdr) - 1)
            return;
    });

    int64_t start = monotonic_ms();
    bool caught_timeout = false;
    try {
        ipc.readRequest();
    } catch (const TimeoutException &) {
        caught_timeout = true;
    }
    int64_t elapsed = monotonic_ms() - start;

    late_header.join();
    close(fds[1]);

    ck_assert_msg(caught_timeout,
                  "Expected TimeoutException on a stalled payload");
    ck_assert_msg(elapsed < 3000,
                  "payload read started a fresh timeout (elapsed %lld ms)",
                  (long long)elapsed);
}
END_TEST

/*
 * A send that is not bounded would hang this test forever (the suite runs
 * with CK_FORK=no, so check's own per-test timeout cannot save us).  The
 * watchdog shuts the socket down from a SIGALRM handler instead, which turns
 * a blocked send() into an EPIPE the assertions below can report.
 */
static volatile sig_atomic_t watchdog_fd = -1;

static void watchdog_handler(int sig) {
    (void)sig;
    if (watchdog_fd != -1) {
        shutdown(watchdog_fd, SHUT_RDWR);
    }
}

/* Arms the watchdog on 'fd'; 0 seconds cancels it. */
static void watchdog(unsigned int seconds, int fd) {
    struct sigaction sa;

    memset(&sa, 0, sizeof(sa));
    sa.sa_handler = seconds ? watchdog_handler : SIG_DFL;
    sa.sa_flags = 0;
    sigaction(SIGALRM, &sa, NULL);
    watchdog_fd = seconds ? fd : -1;
    alarm(seconds);
}

START_TEST(test_io_deadline_bounds_send) {
    int fds[2];
    ck_assert_int_eq(socketpair(AF_UNIX, SOCK_STREAM, 0, fds), 0);

    Ipc ipc(fds[0]);
    ck_assert_int_eq(ipc.io_deadline(1), 0);

    /* The peer stays connected but never reads, so the socket buffer fills
     * and send() blocks.  An un-registered client that chatters requests and
     * ignores the replies pins a worker exactly this way, so the deadline has
     * to cover writes as well as reads. */
    bool caught_timeout = false;
    bool caught_other = false;
    watchdog(5, fds[0]);
    int64_t start = monotonic_ms();
    try {
        for (int i = 0; i < 64; i++) {
            ipc.responseSend(Value(std::string(64 * 1024, 'x')));
        }
    } catch (const TimeoutException &) {
        caught_timeout = true;
    } catch (...) {
        caught_other = true;
    }
    int64_t elapsed = monotonic_ms() - start;
    watchdog(0, -1);

    close(fds[1]);

    ck_assert_msg(!caught_other,
                  "Expected TimeoutException; got another exception, i.e. the "
                  "watchdog had to tear the socket down");
    ck_assert_msg(caught_timeout,
                  "send to a peer that never reads was not bounded");
    /* One send() can overshoot the deadline, so allow for that here. */
    ck_assert_msg(elapsed < 4000,
                  "deadline did not bound the send (elapsed %lld ms)",
                  (long long)elapsed);
}
END_TEST

START_TEST(test_io_deadline_cleared_ignores_kernel_timeout) {
    int fds[2];
    ck_assert_int_eq(socketpair(AF_UNIX, SOCK_STREAM, 0, fds), 0);

    /* Transport::io_deadline() must never depend on SO_RCVTIMEO/SO_SNDTIMEO:
     * the plug-in runs in the lsmd_plugin_t SELinux domain after exec() and
     * is not permitted "setopt" on a socket it did not create, so it could
     * not rely on clearing a pre-existing kernel-level timeout even if one
     * were present. Simulate that (whatever its source) and confirm it has
     * no effect. */
    struct timeval one_sec = {1, 0};
    ck_assert_int_eq(
        setsockopt(fds[0], SOL_SOCKET, SO_RCVTIMEO, &one_sec, sizeof(one_sec)),
        0);
    ck_assert_int_eq(
        setsockopt(fds[0], SOL_SOCKET, SO_SNDTIMEO, &one_sec, sizeof(one_sec)),
        0);

    Ipc ipc(fds[0]);
    ck_assert_int_eq(ipc.io_deadline(1), 0);
    ck_assert_int_eq(ipc.io_deadline(0), 0);

    /* A reply arriving well after the leftover 1s kernel timeout must still
     * be read normally, proving readRequest() neither touches nor depends
     * on SO_RCVTIMEO/SO_SNDTIMEO. */
    std::thread late_writer([&]() {
        usleep(1500000);
        Ipc peer(fds[1]);
        peer.requestSend("systems", Value());
    });

    bool caught = false;
    std::string method;
    try {
        Value req = ipc.readRequest();
        method = req["method"].asString();
    } catch (...) {
        caught = true;
    }
    late_writer.join();

    ck_assert_msg(!caught,
                  "readRequest threw despite a cleared deadline, likely "
                  "still bounded by the leftover kernel-level timeout");
    ck_assert_str_eq(method.c_str(), "systems");
}
END_TEST

static Suite *ipc_suite(void) {
    Suite *s = suite_create("ipc");
    TCase *tc = tcase_create("core");

    tcase_add_test(tc, test_oversized_message_rejected);
    tcase_add_test(tc, test_undersized_message_accepted);
    tcase_add_test(tc, test_io_deadline_fires);
    tcase_add_test(tc, test_io_deadline_partial_header);
    tcase_add_test(tc, test_io_deadline_cleared);
    tcase_add_test(tc, test_io_deadline_drip);
    tcase_add_test(tc, test_io_deadline_spans_messages);
    tcase_add_test(tc, test_io_deadline_spans_header_and_payload);
    tcase_add_test(tc, test_io_deadline_bounds_send);
    tcase_add_test(tc, test_io_deadline_cleared_ignores_kernel_timeout);
    suite_add_tcase(s, tc);
    return s;
}

int main(void) {
    int number_failed;
    Suite *s = ipc_suite();
    SRunner *sr = srunner_create(s);

    srunner_run_all(sr, CK_NORMAL);
    number_failed = srunner_ntests_failed(sr);
    srunner_free(sr);
    return (number_failed == 0) ? 0 : 1;
}
