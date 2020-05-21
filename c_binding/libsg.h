/*
 * Copyright (C) 2016 Red Hat, Inc.
 * (C) Copyright (C) 2017 Hewlett Packard Enterprise Development LP
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

#ifndef _LIBSG_H_
#define _LIBSG_H_

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>

#include "libata.h" /* for _ATA_IDENTIFY_DEVICE_DATA_LEN only */
#include "libstoragemgmt/libstoragemgmt_common.h"

/* SPC-5 rev 7, Table 487 - ASSOCIATION field */
#define _SG_T10_SPC_ASSOCIATION_TGT_PORT 1

#define _SG_T10_SPC_VPD_DI_DESIGNATOR_TYPE_NAA 0x3

/* SPL-4 rev5 4.2.4 SAS address. With trailing \0. */
#define _SG_T10_SPL_SAS_ADDR_LEN 17
/* SPL-4 rev5 4.2.4 SAS address. */
#define _SG_T10_SPL_SAS_ADDR_LEN_BITS 8

#define _SG_T10_SPC_VPD_UNIT_SN            0x80
#define _SG_T10_SPC_VPD_DI                 0x83
#define _SG_T10_SPC_VPD_DI_NAA_235_ID_LEN  8
#define _SG_T10_SPC_VPD_DI_NAA_6_ID_LEN    16
#define _SG_T10_SPC_VPD_DI_NAA_TYPE_2      0x2
#define _SG_T10_SPC_VPD_DI_NAA_TYPE_3      0x3
#define _SG_T10_SPC_VPD_DI_NAA_TYPE_5      0x5
#define _SG_T10_SPC_VPD_DI_NAA_TYPE_6      0x6
#define _SG_T10_SPC_VPD_DI_ASSOCIATION_LUN 0

/* SBC-4 rev9 Table 236 - Block Device Characteristics VPD page */
#define _SG_T10_SBC_VPD_BLK_DEV_CHA 0xb1
/* SBC-4 rev9 Table 237 - MEDIUM ROTATION RATE field */
#define _SG_T10_SBC_MEDIUM_ROTATION_NO_SUPPORT 0
/* SBC-4 rev9 Table 237 - MEDIUM ROTATION RATE field */
#define _SG_T10_SBC_MEDIUM_ROTATION_SSD 1

/* SAT-4 rev4 12.4.2 ATA Information VPD page */
#define _SG_T10_SPC_VPD_ATA_INFO 0x89

/* SPC-5 rev 7, 7.7.16 Supported VPD Pages VPD page */
#define _SG_T10_SPC_VPD_SUP_VPD_PGS 0x00

/* SPC-5 rev 7, Table 142 - INQUIRY command */
#define _SG_T10_SPC_INQUIRY_MAX_LEN 0xffff
/* VPD is a INQUIRY */
#define _SG_T10_SPC_VPD_MAX_LEN _SG_T10_SPC_INQUIRY_MAX_LEN

/* SPC-5 Table 444 - PROTOCOL IDENTIFIER field values */
#define _SG_T10_SPC_PROTOCOL_ID_OBSOLETE 1
/* SPC-5 Table 444 - PROTOCOL IDENTIFIER field values */
#define _SG_T10_SPC_PROTOCOL_ID_RESERVED 0xc

#define _SG_T10_SPC_RECV_DIAG_MAX_LEN 0xffff
/* ^ SPC-5 rev 7, Table 219 - RECEIVE DIAGNOSTIC RESULTS command */
#define _SG_T10_SPC_SEND_DIAG_MAX_LEN 0xffff
/* ^ SPC-5 rev 7, Table 269 - SEND DIAGNOSTIC command */
#define _SG_T10_SPC_PROTOCOL_ID_SAS 6
/* ^ SPC-5 rev 7, Table 444 - PROTOCOL IDENTIFIER field values */

#define _SG_T10_SPC_MODE_SENSE_MAX_LEN 0xffff
/* ^ SPC-5 rev 12, 6.15 MODE SENSE(10) command introduction */

#define _T10_SPC_LOG_SENSE_MAX_LEN 0xffff
/* ^ SPC-5 rev07 6.8 LOG SENSE command */

#define _T10_SPC_REQUEST_SENSE_MAX_LEN 0xff
/* ^ SPC-5 rev07 6.35 REQUEST SENSE command */

#pragma pack(push, 1)

/*
 * SPC-5 rev 7 Table 590 - Designation descriptor
 */
struct _sg_t10_vpd83_dp_header {
    uint8_t code_set : 4;
    uint8_t protocol_id : 4;
    uint8_t designator_type : 4;
    uint8_t association : 2;
    uint8_t reserved_1 : 1;
    uint8_t piv : 1;
    uint8_t reserved_2;
    uint8_t designator_len;
};

/* SPC-5 rev 7, Table 486 - Designation descriptor. */
LSM_DLL_LOCAL struct _sg_t10_vpd83_dp {
    struct _sg_t10_vpd83_dp_header header;
    uint8_t designator[0xff];
};

LSM_DLL_LOCAL struct _sg_t10_vpd83_naa_header {
    uint8_t data_msb : 4;
    uint8_t naa_type : 4;
};

struct _sg_t10_vpd_ata_info {
    uint8_t we_dont_care_0[60];
    uint8_t ata_id_dev_data[_ATA_IDENTIFY_DEVICE_DATA_LEN];
};

