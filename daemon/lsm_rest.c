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
 * License along with this library; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston,
 * MA 02110-1301 USA
 *
 * Author: Gris Ge <fge@redhat.com>
 */

#define _GNU_SOURCE
#include <sys/types.h>
#include <sys/select.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <string.h>
#include <microhttpd.h>
#include <json.h>
#include <libxml/uri.h>
#include <stdio.h>
#include <unistd.h>
#include <errno.h>
#include <sys/param.h>
#include <stdlib.h>

#include "lsm_rest.h"

/*
 TODO: MHD_get_connection_values() with MHD_GET_ARGUMENT_KIND to
       get all query argument
 TODO: Check malloc() return code
*/

void para_list_init(ParaList_t *para_list)
{
	para_list->head = NULL;
}

int para_list_add(ParaList_t *para_list, const char *key_name,
	const void *value, const enum lsm_json_type value_type,
	const ssize_t array_len)
{
	if (para_list == NULL) return -1;
	Parameter_t *new_para_node =
		(Parameter_t *)malloc(sizeof(Parameter_t));
	new_para_node->key_name = key_name;
	new_para_node->value = value;
	new_para_node->value_type = value_type;
	new_para_node->array_len = array_len;
	new_para_node->next = NULL;
	if (para_list->head == NULL){
		para_list->head = new_para_node;
	}else{
		Parameter_t *current = para_list->head;
		while (current->next != NULL){
			current = current->next;
		}
		current->next = new_para_node;
	}
	return 0;
}

void para_list_free(ParaList_t *para_list)
{
	if (para_list == NULL) return;
	if (para_list->head == NULL){
		free(para_list);
	}else{
		Parameter_t *current = para_list->head;
		Parameter_t *next = current->next;
		free(current);
		while (next != NULL){
			current = next;
			next = current->next;
			free(current);
		}
		free(para_list);
	}
	return;
}

json_object *para_to_json(const enum lsm_json_type value_type,
	const void *para_value, ssize_t array_len)
{
	json_object *para_val_obj = NULL;
	switch (value_type) {
	case lsm_json_type_null:
		break;
	case lsm_json_type_int:
		para_val_obj = json_object_new_int64(
			*(int64_t *) para_value);
		break;
	case lsm_json_type_float:
		para_val_obj = json_object_new_double(
			*(double *) para_value);
		break;
	case lsm_json_type_string:
		para_val_obj = json_object_new_string(
			(const char *) para_value);
		break;
	case lsm_json_type_bool:
		para_val_obj = json_object_new_boolean(
			*(json_bool *) para_value);
		break;
	case lsm_json_type_array_str:
		para_val_obj = json_object_new_array();
		ssize_t i;
		for (i=0; i < array_len; i++) {
			json_object *array_member = para_to_json(
				lsm_json_type_string,
				(void *)((char **)para_value)[i], 0);
			json_object_array_add(para_val_obj, array_member);
		}
		break;
	default:
		break;
	}
	return para_val_obj;
}


json_object *para_list_to_json(ParaList_t *para_list)
{
	Parameter_t *cur_node = para_list->head;
	if (cur_node == NULL){
		return NULL;
	}
	json_object * jobj = json_object_new_object();
	while(cur_node !=NULL){
		json_object_object_add(
			jobj,
			cur_node->key_name,
			para_to_json(
				cur_node->value_type,
				cur_node->value,
				cur_node->array_len));
		cur_node = cur_node->next;
	}
	return jobj;
}

