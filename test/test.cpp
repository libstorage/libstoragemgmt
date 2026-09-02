/*
 * SPDX-License-Identifier: LGPL-2.1-or-later
 *
 * Copyright (C) 2011-2023 Red Hat, Inc.
 */

#include "../c_binding/lsm_ipc.hpp"
#include "libstoragemgmt/libstoragemgmt_error.h"

#include <iomanip>
#include <sstream>
#include <sys/socket.h>
#include <unistd.h>

/* check.h's "fail" macro clashes with std::basic_ios::fail(), so it must be
 * included only after all standard library headers are pulled in. */
#include <check.h>

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

static Suite *ipc_suite(void) {
    Suite *s = suite_create("ipc");
    TCase *tc = tcase_create("core");

    tcase_add_test(tc, test_oversized_message_rejected);
    tcase_add_test(tc, test_undersized_message_accepted);
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
