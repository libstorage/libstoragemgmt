/*
 * Copyright (C) 2011-2017 Red Hat, Inc.
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or any later version.
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

#ifndef LIBSTORAGEMGMT_H
#define LIBSTORAGEMGMT_H

#include "libstoragemgmt_common.h"
#include "libstoragemgmt_types.h"

#include "libstoragemgmt_accessgroups.h"
#include "libstoragemgmt_battery.h"
#include "libstoragemgmt_blockrange.h"
#include "libstoragemgmt_capabilities.h"
#include "libstoragemgmt_disk.h"
#include "libstoragemgmt_error.h"
#include "libstoragemgmt_fs.h"
#include "libstoragemgmt_local_disk.h"
#include "libstoragemgmt_nfsexport.h"
#include "libstoragemgmt_pool.h"
#include "libstoragemgmt_snapshot.h"
#include "libstoragemgmt_systems.h"
#include "libstoragemgmt_targetport.h"
#include "libstoragemgmt_volumes.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * lsm_connect_password - Get a connection to a storage provider.
 *
 * Version:
 *      1.0
 *
 * Description:
 *      Get a connection to a storage provider.
 *
 * @uri:
 *      Uniform Resource Identifier (see URI documentation)
 * @password:
 *      Password for the storage array (optional, can be NULL)
 * @conn:
 *      The connection to use for all the other library calls.
 *      When done using the connection it must be freed with a call to
 *      lsm_connect_close().
 * @timeout:
 *      Time-out in milliseconds, (initial value).
 * @e:
 *      Error data if connection failed.
 * @flags:
 *      Reserved for future use, must be LSM_CLIENT_FLAG_RSVD.
 *
 * Return:
 *      Error code as enumerated by 'lsm_error_number'.
 *          * LSM_ERR_OK
 *              On success.
 *          * LSM_ERR_INVALID_ARGUMENT
 *              When any argument is NULL or invalid flags.
 */
int LSM_DLL_EXPORT lsm_connect_password(const char *uri, const char *password,
                                        lsm_connect **conn, uint32_t timeout,
                                        lsm_error_ptr *e, lsm_flag flags);
/**
 * lsm_connect_close - Closes a connection to a storage provider.
 *
 * Version:
 *      1.0
 *
 * Description:
 *      Closes a connection to a storage provisioning.
 *
 * @conn:
 *      Valid connection to close
 * @flags:
 *      Reserved for future use, must be LSM_CLIENT_FLAG_RSVD.
 *
 * Return:
 *      Error code as enumerated by 'lsm_error_number'.
 *          * LSM_ERR_OK
 *              On success.
 *          * LSM_ERR_INVALID_ARGUMENT
 *              When not a valid lsm_connect pointer or invalid flags.
 */
int LSM_DLL_EXPORT lsm_connect_close(lsm_connect *conn, lsm_flag flags);

/**
 * lsm_plugin_info_get - Retrieves information about the plug-in
 *
 * Version:
 *      1.0
 *
 * Description:
 *      Retrieves information about the plug-in.
 *
 * @conn:
 *      Valid lsm_connect pointer.
 * @desc:
 *      Plug-in version. Returned string should be freed manually with free()
 *      library call.
 * @version:
 *      Plug-in version. Returned string should be freed manually.
 * @flags:
 *      Reserved for future use, must be LSM_CLIENT_FLAG_RSVD.
 *
 * Return:
 *      Error code as enumerated by 'lsm_error_number'.
 *          * LSM_ERR_OK
 *              On success.
 *          * LSM_ERR_INVALID_ARGUMENT
 *              When any argument is NULL or not a valid lsm_connect pointer or
 *              invalid flags.
 */
int LSM_DLL_EXPORT lsm_plugin_info_get(lsm_connect *conn, char **desc,
                                       char **version, lsm_flag flags);

/**
 * lsm_available_plugins_list - Retrieves a list of available plug-ins.
 *
 * Version:
 *      1.0
 *
 * Description:
 *      Retrieve a list of available plug-ins.
 *
 * @sep:
 *      Data separator used for string of plugin. Check document of 'plugins'
 *      argument for detail.
 * @plugins:
 *      String list of plug-ins with the form 'desc<sep>version'.
 *      Returned plugins memory should be freed by calling
 *      lsm_string_list_free().
 * @flags:
 *      Reserved for future use, must be LSM_CLIENT_FLAG_RSVD.
 *
 * Return:
 *      Error code as enumerated by 'lsm_error_number'.
 *          * LSM_ERR_OK
 *              On success.
 *          * LSM_ERR_INVALID_ARGUMENT
 *              When any argument is NULL or invalid flags.
 *
 */
int LSM_DLL_EXPORT lsm_available_plugins_list(const char *sep,
                                              lsm_string_list **plugins,
                                              lsm_flag flags);

/**
 * lsm_connect_timeout_set - Sets the time-out for this connection.
 *
 * Version:
 *      1.0
 *
 * Description:
 *      Sets the time-out for this connection.
 *
 * @conn:
 *      Valid lsm_connect pointer.
 * @timeout:
 *      Time-out in ms.
 * @flags:
 *      Reserved for future use, must be LSM_CLIENT_FLAG_RSVD.
 *
 * Return:
 *      Error code as enumerated by 'lsm_error_number'.
 *          * LSM_ERR_OK
 *              On success.
 *          * LSM_ERR_INVALID_ARGUMENT
 *              When any argument is NULL or invalid lsm_connect or invalid
 *              flags.
 */
int LSM_DLL_EXPORT lsm_connect_timeout_set(lsm_connect *conn, uint32_t timeout,
                                           lsm_flag flags);

/**
 * lsm_connect_timeout_get - Gets the time-out for this connection.
 *
 * Version:
 *      1.0
 *
 * Description:
 *      Gets the time-out for this connection.
 *
 * @conn:
 *      Valid lsm_connect pointer.
 * @timeout:
 *      Output pointer of uint32_t. Time-out in ms.
 * @flags:
 *      Reserved for future use, must be LSM_CLIENT_FLAG_RSVD.
 *
 * Return:
 *      Error code as enumerated by 'lsm_error_number'.
 *          * LSM_ERR_OK
 *              On success.
 *          * LSM_ERR_INVALID_ARGUMENT
 *              When any argument is NULL or not a valid lsm_connect pointer
 *              or invalid flags.
 */
int LSM_DLL_EXPORT lsm_connect_timeout_get(lsm_connect *conn, uint32_t *timeout,
                                           lsm_flag flags);

/**
 * lsm_job_status_get - Check on the status of a job with no data returned.
 *
 * Version:
 *      1.0
 *
 * Description:
 *      Check on the status of a job, no data or ignore the data returned on
 *      completion.
 *
 * @conn:
 *      Valid connection.
 * @job_id:
 *      String. Job id
 * @status:
 *      Output pointer of lsm_job_status. Possible values are:
 *          * LSM_JOB_COMPLETE
 *              Job complete with no error.
 *          * LSM_JOB_ERROR
 *              Job complete with error.
 *          * LSM_JOB_INPROGRESS
 *              Job is still in progress.
 * @percent_complete:
 *      Output pointer of uint8_t. Percent job complete. Domain 0..100.
 * @flags:
 *      Reserved for future use, must be LSM_CLIENT_FLAG_RSVD.
 *
 * Return:
 *      Error code as enumerated by 'lsm_error_number'.
 *          * LSM_ERR_OK
 *              On success.
 *          * LSM_ERR_INVALID_ARGUMENT
 *              When any argument is NULL or not a valid lsm_connect pointer
 *              or invalid flags.
 *          * LSM_ERR_NOT_FOUND_JOB
 *              When job not found.
 */
int LSM_DLL_EXPORT lsm_job_status_get(lsm_connect *conn, const char *job_id,
                                      lsm_job_status *status,
                                      uint8_t *percent_complete,
                                      lsm_flag flags);

/**
 * lsm_job_status_pool_get - Check on the status of a job with lsm_pool
 * returned.
 *
 * Version:
 *      1.0
 *
 * Description:
 *      Check on the status of a job with lsm_pool returned.
 *
 * @conn:
 *      Valid connection pointer
 * @job_id:
 *      String. Job to check status on.
 * @status:
 *      Output pointer of lsm_job_status. Possible values are:
 *          * LSM_JOB_COMPLETE
 *              Job complete with no error.
 *          * LSM_JOB_ERROR
 *              Job complete with error.
 *          * LSM_JOB_INPROGRESS
 *              Job is still in progress.
 * @percent_complete:
 *      Output pointer of uint8_t. Percent job complete. Domain 0..100.
 * @pool:
 *      Output pointer of lsm_pool for completed operation.
 *      Returned value must be freed with a call to lsm_pool_record_free().
 * @flags:
 *      Reserved for future use, must be zero
 *
 * Return:
 *      Error code as enumerated by 'lsm_error_number'.
 *          * LSM_ERR_OK
 *              On success.
 *          * LSM_ERR_INVALID_ARGUMENT
 *              When any argument is NULL or not a valid lsm_connect pointer
 *              or invalid flags.
 *          * LSM_ERR_NOT_FOUND_JOB
 *              When job not found.
 */
int LSM_DLL_EXPORT lsm_job_status_pool_get(lsm_connect *conn,
                                           const char *job_id,
                                           lsm_job_status *status,
                                           uint8_t *percent_complete,
                                           lsm_pool **pool, lsm_flag flags);

/**
 * lsm_job_status_volume_get - Check on the status of a job with lsm_volume
 * returned.
 *
 * Version:
 *      1.0
 *
 * Description:
 *      Check on the status of a job and returns the volume information when
 *      complete.
 *
 * @conn:
 *      Valid connection pointer.
 * @job_id:
 *      String. Job to check status on
 * @status:
 *      Output pointer of lsm_job_status. Possible values are:
 *          * LSM_JOB_COMPLETE
 *              Job complete with no error.
 *          * LSM_JOB_ERROR
 *              Job complete with error.
 *          * LSM_JOB_INPROGRESS
 *              Job is still in progress.
 * @percent_complete:
 *      Output pointer of uint8_t. Percent job complete. Domain 0..100.
 * @vol:
 *      Output pointer of lsm_volume for completed operation.
 *      Returned value needs to be freed with a call to
 *      lsm_volume_record_free().
 * @flags:
 *      Reserved for future use, must be LSM_CLIENT_FLAG_RSVD.
 *
 * Return:
 *      Error code as enumerated by 'lsm_error_number'.
 *          * LSM_ERR_OK
 *              On success.
 *          * LSM_ERR_INVALID_ARGUMENT
 *              When any argument is NULL or not a valid lsm_connect pointer
 *              or invalid flags.
 *          * LSM_ERR_NOT_FOUND_JOB
 *              When job not found.
 */
int LSM_DLL_EXPORT lsm_job_status_volume_get(lsm_connect *conn,
                                             const char *job_id,
                                             lsm_job_status *status,
                                             uint8_t *percent_complete,
                                             lsm_volume **vol, lsm_flag flags);

/**
 * lsm_job_status_fs_get - Check on the status of a job with lsm_fs returned.
 *
 * Version:
 *      1.0
 *
 * Description:
 *      Check on the status of a job and return the fs information when
 *      complete.
 * @conn:
 *      Valid connection pointer
 * @job_id:
 *      String. Job to check
 * @status:
 *      Output pointer of lsm_job_status. Possible values are:
 *          * LSM_JOB_COMPLETE
 *              Job complete with no error.
 *          * LSM_JOB_ERROR
 *              Job complete with error.
 *          * LSM_JOB_INPROGRESS
 *              Job is still in progress.
 * @percent_complete:
 *      Output pointer of uint8_t. Percent job complete. Domain 0..100.
 * @fs:
 *      Output pointer of lsm_fs for completed operation.
 *      Returned value must be freed with a call to lsm_fs_record_free().
 * @flags:
 *      Reserved for future use, must be LSM_CLIENT_FLAG_RSVD.
 *
 * Return:
 *      Error code as enumerated by 'lsm_error_number'.
 *          * LSM_ERR_OK
 *              On success.
 *          * LSM_ERR_INVALID_ARGUMENT
 *              When any argument is NULL or not a valid lsm_connect pointer
 *              or invalid flags.
 *          * LSM_ERR_NOT_FOUND_JOB
 *              When job not found.
 */
int LSM_DLL_EXPORT lsm_job_status_fs_get(lsm_connect *conn, const char *job_id,
                                         lsm_job_status *status,
                                         uint8_t *percent_complete, lsm_fs **fs,
                                         lsm_flag flags);

/**
 * lsm_job_status_ss_get - Check on the status of a job with lsm_fs_ss
 * returned.
 *
 * Version:
 *      1.0
 *
 * Description:
 *      Check on the status of a job and return the snapshot information when
 *      compete.
 *
 * @conn:
 *      Valid connection pointer
 * @job:
 *      String. Job id to check
 * @status:
 *      Output pointer of lsm_job_status. Possible values are:
 *          * LSM_JOB_COMPLETE
 *              Job complete with no error.
 *          * LSM_JOB_ERROR
 *              Job complete with error.
 *          * LSM_JOB_INPROGRESS
 *              Job is still in progress.
 * @percent_complete:
 *      Output pointer of uint8_t. Percent job complete. Domain 0..100.
 * @ss:
 *      Output pointer of lsm_fs_ss for completed operation.
 *      Returned value must be freed with a call to lsm_fs_ss_record_free().
 * @flags:
 *      Reserved for future use, must be LSM_CLIENT_FLAG_RSVD.
 *
 * Return:
 *      Error code as enumerated by 'lsm_error_number'.
 *          * LSM_ERR_OK
 *              On success.
 *          * LSM_ERR_INVALID_ARGUMENT
 *              When any argument is NULL or not a valid lsm_connect pointer
 *              or invalid flags.
 *          * LSM_ERR_NOT_FOUND_JOB
 *              When job not found.
 */
int LSM_DLL_EXPORT lsm_job_status_ss_get(lsm_connect *conn, const char *job,
                                         lsm_job_status *status,
                                         uint8_t *percent_complete,
                                         lsm_fs_ss **ss, lsm_flag flags);

/**
 * lsm_job_free - Frees the resources used by a job.
 *
 * Version:
 *      1.0
 *
 * Description:
 *      Frees the resources used by a job.
 *
 * @conn:
 *      Valid connection pointer
 * @job_id:
 *      String. Job ID.
 *      Note: The memory used for the job id string will be freed during this
 *      call and job_id will be set to NULL.
 * @flags:
 *      Reserved for future use, must be LSM_CLIENT_FLAG_RSVD.
 *
 * Return:
 *      Error code as enumerated by 'lsm_error_number'.
 *          * LSM_ERR_OK
 *              On success.
 *          * LSM_ERR_INVALID_ARGUMENT
 *              When any argument is NULL or not a valid lsm_connect pointer
 *              or invalid flags.
 *          * LSM_ERR_NOT_FOUND_JOB
 *              When job not found.
 *
 */
int LSM_DLL_EXPORT lsm_job_free(lsm_connect *conn, char **job_id,
                                lsm_flag flags);