static int connect_socket(const char *uri_str, const char *plugin_dir,
	int *error_no)
{
	int socket_fd = -1;
	xmlURIPtr uri_obj;
	uri_obj = xmlParseURI(uri_str);
	char *uri_scheme = NULL;
	if (uri_obj != NULL){
		uri_scheme = strdup(uri_obj->scheme);
		xmlFreeURI(uri_obj);
		uri_obj = NULL;
	}else{
		*error_no = errno;
		return socket_fd;
	}
	char *plugin_file = NULL;
	if (asprintf(&plugin_file, "%s/%s", plugin_dir, uri_scheme) == -1){
		free(uri_scheme);
		*error_no = ENOMEM;
		return socket_fd;
	}
	free(uri_scheme);

	socket_fd = socket(AF_UNIX, SOCK_STREAM, 0);
	if (socket_fd != -1){
		struct sockaddr_un addr;
		memset(&addr, 0, sizeof(addr));
		addr.sun_family = AF_UNIX;
		if (strlen(plugin_file) > (sizeof(addr.sun_path) - 1)){
			socket_fd = -1;
			fprintf(stderr, "Plugin file path too long: %s, "
				"max is %zu", plugin_file,
				sizeof(addr.sun_path) - 1);
		}
		strcpy(addr.sun_path, plugin_file);
		free(plugin_file);
		if (connect(socket_fd, (struct sockaddr *) &addr,
			sizeof(addr)) != 0){
			*error_no = errno;
			socket_fd = -1;
		}
	}else{
		*error_no = errno;
	}
	return socket_fd;
}


static int send_msg(int socket_fd, const char *msg, int *error_no)
{
	int rc = -1;
	size_t len = strlen(msg);
	size_t new_msg_len = strlen(msg) + LSM_HEADER_LEN + 1;
	char *msg_with_header = (char *)malloc(new_msg_len);
	sprintf(msg_with_header, "%0*zu%s", LSM_HEADER_LEN, len, msg);
	ssize_t written = 0;
	new_msg_len -= 1;
	while (written < new_msg_len) {
		ssize_t wrote = send(
			socket_fd, msg_with_header + written,
			(new_msg_len - written),
			MSG_NOSIGNAL);
		if (wrote != -1){
			written += wrote;
		}else{
			*error_no = errno;
			break;
		}
	}
	if ((written == new_msg_len) && *error_no == 0){
		rc = 0;
	}
	free(msg_with_header);
	return rc;
}

static char *_recv_msg(int socket_fd, size_t count, int *error_no)
{
	char buff[LSM_SOCK_BUFF_LEN];
	size_t amount_read = 0;
	*error_no = 0;
	char *msg = malloc(count + 1);
	memset(msg, 0, count + 1);
	while (amount_read < count) {
		ssize_t rd = (ssize_t)recv(socket_fd, buff,
			MIN(sizeof(buff), count - amount_read), MSG_WAITALL);
		if (rd > 0) {
			memcpy(msg + amount_read, buff, rd);
			amount_read += rd;
		}
		else if(errno == EAGAIN){
			printf("retry\n");
			errno = 0;
			continue; // TODO: don't know why recv() don't block.
		}
		else {
			*error_no = errno;
			break;
		}
	}
	if (*error_no == 0){
		msg[count] = '\0';
		return msg;
	}
	else{
		fprintf(stderr, "recv() got error_no, : %d\n", *error_no);
		free(msg);
		return NULL;
	}
}

static char *recv_msg(int socket_fd, int *error_no)
{
	*error_no = 0;
	char *msg_len_str = _recv_msg(socket_fd, LSM_HEADER_LEN, error_no);
	if (msg_len_str == NULL){
		fprintf(stderr, "Failed to read the JSON length "
			"with error_no%d\n", *error_no);
		return NULL;
	}
	errno = 0;
	size_t msg_len = (size_t)strtoul(msg_len_str, NULL, 10);
	free(msg_len_str);
	if ((errno == ERANGE && (msg_len == LONG_MAX || msg_len == LONG_MIN))
		|| (errno != 0 && msg_len == 0))
	{
		perror("strtol");
		return NULL;
	}
	if (msg_len == 0){
		fprintf(stderr, "No data needed to retrieve\n");
		return NULL;
	}
	char *msg = _recv_msg(socket_fd, msg_len, error_no);
	if (msg == NULL){
		fprintf(stderr, "Failed to retrieve data from socket "
			"with error_no %d\n", *error_no);
		return NULL;
	}
	return msg;
}

