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
 * And most of lines in this file are copyed from or inspired by sg3_utils
 * sg_ses.c which is 3-clauses BSD license. sg3_utils sg_ses.c license attached
 * below:
 */

/*
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

#include <stdint.h>
#include <scsi/sg_cmds_basic.h>
#include <scsi/sg_cmds_extra.h>
#include <scsi/sg_lib.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <inttypes.h>

#include "libses.h"
#include "ptr_list.h"
#include "my_mem.h"

#define _VERBOSE 0
#define _NOISY 0
#define _PCV 1

#define _MAX_3_BITS 7
#define _MAX_4_BITS 0xf

#define _CHAR_ARRAY_MEMCPY(dst, src) \
    { \
        size_t _char_array_size = sizeof((src)) / sizeof((src)[0]); \
        memcpy((dst), (src), _char_array_size); \
        (dst)[_char_array_size] = '\0'; \
        _trim_tailling_space((dst)); \
    }

#define _err_msg_clear() ses_err_msg[0] = '\0'
#define _err_msg(format, ...) \
    { \
        printf(format "\n", ##__VA_ARGS__); \
        snprintf(ses_err_msg, SES_ERR_MSG_LENGTH, format, ##__VA_ARGS__); \
    }

#pragma pack(1)
/*
 * SES-3 rev 10 "Table 11 — Configuration diagnostic page"
 */
struct _conf_header {
    uint8_t page_code;
    /* ^ PAGE CODE */
    uint8_t sec_enc_count;
    /* ^ NUMBER OF SECONDARY SUBENCLOSURES */
    uint16_t len_4;
    /* ^ PAGE LENGTH */
    uint32_t be_gen_code;
    /* ^ GENERATION CODE */
    unsigned char info;
    /* ^ start of rest of data */
};
#pragma pack()


/*
 * SES-3 rev 10 Table 12 — Enclosure descriptor
 */
#pragma pack(1)
struct _conf_enclosure {
    uint8_t enc_pro_ids;
    /* ^ RELATIVE ENCLOSURE SERVICES PROCESS IDENTIFIER and
     *   NUMBER OF ENCLOSURE SERVICES PROCESSES
     */
    uint8_t sub_enc_id;
    /* ^ SUBENCLOSURE IDENTIFIER */
    uint8_t type_dp_header_count;
    /* ^ NUMBER OF TYPE DESCRIPTOR HEADERS */
    uint8_t len_4;
    /* ^ ENCLOSURE DESCRIPTOR LENGTH (m - 3) */
    unsigned char enc_logic_id[8];
    char vendor[8];
    char product[16];
    char rev[4];
};
#pragma pack()

#pragma pack(1)
/*
 * SES-3 rev 10 "Table 31 — Additional Element Status diagnostic page"
 */
struct _add_status_header {
    uint8_t page_code;
    /* ^ PAGE CODE: 0Ah */
    unsigned char reserved;
    uint16_t len_4;
    /* ^ PAGE LENGTH */
    uint32_t be_gen_code;
    /* ^ GENERATION CODE */
};
#pragma pack()

#pragma pack(1)
/*
 * SES-3 rev 10 Table 32 — Additional Element Status descriptor
 */
struct _add_status_dp_header {
    unsigned char byte_0;
    /* ^ Contains INVALID and EIP. */
    uint8_t len_2;
    /* ^ PAGE LENGTH */
    unsigned char byte_2;
    /* ^ EIIOE at bit 0 */
    uint8_t element_index;
    /* ^  ELEMENT INDEX */
    unsigned char info;
};

struct _add_status_sas {
    uint8_t phy_count;
    unsigned char byte_1;
    /* ^ DESCRIPTOR TYPE */
    unsigned char reserved;
    uint8_t dev_slot_num;
    /* ^ DEVICE SLOT NUMBER */
    unsigned char phy_list;
};

struct _add_status_phy {
    uint8_t byte_0;
    unsigned char reserved;
    uint8_t byte_2;
    uint8_t byte_3;
    unsigned char attached_sas_addr[8];
    unsigned char sas_addr[8];
    uint8_t phy_id;
    unsigned char reserved_2[7];
    unsigned char next;
};
#pragma pack()

