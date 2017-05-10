/*
 * Copyright (C) 2017 Red Hat, Inc.
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
 * Author: Gris Ge <fge@redhat.com>
 */

#include <stdlib.h>
#include <stdint.h>
#include <stdio.h>
#include <inttypes.h>
#include <string.h>

#include <libstoragemgmt/libstoragemgmt.h>

#include "string_list_hash_test.h"

#define _good(rc, rc_val, out, err_msg) \
    do { \
        rc_val = rc; \
        if (rc_val != LSM_ERR_OK) { \
            _lsm_err_msg_set(err_msg, "Got rc %d", rc); \
            goto out; \
        } \
    } while(0)

#define _LSM_ERR_MSG_LEN                1024
#define _lsm_err_msg_clear(err_msg) memset(err_msg, 0, _LSM_ERR_MSG_LEN)
#define _lsm_err_msg_set(err_msg, format, ...) \
    snprintf(err_msg, _LSM_ERR_MSG_LEN, format " (%s:%d)", ##__VA_ARGS__, \
             __FILE__, __LINE__)

static int _verify_data_string_list(char *err_msg, lsm_string_list *str_list);
static int _lsm_string_list_test(void);
static int _lsm_hash_test(void);

static int _verify_data_string_list(char *err_msg, lsm_string_list *str_list)
{
    int rc = LSM_ERR_OK;
    const char *tmp_str = NULL;
    uint32_t test_strings_size = sizeof(test_strings) / sizeof(test_strings[0]);
    uint32_t i = 0;

    if (lsm_string_list_size(str_list) != test_strings_size) {
        _lsm_err_msg_set(err_msg, "Got incorrect string list size %" PRIu32
                         ", should be %" PRIu32,
                         lsm_string_list_size(str_list), test_strings_size);
        rc = LSM_ERR_LIB_BUG;
        goto out;
    }

    for (i = 0; i < test_strings_size; ++i) {
        tmp_str = lsm_string_list_elem_get(str_list, i);
        if (tmp_str == NULL) {
            _lsm_err_msg_set(err_msg, "Got NULL string at index %" PRIu32, i);
            rc = LSM_ERR_LIB_BUG;
            goto out;
        }
        if (strcmp(tmp_str, test_strings[i]) != 0) {
            _lsm_err_msg_set(err_msg, "Got corrupted string at index %" PRIu32
                             ", got '%s', should be '%s'", i, tmp_str,
                             test_strings[i]);
            rc = LSM_ERR_LIB_BUG;
            goto out;
        }
    }

 out:
    return rc;
}

static int _lsm_string_list_test(void)
{
    int rc = LSM_ERR_OK;
    uint32_t test_strings_size = sizeof(test_strings) / sizeof(test_strings[0]);
    uint32_t half_size = test_strings_size / 2;
    lsm_string_list *str_list = NULL;
    lsm_string_list *dup_str_list = NULL;
    char err_msg[_LSM_ERR_MSG_LEN];
    uint32_t i = 0;

    _lsm_err_msg_clear(err_msg);

    str_list = lsm_string_list_alloc(half_size);
    if (str_list == NULL) {
        _lsm_err_msg_set(err_msg, "No memory");
        rc = LSM_ERR_NO_MEMORY;
        goto out;
    }

    for (i = 0; i < half_size; ++i)
        _good(lsm_string_list_elem_set(str_list, i, test_strings[i]), rc, out,
              err_msg);

    for (i = half_size; i < test_strings_size; ++i)
        _good(lsm_string_list_append(str_list, test_strings[i]), rc, out,
              err_msg);

    _good(_verify_data_string_list(err_msg, str_list), rc, out, err_msg);

    /* Test duplicate */
    dup_str_list = lsm_string_list_copy(str_list);
    if (dup_str_list == NULL) {
        _lsm_err_msg_set(err_msg, "No memory");
        rc = LSM_ERR_NO_MEMORY;
        goto out;
    }
    _good(_verify_data_string_list(err_msg, dup_str_list), rc, out, err_msg);

    /* Test override */
    for (i = 0; i < test_strings_size; ++i)
        _good(lsm_string_list_elem_set(str_list, i, test_strings[i]), rc, out,
              err_msg);

    _good(_verify_data_string_list(err_msg, str_list), rc, out, err_msg);

    /* Test delete */
    for (i = test_strings_size - 1; i != 0; --i)
        _good(lsm_string_list_delete(str_list, i), rc, out, err_msg);

    _good(lsm_string_list_delete(str_list, 0), rc, out, err_msg);

    _good(lsm_string_list_free(dup_str_list), rc, out, err_msg);
    _good(lsm_string_list_free(str_list), rc, out, err_msg);
    dup_str_list = NULL;
    str_list = NULL;

 out:
    lsm_string_list_free(dup_str_list);
    lsm_string_list_free(str_list);
    if (rc == LSM_ERR_OK)
        printf("lsm_string_list test PASS\n");
    return rc;
}

static int _lsm_hash_test(void)
{
    int rc = 0;
    return rc;
}

int main(void)
{
    if (_lsm_string_list_test() || _lsm_hash_test())
        exit(EXIT_FAILURE);
}