/**
 * lsm_capabilities - Query the capabilities of the storage array.
 *
 * Version:
 *      1.0
 *
 * Description:
 *      Query the capabilities of the storage array.
 *      Capability is used to indicate whether certain functionality is
 *      supported by specified storage system. Please check desired function for
 *      required capability. To verify capability is supported, use
 *      lsm_capability_supported().
 *      If the functionality is not listed in the enumerated capability type
 *      (lsm_capability_type) then that functionality is mandatory and required
 *      to exist.
 *
 * @conn:
 *      Valid lsm_connect pointer.
 * @system:
 *      System of interest
 * @cap:
 *      Output pointer of lsm_storage_capabilities. The storage array
 *      capabilities.
 *      The returned value must be freed with a call to
 *      lsm_capability_record_free().
 * @flags:
 *      Reserved for future use, must be LSM_CLIENT_FLAG_RSVD.
 *
 * Return:
 *      Error code as enumerated by 'lsm_error_number'.
 *          * LSM_ERR_OK
 *              On success.
 *          * LSM_ERR_INVALID_ARGUMENT
 *              When any argument is NULL or not a valid lsm_connect pointer
 *              or not a valid lsm_system or invalid flags.
 *          * LSM_ERR_NOT_FOUND_SYSTEM
 *              When the specified system does not exist.
 */
int LSM_DLL_EXPORT lsm_capabilities(lsm_connect *conn, lsm_system *system,
                                    lsm_storage_capabilities **cap,
                                    lsm_flag flags);

/**
 * lsm_pool_list - Query the list of storage pools on this connection.
 *
 * Version:
 *      1.0
 *
 * Description:
 *      Query the list of storage pools on this connection.
 *      Pool is the only place a volume or a file system could created from.
 *      Pool properties could be retrieved by these functions:
 *          * lsm_pool_id_get()
 *          * lsm_pool_name_get()
 *          * lsm_pool_system_id_get()
 *          * lsm_pool_free_space_get()
 *          * lsm_pool_total_space_get()
 *          * lsm_pool_status_get()
 *          * lsm_pool_status_info_get()
 *          * lsm_pool_unsupported_actions_get()
 *          * lsm_pool_element_type_get()
 *
 * @conn:
 *      Valid lsm_connect pointer.
 * @search_key:
 *      Search key(NULL for all). Valid search keys are: "id", "system_id".
 * @search_value:
 *      Search value.
 * @pool_array:
 *      Output pointer of lsm_pool array. It should be manually freed by
 *      lsm_pool_record_array_free().
 * @count:
 *      Output pointer of uint32_t. Number of storage pools.
 * @flags:
 *      Reserved for future use, must be LSM_CLIENT_FLAG_RSVD.
 *
 * Return:
 *      Error code as enumerated by 'lsm_error_number'.
 *          * LSM_ERR_OK
 *              On success or searched value not found.
 *          * LSM_ERR_INVALID_ARGUMENT
 *              When any argument is NULL or invalid flags or invalid search
 *              key.
 */
int LSM_DLL_EXPORT lsm_pool_list(lsm_connect *conn, char *search_key,
                                 char *search_value, lsm_pool **pool_array[],
                                 uint32_t *count, lsm_flag flags);

/**
 * lsm_volume_list - Gets a list of volumes on this connection.
 *
 * Version:
 *      1.0
 *
 * Description:
 *      Gets a list of volumes on this connection.
 *      Volume properties could be retrieved by these functions:
 *          * lsm_volume_id_get()
 *          * lsm_volume_name_get()
 *          * lsm_volume_system_id_get()
 *          * lsm_volume_vpd83_get()
 *          * lsm_volume_number_of_blocks_get()
 *          * lsm_volume_block_size_get()
 *          * lsm_volume_admin_state_get()
 *          * lsm_volume_pool_id_get()
 *
 * Capability:
 *      LSM_CAP_VOLUMES
 *
 * @conn:
 *      Valid lsm_connect pointer.
 * @search_key:
 *      Search key(NULL for all).
 *      Valid search keys are: "id", "system_id" and "pool_id".
 * @search_value:
 *      Search value.
 * @volumes:
 *      Output pointer of lsm_volume array. It should be manually freed by
 *      lsm_volume_record_array_free().
 * @count:
 *      Output pointer of uint32_t. Number of volumes.
 * @flags:
 *      Reserved for future use, must be LSM_CLIENT_FLAG_RSVD.
 *
 * Return:
 *      Error code as enumerated by 'lsm_error_number'.
 *          * LSM_ERR_OK
 *              On success or searched value not found.
 *          * LSM_ERR_INVALID_ARGUMENT
 *              When any argument is NULL or invalid flags or invalid search
 *              key.
 *          * LSM_ERR_NO_SUPPORT
 *              Not supported.
 */
int LSM_DLL_EXPORT lsm_volume_list(lsm_connect *conn, const char *search_key,
                                   const char *search_value,
                                   lsm_volume **volumes[], uint32_t *count,
                                   lsm_flag flags);

/**
 * lsm_disk_list - Gets a list of disks on this connection.
 *
 * Version:
 *      1.0
 *
 * Description:
 *      Gets a list of disks on this connection.
 *      Disk properties could be retrieved by these functions:
 *          * lsm_disk_id_get()
 *          * lsm_disk_name_get()
 *          * lsm_disk_system_id_get()
 *          * lsm_disk_type_get()
 *          * lsm_disk_number_of_blocks_get()
 *          * lsm_disk_block_size_get()
 *          * lsm_disk_status_get()
 *          * lsm_disk_location_get()
 *          * lsm_disk_rpm_get()
 *          * lsm_disk_link_type_get()
 *          * lsm_disk_vpd83_get()
 *
 * Capability:
 *      LSM_CAP_DISKS
 *
 * @conn:
 *      Valid lsm_connect pointer.
 * @search_key:
 *      Search key(NULL for all).
 *      Valid search keys are: "id", "system_id".
 * @search_value:
 *      Search value.
 * @disks:
 *      Output pointer of lsm_disk array. It should be manually freed by
 *      lsm_disk_record_array_free().
 * @count:
 *      Output pointer of uint32_t. Number of disks.
 * @flags:
 *      Reserved for future use, must be LSM_CLIENT_FLAG_RSVD.
 *
 * Return:
 *      Error code as enumerated by 'lsm_error_number'.
 *          * LSM_ERR_OK
 *              On success or searched value not found.
 *          * LSM_ERR_INVALID_ARGUMENT
 *              When any argument is NULL or invalid flags or invalid search
 *              key.
 *          * LSM_ERR_NO_SUPPORT
 *              Not supported.
 */
int LSM_DLL_EXPORT lsm_disk_list(lsm_connect *conn, const char *search_key,
                                 const char *search_value, lsm_disk **disks[],
                                 uint32_t *count, lsm_flag flags);

/**
 * lsm_volume_create - Creates a new volume
 *
 * Version:
 *      1.0
 *
 * Description:
 *      Creates a new volume(also known as LUN).
 *
 * Capability:
 *      LSM_CAP_VOLUME_CREATE
 *
 * @conn:
 *      Valid lsm_connect pointer.
 * @pool:
 *      Pointer of lsm_pool.
 * @volume_name:
 *      String. Human recognizable name, might be altered or ignored by certain
 *      storage systems.
 * @size:
 *      uint64_t. Size of new volume in bytes, actual size might be bigger than
 *      requested and will be based on array rounding to block size,
 * @provisioning:
 *      lsm_volume_provision_type. Type of volume provisioning to use. Valid
 *      values are:
 *          * LSM_VOLUME_PROVISION_DEFAULT
 *              Let storage system to decided.
 *          * LSM_VOLUME_PROVISION_FULL
 *              Create new fully allocated volume.
 *          * LSM_VOLUME_PROVISION_THIN
 *              Create new thin provisioning volume.
 * @new_volume:
 *      Output pointer of lsm_volume. Will be NULL if storage system support
 *      asynchronous action on this.
 *      Memory must be freed with a call to lsm_volume_record_free().
 * @job:
 *      Output pointer of string. If storage system support asynchronous
 *      action on this, a job will be created and could be tracked via
 *      lsm_job_status_volume_get().
 *      NULL if storage system does not support asynchronous action on this.
 * @flags:
 *      Reserved for future use, must be LSM_CLIENT_FLAG_RSVD.
 *
 * Return:
 *      Error code as enumerated by 'lsm_error_number'.
 *          * LSM_ERR_OK
 *              On success.
 *          * LSM_ERR_JOB_STARTED
 *              A job is started. Please check the 'job' output pointer.
 *          * LSM_ERR_INVALID_ARGUMENT
 *              When any argument is NULL or not a valid lsm_connect pointer
 *              or invalid flags or invalid provisioning type.
 *          * LSM_ERR_NOT_FOUND_POOL
 *              When pool not found.
 *          * LSM_ERR_NOT_ENOUGH_SPACE
 *              Pool does not have enough space.
 *          * LSM_ERR_POOL_NOT_READY
 *              Pool is not ready for volume creation.
 *          * LSM_ERR_NO_SUPPORT
 *              Not supported.
 */
int LSM_DLL_EXPORT lsm_volume_create(lsm_connect *conn, lsm_pool *pool,
                                     const char *volume_name, uint64_t size,
                                     lsm_volume_provision_type provisioning,
                                     lsm_volume **new_volume, char **job,
                                     lsm_flag flags);

/**
 * lsm_volume_resize - Resize an existing volume.
 *
 * Version:
 *      1.0
 *
 * Description:
 *      Resize the specified volume.
 *      Some pool might not allow volume resize, please check
 *      lsm_pool_unsupported_actions_get() for LSM_POOL_UNSUPPORTED_VOLUME_GROW
 *      and LSM_POOL_UNSUPPORTED_VOLUME_SHRINK before invoking this function.
 *
 * Capability:
 *      LSM_CAP_VOLUME_RESIZE
 *
 * @conn:
 *      Valid lsm_connect pointer.
 * @volume:
 *      lsm_volume. Volume to re-size
 * @new_size:
 *      uint64_t. New size of volume in bytes, actual size might be bigger than
 *      requested and will be based on array rounding to block size,
 * @resized_volume:
 *      Output pointer of updated lsm_volume. Might be NULL if storage system
 *      support asynchronous action on this.
 *      Memory must be freed with a call to lsm_volume_record_free().
 * @job:
 *      Output pointer of string. If storage system support asynchronous action
 *      on this, a job will be created and could be tracked via
 *      lsm_job_status_volume_get(). NULL if storage system does not support
 *      asynchronous action on this.
 * @flags:
 *      Reserved for future use, must be LSM_CLIENT_FLAG_RSVD.
 *
 * Return:
 *      Error code as enumerated by 'lsm_error_number'.
 *          * LSM_ERR_OK
 *              On success.
 *          * LSM_ERR_JOB_STARTED
 *              A job is started. Please check the 'job' output pointer.
 *          * LSM_ERR_INVALID_ARGUMENT
 *              When any argument is NULL or not a valid lsm_connect pointer
 *              or invalid flags.
 *          * LSM_ERR_NOT_FOUND_POOL
 *              When pool not found.
 *          * LSM_ERR_NOT_FOUND_VOLUME
 *              When volume not found.
 *          * LSM_ERR_NOT_ENOUGH_SPACE
 *              Pool does not have enough space.
 *          * LSM_ERR_POOL_NOT_READY
 *              Pool is not ready for volume resizing.
 *          * LSM_ERR_NO_STATE_CHANGE
 *              Requested size if identical to current size.
 *          * LSM_ERR_NO_SUPPORT
 *              Not supported.
 */
int LSM_DLL_EXPORT lsm_volume_resize(lsm_connect *conn, lsm_volume *volume,
                                     uint64_t new_size,
                                     lsm_volume **resized_volume, char **job,
                                     lsm_flag flags);

/**
 * lsm_volume_replicate - Replicates a volume
 *
 * Version:
 *      1.0
 *
 * Description:
 *      Replicates a volume.
 *      To delete the new target volume, please use lsm_volume_delete().
 *      If any volume is the replication source, it is only deletable when
 *      lsm_volume_child_dependency() returns 0.
 *
 * Capability:
 *      LSM_CAP_VOLUME_REPLICATE
 *      LSM_CAP_VOLUME_REPLICATE_CLONE
 *      LSM_CAP_VOLUME_REPLICATE_COPY
 *      LSM_CAP_VOLUME_REPLICATE_MIRROR_ASYNC
 *      LSM_CAP_VOLUME_REPLICATE_MIRROR_SYNC
 * @conn:
 *      Valid lsm_connect pointer.
 * @pool:
 *      Pointer of lsm_pool where the new replicate target volume stored in.
 *      If NULL, target volume will be reside in the same pool of source volume.
 * @rep_type:
 *      lsm_replication_type. Valid values are:
 *          * LSM_VOLUME_REPLICATE_CLONE
 *              Point in time read writeable space efficient copy of
 *              data. Also know as read writeable snapshot.
 *          * LSM_VOLUME_REPLICATE_COPY
 *              Full bitwise copy of the data (occupies full space).
 *          * LSM_VOLUME_REPLICATE_MIRROR_ASYNC
 *              I/O will be blocked until I/O reached source storage systems.
 *              The source storage system will use copy the changes data to
 *              target system in a predefined interval. There will be a small
 *              data differences between source and target.
 *          * LSM_VOLUME_REPLICATE_MIRROR_SYNC
 *              I/O will be blocked until I/O reached both source and
 *              target storage systems. There will be no data difference
 *              between source and target storage systems.
 *
 * @volume_src:
 *      Pointer of replication source lsm_volume.
 * @name:
 *      String. Human recognizable name, might be altered or ignored by certain
 *      storage system.
 * @new_replicant:
 *      Output pointer of new replication target lsm_volume. Will be NULL if
 *      storage system support asynchronous action on this.
 *      Memory must be freed with a call to lsm_volume_record_free().
 * @job:
 *      Output pointer of string. If storage system support asynchronous action
 *      on this, a job will be created and could be tracked via
 *      lsm_job_status_volume_get(). NULL if storage system does not support
 *      asynchronous action on this.
 * @flags:
 *      Reserved for future use, must be LSM_CLIENT_FLAG_RSVD.
 *
 * Return:
 *      Error code as enumerated by 'lsm_error_number'.
 *          * LSM_ERR_OK
 *              On success.
 *          * LSM_ERR_JOB_STARTED
 *              A job is started. Please check the 'job' output pointer.
 *          * LSM_ERR_INVALID_ARGUMENT
 *              When any argument is NULL or not a valid lsm_connect pointer
 *              or invalid flags or invalid replication type.
 *          * LSM_ERR_NOT_FOUND_VOLUME
 *              When volume not found.
 *          * LSM_ERR_NOT_FOUND_POOL
 *              When pool not found.
 *          * LSM_ERR_NOT_ENOUGH_SPACE
 *              Pool does not have enough space.
 *          * LSM_ERR_POOL_NOT_READY
 *              Pool is not ready.
 *          * LSM_ERR_NO_SUPPORT
 *              Not supported.
 */