static char *rpc(int socket_fd, const char *method, ParaList_t *para_list,
	int *error_no)
{
	*error_no = 0;
	json_object *jobj = json_object_new_object();
	json_object_object_add(jobj,"method", json_object_new_string(method));
	json_object *js_params = para_list_to_json(para_list);
	if (js_params != NULL){
		json_object_object_add(jobj,"params", js_params);
	}
	json_object_object_add(jobj,"id", json_object_new_int(LSM_DEFAULT_ID));
	const char *json_string = json_object_to_json_string_ext(
		jobj, JSON_C_TO_STRING_PRETTY);
	printf ("Sending JSON to plugin:\n%s\n", json_string); // code_debug
	*error_no = 0;
	int rc = send_msg(socket_fd, json_string, error_no);
	json_object_put(jobj);
	if (rc != 0){
		fprintf(stderr, "Got error when sending message to socket, "
			"rc=%d, error_no=%d\n", rc, *error_no);
		return NULL;
	}
	char *recv_json_string = NULL;
	recv_json_string = recv_msg(socket_fd, error_no);
	if (*error_no != 0){
		printf("Got error when receiving message to socket,"
			"error_no=%d\n", *error_no);
		free(recv_json_string);
		return NULL;
	}
	if (recv_json_string == NULL){
		printf("No data retrieved\n");
		return NULL;
	}
	json_object *recv_json = json_tokener_parse(recv_json_string);
	free(recv_json_string);
	json_object *result_json;
	if (!json_object_object_get_ex(recv_json, "result", &result_json)){
		printf("No 'result' node in received JSON data");
		json_object_put(recv_json);
		return NULL;
	}
	char *result_str;
	result_str = (char*) json_object_to_json_string_ext(
		result_json,
		JSON_C_TO_STRING_PRETTY);
	char *rc_msg = strdup(result_str);
	json_object_put(recv_json);
	return rc_msg;
}

static int plugin_startup(int socket_fd, const char *uri, const char *pass,
	int tmo)
{
	printf("Starting the plugin\n");
	int error_no = 0;
	enum lsm_json_type pass_type = lsm_json_type_string;
	if (pass == NULL){
		pass_type = lsm_json_type_null;
	}
	ParaList_t *para_list = (ParaList_t *)malloc(sizeof(ParaList_t));
	para_list_init(para_list);
	para_list_add(para_list, "uri", uri, lsm_json_type_string, 0);
	para_list_add(para_list, "password", pass, pass_type, 0);
	para_list_add(para_list, "timeout", &tmo, lsm_json_type_int, 0);
	char *msg = rpc(socket_fd, "plugin_register", para_list, &error_no);
	free(msg);
	para_list_free(para_list);
	return error_no;
}

static int plugin_shutdown(int socket_fd)
{
	printf("Shutting down the plugin\n");
	int error_no = 0;
	ParaList_t *para_list = (ParaList_t *)malloc(sizeof(ParaList_t));
	para_list_init(para_list);
	static int lsm_flags = 0;
	para_list_add(para_list, "flags", &lsm_flags, lsm_json_type_int, 0);
	char *msg = rpc(socket_fd, "plugin_unregister", para_list, &error_no);
	free(msg);
	para_list_free(para_list);
	return error_no;
}

static char *v01_query(int socket_fd, const char* method,
	ParaList_t *para_list, int *error_no)
{
	*error_no = 0;
	if (para_list == NULL){
		para_list = (ParaList_t *)malloc(sizeof(ParaList_t));
		para_list_init(para_list);
	}
	int lsm_flags = 0;
	para_list_add(para_list, "flags", &lsm_flags, lsm_json_type_int, 0);
	char *json_str = rpc(socket_fd, method, para_list, error_no);
	para_list_free(para_list);
	return json_str;
}

