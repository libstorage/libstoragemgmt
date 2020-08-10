/*
 * Copyright (C) 2016 Red Hat, Inc.
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

#ifndef _SIMC_UTILS_H_
#define _SIMC_UTILS_H_

#include <openssl/md5.h>
#include <sqlite3.h>
#include <stdbool.h>
#include <stdlib.h>
#include <string.h>

#include <libstoragemgmt/libstoragemgmt_plug_interface.h>

#include "db.h"

struct _simc_private_data {
    struct sqlite3 *db;
    uint32_t timeout;
};

#define _UNUSED(x)        (void)(x)
#define _MD5_HASH_STR_LEN MD5_DIGEST_LENGTH * 2 + 1
#define _LSM_ERR_MSG_LEN  4096

#define _VPD_83_LEN 17
/* ^ 6h IEEE Registered ID which it 16 bits hex string. */

#define _BUFF_SIZE 1024

#define _good(rc, rc_val, out)                                                 \
    do {                                                                       \
        rc_val = rc;                                                           \
        if (rc_val != LSM_ERR_OK)                                              \
            goto out;                                                          \
    } while (0)

#define _alloc_null_check(err_msg, ptr, rc, goto_out)                          \
    do {                                                                       \
        if (ptr == NULL) {                                                     \
            rc = LSM_ERR_NO_MEMORY;                                            \
            _lsm_err_msg_set(err_msg, "No memory");                            \
            goto goto_out;                                                     \
        }                                                                      \
    } while (0)

#define _snprintf_buff(err_msg, rc, out, buff, format, ...)                    \
    do {                                                                       \
        if (buff != NULL)                                                      \
            snprintf(buff, sizeof(buff) / sizeof(char), format,                \
                     ##__VA_ARGS__);                                           \
        if (strlen(buff) == sizeof(buff) / sizeof(char) - 1) {                 \
            rc = LSM_ERR_PLUGIN_BUG;                                           \
            _lsm_err_msg_set(err_msg, "Buff too small");                       \
            goto out;                                                          \
        }                                                                      \
    } while (0)

#define _lsm_err_msg_clear(err_msg)                                            \
    do {                                                                       \
        if (err_msg != NULL)                                                   \
            memset(err_msg, 0, _LSM_ERR_MSG_LEN);                              \
    } while (0)

#define _lsm_err_msg_set(err_msg, format, ...)                                 \
    do {                                                                       \
        if (err_msg != NULL)                                                   \
            snprintf(err_msg, _LSM_ERR_MSG_LEN, format, ##__VA_ARGS__);        \
    } while (0)

#define _vec_to_lsm_xxx_array(err_msg, vec, lsm_xxx_type, conv_func, array,    \
                              count, rc, out)                                  \
    do {                                                                       \
        uint64_t __i = 0;                                                      \
        lsm_xxx_type *__lsm_xxx = NULL;                                        \
        lsm_hash *__sim_xxx = NULL;                                            \
        *array = (lsm_xxx_type **)malloc(sizeof(lsm_xxx_type *) *              \
                                         _vector_size(vec));                   \
        _alloc_null_check(err_msg, *array, rc, out);                           \
        *count = _vector_size(vec);                                            \
        for (; __i < *count; ++__i)                                            \
            (*array)[__i] = NULL;                                              \
        _vector_for_each(vec, __i, __sim_xxx) {                                \
            __lsm_xxx = conv_func(err_msg, __sim_xxx);                         \
            if (__lsm_xxx == NULL) {                                           \
                rc = LSM_ERR_PLUGIN_BUG;                                       \
                goto out;                                                      \
            }                                                                  \
            (*array)[__i] = __lsm_xxx;                                         \
        }                                                                      \
    } while (0)

#define _xxx_list_func_gen(func_name, rc_type, conv_func, filter_func, table,  \
                           lsm_xxx_array_free_func)                            \
    int func_name(lsm_plugin_ptr c, const char *search_key,                    \
                  const char *search_value, rc_type **array[],                 \
                  uint32_t *count, lsm_flag flags) {                           \
        int rc = LSM_ERR_OK;                                                   \
        struct _vector *vec = NULL;                                            \
        sqlite3 *db = NULL;                                                    \
        char err_msg[_LSM_ERR_MSG_LEN];                                        \
        _UNUSED(flags);                                                        \
        _lsm_err_msg_clear(err_msg);                                           \
        _check_null_ptr(err_msg, 2 /* argument count */, array, count);        \
        _good(_get_db_from_plugin_ptr(err_msg, c, &db), rc, out);              \
        _good(_db_sql_trans_begin(err_msg, db), rc, out);                      \
        _good(_db_sql_exec(err_msg, db, "SELECT * from " table ";", &vec), rc, \
              out);                                                            \
        if (_vector_size(vec) == 0) {                                          \
            *array = NULL;                                                     \
            *count = 0;                                                        \
            goto out;                                                          \
        }                                                                      \
        _vec_to_lsm_xxx_array(err_msg, vec, rc_type, conv_func, array, count,  \
                              rc, out);                                        \
    out:                                                                       \
        _db_sql_trans_rollback(db);                                            \
        _db_sql_exec_vec_free(vec);                                            \
        if (rc != LSM_ERR_OK) {                                                \
            if (*array != NULL) {                                              \
                lsm_xxx_array_free_func(*array, *count);                       \
                *array = NULL;                                                 \
                *count = 0;                                                    \
            }                                                                  \
            lsm_log_error_basic(c, rc, err_msg);                               \
        } else {                                                               \
            filter_func(search_key, search_value, *array, count);              \
        }                                                                      \
        return rc;                                                             \
    }
int _get_db_from_plugin_ptr(char *err_msg, lsm_plugin_ptr c, sqlite3 **db);

/*
 * data:        Non-NULL pointer to a string.
 * out_hash:    Pointer to char[_MD5_HASH_STR_LEN]
 */
void _md5(const char *data, char *out_hash);

/*
 * true if file exists or false.
 */
bool _file_exists(const char *path);

int _str_to_int(char *err_msg, const char *str, int *val);

int _str_to_uint32(char *err_msg, const char *str, uint32_t *val);

int _str_to_uint64(char *err_msg, const char *str, uint64_t *val);

int _check_null_ptr(char *err_msg, int arg_count, ...);

/*
 *
 * buff:        char[_VPD_83_LEN]
 */
const char *_random_vpd(char *buff);

#endif /* End of _SIMC_UTILS_H_ */