int LSM_DLL_EXPORT lsm_volume_replicate(lsm_connect *conn, lsm_pool *pool,
                                        lsm_replication_type rep_type,
                                        lsm_volume *volume_src,
                                        const char *name,
                                        lsm_volume **new_replicant, char **job,
                                        lsm_flag flags);

/**
 * lsm_volume_replicate_range_block_size - Block size for replicate range.
 *
 * Version:
 *      1.0
 *
 * Description:
 *      Block size for the lsm_volume_replicate_range().
 *
 * Capability:
 *      LSM_CAP_VOLUME_COPY_RANGE_BLOCK_SIZE
 *
 * @conn:
 *      Valid connection.
 * @system:
 *      The pointer of lsm_system.
 * @bs:
 *      Output pointer of uint32_t. Block size in bytes.
 * @flags:
 *      Reserved for future use, must be LSM_CLIENT_FLAG_RSVD.
 *
 * Return:
 *      Error code as enumerated by 'lsm_error_number'.
 *          * LSM_ERR_OK
 *              On success.
 *          * LSM_ERR_INVALID_ARGUMENT
 *              When any argument is NULL or not a valid lsm_connect pointer
 *              or invalid flags or invalid system pointer.
 *          * LSM_ERR_NOT_FOUND_SYSTEM
 *              When system not found.
 *          * LSM_ERR_NO_SUPPORT
 *              Not supported.
 */
int LSM_DLL_EXPORT lsm_volume_replicate_range_block_size(lsm_connect *conn,
                                                         lsm_system *system,
                                                         uint32_t *bs,
                                                         lsm_flag flags);

/**
 * lsm_volume_replicate_range - Replicates a portion of a volume to a volume.
 *
 * Version:
 *      1.0
 *
 * Description:
 *      Replicates a portion of specified source volume to target volume.
 *
 * Capability:
 *      LSM_CAP_VOLUME_COPY_RANGE
 *      LSM_CAP_VOLUME_COPY_RANGE_CLONE
 *      LSM_CAP_VOLUME_COPY_RANGE_COPY
 *
 * @conn:
 *      Valid connection.
 * @rep_type:
 *      lsm_replication_type. Valid values are:
 *          * LSM_VOLUME_REPLICATE_CLONE
 *              Point in time read writeable space efficient copy of
 *              data. Also know as read writeable snapshot.
 *          * LSM_VOLUME_REPLICATE_COPY
 *              Full bitwise copy of the data (occupies full space).
 * @source:
 *      Pointer of replication source lsm_volume.
 * @dest:
 *      Pointer of replication target lsm_volume. Could be the same as source.
 * @ranges:
 *      Array of lsm_block_range. Please use
 *      lsm_block_range_record_array_alloc() and lsm_block_range_record_alloc()
 *      to create it.
 * @num_ranges:
 *      uint32_t. Number of entries in ranges.
 * @job:
 *      Output pointer of string. If storage system support asynchronous
 *      action on this, a job will be created and could be tracked
 *      via lsm_job_status_get(). NULL if storage system does not support
 *      asynchronous action on this.
 * @flags:
 *      Reserved for future use, must be LSM_CLIENT_FLAG_RSVD.
 *
 * Return:
 *      Error code as enumerated by 'lsm_error_number'.
 *          * LSM_ERR_OK
 *              On success.
 *          * LSM_ERR_JOB_STARTED
 *              A job is started. Please check the 'job' output pointer.
 *          * LSM_ERR_INVALID_ARGUMENT
 *              When any argument is NULL or not a valid lsm_connect pointer
 *              or invalid flags or invalid replication type.
 *          * LSM_ERR_NOT_FOUND_VOLUME
 *              When volume not found.
 *          * LSM_ERR_NOT_FOUND_POOL
 *              When pool not found.
 *          * LSM_ERR_POOL_NOT_READY
 *              Pool is not ready.
 *          * LSM_ERR_NO_SUPPORT
 *              Not supported.
 */
int LSM_DLL_EXPORT lsm_volume_replicate_range(
    lsm_connect *conn, lsm_replication_type rep_type, lsm_volume *source,
    lsm_volume *dest, lsm_block_range **ranges, uint32_t num_ranges, char **job,
    lsm_flag flags);

/**
 * lsm_volume_delete - Delete a volume.
 *
 * Version:
 *      1.0
 *
 * Description:
 *      Deletes a volume/LUN and its data is lost!
 *      If specified volume is masked to any access_group, it cannot be deleted.
 *      You may use `lsm_access_groups_granted_to_volume()` and
 *      `lsm_volume_unmask()` before `lsm_volume_delete()`.
 *      If specified volume is has child dependency, it cannot be deleted.
 *      You may use `lsm_volume_child_dependency()` and
 *      `lsm_volume_child_dependency_delete()` before `lsm_volume_delete()`.
 *
 * Capability:
 *      LSM_CAP_VOLUME_DELETE
 *
 * @conn:
 *      Valid lsm_connect pointer.
 * @volume:
 *      Pointer of lsm_volume that is to be deleted.
 * @job:
 *      Output pointer of string. If storage system support asynchronous
 *      action on this, a job will be created and could be tracked
 *      via lsm_job_status_get(). NULL if storage system does not support
 *      asynchronous action on this.
 * @flags:
 *      Reserved for future use, must be LSM_CLIENT_FLAG_RSVD.
 *
 * Return:
 *      Error code as enumerated by 'lsm_error_number'.
 *          * LSM_ERR_OK
 *              On success.
 *          * LSM_ERR_JOB_STARTED
 *              A job is started. Please check the 'job' output pointer.
 *          * LSM_ERR_INVALID_ARGUMENT
 *              When any argument is NULL or not a valid lsm_connect pointer
 *              or invalid flags.
 *          * LSM_ERR_NOT_FOUND_VOLUME
 *              When volume not found.
 *          * LSM_ERR_POOL_NOT_READY
 *              Pool is not ready.
 *          * LSM_ERR_NO_SUPPORT
 *              Not supported.
 *          * LSM_ERR_HAS_CHILD_DEPENDENCY
 *              Specified volume has child dependencies.
 */
int LSM_DLL_EXPORT lsm_volume_delete(lsm_connect *conn, lsm_volume *volume,
                                     char **job, lsm_flag flags);

/**
 * lsm_volume_enable - Set a Volume to online
 *
 * Version:
 *      1.0
 *
 * Description:
 *      Enable the specified volume when that volume is disabled by
 *      administrator or via lsm_volume_disable().
 *
 * Capability:
 *      LSM_CAP_VOLUME_ENABLE
 *
 * @conn:
 *      Valid lsm_connect pointer.
 * @volume:
 *      The pointer of lsm_volume to be enabled.
 * @flags:
 *      Reserved for future use, must be LSM_CLIENT_FLAG_RSVD.
 *
 * Return:
 *      Error code as enumerated by 'lsm_error_number'.
 *          * LSM_ERR_OK
 *              On success.
 *          * LSM_ERR_INVALID_ARGUMENT
 *              When any argument is NULL or not a valid lsm_connect pointer
 *              or invalid flags.
 *          * LSM_ERR_NOT_FOUND_VOLUME
 *              When volume not found.
 *          * LSM_ERR_POOL_NOT_READY
 *              Pool is not ready.
 *          * LSM_ERR_NO_SUPPORT
 *              Not supported.
 */
int LSM_DLL_EXPORT lsm_volume_enable(lsm_connect *conn, lsm_volume *volume,
                                     lsm_flag flags);

/**
 * lsm_volume_disable - Set a Volume to offline
 *
 * Version:
 *      1.0
 *
 * Description:
 *      Disable the read and write access to the specified volume.
 *
 * Capability:
 *      LSM_CAP_VOLUME_DISABLE
 *
 * @conn:
 *      Valid lsm_connect pointer.
 * @volume:
 *      The pointer of lsm_volume to be disabled.
 * @flags:
 *      Reserved for future use, must be LSM_CLIENT_FLAG_RSVD.
 *
 * Return:
 *      Error code as enumerated by 'lsm_error_number'.
 *          * LSM_ERR_OK
 *              On success.
 *          * LSM_ERR_INVALID_ARGUMENT
 *              When any argument is NULL or not a valid lsm_connect pointer
 *              or invalid flags.
 *          * LSM_ERR_NOT_FOUND_VOLUME
 *              When volume not found.
 *          * LSM_ERR_POOL_NOT_READY
 *              Pool is not ready.
 *          * LSM_ERR_NO_SUPPORT
 *              Not supported.
 */
int LSM_DLL_EXPORT lsm_volume_disable(lsm_connect *conn, lsm_volume *volume,
                                      lsm_flag flags);

/**
 * lsm_iscsi_chap_auth - Set iSCSI CHAP authentication.
 *
 * Version:
 *      1.0
 *
 * Description:
 *      Set the username password for iSCSI CHAP authentication, inbound and
 *      outbound.
 *
 * @conn:
 *      Valid connection pointer
 * @init_id:
 *      String. The iSCSI Initiator IQN.
 * @in_user:
 *      String. The inbound user name
 * @in_password:
 *      String. The inbound password
 * @out_user:
 *      String. The outbound user name
 * @out_password:
 *      String. The outbound password
 * @flags:
 *      Reserved for future use, must be LSM_CLIENT_FLAG_RSVD.
 *
 * Return:
 *      Error code as enumerated by 'lsm_error_number'.
 *          * LSM_ERR_OK
 *              On success.
 *          * LSM_ERR_INVALID_ARGUMENT
 *              When not a valid lsm_connect pointer or invalid flags.
 *          * LSM_ERR_NO_SUPPORT
 *              Not supported.
 */
int LSM_DLL_EXPORT lsm_iscsi_chap_auth(lsm_connect *conn, const char *init_id,
                                       const char *in_user,
                                       const char *in_password,
                                       const char *out_user,
                                       const char *out_password,
                                       lsm_flag flags);

/**
 * lsm_access_group_list - Retrieves a list of access groups.
 *
 * Version:
 *      1.0
 *
 * Description:
 *      Gets a list of access group on this connection. Access group
 *      is also known as host group on some storage system, it defines a group
 *      of initiators sharing the same access to the volume.
 *      Access group properties could be retrieved by these functions:
 *          * lsm_access_group_id_get()
 *          * lsm_access_group_name_get()
 *          * lsm_access_group_system_id_get()
 *          * lsm_access_group_initiator_id_get()
 *
 * Capability:
 *      LSM_CAP_ACCESS_GROUPS
 *
 * @conn:
 *      Valid lsm_connect pointer.
 * @search_key:
 *      Search key(NULL for all).
 *      Valid search keys are: "id", "system_id".
 * @search_value:
 *      Search value.
 * @groups:
 *      Output pointer of lsm_access_group array. It should be manually freed by
 *      lsm_access_group_record_array_free().
 * @group_count:
 *      Output pointer of uint32_t. Number of access groups.
 * @flags:
 *      Reserved for future use, must be LSM_CLIENT_FLAG_RSVD.
 *
 * Return:
 *      Error code as enumerated by 'lsm_error_number'.
 *          * LSM_ERR_OK
 *              On success.
 *          * LSM_ERR_INVALID_ARGUMENT
 *              When any argument is NULL or invalid flags or invalid search
 *              key.
 *          * LSM_ERR_NO_SUPPORT
 *              Not supported.
 */
int LSM_DLL_EXPORT lsm_access_group_list(lsm_connect *conn,
                                         const char *search_key,
                                         const char *search_value,
                                         lsm_access_group **groups[],
                                         uint32_t *group_count, lsm_flag flags);

/**
 * lsm_access_group_create - Create a new access group.
 *
 * Version:
 *      1.0
 *
 * Description:
 *      Creates a new access group with one initiator in it. You may expand
 *      the access group by adding more initiators via
 *      lsm_access_group_initiator_add().
 *
 * Capability:
 *      LSM_CAP_ACCESS_GROUP_CREATE_WWPN
 *      LSM_CAP_ACCESS_GROUP_CREATE_ISCSI_IQN
 *
 * @conn:
 *      Valid lsm_connect pointer.
 * @name:
 *      String. Human recognizable name, might be altered or ignored by certain
 *      storage system.
 * @init_id:
 *      String. Initiator id to be added to group.
 * @init_type:
 *      lsm_access_group_init_type. Valid initiator types are:
 *          * LSM_ACCESS_GROUP_INIT_TYPE_ISCSI_IQN
 *              iSCSI IQN.
 *          * LSM_ACCESS_GROUP_INIT_TYPE_WWPN
 *              FC WWPN
 * @system:
 *      Pointer of lsm_system to create access group for.
 * @access_group:
 *      Output pointer of newly created lsm_access_group.
 *      Returned value must be freed with function
 *      lsm_access_group_record_free().
 * @flags:
 *      Reserved for future use, must be LSM_CLIENT_FLAG_RSVD.
 *
 * Return:
 *      Error code as enumerated by 'lsm_error_number'.
 *          * LSM_ERR_OK
 *              On success.
 *          * LSM_ERR_INVALID_ARGUMENT
 *              When any argument is NULL or invalid flags or invalid system
 *              pointer or invalid init_type or illegal initiator ID string.
 *          * LSM_ERR_NO_SUPPORT
 *              Not supported.
 *          * LSM_ERR_NOT_FOUND_SYSTEM
 *              When the specified system does not exist.
 */
int LSM_DLL_EXPORT lsm_access_group_create(lsm_connect *conn, const char *name,
                                           const char *init_id,
                                           lsm_access_group_init_type init_type,
                                           lsm_system *system,
                                           lsm_access_group **access_group,
                                           lsm_flag flags);

/**
 * lsm_access_group_delete - Deletes an access group.
 *
 * Version:
 *      1.0
 *
 * Description:
 *      Deletes an access group. Only access group with no volume masked can
 *      be deleted.
 *
 * Capability:
 *      LSM_CAP_ACCESS_GROUP_DELETE
 *
 * @conn:
 *      Valid lsm_connect pointer.
 * @access_group:
 *      Pointer of lsm_access_group to be deleted.
 * @flags:
 *      Reserved for future use, must be LSM_CLIENT_FLAG_RSVD.
 *
 * Return:
 *      Error code as enumerated by 'lsm_error_number'.
 *          * LSM_ERR_OK
 *              On success.
 *          * LSM_ERR_INVALID_ARGUMENT
 *              When any argument is NULL or not a valid lsm_connect pointer
 *              or invalid flags.
 *          * LSM_ERR_NOT_FOUND_ACCESS_GROUP
 *              When access group not found.
 *          * LSM_ERR_IS_MASKED
 *              When access group has volume masked.
 *          * LSM_ERR_NO_SUPPORT
 *              Not supported.
 */
int LSM_DLL_EXPORT lsm_access_group_delete(lsm_connect *conn,
                                           lsm_access_group *access_group,
                                           lsm_flag flags);