static int _sg_rc_to_ses_rc(int sg_rc);
static void _trim_tailling_space(char *str);
static int _ses_sg_open(const char *sg_path, int *fd);
static void _ses_sg_close(int fd);
static int _parse_add_st_sas(struct _add_status_sas *add_st_sas,
                             struct pointer_list *all_disks,
                             unsigned char *end);

/*
 * TODO(Gris Ge): Split this functions into pieces
 */
int ses_enclosure_get(const char *sg_path, struct SES_enclosure **ses_enc)
{
    int fd = -1;
    int rc = 0;
    int sg_rc = 0;
    unsigned char ses_conf_buff[SES_T10_MAX_OUTPUT_SIZE];
    uint32_t conf_page_len = 0;
    uint32_t i = 0;
    void *p = NULL;
    void *end = NULL;
    struct _conf_header *cf_header = NULL;
    struct _conf_enclosure *cf_enc = NULL;

    _err_msg_clear();

    if (ses_enc == NULL)
        return SES_ERR_INVALID_ARGUMENT;

    *ses_enc = NULL;

    rc = _ses_sg_open(sg_path, &fd);
    if (rc != 0)
        return rc;

    memset(ses_conf_buff, 0, SES_T10_MAX_OUTPUT_SIZE);

    sg_rc = sg_ll_receive_diag(fd, _PCV, SES_T10_DPC_CONF_DIAG,
                               ses_conf_buff, SES_T10_MAX_OUTPUT_SIZE,
                               _NOISY, _VERBOSE);
    if (sg_rc != 0) {
        rc = _sg_rc_to_ses_rc(sg_rc);
        _err_msg("Failed to execute sg_ll_receive_diag(), error: %d", sg_rc);
        goto out;
    }

    cf_header = (struct _conf_header *) &ses_conf_buff;
    if (cf_header->page_code != SES_T10_DPC_CONF_DIAG) {
        _err_msg("BUG: Got returned page not SES_T10_DPC_CONF_DIAG");
        rc = SES_ERR_BUG;
        goto out;
    }

    conf_page_len = be16toh(cf_header->len_4) + 4;
    end = &ses_conf_buff + conf_page_len - 1;

    /* Only check the first sub enclosure(primary subenclosure) */
    cf_enc = (struct _conf_enclosure *) &cf_header->info;

    if ((void *)cf_enc + sizeof(struct _conf_enclosure) > end) {
        _err_msg("Corrupted data: facing memory boundary");
        rc = SES_ERR_NO_SUPPORT;
        goto out;
    }

    *ses_enc =
        (struct SES_enclosure *) malloc_or_die(sizeof(struct SES_enclosure));

    (*ses_enc)->esp_id = (cf_enc->enc_pro_ids >> 4) & _MAX_3_BITS;
    (*ses_enc)->esp_count = cf_enc->enc_pro_ids & _MAX_3_BITS;

    sprintf((*ses_enc)->id, "%02x%02x%02x%02x%02x%02x%02x%02x",
            cf_enc->enc_logic_id[0], cf_enc->enc_logic_id[1],
            cf_enc->enc_logic_id[2], cf_enc->enc_logic_id[3],
            cf_enc->enc_logic_id[4], cf_enc->enc_logic_id[5],
            cf_enc->enc_logic_id[6], cf_enc->enc_logic_id[7]);
    _CHAR_ARRAY_MEMCPY((*ses_enc)->vendor, cf_enc->vendor);
    _CHAR_ARRAY_MEMCPY((*ses_enc)->product, cf_enc->product);
    _CHAR_ARRAY_MEMCPY((*ses_enc)->rev, cf_enc->rev);

out:
    _ses_sg_close(fd);

    if (rc != 0) {
        if (*ses_enc != NULL)
            ses_enclosure_free(*ses_enc);
        *ses_enc = NULL;

    }

    return rc;
}

