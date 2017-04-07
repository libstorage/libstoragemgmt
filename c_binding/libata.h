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

#ifndef _LIBATA_H_
#define _LIBATA_H_

#include <stdint.h>
#include "libstoragemgmt/libstoragemgmt_common.h"

#define _ATA_IDENTIFY_DEVICE_DATA_LEN                        512

#define _ATA_REGISTER_INPUT_28_BIT_LENGTH                    7
#define _ATA_REGISTER_OUTPUT_28_BIT_LENGTH                   7
#define ATA_PASS_THROUGH_12_LEN                              12
#define ATA_PASS_THROUGH_12                                  0xa1

/*
 * ACS-3 7.48.8 SMART RETURN STATUS
 */
#define ATA_SMART_RETURN_STATUS_SUBCOMMAND                   0xda
#define ATA_SMART_COMMAND                                    0xb0

#define ATA_NON_DATA_COMMAND                                 0
#define ATA_DATA_IN_COMMAND                                  1
#define ATA_DATA_OUT_COMMAND                                 2

/*
 * ACS-3 Table 210
 */
#define SMART_STATUS_LBA_MID_THRESHOLD_EXCEEDED              0xf4
#define SMART_STATUS_LBA_HIGH_THRESHOLD_EXCEEDED             0x2c
#define SMART_STATUS_LBA_MID_DEFAULT                         0x4f
#define SMART_STATUS_LBA_HIGH_DEFAULT                        0xc2

#pragma pack(push, 1)

struct _ata_registers_input_28_bit {
    uint8_t feature;
    uint8_t count;
    uint8_t lba_low;
    uint8_t lba_mid;
    uint8_t lba_high;
    uint8_t device;
    uint8_t command;
};

struct _ata_registers_output_28_bit {
    uint8_t error;
    uint8_t count;
    uint8_t lba_low;
    uint8_t lba_mid;
    uint8_t lba_high;
    uint8_t device;
    uint8_t status;
};

#pragma pack(pop)

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
 * Preconditions:
 * ata_cmd != NULL
 */
LSM_DLL_LOCAL void _ata_smart_status_fill_registers(uint8_t *ata_cmd,
                                                    uint8_t cmd,
                                                    uint8_t features,
                                                    uint8_t lba_high,
                                                    uint8_t lba_mid,
                                                    uint8_t lba_low,
                                                    uint8_t count,
                                                    uint8_t device);

/*
 * Preconditions:
 * ata_output_regs != NULL
 */
LSM_DLL_LOCAL int32_t _ata_smart_status_interpret_output_regs(uint8_t *ata_output_regs);

#endif  /* End of _LIBATA_H_ */