/**
 * lsm_access_group_initiator_add - Adds an initiator to the access group
 *
 * Version:
 *      1.0
 *
 * Description:
 *      Add an initiator to the specified access group.
 *
 * Capability:
 *      LSM_CAP_ACCESS_GROUP_INITIATOR_ADD_WWPN
 *      LSM_CAP_ACCESS_GROUP_INITIATOR_ADD_ISCSI_IQN
 *
 * @conn:
 *      Valid lsm_connect pointer.
 * @access_group:
 *      Pointer of lsm_access_group to modify.
 * @init_id:
 *      String. Initiator id to be added to group.
 * @init_type:
 *      lsm_access_group_init_type. Valid initiator types are:
 *          * LSM_ACCESS_GROUP_INIT_TYPE_ISCSI_IQN
 *              iSCSI IQN.
 *          * LSM_ACCESS_GROUP_INIT_TYPE_WWPN
 *              FC WWPN
 * @updated_access_group:
 *      Output pointer of the updated lsm_access_group.
 *      Returned value must be freed with lsm_access_group_record_free().
 * @flags:
 *      Reserved for future use, must be LSM_CLIENT_FLAG_RSVD.
 *
 * Return:
 *      Error code as enumerated by 'lsm_error_number'.
 *          * LSM_ERR_OK
 *              On success.
 *          * LSM_ERR_INVALID_ARGUMENT
 *              When any argument is NULL or not a valid lsm_connect pointer
 *              or invalid flags or invalid lsm_access_group pointer or
 *              illegal initiator or invalid init_type.
 *          * LSM_ERR_NOT_FOUND_ACCESS_GROUP
 *              When access group not found.
 *          * LSM_ERR_EXISTS_INITIATOR
 *              When specified initiator is in other access group.
 *          * LSM_ERR_NO_STATE_CHANGE
 *              When specified initiator is already in specified access group.
 *          * LSM_ERR_NO_SUPPORT
 *              Not supported.
 */
int LSM_DLL_EXPORT lsm_access_group_initiator_add(
    lsm_connect *conn, lsm_access_group *access_group, const char *init_id,
    lsm_access_group_init_type init_type,
    lsm_access_group **updated_access_group, lsm_flag flags);

/**
 * lsm_access_group_initiator_delete - Deletes an initiator from an access group
 *
 * Version:
 *      1.0
 *
 * Description:
 *      Delete an initiator to the specified access group.
 *
 * Capability:
 *      LSM_CAP_ACCESS_GROUP_INITIATOR_DELETE
 *
 * @conn:
 *      Valid lsm_connect pointer.
 * @access_group:
 *      Pointer of lsm_access_group to modify.
 * @initiator_id:
 *      String. Initiator id to be deleted from group.
 * @init_type:
 *      lsm_access_group_init_type. Valid initiator types are:
 *          * LSM_ACCESS_GROUP_INIT_TYPE_ISCSI_IQN
 *              iSCSI IQN.
 *          * LSM_ACCESS_GROUP_INIT_TYPE_WWPN
 *              FC WWPN
 * @updated_access_group:
 *      Output pointer of the updated lsm_access_group.
 *      Returned value must be freed with lsm_access_group_record_free().
 * @flags:
 *      Reserved for future use, must be LSM_CLIENT_FLAG_RSVD.
 *
 * Return:
 *      Error code as enumerated by 'lsm_error_number'.
 *          * LSM_ERR_OK
 *              On success.
 *          * LSM_ERR_INVALID_ARGUMENT
 *              When any argument is NULL or not a valid lsm_connect pointer
 *              or invalid flags or invalid lsm_access_group pointer or
 *              illegal initiator or invalid init_type.
 *          * LSM_ERR_NOT_FOUND_ACCESS_GROUP
 *              When access group not found.
 *          * LSM_ERR_NO_STATE_CHANGE
 *              When specified initiator is not in specified access group.
 *          * LSM_ERR_LAST_INIT_IN_ACCESS_GROUP
 *              When specified initiator is the last initiator in specified
 *              access group. Please use lsm_access_group_delete() instead.
 *          * LSM_ERR_NO_SUPPORT
 *              Not supported.
 */
int LSM_DLL_EXPORT lsm_access_group_initiator_delete(
    lsm_connect *conn, lsm_access_group *access_group, const char *initiator_id,
    lsm_access_group_init_type init_type,
    lsm_access_group **updated_access_group, lsm_flag flags);

/**
 * lsm_volume_mask - Grants access to a volume for the specified group
 * Version:
 *      1.0
 *
 * Description:
 *      Grants access to a volume for the specified group, also known as
 *      LUN masking or mapping.
 *
 * Capability:
 *      LSM_CAP_VOLUME_MASK
 *
 * @conn:
 *      Valid connection.
 * @access_group:
 *      Pointer of lsm_access_group.
 * @volume:
 *      Pointer of lsm_volume.
 * @flags:
 *      Reserved for future use, must be LSM_CLIENT_FLAG_RSVD.
 *
 * Return:
 *      Error code as enumerated by 'lsm_error_number'.
 *          * LSM_ERR_OK
 *              On success.
 *          * LSM_ERR_INVALID_ARGUMENT
 *              When any argument is NULL or not a valid lsm_connect pointer
 *              or invalid flags or invalid lsm_access_group pointer or
 *              invalid lsm_volume.
 *          * LSM_ERR_NOT_FOUND_ACCESS_GROUP
 *              When access group not found.
 *          * LSM_ERR_NOT_FOUND_VOLUME
 *              When volume not found.
 *          * LSM_ERR_NO_STATE_CHANGE
 *              When specified volume is already masked to specified access
 *              group.
 *          * LSM_ERR_NO_SUPPORT
 *              Not supported.
 */
int LSM_DLL_EXPORT lsm_volume_mask(lsm_connect *conn,
                                   lsm_access_group *access_group,
                                   lsm_volume *volume, lsm_flag flags);

/**
 * lsm_volume_unmask - Revokes access to a volume for the specified group
 * Version:
 *      1.0
 *
 * Description:
 *      Revokes access to a volume for the specified group.
 *
 * Capability:
 *      LSM_CAP_VOLUME_UNMASK
 *
 * @conn:
 *      Valid connection.
 * @access_group:
 *      Pointer of lsm_access_group.
 * @volume:
 *      Pointer of lsm_volume.
 * @flags:
 *      Reserved for future use, must be LSM_CLIENT_FLAG_RSVD.
 *
 * Return:
 *      Error code as enumerated by 'lsm_error_number'.
 *          * LSM_ERR_OK
 *              On success.
 *          * LSM_ERR_INVALID_ARGUMENT
 *              When any argument is NULL or not a valid lsm_connect pointer
 *              or invalid flags or invalid lsm_access_group pointer or
 *              invalid lsm_volume.
 *          * LSM_ERR_NOT_FOUND_ACCESS_GROUP
 *              When access group not found.
 *          * LSM_ERR_NOT_FOUND_VOLUME
 *              When volume not found.
 *          * LSM_ERR_NO_STATE_CHANGE
 *              When specified volume is not masked to specified access
 *              group.
 *          * LSM_ERR_NO_SUPPORT
 *              Not supported.
 */
int LSM_DLL_EXPORT lsm_volume_unmask(lsm_connect *conn,
                                     lsm_access_group *access_group,
                                     lsm_volume *volume, lsm_flag flags);

/**
 * lsm_volumes_accessible_by_access_group - Query volumes that the
 * specified access group has access to.
 *
 * Version:
 *      1.0
 *
 * Description:
 *      Return a list of volumes which is accessible by specified access group.
 *
 * Capability:
 *      LSM_CAP_VOLUMES_ACCESSIBLE_BY_ACCESS_GROUP
 *
 * @conn:
 *      Valid connection.
 * @group:
 *      Pointer of lsm_access_group.
 * @volumes:
 *      Output pointer of lsm_volume array.
 *      Returned value must be freed with a call to
 *      lsm_volume_record_array_free().
 * @count:
 *      uint32_t. Number of volumes.
 * @flags:
 *      Reserved for future use, must be LSM_CLIENT_FLAG_RSVD.
 *
 * Return:
 *      Error code as enumerated by 'lsm_error_number'.
 *          * LSM_ERR_OK
 *              On success.
 *          * LSM_ERR_INVALID_ARGUMENT
 *              When any argument is NULL or not a valid lsm_connect pointer
 *              or invalid flags or invalid lsm_access_group pointer.
 *          * LSM_ERR_NOT_FOUND_ACCESS_GROUP
 *              When access group not found.
 *          * LSM_ERR_NO_SUPPORT
 *              Not supported.
 */
int LSM_DLL_EXPORT lsm_volumes_accessible_by_access_group(
    lsm_connect *conn, lsm_access_group *group, lsm_volume **volumes[],
    uint32_t *count, lsm_flag flags);

/**
 * lsm_access_groups_granted_to_volume - Retrieves the access groups that have
 * access to the specified volume.
 *
 * Version:
 *      1.0
 *
 * Description:
 *      Return a list of volumes which is accessible by specified access group.
 *
 * Capability:
 *      LSM_CAP_ACCESS_GROUPS_GRANTED_TO_VOLUME
 *
 * @conn:
 *      Valid connection.
 * @volume:
 *      Pointer of lsm_volume.
 * @groups:
 *      Output pointer of lsm_access_group array.
 *      Returned value must be freed with a call to
 *      lsm_access_group_record_array_free().
 * @group_count:
 *      uint32_t. Number of access groups.
 * @flags:
 *      Reserved for future use, must be LSM_CLIENT_FLAG_RSVD.
 *
 * Return:
 *      Error code as enumerated by 'lsm_error_number'.
 *          * LSM_ERR_OK
 *              On success.
 *          * LSM_ERR_INVALID_ARGUMENT
 *              When any argument is NULL or not a valid lsm_connect pointer
 *              or invalid flags or invalid lsm_volume pointer.
 *          * LSM_ERR_NOT_FOUND_VOLUME
 *              When volume not found.
 *          * LSM_ERR_NO_SUPPORT
 *              Not supported.
 */
int LSM_DLL_EXPORT lsm_access_groups_granted_to_volume(
    lsm_connect *conn, lsm_volume *volume, lsm_access_group **groups[],
    uint32_t *group_count, lsm_flag flags);

/**
 * lsm_volume_child_dependency - Check whether volume has child dependencies.
 *
 * Version:
 *      1.0
 *
 * Description:
 *      Returns 1 if the specified volume has child dependencies,
 *      else returns 0.
 *      Child dependencies means specified volume is acting as source volume
 *      of a replication.
 *
 * Capability:
 *      LSM_CAP_VOLUME_CHILD_DEPENDENCY
 *
 * @conn:
 *      Valid connection.
 * @volume:
 *      Pointer of lsm_volume.
 * @yes:
 *      uint8_t. 1 == Yes, 0 == No
 * @flags:
 *      Reserved for future use, must be LSM_CLIENT_FLAG_RSVD.
 *
 * Return:
 *      Error code as enumerated by 'lsm_error_number'.
 *          * LSM_ERR_OK
 *              On success.
 *          * LSM_ERR_INVALID_ARGUMENT
 *              When any argument is NULL or not a valid lsm_connect pointer
 *              or invalid flags or invalid volume.
 *          * LSM_ERR_NOT_FOUND_VOLUME
 *              When volume not found.
 *          * LSM_ERR_NO_SUPPORT
 *              Not supported.
 */
int LSM_DLL_EXPORT lsm_volume_child_dependency(lsm_connect *conn,
                                               lsm_volume *volume, uint8_t *yes,
                                               lsm_flag flags);

/**
 * lsm_volume_child_dependency_delete - Delete all child dependencies of
 * the specified volume.
 *
 * Version:
 *      1.0
 *
 * Description:
 *      Instruct storage system to remove all child dependencies of the
 *      specified volume by duplicating the required storage before breaking
 *      replication relationship.
 *
 * Capability:
 *      LSM_CAP_VOLUME_CHILD_DEPENDENCY_RM
 *
 * @conn:
 *      Valid connection.
 * @volume:
 *      Pointer of lsm_volume.
 * @job:
 *      Output pointer of string. If storage system support asynchronous
 *      action on this, a job will be created and could be tracked
 *      via lsm_job_status_get(). NULL if storage system does not support
 *      asynchronous action on this.
 * @flags:
 *      Reserved for future use, must be LSM_CLIENT_FLAG_RSVD.
 *
 * Return:
 *      Error code as enumerated by 'lsm_error_number'.
 *          * LSM_ERR_OK
 *              On success.
 *          * LSM_ERR_JOB_STARTED
 *              A job is started. Please check the 'job' output pointer.
 *          * LSM_ERR_INVALID_ARGUMENT
 *              When any argument is NULL or not a valid lsm_connect pointer
 *              or invalid flags or invalid lsm_volume pointer.
 *          * LSM_ERR_NOT_FOUND_VOLUME
 *              When volume not found.
 *          * LSM_ERR_NO_STATE_CHANGE
 *              When volume is not a replication source.
 *          * LSM_ERR_NO_SUPPORT
 *              Not supported.
 */
int LSM_DLL_EXPORT lsm_volume_child_dependency_delete(lsm_connect *conn,
                                                      lsm_volume *volume,
                                                      char **job,
                                                      lsm_flag flags);

/**
 * lsm_system_list - Gets a list of systems on this connection.
 *
 * Version:
 *      1.0
 *
 * Description:
 *      Gets a list of systems on this connection. A system represents a storage
 *      array or direct attached storage RAID. Examples include:
 *          * A hardware RAID card: LSI MegaRAID, HP SmartArray.
 *          * A storage area network (SAN): EMC VNX, NetApp Filer
 *          * A software solution running on commodity hardware: Linux targetd,
 *            Nexenta
 *
 *      System properties could be retrieved by these functions:
 *          * lsm_system_id_get()
 *          * lsm_system_name_get()
 *          * lsm_system_status_get()
 *          * lsm_system_fw_version_get()
 *          * lsm_system_read_cache_pct_get()
 *          * lsm_system_mode_get()
 *
 * @conn:
 *      Valid connection.
 * @systems:
 *      Output pointer of lsm_system array. Returned data should be freed by
 *      lsm_system_record_array_free().
 * @system_count:
 *      uint32_t. Number of systems.
 * @flags:
 *      Reserved for future use, must be LSM_CLIENT_FLAG_RSVD.
 *
 * Return:
 *      Error code as enumerated by 'lsm_error_number'.
 *          * LSM_ERR_OK
 *              On success or searched value not found.
 *          * LSM_ERR_INVALID_ARGUMENT
 *              When any argument is NULL or invalid flags.
 */
int LSM_DLL_EXPORT lsm_system_list(lsm_connect *conn, lsm_system **systems[],
                                   uint32_t *system_count, lsm_flag flags);