static char *lsm_api_0_1(struct MHD_Connection *connection,
	const char *uri, const char * pass,
	const char *url, const char *method,
	const char *upload_data)
{
	const char *plugin_dir = getenv("LSM_UDS_PATH");
	if (plugin_dir == NULL){
		plugin_dir = LSM_UDS_PATH_DEFAULT;
		fprintf(stdout, "Using default LSM_UDS_PATH: %s\n",
			plugin_dir);
	}
	int error_no = 0;
	int socket_fd = connect_socket(uri, plugin_dir, &error_no);
	if (socket_fd == -1){
		fprintf(stderr, "Failed to connecting to the socket for URI "
			"%s with error_no %d\n", uri, error_no);
		return NULL;
	}
	error_no = plugin_startup(socket_fd, uri, pass, LSM_REST_TMO);
	if (error_no != 0){
		fprintf(stderr, "Failed to register plugin, "
			"error_no %d", error_no);
		plugin_shutdown(socket_fd);
		shutdown(socket_fd, 0);
		return NULL;
	}
	error_no = 0;
	char *json_msg = NULL;
	int i;
	int flag_found=0;
	for(i=0; i < sizeof(lsm_query_strs)/sizeof(char*); i++){
		if (0 == strcmp(url, lsm_query_strs[i])){
			flag_found = 1;
			json_msg = v01_query(
				socket_fd, lsm_query_strs[i], NULL,
				&error_no);
			break;
		}
	}
	if (flag_found == 0){
		fprintf(stderr, "Not supported: %s\n", url);
	}
	if (error_no != 0){
		fprintf(stderr, "Failed to call method %s(), error_no: %d\n",
			url, error_no);
	}
	error_no = plugin_shutdown(socket_fd);
	if (error_no != 0){
		fprintf(stderr, "Failed to unregister plugin, "
			"error_no %d", error_no);
	}
	shutdown(socket_fd, 0);
	return json_msg;
}

static int answer_to_connection(void *cls, struct MHD_Connection *connection,
	const char *url,
	const char *method, const char *version,
	const char *upload_data,
	size_t *upload_data_size, void **con_cls)
{
	printf ("New '%s' request, URL: '%s'\n", method, url); // code_debug
	struct MHD_Response *response;

	if (0 != strcmp (method, "GET")){
		return MHD_NO;
	}

	if (strlen(url) == 1){
		return MHD_NO;
	}

	const char *uri = MHD_lookup_connection_value(connection,
		MHD_GET_ARGUMENT_KIND, "uri");

	const char *pass= MHD_lookup_connection_value(connection,
		MHD_GET_ARGUMENT_KIND, "pass");

	int ret;
	char api_version[LSM_API_VER_LEN + 1];
	memcpy(api_version, url + 1 , LSM_API_VER_LEN);
	// url + 1 is used to get rid of leading '/'
	api_version[LSM_API_VER_LEN] = '\0';
	char *json_str = NULL;
	size_t url_no_api_ver_len = strlen(url) - strlen(api_version) - 1 - 1;
	// -1 -1 means remove two leading /
	// example: /v0.1/systems  --change to--> systems
	char *url_no_api_ver = malloc(url_no_api_ver_len + 1);
	strcpy(url_no_api_ver, url+strlen(api_version) + 1 + 1);
	if ( 0 == strcmp(api_version, "v0.1" )){
		printf("v0.1 API request found\n"); // code_debug
		json_str = lsm_api_0_1(connection, uri, pass, url_no_api_ver,
			method, upload_data);
		free(url_no_api_ver);
		if(json_str == NULL){
			return MHD_NO;
		}
	}else{
		free(url_no_api_ver);
		return MHD_NO;
	}
	response = MHD_create_response_from_buffer(
		strlen(json_str),
		(void*) json_str, MHD_RESPMEM_MUST_FREE);

	MHD_add_response_header(response, "Content-Type", LSM_JSON_MIME);

	ret = MHD_queue_response(connection, MHD_HTTP_OK, response);
	MHD_destroy_response(response);
	return ret;
}

int main (int argc, char **argv)
{
	struct MHD_Daemon *daemon;
	daemon = MHD_start_daemon(
		MHD_USE_SELECT_INTERNALLY, LSM_REST_PORT, NULL, NULL,
		&answer_to_connection, NULL, MHD_OPTION_END);
	while(1){
		sleep(60);
	}
	MHD_stop_daemon(daemon);
	return EXIT_SUCCESS;
}
