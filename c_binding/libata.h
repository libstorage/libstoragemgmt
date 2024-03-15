/*
 * SPDX-License-Identifier: LGPL-2.1-or-later
 *
 * Copyright (C) 2016-2023 Red Hat, Inc.
 * (C) Copyright (C) 2017 Hewlett Packard Enterprise Development LP
 *
 * Author: Gris Ge <fge@redhat.com>
 */

#ifndef _LIBATA_H_
#define _LIBATA_H_

#include "libstoragemgmt/libstoragemgmt_common.h"
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define _ATA_IDENTIFY_DEVICE_DATA_LEN 512

/*
 * ACS-3 7.48.8 SMART RETURN STATUS – B0h/DAh, Non-Data
 */
#define _ATA_FEATURE_SMART_RETURN_STATUS      0xda
#define _ATA_CMD_SMART_RETURN_STATUS          0xb0
#define _ATA_CMD_SMART_RETURN_STATUS_LBA_MID  0x4f
#define _ATA_CMD_SMART_RETURN_STATUS_LBA_HIGH 0xc2
/* ^ lba 8:23 should be 0xc24f by ACS-3 Table 135 — SMART RETURN STATUS
 * command inputs
 */

/*
 * Preconditions:
 *  id_dev_data != NULL
 *  id_dev_data is uint8_t[_LIBATA_IDENTIFY_DEVICE_DATA_LEN]
 *  link_speed != NULL;
 * Return:
 *  LSM_ERR_XXX.
 */
LSM_DLL_LOCAL int _ata_cur_speed_get(char *err_msg, uint8_t *id_dev_data,
                                     uint32_t *link_speed);

/*
 * return health status: LSM_DISK_HEALTH_STATUS_FAIL and etc
 */
LSM_DLL_LOCAL int32_t _ata_health_status(uint8_t status, uint8_t lba_mid,
                                         uint8_t lba_high);

#ifdef __cplusplus
}
#endif
#endif /* End of _LIBATA_H_ */