/**
 * lsm_fs_list - Gets a list of file systems on this connection.
 *
 * Version:
 *      1.0
 *
 * Description:
 *      Gets a list of file systems on this connection.
 *      Network Attached Storage (NAS) Storage array can expose a Filesystem to
 *      host OS via IP network using NFS or CIFS protocol. The host OS treats it
 *      as a mount point or a folder containing files depending on client
 *      operating system.
 *      File system properties could be retrieved by these functions:
 *          * lsm_fs_id_get()
 *          * lsm_fs_name_get()
 *          * lsm_fs_system_id_get()
 *          * lsm_fs_pool_id_get()
 *          * lsm_fs_total_space_get()
 *          * lsm_fs_free_space_get()
 *
 * Capability:
 *      LSM_CAP_FS
 *
 * @conn:
 *      Valid connection.
 * @search_key:
 *      Search key (NULL for all)
 *      Valid search keys are: "id", "system_id" and "pool_id".
 * @search_value:
 *      Search value.
 * @fs:
 *      Output pointer of lsm_fs array. It should be manually freed by
 *      lsm_fs_record_array_free().
 * @fs_count:
 *      Number of file systems.
 * @flags:
 *      Reserved for future use, must be LSM_CLIENT_FLAG_RSVD.
 *
 * Return:
 *      Error code as enumerated by 'lsm_error_number'.
 *          * LSM_ERR_OK
 *              On success or searched value not found.
 *          * LSM_ERR_INVALID_ARGUMENT
 *              When any argument is NULL or invalid flags or invalid search
 *              key.
 *          * LSM_ERR_NO_SUPPORT
 *              Not supported.
 */
int LSM_DLL_EXPORT lsm_fs_list(lsm_connect *conn, const char *search_key,
                               const char *search_value, lsm_fs **fs[],
                               uint32_t *fs_count, lsm_flag flags);

/**
 * lsm_fs_create - Creates a new file system
 *
 * Version:
 *      1.0
 *
 * Description:
 *      Creates a new file system for NFS or CIFS share.
 *
 * Capability:
 *      LSM_CAP_FS_CREATE
 *
 * @conn:
 *      Valid connection.
 * @pool:
 *      Pointer of lsm_pool.
 * @name:
 *      String. Human recognizable name, might be altered or ignored by certain
 *      storage system.
 * @size_bytes:
 *      uint64_t. Size of new file system in bytes, actual size might be bigger
 *      than requested and will be based on array rounding to block size,
 * @fs:
 *      Output pointer of lsm_fs. Will be NULL if storage system support
 *      asynchronous action on this.
 *      Returned value must be freed with lsm_fs_record_free().
 * @job:
 *      Output pointer of string. If storage system support asynchronous
 *      action on this, a job will be created and could be tracked via
 *      lsm_job_status_fs_get().
 *      NULL if storage system does not support asynchronous action on this.
 * @flags:
 *      Reserved for future use, must be LSM_CLIENT_FLAG_RSVD.
 *
 * Return:
 *      LSM_ERR_OK on success, LSM_ERR_JOB_STARTED if async. ,
 *          * LSM_ERR_OK
 *              On success.
 *          * LSM_ERR_JOB_STARTED
 *              A job is started. Please check the 'job' output pointer.
 *          * LSM_ERR_INVALID_ARGUMENT
 *              When any argument is NULL or not a valid lsm_connect pointer
 *              or invalid flags.
 *          * LSM_ERR_NOT_FOUND_POOL
 *              When pool not found.
 *          * LSM_ERR_NOT_ENOUGH_SPACE
 *              Pool does not have enough space.
 *          * LSM_ERR_POOL_NOT_READY
 *              Pool is not ready for file system creation.
 *          * LSM_ERR_NO_SUPPORT
 *              Not supported.
 */
int LSM_DLL_EXPORT lsm_fs_create(lsm_connect *conn, lsm_pool *pool,
                                 const char *name, uint64_t size_bytes,
                                 lsm_fs **fs, char **job, lsm_flag flags);

/**
 * lsm_fs_delete - Deletes a file system
 *
 * Version:
 *      1.0
 *
 * Description:
 *      Deletes a file system and its data is lost!
 *      When file system has snapshot attached, all its snapshot will be deleted
 *      also.
 *      When file system is exported, all its exports will be deleted also.
 *      If specified file system is has child dependency, it cannot be deleted.
 *      You may use `lsm_fs_child_dependency()` and
 *      `lsm_fs_child_dependency_delete()` before `lsm_fs_delete()`.
 *
 * Capability:
 *      LSM_CAP_FS_DELETE
 *
 * @conn:
 *      Valid connection.
 * @fs:
 *      Pointer of lsm_fs that is to be deleted.
 * @job:
 *      Output pointer of string. If storage system support asynchronous
 *      action on this, a job will be created and could be tracked
 *      via lsm_job_status_get(). NULL if storage system does not support
 *      asynchronous action on this.
 * @flags:
 *      Reserved for future use, must be LSM_CLIENT_FLAG_RSVD.
 *
 * Return:
 *      Error code as enumerated by 'lsm_error_number'.
 *          * LSM_ERR_OK
 *              On success.
 *          * LSM_ERR_JOB_STARTED
 *              A job is started. Please check the 'job' output pointer.
 *          * LSM_ERR_INVALID_ARGUMENT
 *              When any argument is NULL or not a valid lsm_connect pointer
 *              or invalid flags.
 *          * LSM_ERR_NOT_FOUND_FS
 *              When file system not found.
 *          * LSM_ERR_POOL_NOT_READY
 *              Pool is not ready.
 *          * LSM_ERR_NO_SUPPORT
 *              Not supported.
 *          * LSM_ERR_HAS_CHILD_DEPENDENCY
 *              Specified volume has child dependencies.
 */
int LSM_DLL_EXPORT lsm_fs_delete(lsm_connect *conn, lsm_fs *fs, char **job,
                                 lsm_flag flags);

/**
 * lsm_fs_clone - Clones an existing file system
 *
 * Version:
 *      1.0
 *
 * Description:
 *      Create a point in time read writeable space efficient copy of
 *      specified file system, also know as read writeable snapshot.
 *      The new file system will reside in the same pool of specified file
 *      system.
 *
 * Capability:
 *      LSM_CAP_FS_CLONE
 *
 * @conn:
 *      Valid connection.
 * @src_fs:
 *      Pointer of lsm_fs for source file system.
 * @name:
 *      String. Human recognizable name for new file system, might be altered
 *      or ignored by certain storage system.
 * @optional_ss:
 *      Pointer of lsm_fs_ss. File system snapshot to base clone from.
 *      Set to NULL of not needed..
 * @cloned_fs:
 *      Output pointer of lsm_fs for the newlly created file system.
 *      Will be NULL if storage system support asynchronous action on this.
 *      Returned value must be freed with a call to lsm_fs_record_free().
 * @job:
 *      Output pointer of string. If storage system support asynchronous action
 *      on this, a job will be created and could be tracked via
 *      lsm_job_status_fs_get(). NULL if storage system does not support
 *      asynchronous action on this.
 * @flags:
 *      Reserved for future use, must be LSM_CLIENT_FLAG_RSVD.
 *
 * Return:
 *      Error code as enumerated by 'lsm_error_number'.
 *          * LSM_ERR_OK
 *              On success.
 *          * LSM_ERR_JOB_STARTED
 *              A job is started. Please check the 'job' output pointer.
 *          * LSM_ERR_INVALID_ARGUMENT
 *              When any argument is NULL or not a valid lsm_connect pointer
 *              or invalid flags.
 *          * LSM_ERR_NOT_FOUND_FS
 *              When file system not found.
 *          * LSM_ERR_NOT_ENOUGH_SPACE
 *              Pool does not have enough space.
 *          * LSM_ERR_POOL_NOT_READY
 *              Pool is not ready.
 *          * LSM_ERR_NO_SUPPORT
 *              Not supported.
 */
int LSM_DLL_EXPORT lsm_fs_clone(lsm_connect *conn, lsm_fs *src_fs,
                                const char *name, lsm_fs_ss *optional_ss,
                                lsm_fs **cloned_fs, char **job, lsm_flag flags);

/**
 * lsm_fs_child_dependency - Checks whether file system has a child dependency.
 *
 * Version:
 *      1.0
 *
 * Description:
 *      Returns 1 if the specified file system has child dependencies, else
 *      returns 0.
 *
 * Capability:
 *      LSM_CAP_FS_CHILD_DEPENDENCY
 *
 * @conn:
 *      Valid connection.
 * @fs:
 *      Point of lsm_fs.
 * @files:
 *      Pointer of lsm_string_list. Only check on specific files if defined.
 *      If NULL, just check the file system.
 * @yes:
 *      uint8_t. 1 == Yes, 0 == No
 * @flags:
 *      Reserved for future use, must be LSM_CLIENT_FLAG_RSVD.
 *
 * Return:
 *      Error code as enumerated by 'lsm_error_number'.
 *          * LSM_ERR_OK
 *              On success.
 *          * LSM_ERR_INVALID_ARGUMENT
 *              When any argument is NULL or not a valid lsm_connect pointer
 *              or invalid flags or invalid file system.
 *          * LSM_ERR_NOT_FOUND_FS
 *              When file system not found.
 *          * LSM_ERR_NO_SUPPORT
 *              Not supported.
 */
int LSM_DLL_EXPORT lsm_fs_child_dependency(lsm_connect *conn, lsm_fs *fs,
                                           lsm_string_list *files, uint8_t *yes,
                                           lsm_flag flags);

/**
 * lsm_fs_child_dependency_delete - Delete all child dependencies of specified
 * file system.
 *
 * Version:
 *      1.0
 *
 * Description:
 *      Deletes child dependencies by duplicating the required storage to
 *      remove.
 *      Note: This could take a long time to complete based on dependencies.
 *
 * Capability:
 *      LSM_CAP_FS_CHILD_DEPENDENCY_RM
 *      LSM_CAP_FS_CHILD_DEPENDENCY_RM_SPECIFIC_FILES
 *
 * @conn:
 *      Valid connection.
 * @fs:
 *      Point of lsm_fs to remove dependencies for.
 * @files:
 *      Pointer of lsm_string_list. Only work on specific files if defined.
 *      If NULL, just work on all files in this file system.
 * @job:
 *      Output pointer of string. If storage system support asynchronous
 *      action on this, a job will be created and could be tracked via
 *      lsm_job_status_get().
 *      NULL if storage system does not support asynchronous action on this.
 * @flags:
 *      Reserved for future use, must be LSM_CLIENT_FLAG_RSVD.
 *
 * Return:
 *      Error code as enumerated by 'lsm_error_number'.
 *          * LSM_ERR_OK
 *              On success.
 *          * LSM_ERR_JOB_STARTED
 *              A job is started. Please check the 'job' output pointer.
 *          * LSM_ERR_NOT_FOUND_FS
 *              When file system not found.
 *          * LSM_ERR_NO_STATE_CHANGE
 *              When file system has no child dependency.
 */
int LSM_DLL_EXPORT lsm_fs_child_dependency_delete(lsm_connect *conn, lsm_fs *fs,
                                                  lsm_string_list *files,
                                                  char **job, lsm_flag flags);

/**
 * lsm_fs_resize - Resizes a file system
 *
 * Version:
 *      1.0
 *
 * Description:
 *      Resize the specified file system.
 *
 * Capability:
 *      LSM_CAP_FS_RESIZE
 *
 * @conn:
 *      Valid connection.
 * @fs:
 *      Pointer of lsm_fs to re-size
 * @new_size_bytes:
 *      uint64_t. New size of file system in bytes, actual size might be bigger
 *      than requested and will be based on array rounding to block size,
 * @rfs:
 *      Output pointer of updated lsm_fs. Might be NULL if storage system
 *      support asynchronous action on this.
 * @job_id:
 *      Output pointer of string. If storage system support asynchronous action
 *      on this, a job will be created and could be tracked via
 *      lsm_job_status_fs_get(). NULL if storage system does not support
 *      asynchronous action on this.
 * @flags:
 *      Reserved for future use, must be LSM_CLIENT_FLAG_RSVD.
 *
 * Return:
 *      Error code as enumerated by 'lsm_error_number'.
 *          * LSM_ERR_OK
 *              On success.
 *          * LSM_ERR_JOB_STARTED
 *              A job is started. Please check the 'job' output pointer.
 *          * LSM_ERR_INVALID_ARGUMENT
 *              When any argument is NULL or not a valid lsm_connect pointer
 *              or invalid flags.
 *          * LSM_ERR_NOT_FOUND_FS
 *              When file system not found.
 *          * LSM_ERR_NOT_ENOUGH_SPACE
 *              Pool does not have enough space.
 *          * LSM_ERR_POOL_NOT_READY
 *              Pool is not ready for volume resizing.
 *          * LSM_ERR_NO_STATE_CHANGE
 *              Requested size if identical to current size.
 *          * LSM_ERR_NO_SUPPORT
 *              Not supported.
 */
int LSM_DLL_EXPORT lsm_fs_resize(lsm_connect *conn, lsm_fs *fs,
                                 uint64_t new_size_bytes, lsm_fs **rfs,
                                 char **job_id, lsm_flag flags);

/**
 * lsm_fs_file_clone - Clones a file on a file system.
 *
 * Version:
 *      1.0
 *
 * Description:
 *      Clones the specified file on a file system.
 *
 * Capability:
 *      LSM_CAP_FILE_CLONE
 *
 * @conn:
 *      Valid connection.
 * @fs:
 *      Pointer of lsm_fs which file resides.
 * @src_file_name:
 *      String. Source file relative name & path.
 * @dest_file_name:
 *      String. Destination file relative name & path.
 * @snapshot:
 *      Pointer of lsm_fs_ss. Snapshot of source file is based on.
 *      If NULL, use current state of this file.
 *      Returned value must be freed with a call to lsm_fs_ss_record_free().
 * @job:
 *      Output pointer of string. If storage system support asynchronous action
 *      on this, a job will be created and could be tracked via
 *      lsm_job_status_get(). NULL if storage system does not support
 *      asynchronous action on this.
 * @flags:
 *      Reserved for future use, must be LSM_CLIENT_FLAG_RSVD.
 *
 * Return:
 *      Error code as enumerated by 'lsm_error_number'.
 *          * LSM_ERR_OK
 *              On success.
 *          * LSM_ERR_JOB_STARTED
 *              A job is started. Please check the 'job' output pointer.
 *          * LSM_ERR_INVALID_ARGUMENT
 *              When any argument is NULL or not a valid lsm_connect pointer
 *              or invalid flags.
 *          * LSM_ERR_NOT_FOUND_FS
 *              When file system not found.
 *          * LSM_ERR_NOT_ENOUGH_SPACE
 *              Pool does not have enough space.
 *          * LSM_ERR_NO_SUPPORT
 *              Not supported.
 */
int LSM_DLL_EXPORT lsm_fs_file_clone(lsm_connect *conn, lsm_fs *fs,
                                     const char *src_file_name,
                                     const char *dest_file_name,
                                     lsm_fs_ss *snapshot, char **job,
                                     lsm_flag flags);