int ses_disk_list_get(const char *sg_path, struct SES_disk ***disks,
                      uint32_t *count)
{
    int rc = SES_OK;
    int sg_rc = 0;
    int fd = -1;
    unsigned char ses_add_status_buff[SES_T10_MAX_OUTPUT_SIZE];
    struct pointer_list *all_disks = NULL;
    uint32_t add_st_page_len = 0;
    uint32_t i = 0;
    struct _add_status_header *add_st_header = NULL;
    struct _add_status_dp_header *add_st_dp_header = NULL;
    struct _add_status_sas *add_st_sas = NULL;
    struct _add_status_phy *phy = NULL;
    uint16_t add_st_dp_len = 0;
    struct SES_disk *disk = NULL;
    uint8_t protocol_id = 0;
    uint8_t eip = 0;
    void *p = NULL;
    void *end = NULL;

    _err_msg_clear();

    if ((disks == NULL) || (count == NULL))
        return SES_ERR_INVALID_ARGUMENT;

    rc = _ses_sg_open(sg_path, &fd);
    if (rc != 0)
        return rc;

    memset(ses_add_status_buff, 0, SES_T10_MAX_OUTPUT_SIZE);

    sg_rc = sg_ll_receive_diag(fd, _PCV, SES_T10_DPC_ADD_STATUS_DIAG,
                               ses_add_status_buff, SES_T10_MAX_OUTPUT_SIZE,
                               _NOISY, _VERBOSE);
    if (sg_rc != 0) {
        _err_msg("Failed to execute sg_ll_receive_diag(), error: %d", sg_rc);
        rc = _sg_rc_to_ses_rc(sg_rc);
        goto out;
    }

    all_disks = ptr_list_new();

    add_st_header = (struct _add_status_header *) &ses_add_status_buff;
    add_st_page_len = be16toh(add_st_header->len_4) + 4;
    end = ses_add_status_buff + add_st_page_len - 1;

    p = ses_add_status_buff + sizeof(struct _add_status_header);
    while (p + sizeof(struct _add_status_dp_header) <= end) {
        add_st_dp_header = (struct _add_status_dp_header *) p;
        add_st_dp_len = add_st_dp_header->len_2 + 2;
        p += add_st_dp_len;
        if ((add_st_dp_header->byte_0 >> 7 /*INVALID*/) & 1) {
            continue;
        }
        eip = (add_st_dp_header->byte_0 >> 4) & 1;
        protocol_id = add_st_dp_header->byte_0 & _MAX_4_BITS;
        if (eip != 1) {
            _err_msg("illegal EIP value: 0 SES-2 and SES-3 require EIP == 1");
            rc = SES_ERR_NO_SUPPORT;
            goto out;
        }
        switch (protocol_id) {
        case SES_T10_PROTOCOL_SAS :
            if ((void *)&add_st_dp_header->info + sizeof(struct _add_status_sas)
                > end) {
                _err_msg("Corrupted data: facing memory boundary");
                rc = SES_ERR_NO_SUPPORT;
                goto out;
            }
            _parse_add_st_sas((struct _add_status_sas *)&add_st_dp_header->info,
                              all_disks, end);

            break;
        case SES_T10_PROTOCOL_FC :
            break;
        case SES_T10_PROTOCOL_PCIE :
            break;
        default :
            _err_msg("BUG: Unknown add_st_dp_header protocol id: %"PRIu8 "",
                     protocol_id);
            rc = SES_ERR_BUG;
            goto out;
        }
    }

    ptr_list_2_array(all_disks, (void ***) disks, count);

 out:
    _ses_sg_close(fd);
    if (rc != 0) {
        if (all_disks != NULL) {
            ptr_list_free(all_disks);
            all_disks = NULL;
        }
    }

    free(all_disks);
    disks = NULL;

    return rc;
}

void ses_disk_list_free(struct SES_disk **disks, uint32_t count)
{
    uint32_t i = 0;
    if (disks == NULL)
        return;
    for (; i < count; ++i) {
        free(disks[i]);
    }
    free(disks);
    disks = NULL;
}


void ses_enclosure_free(struct SES_enclosure *ses_enc)
{
    free(ses_enc);
    ses_enc = NULL;
}

static int _sg_rc_to_ses_rc(int sg_rc)
{
    if (sg_rc == 0)
        return 0;

    if (sg_rc == SG_LIB_CAT_INVALID_OP)
        return SES_ERR_NO_SUPPORT;

    return SES_ERR_BUG;
}

