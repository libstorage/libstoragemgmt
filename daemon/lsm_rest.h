/*
 * Copyright (C) 2011-2013 Red Hat, Inc.
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

#ifndef LIBSTORAGEMGMT_REST_H
#define LIBSTORAGEMGMT_REST_H

#ifdef __cplusplus
extern "C" {
#endif

#define LSM_REST_PORT 8888
#define LSM_REST_TMO 60000
#define LSM_SOCK_BUFF_LEN 4096
#define LSM_DEFAULT_ID 100
#define LSM_JSON_MIME "application/json"
#define LSM_HEADER_LEN 10
#define LSM_API_VER_LEN 4
#define LSM_UDS_PATH_DEFAULT "/var/run/lsm/ipc"

enum lsm_json_type {
	lsm_json_type_null,
	lsm_json_type_int,
	lsm_json_type_float,
	lsm_json_type_string,
	lsm_json_type_bool,
	lsm_json_type_array_str,
};

static const char *lsm_query_strs[] = {
	"systems", "volumes", "pools", "disks", "fs", "access_groups",
	"initiators",
};

typedef struct Parameter {
	const char *key_name;
	const void *value;
	enum lsm_json_type value_type;
	ssize_t array_len;	// only useful for ARRAY_STR type.
	struct Parameter *next;
} Parameter_t;

typedef struct ParaList {
	Parameter_t *head;
} ParaList_t;

void para_list_init(ParaList_t *);

int para_list_add(ParaList_t *, const char *, const void *,
	const enum lsm_json_type, const ssize_t);

void para_list_free(ParaList_t *);

json_object *para_to_json(const enum lsm_json_type, const void *,
	const ssize_t);

json_object *para_list_to_json(ParaList_t *);

#ifdef __cplusplus
}
#endif

#endif	/* LIBSTORAGEMGMT_REST_H */