/**
 * lsm_fs_ss_list - Gets a list of snapshots of specified file system
 *
 * Version:
 *      1.0
 *
 * Description:
 *      Gets a list of file systems snapshots on this connection.
 *      File system snapshot properties could be retrieved by these functions:
 *          * lsm_fs_ss_id_get()
 *          * lsm_fs_ss_name_get()
 *          * lsm_fs_ss_time_stamp_get()
 *
 * Capability:
 *      LSM_CAP_FS_SNAPSHOTS
 *
 * @conn:
 *      Valid connection.
 * @fs:
 *      Pointer of lsm_fs to check for snapshots.
 * @ss:
 *      Output pointer of lsm_fs_ss array. It should be manually freed by
 *      lsm_fs_ss_record_array_free().
 * @ss_count:
 *      Number of elements in the array.
 * @flags:
 *      Reserved for future use, must be LSM_CLIENT_FLAG_RSVD.
 *
 * Return:
 *      Error code as enumerated by 'lsm_error_number'.
 *          * LSM_ERR_OK
 *              On success or searched value not found.
 *          * LSM_ERR_INVALID_ARGUMENT
 *              When any argument is NULL or invalid flags or invalid search
 *              key.
 *          * LSM_ERR_NO_SUPPORT
 *              Not supported.
 */
int LSM_DLL_EXPORT lsm_fs_ss_list(lsm_connect *conn, lsm_fs *fs,
                                  lsm_fs_ss **ss[], uint32_t *ss_count,
                                  lsm_flag flags);

/**
 * lsm_fs_ss_create - Creates a file system snapshot
 *
 * Version:
 *      1.0
 *
 * Description:
 *      Creates a new file system snapshot.
 *
 * Capability:
 *      LSM_CAP_FS_SNAPSHOT_CREATE
 *
 * @conn:
 *      Valid connection.
 * @fs:
 *      Pointer of lsm_fs to create snapshot.
 * @name:
 *      String. Human recognizable name, might be altered or ignored by certain
 *      storage system.
 * @snapshot:
 *      Output pointer of lsm_fs_ss for newly created snapshot.
 * @job:
 *      Output pointer of string. If storage system support asynchronous action
 *      on this, a job will be created and could be tracked via
 *      lsm_job_status_ss_get(). NULL if storage system does not support
 *      asynchronous action on this.
 * @flags:
 *      Reserved for future use, must be LSM_CLIENT_FLAG_RSVD.
 *
 * Return:
 *      Error code as enumerated by 'lsm_error_number'.
 *          * LSM_ERR_OK
 *              On success.
 *          * LSM_ERR_JOB_STARTED
 *              A job is started. Please check the 'job' output pointer.
 *          * LSM_ERR_INVALID_ARGUMENT
 *              When any argument is NULL or not a valid lsm_connect pointer
 *              or invalid flags.
 *          * LSM_ERR_NOT_FOUND_FS
 *              When file system not found.
 *          * LSM_ERR_NOT_ENOUGH_SPACE
 *              Pool does not have enough space.
 *          * LSM_ERR_POOL_NOT_READY
 *              Pool is not ready for volume creation.
 *          * LSM_ERR_NO_SUPPORT
 *              Not supported.
 */
int LSM_DLL_EXPORT lsm_fs_ss_create(lsm_connect *conn, lsm_fs *fs,
                                    const char *name, lsm_fs_ss **snapshot,
                                    char **job, lsm_flag flags);

/**
 * lsm_fs_ss_delete - Deletes a snapshot
 *
 * Version:
 *      1.0
 *
 * Description:
 *      Deletes the specified file system snapshot.
 *
 * Capability:
 *      LSM_CAP_FS_SNAPSHOT_DELETE
 *
 * @conn:
 *      Valid connection.
 * @fs:
 *      Pointer of lsm_fs.
 * @ss:
 *      Pointer of lsm_fs_ss to delete.
 * @job:
 *      Output pointer of string. If storage system support asynchronous
 *      action on this, a job will be created and could be tracked via
 *      lsm_job_status_get().
 *      NULL if storage system does not support asynchronous action on this.
 * @flags:
 *      Reserved for future use, must be LSM_CLIENT_FLAG_RSVD.
 *
 * Return:
 *      Error code as enumerated by 'lsm_error_number'.
 *          * LSM_ERR_OK
 *              On success.
 *          * LSM_ERR_JOB_STARTED
 *              A job is started. Please check the 'job' output pointer.
 *          * LSM_ERR_INVALID_ARGUMENT
 *              When any argument is NULL or not a valid lsm_connect pointer
 *              or invalid flags.
 *          * LSM_ERR_NOT_FOUND_FS
 *              When file system not found.
 *          * LSM_ERR_NOT_FOUND_FS_SS
 *              When file system snapshot not found.
 *          * LSM_ERR_NO_SUPPORT
 *              Not supported.
 */
int LSM_DLL_EXPORT lsm_fs_ss_delete(lsm_connect *conn, lsm_fs *fs,
                                    lsm_fs_ss *ss, char **job, lsm_flag flags);

/**
 * lsm_fs_ss_restore - Restores a file system snapshot.
 *
 * Version:
 *      1.0
 *
 * Description:
 *      Restores a file system or files to a previous state as specified in the
 *      snapshot.
 *
 * Capability:
 *      LSM_CAP_FS_SNAPSHOT_RESTORE
 *      LSM_CAP_FS_SNAPSHOT_RESTORE_SPECIFIC_FILES
 *
 * @conn:
 *      Valid connection.
 * @fs:
 *      Pointer of lsm_fs which contains the snapshot
 * @ss:
 *      Pointer of lsm_fs_ss to restore to.
 * @files:
 *      Pointer of lsm_string_list. If defined, only restore specified files.
 *      If NULL and 'all_files' set to 1, restore all files.
 * @restore_files:
 *      Pointer of lsm_string_list. If defined, rename restored files to
 *      defined file paths and names.
 * @all_files:
 *      int. 0 for only restore defined files, 1 for restore for all files.
 * @job:
 *      Output pointer of string. If storage system support asynchronous action
 *      on this, a job will be created and could be tracked via
 *      lsm_job_status_get(). NULL if storage system does not support
 *      asynchronous action on this.
 * @flags:
 *      Reserved for future use, must be LSM_CLIENT_FLAG_RSVD.
 *
 * Return:
 *      Error code as enumerated by 'lsm_error_number'.
 *          * LSM_ERR_OK
 *              On success.
 *          * LSM_ERR_JOB_STARTED
 *              A job is started. Please check the 'job' output pointer.
 *          * LSM_ERR_INVALID_ARGUMENT
 *              When any argument is NULL or not a valid lsm_connect pointer
 *              or invalid flags.
 *          * LSM_ERR_NOT_FOUND_FS
 *              When file system not found.
 *          * LSM_ERR_NOT_FOUND_FS_SS
 *              When file system snapshot not found.
 *          * LSM_ERR_NO_SUPPORT
 *              Not supported.
 */
int LSM_DLL_EXPORT lsm_fs_ss_restore(lsm_connect *conn, lsm_fs *fs,
                                     lsm_fs_ss *ss, lsm_string_list *files,
                                     lsm_string_list *restore_files,
                                     int all_files, char **job, lsm_flag flags);

/**
 * lsm_nfs_auth_types - Gets supported NFS client authentication types.
 *
 * Version:
 *      1.0
 *
 * Description:
 *      Returns the types of NFS client authentication the array supports.
 *
 * Capability:
 *      LSM_CAP_EXPORT_AUTH
 *
 * @conn:
 *      Valid connection.
 * @types:
 *      Pointer of lsm_string_list. List of NFS client authentication types.
 *      Returned value must be freed with a call to lsm_string_list_free().
 * @flags:
 *      Reserved for future use, must be LSM_CLIENT_FLAG_RSVD.
 *
 * Return:
 *      Error code as enumerated by 'lsm_error_number'.
 *          * LSM_ERR_OK
 *              On success.
 *          * LSM_ERR_NO_SUPPORT
 *              Not supported.
 */
int LSM_DLL_EXPORT lsm_nfs_auth_types(lsm_connect *conn,
                                      lsm_string_list **types, lsm_flag flags);

/**
 * lsm_nfs_list - Gets a list of NFS exports on this connection.
 *
 * Version:
 *      1.0
 *
 * Description:
 *      Gets a list of NFS export on this connection.
 *      NFS export properties could be retrieved by these functions:
 *          * lsm_nfs_export_id_get()
 *          * lsm_nfs_export_fs_id_get()
 *          * lsm_nfs_export_export_path_get()
 *          * lsm_nfs_export_auth_type_get()
 *          * lsm_nfs_export_root_get()
 *          * lsm_nfs_export_read_write_get()
 *          * lsm_nfs_export_read_only_get()
 *          * lsm_nfs_export_anon_gid_get()
 *          * lsm_nfs_export_anon_uid_get()
 *          * lsm_nfs_export_options_get()
 *
 * Capability:
 *      LSM_CAP_EXPORTS
 *
 * @conn:
 *      Valid connection.
 * @search_key:
 *      Search key(NULL for all).
 *      Valid search keys are: "id", "fs_id".
 * @search_value:
 *      Search value.
 * @exports:
 *      Out pointer of lsm_nfs_export array. It should be manually freed by
 *      lsm_nfs_export_record_array_free().
 * @count:
 *      uint32_t. Number of items in array.
 * @flags:
 *      Reserved for future use, must be LSM_CLIENT_FLAG_RSVD.
 *
 * Return:
 *      Error code as enumerated by 'lsm_error_number'.
 *          * LSM_ERR_OK
 *              On success or searched value not found.
 *          * LSM_ERR_INVALID_ARGUMENT
 *              When any argument is NULL or invalid flags or invalid search
 *              key.
 *          * LSM_ERR_NO_SUPPORT
 *              Not supported.
 */
int LSM_DLL_EXPORT lsm_nfs_list(lsm_connect *conn, const char *search_key,
                                const char *search_value,
                                lsm_nfs_export **exports[], uint32_t *count,
                                lsm_flag flags);

/**
 * lsm_nfs_export_fs - Creates or modifies an NFS export.
 *
 * Version:
 *      1.0
 *
 * Description:
 *      Creates or modifies an NFS export.
 *
 * Capability:
 *      LSM_CAP_EXPORT_FS
 *
 * @conn:
 *      Valid connection.
 * @fs_id:
 *      String. File system ID to export.
 * @export_path:
 *      String. Export path.
 * @root_list:
 *      Pointer of lsm_string_list. List of hosts that have root access
 * @rw_list:
 *      Pointer of lsm_string_list. List of hosts that have read/write access
 * @ro_list:
 *      Pointer of lsm_string_list. List of hosts that have read only access
 * @anon_uid:
 *      uint64_t. UID to map to anonymous.
 * @anon_gid:
 *      uint64_t. GID to map to anonymous.
 * @auth_type:
 *      String. NFS client authentication type.
 * @options:
 *      Array specific options
 * @exported:
 *      Output pointer of lsm_nfs_export for newly created NFS export.
 *      Returned value must be freed with lsm_nfs_export_record_free().
 * @flags:
 *      Reserved for future use, must be LSM_CLIENT_FLAG_RSVD.
 *
 * Return:
 *      Error code as enumerated by 'lsm_error_number'.
 *          * LSM_ERR_OK
 *              On success.
 *          * LSM_ERR_INVALID_ARGUMENT
 *              When any argument is NULL or not a valid lsm_connect pointer
 *              or invalid flags.
 *          * LSM_ERR_NOT_FOUND_FS
 *              When file system not found.
 */
int LSM_DLL_EXPORT lsm_nfs_export_fs(lsm_connect *conn, const char *fs_id,
                                     const char *export_path,
                                     lsm_string_list *root_list,
                                     lsm_string_list *rw_list,
                                     lsm_string_list *ro_list,
                                     uint64_t anon_uid, uint64_t anon_gid,
                                     const char *auth_type, const char *options,
                                     lsm_nfs_export **exported, lsm_flag flags);

/**
 * lsm_nfs_export_delete - Deletes the specified NFS export.
 *
 * Version:
 *      1.0
 *
 * Description:
 *      Deletes the specified NFS export.
 *
 * Capability:
 *      LSM_CAP_EXPORT_REMOVE
 *
 * @conn:
 *      Valid connection.
 * @e:
 *      The pointer of lsm_nfs_export to remove.
 * @flags:
 *      Reserved for future use, must be LSM_CLIENT_FLAG_RSVD.
 *
 * Return:
 *      Error code as enumerated by 'lsm_error_number'.
 *          * LSM_ERR_OK
 *              On success.
 *          * LSM_ERR_NOT_FOUND_NFS_EXPORT
 *              When NFS export not found.
 */
int LSM_DLL_EXPORT lsm_nfs_export_delete(lsm_connect *conn, lsm_nfs_export *e,
                                         lsm_flag flags);

/**
 * lsm_target_port_list - Gets a list of target ports on this connection.
 *
 * Version:
 *      1.0
 *
 * Description:
 *      Gets a list of target ports on this connection.
 *      Target port are the front-end port of storage system which storage
 *      user/client connect to and get storage service from.
 *      Target port properties could be retrieved by these functions:
 *          * lsm_target_port_id_get()
 *          * lsm_target_port_type_get()
 *          * lsm_target_port_system_id_get()
 *          * lsm_target_port_network_address_get()
 *          * lsm_target_port_physical_address_get()
 *          * lsm_target_port_physical_name_get()
 *          * lsm_target_port_service_address_get()
 *
 * Capability:
 *      LSM_CAP_TARGET_PORTS
 *
 * @conn:
 *      Valid connection.
 * @search_key:
 *      Search key(NULL for all).
 *      Valid search keys are: "id", and "system_id".
 * @search_value:
 *      Search value.
 * @target_ports:
 *      Output pointer of lsm_target_port array.
 *      Return value must be freed with a call to
 *      lsm_target_port_record_array_free().
 * @count:
 *      Output pointer of uint32_t. Number of target ports.
 * @flags:
 *      Reserved for future use, must be LSM_CLIENT_FLAG_RSVD.
 *
 * Return:
 *      Error code as enumerated by 'lsm_error_number'.
 *          * LSM_ERR_OK
 *              On success or searched value not found.
 *          * LSM_ERR_INVALID_ARGUMENT
 *              When any argument is NULL or invalid flags or invalid search
 *              key.
 *          * LSM_ERR_NO_SUPPORT
 *              Not supported.
 */
int LSM_DLL_EXPORT lsm_target_port_list(lsm_connect *conn,
                                        const char *search_key,
                                        const char *search_value,
                                        lsm_target_port **target_ports[],
                                        uint32_t *count, lsm_flag flags);

