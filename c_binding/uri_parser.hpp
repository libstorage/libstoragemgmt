/*
 * Copyright (C) 2022 Red Hat, Inc.
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
 */

#pragma once

#ifndef __URIPARSER_H_
#define __URIPARSER_H_

#include <algorithm>
#include <iostream>
#include <stdexcept>
#include <string>
#include <unordered_map>

namespace lsm_uri {

using query_key_value = std::unordered_map<std::string, std::string>;

struct uri {
    bool valid = false;
    std::string scheme;
    std::string username;
    std::string host;
    long port = -1;
    std::string path;
    query_key_value query;
    std::string fragment;
};

inline bool verify_scheme(std::string s) {
    if (s.empty() || std::find_if_not(s.begin(), s.end(), [&](char c) {
                         return std::isalnum(c) || c == '+' || c == '.' ||
                                c == '-';
                     }) != s.end()) {
        return false;
    }

    return true;
}

inline std::tuple<std::string, std::string> pair(std::string s) {
    auto i = s.find("=");
    if (i == std::string::npos) {
        return {s, ""};
    }
    return {s.substr(0, i), s.substr(i + 1)};
}

inline query_key_value parse_qs(std::string qs) {
    query_key_value rc;

    std::string key;
    std::string value;

    if (!qs.length())
        return rc;

    std::size_t amp;
    do {
        amp = qs.find("&");

        if (amp == std::string::npos) {
            // No separator found
            std::tie(key, value) = pair(qs);
            rc[key] = value;

        } else {
            // & found
            std::tie(key, value) = pair(qs.substr(0, amp));
            rc[key] = value;
            qs = qs.substr(amp + 1);
        }

    } while (amp != std::string::npos);

    return rc;
}

inline long port(std::string s) {
    long rc = -1;

    if (s.empty() || std::find_if_not(s.begin(), s.end(), [&](char c) {
                         return std::isdigit(c);
                     }) != s.end()) {
        return rc;
    }

    // Convert string to long
    try {
        rc = std::stol(s);
    } catch (const std::invalid_argument &ia) {
    }

    return rc;
}

inline struct uri parse(std::string uri) {
    struct uri rc;

    std::size_t req;

    // Library requires :// in uri
    if (uri.empty() || ((req = uri.find("://")) == std::string::npos)) {
        return rc;
    }

    std::string scheme_tmp = uri.substr(0, req);
    if (!verify_scheme(scheme_tmp)) {
        return rc;
    }

    rc.scheme = scheme_tmp;

    auto remainder = uri.substr(req + 3);

    // Check to see if we have a fragment
    auto frag = remainder.rfind("#");
    if (frag != std::string::npos) {
        rc.fragment = remainder.substr(frag + 1);
        remainder = remainder.substr(0, frag);
    }

    // Check for query string
    auto qs = remainder.rfind("?");
    if (qs != std::string::npos) {
        rc.query = parse_qs(remainder.substr(qs + 1));
        remainder = remainder.substr(0, qs);
    }

    // process location & path
    auto path_start = remainder.find("/");
    if (path_start != std::string::npos) {
        rc.path = remainder.substr(path_start);
        remainder = remainder.substr(0, path_start);
    }

    // process username, port, then host
    auto username = remainder.find("@");
    if (username != std::string::npos) {
        rc.username = remainder.substr(0, username);
        remainder = remainder.substr(username + 1);
    }

    // Check for port on host
    auto port_del = remainder.rfind(":");
    if (port_del != std::string::npos) {
        // Check if this is a valid port, could be ipv6 addr.
        long tmp_port = port(remainder.substr(port_del + 1));
        if (tmp_port != -1) {
            rc.port = tmp_port;
            remainder = remainder.substr(0, port_del);
        }
    }

    // Only part left should be host, which is a ipv4, hostname, or ipv6
    rc.host = remainder;
    rc.valid = true;
    return rc;
}

} // namespace lsm_uri

#endif