#pragma pack(pop)

/*
 * Preconditions:
 *  err_msg != NULL
 *  disk_path != NULL
 *  fd != NULL
 */
LSM_DLL_LOCAL int _sg_io_open_ro(char *err_msg, const char *disk_path, int *fd);

/*
 * Preconditions:
 *  err_msg != NULL
 *  fd >= 0
 *  data != NULL
 *  data is uint8_t[_SG_T10_SPC_VPD_MAX_LEN]
 */
LSM_DLL_LOCAL int _sg_io_vpd(char *err_msg, int fd, uint8_t page_code,
                             uint8_t *data);

/*
 * Preconditions:
 *  dps != NULL
 */
LSM_DLL_LOCAL void _sg_t10_vpd83_dp_array_free(struct _sg_t10_vpd83_dp **dps,
                                               uint16_t dp_count);

/*
 * Preconditions:
 *  err_msg != NULL
 *  vpd_data != NULL
 *  vpd_data is uint8_t[_SG_T10_SPC_VPD_MAX_LEN]
 *  serial_num != NULL
 *  serial_num_max_len != 0
 */
LSM_DLL_LOCAL int _sg_parse_vpd_80(char *err_msg, uint8_t *vpd_data,
                                   uint8_t *serial_num,
                                   uint16_t serial_num_max_len);

/*
 * Preconditions:
 *  err_msg != NULL
 *  vpd_data != NULL
 *  vpd_data is uint8_t[_SG_T10_SPC_VPD_MAX_LEN]
 *  dps != NULL
 *  dp_count != NULL
 */
LSM_DLL_LOCAL int _sg_parse_vpd_83(char *err_msg, uint8_t *vpd_data,
                                   struct _sg_t10_vpd83_dp ***dps,
                                   uint16_t *dp_count);
/*
 * Preconditions:
 *  vpd_0_data != NULL
 *  vpd_0_data is uint8_t[_SG_T10_SPC_VPD_MAX_LEN]
 */
LSM_DLL_LOCAL bool _sg_is_vpd_page_supported(uint8_t *vpd_0_data,
                                             uint8_t page_code);

/*
 * Preconditions:
 *  err_msg != NULL
 *  disk_path != NULL
 *  fd != NULL
 */
LSM_DLL_LOCAL int _sg_io_open_rw(char *err_msg, const char *disk_path, int *fd);

/*
 * Preconditions:
 *  err_msg != NULL
 *  fd >= 0
 *  tp_sas_addr != NULL
 *  tp_sas_addr is char[_SG_T10_SPL_SAS_ADDR_LEN]
 */
LSM_DLL_LOCAL int _sg_tp_sas_addr_of_disk(char *err_msg, int fd,
                                          char *tp_sas_addr);

/*
 * Preconditions:
 *  err_msg != NULL
 *  fd >= 0
 *  returned_sense_data != NULL
 */
LSM_DLL_LOCAL int _sg_request_sense(char *err_msg, int fd,
                                    uint8_t *returned_sense_data);

/*
 * Preconditions:
 *  None
 */
LSM_DLL_LOCAL int32_t _sg_info_excep_interpret_asc(uint8_t asc);

/*
 * Preconditions:
 *  err_msg != NULL
 *  fd >= 0
 *  health_status != NULL
 */
LSM_DLL_LOCAL int _sg_sas_health_status(char *err_msg, int fd,
                                        int32_t *health_status);

/*
 * Preconditions:
 *  err_msg != NULL
 *  fd >= 0
 *  health_status != NULL
 */
LSM_DLL_LOCAL int _sg_ata_health_status(char *err_msg, int fd,
                                        int32_t *health_status);

/*
 * Preconditions:
 *  err_msg != NULL
 *  fd >= 0
 *  data != NULL
 *  data is uint8_t[_SG_T10_SPC_RECV_DIAG_MAX_LEN]
 */
LSM_DLL_LOCAL int _sg_io_recv_diag(char *err_msg, int fd, uint8_t page_code,
                                   uint8_t *data);

/*
 * Preconditions:
 *  err_msg != NULL
 *  fd >= 0
 *  data != NULL
 *  data is uint8_t[_SG_T10_SPC_SEND_DIAG_MAX_LEN]
 */
LSM_DLL_LOCAL int _sg_io_send_diag(char *err_msg, int fd, uint8_t *data,
                                   uint16_t data_len);

/*
 * IOCTL MODE SENSE(10) and remove mode parameter header and Block descriptors.
 * In SCSI SPC-5, `MODE SENSE (6)` is deprecated.
 * Preconditions:
 *  err_msg != NULL
 *  fd >= 0
 *  data != NULL
 *  data is uint8_t[_SG_T10_SPC_MODE_SENSE_MAX_LEN]
 */
LSM_DLL_LOCAL int _sg_io_mode_sense(char *err_msg, int fd, uint8_t page_code,
                                    uint8_t sub_page_code, uint8_t *data);

/*
 * Use SCSI_IOCTL_GET_BUS_NUMBER IOCTL to get host number of given SCSI disk.
 * Preconditions:
 *  err_msg != NULL
 *  fd >= 0
 *  host_no != NULL
 */
LSM_DLL_LOCAL int _sg_host_no(char *err_msg, int fd, unsigned int *host_no);

#endif /* End of _LIBSG_H_ */