/**
 * lsm_volume_raid_info - Retrieves the RAID information of specified volume
 *
 * Version:
 *      1.2.
 *
 * Description:
 *      Retrieves the RAID information of specified volume.
 *
 * Capability:
 *      LSM_CAP_VOLUME_RAID_INFO
 *
 * @conn:
 *      Valid connection.
 * @volume:
 *      Pointer of lsm_volume.
 * @raid_type:
 *      lsm_volume_raid_type. Valid values are:
 *          LSM_VOLUME_RAID_TYPE_RAID0
 *              Stripe.
 *          LSM_VOLUME_RAID_TYPE_RAID1
 *              Two disks Mirror
 *          LSM_VOLUME_RAID_TYPE_RAID3
 *              Byte-level striping with dedicated parity
 *          LSM_VOLUME_RAID_TYPE_RAID4
 *              Block-level striping with dedicated parity
 *          LSM_VOLUME_RAID_TYPE_RAID5
 *              Block-level striping with distributed parity
 *          LSM_VOLUME_RAID_TYPE_RAID6
 *              Block-level striping with two distributed parities,
 *              aka, RAID-DP
 *          LSM_VOLUME_RAID_TYPE_RAID10
 *              Stripe of mirrors
 *          LSM_VOLUME_RAID_TYPE_RAID15
 *              Parity of mirrors
 *          LSM_VOLUME_RAID_TYPE_RAID16
 *              Dual parity of mirrors
 *          LSM_VOLUME_RAID_TYPE_RAID50
 *              Stripe of parities
 *          LSM_VOLUME_RAID_TYPE_RAID60
 *              Stripe of dual parities
 *          LSM_VOLUME_RAID_TYPE_RAID51
 *              Mirror of parities
 *          LSM_VOLUME_RAID_TYPE_RAID61
 *              Mirror of dual parities
 *          LSM_VOLUME_RAID_TYPE_JBOD
 *              Just bunch of disks, no parity, no striping.
 *          LSM_VOLUME_RAID_TYPE_UNKNOWN
 *              The plugin failed to detect the volume's RAID type.
 *          LSM_VOLUME_RAID_TYPE_MIXED
 *              This volume contains multiple RAID settings.
 *          LSM_VOLUME_RAID_TYPE_OTHER
 *              Vendor specific RAID type
 * @strip_size:
 *      uint32_t. The size of strip on each disk or other storage extent.
 *      For RAID1/JBOD, it should be set as sector size.
 *      If plugin failed to detect strip size, it should be set as
 *      LSM_VOLUME_STRIP_SIZE_UNKNOWN(0).
 * @disk_count:
 *      uint32_t. The count of disks used for assembling the RAID group(s) where
 *      this volume allocated from. For any RAID system using the slice of disk,
 *      this value indicate how many disk slices are used for the RAID. For
 *      example, on LVM RAID, the 'disk_count' here indicate the count of PVs
 *      used for certain volume. Another example, on EMC VMAX, the 'disk_count'
 *      here indicate how many hyper volumes are used for this volume. For any
 *      RAID system using remote LUN for data storing, each remote LUN should be
 *      count as a disk.  If the plugin failed to detect disk_count, it should
 *      be set as LSM_VOLUME_DISK_COUNT_UNKNOWN(0).
 * @min_io_size:
 *      uint32_t. The minimum I/O size, device preferred I/O size for random
 *      I/O.  Any I/O size not equal to a multiple of this value may get
 *      significant speed penalty. Normally it refers to strip size of each
 *      disk(extent).
 *      If plugin failed to detect min_io_size, it should try these values in
 *      the sequence of:
 *      logical sector size -> physical sector size ->
 *      LSM_VOLUME_MIN_IO_SIZE_UNKNOWN(0).
 * @opt_io_size:
 *      uint32_t. The optimal I/O size, device preferred I/O size for sequential
 *      I/O. Normally it refers to RAID group stripe size. If plugin failed to
 *      detect opt_io_size, it should be set to
 *      LSM_VOLUME_OPT_IO_SIZE_UNKNOWN(0).
 * @flags:
 *      Reserved for future use, must be LSM_CLIENT_FLAG_RSVD.
 *
 * Return:
 *      Error code as enumerated by 'lsm_error_number'.
 *          * LSM_ERR_OK
 *              On success.
 *          * LSM_ERR_INVALID_ARGUMENT
 *              When any argument is NULL or invalid flags.
 *          * LSM_ERR_NO_SUPPORT
 *              Not supported.
 */
int LSM_DLL_EXPORT lsm_volume_raid_info(lsm_connect *conn, lsm_volume *volume,
                                        lsm_volume_raid_type *raid_type,
                                        uint32_t *strip_size,
                                        uint32_t *disk_count,
                                        uint32_t *min_io_size,
                                        uint32_t *opt_io_size, lsm_flag flags);

/**
 * lsm_pool_member_info - Retrieves the membership of given pool.
 *
 * Version:
 *      1.2.
 *
 * Description:
 *      Retrieves the membership information of certain pool:
 *          RAID type, member type and member ids.
 *      Currently, LibStorageMgmt supports two types of pool:
 *      * Sub-pool -- LSM_POOL_MEMBER_TYPE_POOL
 *          Pool space is allocated from parent pool.
 *          Example:
 *              * NetApp ONTAP volume
 *
 *      * Disk RAID pool -- LSM_POOL_MEMBER_TYPE_DISK
 *          Pool is a RAID group assembled by disks.
 *          Example:
 *              * LSI MegaRAID disk group
 *              * EMC VNX pool
 *              * NetApp ONTAP aggregate
 *
 * Capability:
 *      LSM_CAP_POOL_MEMBER_INFO
 *
 * @conn:
 *      Valid connection.
 * @pool:
 *      The pointer of lsm_pool.
 * @raid_type:
 *      lsm_volume_raid_type. Valid values are:
 *          LSM_VOLUME_RAID_TYPE_RAID0
 *              Stripe.
 *          LSM_VOLUME_RAID_TYPE_RAID1
 *              Two disks Mirror
 *          LSM_VOLUME_RAID_TYPE_RAID3
 *              Byte-level striping with dedicated parity
 *          LSM_VOLUME_RAID_TYPE_RAID4
 *              Block-level striping with dedicated parity
 *          LSM_VOLUME_RAID_TYPE_RAID5
 *              Block-level striping with distributed parity
 *          LSM_VOLUME_RAID_TYPE_RAID6
 *              Block-level striping with two distributed parities,
 *              aka, RAID-DP
 *          LSM_VOLUME_RAID_TYPE_RAID10
 *              Stripe of mirrors
 *          LSM_VOLUME_RAID_TYPE_RAID15
 *              Parity of mirrors
 *          LSM_VOLUME_RAID_TYPE_RAID16
 *              Dual parity of mirrors
 *          LSM_VOLUME_RAID_TYPE_RAID50
 *              Stripe of parities
 *          LSM_VOLUME_RAID_TYPE_RAID60
 *              Stripe of dual parities
 *          LSM_VOLUME_RAID_TYPE_RAID51
 *              Mirror of parities
 *          LSM_VOLUME_RAID_TYPE_RAID61
 *              Mirror of dual parities
 *          LSM_VOLUME_RAID_TYPE_JBOD
 *              Just bunch of disks, no parity, no striping.
 *          LSM_VOLUME_RAID_TYPE_UNKNOWN
 *              The plugin failed to detect the volume's RAID type.
 *          LSM_VOLUME_RAID_TYPE_MIXED
 *              This volume contains multiple RAID settings.
 *          LSM_VOLUME_RAID_TYPE_OTHER
 *              Vendor specific RAID type
 * @member_type:
 *      lsm_pool_member_type. Valid values are:
 *          * LSM_POOL_MEMBER_TYPE_POOL
 *              Current pool(also known as sub-pool) is allocated from
 *              other pool(parent pool). The 'raid_type' will set to
 *              RAID_TYPE_OTHER unless certain RAID system support RAID
 *              using space of parent pools.
 *          * LSM_POOL_MEMBER_TYPE_DISK
 *              Pool is created from RAID group using whole disks.
 *          * LSM_POOL_MEMBER_TYPE_OTHER
 *              Vendor specific RAID member type.
 *          * LSM_POOL_MEMBER_TYPE_UNKNOWN
 *              Plugin failed to detect the RAID member type.
 * @member_ids:
 *      Pointer of lsm_string_list.
 *      When 'member_type' is LSM_POOL_MEMBER_TYPE_POOL, the 'member_ids' will
 *      contain a list of parent Pool IDs.
 *      When 'member_type' is LSM_POOL_MEMBER_TYPE_DISK, the 'member_ids' will
 *      contain a list of disk IDs.
 *      When 'member_type' is LSM_POOL_MEMBER_TYPE_OTHER or
 *      LSM_POOL_MEMBER_TYPE_UNKNOWN, the member_ids should be NULL.
 *      Memory need to be freed via lsm_string_list_free().
 * @flags:
 *      Reserved for future use, must be LSM_CLIENT_FLAG_RSVD.
 *
 * Return:
 *      Error code as enumerated by 'lsm_error_number'.
 *          * LSM_ERR_OK
 *              On success.
 *          * LSM_ERR_INVALID_ARGUMENT
 *              When any argument is NULL or invalid flags.
 *          * LSM_ERR_NO_SUPPORT
 *              Not supported.
 */
int LSM_DLL_EXPORT lsm_pool_member_info(lsm_connect *conn, lsm_pool *pool,
                                        lsm_volume_raid_type *raid_type,
                                        lsm_pool_member_type *member_type,
                                        lsm_string_list **member_ids,
                                        lsm_flag flags);

/**
 * lsm_volume_raid_create_cap_get - Retrieves supported capability of
 * lsm_volume_raid_create()
 *
 * Version:
 *      1.2
 *
 * Description:
 *      Only available for hardware RAID cards. Query all supported RAID types
 *      and strip sizes which could be used in lsm_volume_raid_create()
 *      functions.
 *
 * Capability:
 *      LSM_CAP_VOLUME_RAID_CREATE
 *
 * @conn:
 *      Valid connection.
 * @system:
 *      Pointer of lsm_system.
 * @supported_raid_types:
 *      Output pointer of uint32_t array. Containing lsm_volume_raid_type
 *      values. Memory should be freed via free(). Valid values are:
 *          LSM_VOLUME_RAID_TYPE_RAID0
 *              Stripe.
 *          LSM_VOLUME_RAID_TYPE_RAID1
 *              Two disks Mirror
 *          LSM_VOLUME_RAID_TYPE_RAID3
 *              Byte-level striping with dedicated parity
 *          LSM_VOLUME_RAID_TYPE_RAID4
 *              Block-level striping with dedicated parity
 *          LSM_VOLUME_RAID_TYPE_RAID5
 *              Block-level striping with distributed parity
 *          LSM_VOLUME_RAID_TYPE_RAID6
 *              Block-level striping with two distributed parities,
 *              aka, RAID-DP
 *          LSM_VOLUME_RAID_TYPE_RAID10
 *              Stripe of mirrors
 *          LSM_VOLUME_RAID_TYPE_RAID15
 *              Parity of mirrors
 *          LSM_VOLUME_RAID_TYPE_RAID16
 *              Dual parity of mirrors
 *          LSM_VOLUME_RAID_TYPE_RAID50
 *              Stripe of parities
 *          LSM_VOLUME_RAID_TYPE_RAID60
 *              Stripe of dual parities
 *          LSM_VOLUME_RAID_TYPE_RAID51
 *              Mirror of parities
 *          LSM_VOLUME_RAID_TYPE_RAID61
 *              Mirror of dual parities
 *          LSM_VOLUME_RAID_TYPE_JBOD
 *              Just bunch of disks, no parity, no striping.
 * @supported_raid_type_count:
 *      Output pointer of uint32_t. Indicate the item count of
 *      supported_raid_types array.
 * @supported_strip_sizes:
 *      The pointer of uint32_t array. Containing all supported strip sizes.
 *      Memory should be freed via free().
 * @supported_strip_size_count:
 *      The pointer of uint32_t. Indicate the item count of
 *      supported_strip_sizes array.
 * @flags:
 *      Reserved for future use, must be LSM_CLIENT_FLAG_RSVD.
 *
 * Return:
 *      Error code as enumerated by 'lsm_error_number'.
 *          * LSM_ERR_OK
 *              On success.
 *          * LSM_ERR_INVALID_ARGUMENT
 *              When any argument is NULL or invalid flags.
 *          * LSM_ERR_NO_SUPPORT
 *              Not supported.
 */
int LSM_DLL_EXPORT lsm_volume_raid_create_cap_get(
    lsm_connect *conn, lsm_system *system, uint32_t **supported_raid_types,
    uint32_t *supported_raid_type_count, uint32_t **supported_strip_sizes,
    uint32_t *supported_strip_size_count, lsm_flag flags);

/**
 * lsm_volume_raid_create - Create a RAID volume.
 *
 * Version:
 *      1.2
 *
 * Description:
 *      Only available for hardware RAID cards.
 *      Create a disk RAID pool and allocate entire full space to new volume.
 *
 * Capability:
 *      LSM_CAP_VOLUME_RAID_CREATE
 *
 * @conn:
 *      Valid connection.
 * @name:
 *      String. Human recognizable name, might be altered or ignored by certain
 *      storage system.
 * @raid_type:
 *      lsm_volume_raid_type. Please refer to the returns of
 *      lsm_volume_raid_create_cap_get() function for supported RAID type.
 * @disks:
 *      An array of lsm_disk pointer.
 * @disk_count:
 *      The count of lsm_disk in 'disks' argument.
 * @strip_size:
 *      uint32_t. The strip size in bytes. Please refer to the returns of
 *      lsm_volume_raid_create_cap_get() function for supported strip sizes.
 * @new_volume:
 *      Output pointer of lsm_volume for newly created volume.
 *      Returned value must be freed with a call to lsm_volume_record_free().
 * @flags:
 *      Reserved for future use, must be LSM_CLIENT_FLAG_RSVD.
 *
 * Return:
 *      Error code as enumerated by 'lsm_error_number'.
 *          * LSM_ERR_OK
 *              On success.
 *          * LSM_ERR_INVALID_ARGUMENT
 *              When any argument is NULL or not a valid lsm_connect pointer
 *              or invalid flags.
 *          * LSM_ERR_NOT_FOUND_DISK
 *              When disk not found.
 *          * LSM_ERR_DISK_NOT_FREE
 *              When disk not free.
 *          * LSM_ERR_NO_SUPPORT
 *              Not supported.
 */
int LSM_DLL_EXPORT lsm_volume_raid_create(
    lsm_connect *conn, const char *name, lsm_volume_raid_type raid_type,
    lsm_disk *disks[], uint32_t disk_count, uint32_t strip_size,
    lsm_volume **new_volume, lsm_flag flags);

/**
 * lsm_volume_ident_led_on - Turn on the identification LED for the specified
 * volume.
 *
 *
 * Version:
 *      1.3
 *
 * Description:
 *      Only available for hardware RAID cards.
 *      Turn on the identification LED for the specified volume.
 *
 * Capability:
 *      LSM_CAP_VOLUME_LED
 *
 * @conn:
 *      Valid connection.
 * @volume:
 *      Pointer of lsm_volume.
 * @flags:
 *      Reserved for future use, must be LSM_CLIENT_FLAG_RSVD.
 *
 * Return:
 *      Error code as enumerated by 'lsm_error_number'.
 *          * LSM_ERR_OK
 *              On success.
 *          * LSM_ERR_INVALID_ARGUMENT
 *              When any argument is NULL or not a valid lsm_connect pointer
 *              or invalid flags.
 *          * LSM_ERR_NOT_FOUND_VOLUME
 *              When volume not found.
 *          * LSM_ERR_NO_SUPPORT
 *              Not supported.
 */
