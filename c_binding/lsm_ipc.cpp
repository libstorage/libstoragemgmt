/*
 * Copyright (C) 2011-2014,2018 Red Hat, Inc.
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; If not, see <http://www.gnu.org/licenses/>.
 *
 * Author: tasleson
 */

#include "lsm_ipc.hpp"

#include "libstoragemgmt/libstoragemgmt_plug_interface.h"

#include <algorithm>
#include <errno.h>
#include <iomanip>
#include <iostream>
#include <limits.h>
#include <list>
#include <sstream>
#include <stdio.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/un.h>
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

Transport::Transport() : s(-1) {}

Transport::Transport(int socket_desc) : s(socket_desc) {}

int Transport::msg_send(const std::string &msg, int &error_code) {
    int rc = -1;
    error_code = 0;

    if (msg.size() > 0) {
        ssize_t written = 0;
        // fprintf(stderr, ">>> %s\n", msg.c_str());
        std::string data = zero_pad_num(msg.size()) + msg;
        ssize_t msg_size = data.size();

        while (written < msg_size) {
            int wrote = send(s, data.c_str() + written, (msg_size - written),
                             MSG_NOSIGNAL); // Prevent SIGPIPE on write
            if (wrote != -1) {
                written += wrote;
            } else {
                error_code = errno;
                break;
            }
        }

        if ((written == msg_size) && error_code == 0) {
            rc = 0;
        }
    }
    return rc;
}

static std::string string_read(int fd, size_t count, int &error_code) {
    char buff[4096];
    size_t amount_read = 0;
    std::string rc = "";

    error_code = 0;

    while (amount_read < count) {
        ssize_t rd =
            recv(fd, buff, std::min(sizeof(buff), (count - amount_read)),
                 MSG_WAITALL);
        if (rd > 0) {
            amount_read += rd;
            rc += std::string(buff, rd);
        } else {
            error_code = errno;
            break;
        }
    }

    if ((amount_read == count) && (error_code == 0))
        return rc;
    else
        throw EOFException("");
}

std::string Transport::msg_recv(int &error_code) {
    std::string msg;
    error_code = 0;
    unsigned long int payload_len = 0;
    std::string len = string_read(s, HDR_LEN, error_code); // Read the length
    if (len.size() && error_code == 0) {
        payload_len = strtoul(len.c_str(), NULL, 10);
        if (payload_len < 0x80000000) { /* Should be big enough */
            msg = string_read(s, payload_len, error_code);
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

Transport::~Transport() { close(); }

void Transport::close() {
    if (s >= 0) {
        ::close(s);
        // Regardless, clear out socket
        s = -1;
    }
}

EOFException::EOFException(std::string m) : std::runtime_error(m) {}

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

Ipc::Ipc(std::string socket_path) {
    int e = 0;
    int fd = Transport::socket_get(socket_path, e);
    if (fd >= 0) {
        t = Transport(fd);
    }
}

Ipc::~Ipc() { t.close(); }

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
        std::string em =
            std::string("Error sending message: errno ") + ::to_string(ec);
        throw LsmException((int)LSM_ERR_TRANSPORT_COMMUNICATION, em);
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
        std::string em = std::string("Error sending error message: errno ") +
                         ::to_string(ec);
        throw LsmException((int)LSM_ERR_TRANSPORT_COMMUNICATION, em);
    }
}

Value Ipc::readRequest(void) {
    int ec;
    std::string resp = t.msg_recv(ec);
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
        std::string em =
            std::string("Error sending response: errno ") + ::to_string(ec);
        throw LsmException((int)LSM_ERR_TRANSPORT_COMMUNICATION, em);
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
