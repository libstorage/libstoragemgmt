/*
 * Copyright (c) 2015 Red Hat, Inc.
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. The name of the author may not be used to endorse or promote products
 *    derived from this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE AUTHOR OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 *
 * Author:  Gris Ge <fge@redhat.com>
 */

/*
 * Please note this file is __NOT__ LGPL license but 3-clauses BSD license.
 * And most of lines in this file are based on sg3_utils sg_ses.c
 * which is 3-clauses BSD license. sg3_utils sg_ses.c license attached below:
 *
 * Copyright (c) 1999-2013 Douglas Gilbert.
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. The name of the author may not be used to endorse or promote products
 *    derived from this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE AUTHOR OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */

#ifndef _LIBSES_H_
#define _LIBSES_H_

#include <stdint.h>
#include <stdbool.h>

/*
 * Layout(Multiple enclosure services processes in a subenclosure)
 *
 *                      Linux /dev/sgX
 *                           |
 *                  ---------------------------------
 *                  |                    |          | ....
 *              sub_enclosure       sub_enclosure
 *                  |
 *      -------------------------
 *      |                       |
 *  enclosure service       enclosure service
 *      |                       |
 *      |                       |
 *      +-----------+-------+---|---- ...
 *      | +---------|--+----|-+-+---- ...
 *      | |         |  |    | |     | ...
 *      disk        disk    fan
 */

#define SES_OK 0
#define SES_ERR_BUG 1
#define SES_ERR_OPEN_FAIL 2
#define SES_ERR_NO_SUPPORT 3
#define SES_ERR_INVALID_ARGUMENT 4


#define SES_T10_DPC_CONF_DIAG 0x01
#define SES_T10_DPC_STATUS_DIAG 0x02
#define SES_T10_DPC_ADD_STATUS_DIAG 0x0a

/*
 * SPC-5 rev3 "4.2.5.6 Allocation length" allows 0xffff size.
 *
 */
#define SES_T10_MAX_OUTPUT_SIZE 0xffff

/*
 * SPC-5 rev3 Table 139 — PERIPHERAL DEVICE TYPE field
 */
#define SES_T10_PERIPHERAL_DEV_TYPE 0x0d

/*
 * SPC-5 rev3 6.5.2 Standard INQUIRY data, ENCSERV
 * byte 6 bit 6.
 */
#define SES_T10_STD_INQ_BYTE_6_ENC_SERV 0x4

/*
 * SES-3 rev10 "4.3.1 Subenclosures overview"
 */
#define SES_T10_MAX_SUB_ENCLOSURE 0xff

/*
 * SPC-5 rev 3 "7.6.1 Protocol specific parameters introduction"
 * PROTOCOL IDENTIFIER
 */
#define SES_T10_PROTOCOL_FC 0x0
#define SES_T10_PROTOCOL_SAS 0x6
#define SES_T10_PROTOCOL_PCIE 0xb

#define SES_ERR_MSG_LENGTH 512
static char ses_err_msg[SES_ERR_MSG_LENGTH];

enum ses_disk_link_type {
    SES_DISK_LINK_TYPE_SATA,
    SES_DISK_LINK_TYPE_SAS,
    SES_DISK_LINK_TYPE_FC,
    SES_DISK_LINK_TYPE_NVME,
};
/*
 * LSM ses plugin is the only user of this library, hence no need to
 * use opaque structure here.
 */
struct SES_disk {
    uint32_t item_index;
    /* ^ The disk index(start with 0) in configure page "Type descriptor header
     * list".
     */
    enum ses_disk_link_type link_type;
    char id[21];
    /* ^    FC:     16 bytes hex WWNN
     *      SATA:   16 bytes hex SAS address of STP target port.
     *      SAS:    16 bytes hex disk SAS address.
     *      NVMe:   20 bytes serial number of NVMe.
     */
    uint32_t slot_num;
};

struct SES_enclosure {
    char vendor[9];
    /* ^ ENCLOSURE VENDOR IDENTIFICATION, with additional tail '\0' */
    char product[17];
    /* ^ PRODUCT IDENTIFICATION, with additional tail '\0' */
    char rev[5];
    /* ^ PRODUCT REVISION LEVEL, with additional tail '\0' */
    char id[17];
    /* ^ ENCLOSURE LOGICAL IDENTIFIER: 8-byte NAA identifier */
    uint8_t esp_id;
    /* ^ RELATIVE ENCLOSURE SERVICES PROCESS IDENTIFIER */
    uint8_t esp_count;
    /* ^ NUMBER OF ENCLOSURE SERVICES PROCESSES */
};

/*
 * return errno when error.
 */
int ses_enclosure_get(const char *sg_path, struct SES_enclosure **ses_enc);

/*
 * Return a list of struct SES_disk attached to provided enclosure.
 * Each physical link will be treated as a ses_disk.
 * Return errno when error.
 */
int ses_disk_list_get(const char *sg_path, struct SES_disk ***disks,
                      uint32_t *count);

void ses_enclosure_free(struct SES_enclosure *ses_enc);

void ses_disk_list_free(struct SES_disk **disks, uint32_t count);

#endif  /* End of _LIBSES_H_ */