int LSM_DLL_EXPORT lsm_volume_ident_led_on(lsm_connect *conn,
                                           lsm_volume *volume, lsm_flag flags);

/**
 * lsm_volume_ident_led_off - Turn off the identification LED for the specified
 * volume.
 *
 *
 * Version:
 *      1.3
 *
 * Description:
 *      Only available for hardware RAID cards.
 *      Turn off the identification LED for the specified volume.
 *
 * Capability:
 *      LSM_CAP_VOLUME_LED
 *
 * @conn:
 *      Valid connection.
 * @volume:
 *      Pointer of lsm_volume.
 * @flags:
 *      Reserved for future use, must be LSM_CLIENT_FLAG_RSVD.
 *
 * Return:
 *      Error code as enumerated by 'lsm_error_number'.
 *          * LSM_ERR_OK
 *              On success.
 *          * LSM_ERR_INVALID_ARGUMENT
 *              When any argument is NULL or not a valid lsm_connect pointer
 *              or invalid flags.
 *          * LSM_ERR_NOT_FOUND_VOLUME
 *              When volume not found.
 *          * LSM_ERR_NO_SUPPORT
 *              Not supported.
 */
int LSM_DLL_EXPORT lsm_volume_ident_led_off(lsm_connect *conn,
                                            lsm_volume *volume, lsm_flag flags);

/**
 * lsm_system_read_cache_pct_update - Changes the read cache percentage for the
 * specified system.
 *
 * Version:
 *      1.3
 *
 * Description:
 *      Only available for hardware RAID cards.
 *      Change the read cache percentage for the specified system.
 *
 * Capability:
 *      LSM_CAP_SYS_READ_CACHE_PCT_UPDATE
 *
 * @conn:
 *      Valid connection.
 * @system:
 *      The pointer of lsm_system.
 * @read_pct:
 *      uint32_t. Desired read cache percentage. O for disable read cache.
 * @flags:
 *      Reserved for future use, must be LSM_CLIENT_FLAG_RSVD.
 *
 * Return:
 *      Error code as enumerated by 'lsm_error_number'.
 *          * LSM_ERR_OK
 *              On success.
 *          * LSM_ERR_INVALID_ARGUMENT
 *              When any argument is NULL or not a valid lsm_connect pointer
 *              or invalid flags.
 *          * LSM_ERR_NOT_FOUND_SYSTEM
 *              When system not found.
 *          * LSM_ERR_NO_SUPPORT
 *              Not supported.
 */
int LSM_DLL_EXPORT lsm_system_read_cache_pct_update(lsm_connect *conn,
                                                    lsm_system *system,
                                                    uint32_t read_pct,
                                                    lsm_flag flags);

/**
 * lsm_battery_list - Gets a list of batteries on this connection.
 *
 * Version:
 *      1.3.
 *
 * Description:
 *      Gets a list of batteries on this connection.
 *      When present, super capacitors will also be included.
 *      Battery properties could be retrieved by these functions:
 *          * lsm_battery_id_get()
 *          * lsm_battery_name_get()
 *          * lsm_battery_system_id_get()
 *          * lsm_battery_type_get()
 *          * lsm_battery_status_get()
 *
 * Capability:
 *      LSM_CAP_BATTERIES
 *
 * @conn:
 *      Valid lsm_connect pointer.
 * @search_key:
 *      Search key (NULL for all)
 *      Valid search keys are: "id", and "system_id".
 * @search_value:
 *      Search value.
 * @bs:
 *      Output pointer of lsm_battery array.
 *      Returned value must be freed by calling lsm_battery_record_array_free().
 * @count:
 *      Output pointer of uint32_t. Number of batteries.
 * @flags:
 *      Reserved for future use, must be LSM_CLIENT_FLAG_RSVD.
 *
 * Return:
 *      Error code as enumerated by 'lsm_error_number'.
 *          * LSM_ERR_OK
 *              On success or searched value not found.
 *          * LSM_ERR_INVALID_ARGUMENT
 *              When any argument is NULL or invalid flags or invalid search
 *              key.
 *          * LSM_ERR_NO_SUPPORT
 *              Not supported.
 */
int LSM_DLL_EXPORT lsm_battery_list(lsm_connect *conn, const char *search_key,
                                    const char *search_value,
                                    lsm_battery **bs[], uint32_t *count,
                                    lsm_flag flags);

/**
 * lsm_volume_cache_info - Query RAM cache information for the specified volume.
 *
 * Version:
 *      1.3.
 *
 * Description:
 *      Query RAM cache settings for the specified volume.
 *
 * Capability:
 *      LSM_CAP_VOLUME_CACHE_INFO
 *
 * @conn:
 *      Valid connection.
 * @volume:
 *      Pointer of lsm_volume.
 * @flags:
 *      Reserved for future use, must be LSM_CLIENT_FLAG_RSVD.
 * @write_cache_policy:
 *      uint32_t. The write cache policy. Valid values are:
 *          * LSM_VOLUME_WRITE_CACHE_POLICY_WRITE_BACK
 *              The storage system will use write back mode if cache hardware
 *              found.
 *          * LSM_VOLUME_WRITE_CACHE_POLICY_AUTO
 *              The controller will use write back mode when battery/capacitor
 *              is in good health, otherwise, write through mode.
 *          * LSM_VOLUME_WRITE_CACHE_POLICY_WRITE_THROUGH
 *              The storage system will use write through mode.
 *          * LSM_VOLUME_WRITE_CACHE_POLICY_UNKNOWN
 *              Plugin failed to detect this setting.
 * @write_cache_status:
 *      uint32_t.  The status of write cache. Valid values are:
 *          * LSM_VOLUME_WRITE_CACHE_STATUS_WRITE_THROUGH
 *          * LSM_VOLUME_WRITE_CACHE_STATUS_WRITE_BACK
 *          * LSM_VOLUME_WRITE_CACHE_STATUS_UNKNOWN
 * @read_cache_policy:
 *      uint32_t. The policy for read cache. Valid values are:
 *          * LSM_VOLUME_READ_CACHE_POLICY_ENABLED
 *              Read cache is enabled, when reading I/O on previous unchanged
 *              written I/O or read I/O in cache will be returned to I/O
 *              initiator immediately without checking backing store(normally
 *              disk).
 *          * LSM_VOLUME_READ_CACHE_POLICY_DISABLED
 *              Read cache is disabled.
 *          * LSM_VOLUME_READ_CACHE_POLICY_UNKNOWN
 *              Plugin failed to detect the read cache policy.
 * @read_cache_status:
 *      uint32_t. The status of read cache. Valid values are:
 *          * LSM_VOLUME_READ_CACHE_STATUS_ENABLED
 *          * LSM_VOLUME_READ_CACHE_STATUS_DISABLED
 *          * LSM_VOLUME_READ_CACHE_STATUS_UNKNOWN
 * @physical_disk_cache:
 *     Whether physical disk's cache is enabled or not. Please be advised,
 *     HDD's physical disk ram cache might not be protected by storage system's
 *     battery or capacitor on sudden power loss, you could lose data if a power
 *     failure occurs during a write process. For SSD's physical disk cache,
 *     please check with the vendor of your hardware RAID card and SSD disk.
 *     Valid values are:
 *          * LSM_VOLUME_PHYSICAL_DISK_CACHE_ENABLED
 *              Physical disk cache enabled.
 *          * LSM_VOLUME_PHYSICAL_DISK_CACHE_DISABLED
 *              Physical disk cache disabled.
 *          * LSM_VOLUME_PHYSICAL_DISK_CACHE_USE_DISK_SETTING
 *              Physical disk cache is determined by the disk vendor via
 *              physical disks' SCSI caching mode page(0x08 page). It is
 *              strongly suggested to change this value to
 *              LSM_VOLUME_PHYSICAL_DISK_CACHE_ENABLED or
 *              LSM_VOLUME_PHYSICAL_DISK_CACHE_DISABLED
 *          * LSM_VOLUME_PHYSICAL_DISK_CACHE_UNKNOWN
 *              Plugin failed to detect the physical disk status.
 *
 * Return:
 *      Error code as enumerated by 'lsm_error_number'.
 *          * LSM_ERR_OK
 *              On success.
 *          * LSM_ERR_NOT_FOUND_VOLUME
 *              When volume not found.
 *          * LSM_ERR_INVALID_ARGUMENT
 *              When any argument is NULL or invalid flags.
 *          * LSM_ERR_NO_SUPPORT
 *              Not supported.
 */
int LSM_DLL_EXPORT lsm_volume_cache_info(
    lsm_connect *conn, lsm_volume *volume, uint32_t *write_cache_policy,
    uint32_t *write_cache_status, uint32_t *read_cache_policy,
    uint32_t *read_cache_status, uint32_t *physical_disk_cache, lsm_flag flags);

/**
 * lsm_volume_physical_disk_cache_update - Change RAM physical disk cache
 * setting of specified volume.
 *
 * Version:
 *      1.3
 *
 * Description:
 *      Change the setting of RAM physical disk cache of specified volume. On
 *      some product(like HPE SmartArray), this action will be effective at
 *      system level which means that even you are requesting a change on a
 *      specified volume, this change will apply to all other volumes on the
 *      same controller(system).
 *
 * Capability:
 *      LSM_CAP_VOLUME_PHYSICAL_DISK_CACHE_UPDATE
 *      LSM_CAP_VOLUME_PHYSICAL_DISK_CACHE_UPDATE_SYSTEM_LEVEL
 *
 * @conn:
 *      Valid lsm_connect pointer.
 * @volume:
 *      Pointer of lsm_volume.
 * @pdc:
 *      uint32_t. Physical disk cache setting, valid values are:
 *          * LSM_VOLUME_PHYSICAL_DISK_CACHE_ENABLED
 *              Enable physical disk cache.
 *          * LSM_VOLUME_PHYSICAL_DISK_CACHE_DISABLED
 *              Disable physical disk cache
 * @flags:
 *      Reserved for future use, must be LSM_CLIENT_FLAG_RSVD.
 *
 * Return:
 *      Error code as enumerated by 'lsm_error_number'.
 *          * LSM_ERR_OK
 *              On success.
 *          * LSM_ERR_NOT_FOUND_VOLUME
 *              When volume not found.
 *          * LSM_ERR_INVALID_ARGUMENT
 *              When any argument is NULL or invalid flags.
 *          * LSM_ERR_NO_SUPPORT
 *              Not supported.
 */
int LSM_DLL_EXPORT lsm_volume_physical_disk_cache_update(lsm_connect *conn,
                                                         lsm_volume *volume,
                                                         uint32_t pdc,
                                                         lsm_flag flags);

/**
 * lsm_volume_write_cache_policy_update - Change RAM write cache policy of
 * the specified volume
 *
 * Version:
 *      1.3
 *
 * Description:
 *      Change the RAM write cache policy on specified volume. If
 *      LSM_CAP_VOLUME_WRITE_CACHE_POLICY_UPDATE_IMPACT_READ is supported(e.g.
 * HPE SmartArray), the changes on write cache policy might also impact read
 *      cache policy. If
 * LSM_CAP_VOLUME_WRITE_CACHE_POLICY_UPDATE_WB_IMPACT_OTHER is supported(e.g.
 * HPE SmartArray), changing write cache policy to write back mode might impact
 * other volumes in the same system.
 *
 * Capability:
 *      LSM_CAP_VOLUME_WRITE_CACHE_POLICY_UPDATE_AUTO
 *      LSM_CAP_VOLUME_WRITE_CACHE_POLICY_UPDATE_WRITE_BACK
 *      LSM_CAP_VOLUME_WRITE_CACHE_POLICY_UPDATE_WRITE_THROUGH
 *      LSM_CAP_VOLUME_WRITE_CACHE_POLICY_UPDATE_WB_IMPACT_OTHER
 *      LSM_CAP_VOLUME_WRITE_CACHE_POLICY_UPDATE_IMPACT_READ
 *
 * @conn:
 *      Valid lsm_connect pointer.
 * @volume:
 *      A single lsm_volume
 * @wcp:
 *      uint32_t. Write cache policy. Valid values are:
 *          * LSM_VOLUME_WRITE_CACHE_POLICY_WRITE_BACK
 *              Change to write back mode.
 *          * LSM_VOLUME_WRITE_CACHE_POLICY_AUTO
 *              Change to auto mode: use write back mode when battery/capacitor
 *              is healthy, otherwise use write through.
 *          * LSM_VOLUME_WRITE_CACHE_POLICY_WRITE_THROUGH
 *              Change to write through mode.
 * @flags:
 *      Reserved for future use, must be LSM_CLIENT_FLAG_RSVD.
 *
 * Return:
 *      LSM_ERR_OK on success else error reason
 *          * LSM_ERR_OK
 *              On success.
 *          * LSM_ERR_NOT_FOUND_VOLUME
 *              When volume not found.
 *          * LSM_ERR_INVALID_ARGUMENT
 *              When any argument is NULL or invalid flags.
 *          * LSM_ERR_NO_SUPPORT
 *              Not supported.
 */
int LSM_DLL_EXPORT lsm_volume_write_cache_policy_update(lsm_connect *conn,
                                                        lsm_volume *volume,
                                                        uint32_t wcp,
                                                        lsm_flag flags);
/**
 * lsm_volume_read_cache_policy_update - Change RAM read cache policy of the
 * specified volume
 *
 * Version:
 *      1.3
 *
 * Description:
 *      Change the RAM read cache policy of the specified volume.  If
 *      LSM_CAP_VOLUME_READ_CACHE_POLICY_UPDATE_IMPACT_WRITE is supported(like
 *      HPE SmartArray), the changes on write cache policy might also impact
 *      read cache policy.
 *
 * Capability:
 *      LSM_CAP_VOLUME_READ_CACHE_POLICY_UPDATE
 *      LSM_CAP_VOLUME_READ_CACHE_POLICY_UPDATE_IMPACT_WRITE
 *
 * @conn:
 *      Valid lsm_connect pointer.
 * @volume:
 *      Pointer of lsm_string.
 * @rcp:
 *      uint32_t. Read cache policy. Valid values are:
 *          * LSM_VOLUME_READ_CACHE_POLICY_ENABLED
 *              Enable read cache.
 *          * LSM_VOLUME_READ_CACHE_POLICY_DISABLED
 *              Disable read cache.
 * @flags:
 *      Reserved for future use, must be LSM_CLIENT_FLAG_RSVD.
 *
 * Return:
 *      LSM_ERR_OK on success else error reason
 *          * LSM_ERR_OK
 *              On success.
 *          * LSM_ERR_NOT_FOUND_VOLUME
 *              When volume not found.
 *          * LSM_ERR_INVALID_ARGUMENT
 *              When any argument is NULL or invalid flags.
 *          * LSM_ERR_NO_SUPPORT
 *              Not supported.
 */
int LSM_DLL_EXPORT lsm_volume_read_cache_policy_update(lsm_connect *conn,
                                                       lsm_volume *volume,
                                                       uint32_t rcp,
                                                       lsm_flag flags);

#ifdef __cplusplus
}
#endif
#endif /* LIBSTORAGEMGMT_H */