void _trim_tailling_space(char *str)
{
    char *end = NULL;

    if (*str == '\0')
        return;

    end = str + strlen(str) - 1;

    while(end > str && *end == ' ') --end;

    *(end + 1) = 0;
}

/*
 * Open /dev/sg* and check whether has SES service.
 * Return 0 if no error.
 */
static int _ses_sg_open(const char *sg_path, int *fd)
{
    int sg_rc = 0;
    int rc = 0;
    struct sg_simple_inquiry_resp simple_inq_resp;

    _err_msg_clear();

    if ((sg_path == NULL) || (fd == NULL) || (*sg_path == '\0'))
        return SES_ERR_INVALID_ARGUMENT;

    *fd = sg_cmds_open_device(sg_path, 1 /*read only*/, _VERBOSE);

    if (*fd < 0) {
        _err_msg("Failed on sg_cmds_open_device(): error %d", errno);
        return SES_ERR_OPEN_FAIL;
    }

    sg_rc = sg_simple_inquiry(*fd, &simple_inq_resp, _NOISY, _VERBOSE);
    if (sg_rc != 0) {
        _err_msg("Failed to open %s, error: %d", sg_path, rc);
        rc = _sg_rc_to_ses_rc(sg_rc);
        goto out;
    }

    if ((simple_inq_resp.peripheral_type != SES_T10_PERIPHERAL_DEV_TYPE) &&
        ! (simple_inq_resp.byte_6 & SES_T10_STD_INQ_BYTE_6_ENC_SERV)) {
        rc = SES_ERR_NO_SUPPORT;
        goto out;
    }

 out:
    if (rc != 0)
        _ses_sg_close(*fd);

    return rc;
}

static void _ses_sg_close(int fd)
{
    if (fd >= 0)
        sg_cmds_close_device(fd);
}

static int _parse_add_st_sas(struct _add_status_sas *add_st_sas,
                             struct pointer_list *all_disks, unsigned char *end)
{
    uint32_t slot_num = 0;
    uint32_t i = 0;
    unsigned char *cur_phy = NULL;
    struct _add_status_phy *phy = NULL;
    int rc = 0;
    struct SES_disk *disk = NULL;

    if ((add_st_sas->byte_1 >> 7 != 0) || (add_st_sas->byte_1 >> 6 != 0)) {
        /* ^ bit 7 and 6 is the DESCRIPTOR TYPE:
         *  00b: Device slot or Array Device Slot
         *  01b: SAS expander, SCSI init port, SCSI target port,
         *       Enclosure Services Controller Electronics elements
         */
        return rc;
    }
    slot_num = add_st_sas->dev_slot_num;
    for (cur_phy = &add_st_sas->phy_list; i < add_st_sas->phy_count; ++i) {
        /*
         * Each SAS phy link will be treated as a ses_disk.
         */
        if (cur_phy > end) {
            _err_msg("Corrupted data: facing memory boundary");
            rc = SES_ERR_NO_SUPPORT;
            goto out;
        }

        phy = (struct _add_status_phy *) cur_phy;
        if (&phy->next > (end + 1 /*there is no next on tailing phy*/)) {
            _err_msg("Corrupted data: facing memory boundary");
            rc = SES_ERR_NO_SUPPORT;
            goto out;
        }

        disk = (struct SES_disk *) malloc_or_die(sizeof(struct SES_disk));

        disk->item_index = 0; // TODO(Gris Ge): update item index

        ptr_list_add(all_disks, disk);

        disk->slot_num = slot_num;

        if (phy->byte_3 & 1 /* SATA_DEVICE */) {
            /* SATA device: use expander port SAS address as disk ID */
            disk->link_type = SES_DISK_LINK_TYPE_SATA;
        } else {
            /* SAS device: use disk phy port SAS address as disk ID */
            disk->link_type = SES_DISK_LINK_TYPE_SAS;
        }
        sprintf(disk->id,
                "%02x%02x%02x%02x%02x%02x%02x%02x",
                phy->sas_addr[0],
                phy->sas_addr[1],
                phy->sas_addr[2],
                phy->sas_addr[3],
                phy->sas_addr[4],
                phy->sas_addr[5],
                phy->sas_addr[6],
                phy->sas_addr[7]);
        cur_phy = &phy->next;
    }

 out:
    return rc;
}
