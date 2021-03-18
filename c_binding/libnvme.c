/*
 * Copyright (C) 2021 Red Hat, Inc.
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
 * Author: Tony Asleson <tasleson@redhat.com>
 */

#include <errno.h>
#include <linux/nvme_ioctl.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <sys/ioctl.h>

#include "libstoragemgmt/libstoragemgmt_error.h"
#include "utils.h"

/*
 * Note:
 * When libnvme gets released we will switch all of this code over to using it!
 */

struct smart_data {
    uint8_t critical_warning;
    uint8_t unused_rsvd[511];
};

int _nvme_health_status(char *err_msg, int fd, int32_t *health_status) {

    /*
     * Code based on NVMe Revision 1.4 June 10, 2019
     * 5.14 Get Log Page Command
     *
     * Command dwords 10, 11, 12, 13, 14 are applicable for get log page command
     *
     *         Bits
     * cdw10 = 31:16 - Number of dwords lower
     *         15    - Retain Asynchronous Event (RAE)
     *         14:12 - Reserved
     *         11: 8 - Log Specific Field (LSP)
     *          7: 0 - Log Page Idenfifier (LID)
     * cdw11 = 31:16 - Log Specific Identifier, Not using
     *         15: 0 - Number of dwords upper
     * cdw12 = 31: 0 - Log Page Offset Lower (LPOL) Not using
     * cdw13 = 31: 0 - Log Page Offset Upper (LPOU) Not using
     * cdw14 = 31: 7 - Reserved
     *          6: 0 - UUID index, Not using
     */

    /* Number of dwords is ZERO based! */
    uint32_t number_dwords = (sizeof(struct smart_data) >> 2) - 1;
    uint16_t number_dword_upper = number_dwords >> 16;
    uint16_t number_dword_lower = number_dwords & 0xffff;

    /* LSP == 0, RAE == 0, LID = 2 (Smart/Health information) */
    uint32_t cdw10 = number_dword_lower << 16 | 0x02;

    struct smart_data data;
    memset(&data, 0, sizeof(data));

    struct nvme_admin_cmd cmd = {
        .opcode = 0x02,     // nvme admin get log page
        .nsid = 0xffffffff, // All namespaces
        .addr = (uint64_t)(uintptr_t)&data,
        .data_len = sizeof(data),
        .cdw10 = cdw10,
        .cdw11 = number_dword_upper,
        .cdw12 = 0,
        .cdw13 = 0,
        .cdw14 = 0,
    };

    errno = 0;
    int rc = ioctl(fd, NVME_IOCTL_ADMIN_CMD, &cmd);
    if (0 == rc) {
        /* If any bits are set we are calling this a fail */
        *health_status = (data.critical_warning == 0)
                             ? LSM_DISK_HEALTH_STATUS_GOOD
                             : LSM_DISK_HEALTH_STATUS_FAIL;
        return LSM_ERR_OK;
    } else if (-1 == rc) {
        int errno_cpy = errno;
        snprintf(err_msg, _LSM_ERR_MSG_LEN,
                 "Unexpected return from ioctl %d (%s)", rc,
                 strerror(errno_cpy));
    } else {
        /* We got a nvme status code */
        snprintf(err_msg, _LSM_ERR_MSG_LEN,
                 "Unexpected return from ioctl, nvme status code 0x%X", rc);
    }
    return LSM_ERR_LIB_BUG;
}
