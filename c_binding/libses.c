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

/* For strerror_r() */
#define _GNU_SOURCE

#include "libsg.h"
#include "libses.h"
#include "utils.h"
#include "libstoragemgmt/libstoragemgmt_plug_interface.h"

#include <string.h>
#include <stdlib.h>
#include <stdbool.h>
#include <sys/types.h>
#include <dirent.h>
#include <errno.h>
#include <unistd.h>
#include <inttypes.h>
#include <assert.h>

/* SPC-5 Table 139 - PERIPHERAL DEVICE TYPE field */
#define _LINUX_SCSI_DEV_TYPE_SES                "13"  /* 0x0d  */
#define _LINUX_SCSI_DEV_TYPE_SES_LEN            2
/* just two digits like above */

#define _SYSFS_BSG_ROOT_PATH                    "/sys/class/bsg"

#define _T10_SES_CFG_PG_CODE                    0x01
#define _T10_SES_STATUS_PG_CODE                 0x02

/* SES-3 rev 11a Table 30 - Additional Element Status diagnostic page */
#define _T10_SES_ADD_STATUS_PG_CODE             0x0a

/* SES-3 rev 11a Table 81 - Array Device Slot status element */
#define _T10_SES_DEV_SLOT_STATUS_LEN            4

/* SES-3 rev 11a Table 38 - DESCRIPTOR TYPE field */
#define _T10_SES_DESCRIPTOR_TYPE_DEV_SLOT       0

#define _T10_SES_CTRL_PRDFAIL_BYTES             0
#define _T10_SES_CTRL_PRDFAIL_BIT               6
#define _T10_SES_CTRL_SELECT_BYTES              0
/* SES-3 rev 11a Table 69 - Control element format */
#define _T10_SES_CTRL_SELECT_BIT                7
#define _T10_SES_CTRL_RQST_IDENT_BYTES          2
#define _T10_SES_CTRL_RQST_IDENT_BIT            1
#define _T10_SES_CTRL_RQST_FAULT_BYTES          3
/* SES-3 rev 11a Table 80 - Array Device Slot control element */
#define _T10_SES_CTRL_RQST_FAULT_BIT            5

/* SES-3 rev 11a Table 31 - Additional Element Status descriptor with the EIP
 *                          bit set to one
 */
#define _T10_SES_ADD_DP_INCLUDE_OVERALL         1

/* SES-3 Table 12 - Type descriptor header format */
#define _T10_SES_CFG_DP_HDR_LEN                 4

#pragma pack(push, 1)
/*
 * SES-3 rev 11a "Table 30 - Additional Element Status diagnostic page"
 */
struct _ses_add_st {
    uint8_t page_code;
    uint8_t reserved;
    uint16_t len_be;
    uint32_t gen_code_be;
    uint8_t dp_list_begin;
};

/*
 * SES-3 rev 11a Table 31 - Additional Element Status descriptor
 */
struct _ses_add_st_dp {
#if __BYTE_ORDER__ == __ORDER_LITTLE_ENDIAN__
    uint8_t protocol_id         : 4;
    uint8_t eip                 : 1;
    uint8_t reserved            : 2;
    uint8_t invalid             : 1;
#else
    uint8_t invalid             : 1;
    uint8_t reserved            : 2;
    uint8_t eip                 : 1;
    uint8_t protocol_id         : 4;
#endif
    uint8_t len;
#if __BYTE_ORDER__ == __ORDER_LITTLE_ENDIAN__
    uint8_t eiioe               : 1;
    uint8_t reserved_2          : 7;
#else
    uint8_t reserved_2          : 7;
    uint8_t eiioe               : 1;
#endif
    uint8_t element_index;
    uint8_t data_begin;
};

/*
 * SES-3 rev 11a Table 39 - Additional Element Status descriptor
 * protocol-specific information for Device Slot elements and Array Device Slot
 * elements for SAS with the EIP bit set to one
 */
struct _ses_add_st_dp_sas {
    uint8_t phy_count;
#if __BYTE_ORDER__ == __ORDER_LITTLE_ENDIAN__
    uint8_t not_all_phy         : 1;
    uint8_t reserved            : 5;
    uint8_t dp_type             : 2;
#else
    uint8_t dp_type             : 2;
    uint8_t reserved            : 5;
    uint8_t not_all_phy         : 1;
#endif
    uint8_t reserved_2;
    uint8_t dev_slot_num;
    uint8_t phy_list;
};

/*
 * SES-3 rev 11a Table 41 - Phy descriptor
 */
struct _ses_add_st_sas_phy {
#if __BYTE_ORDER__ == __ORDER_LITTLE_ENDIAN__
    uint8_t reserved            : 4;
    uint8_t dev_type            : 3;
    uint8_t reserved_2          : 1;
#else
    uint8_t reserved_2          : 1;
    uint8_t dev_type            : 3;
    uint8_t reserved            : 4;
#endif
    uint8_t reserved_3;
    uint8_t we_dont_care_2;
    uint8_t we_dont_care_3;
    uint8_t attached_sas_addr[8];
    uint8_t sas_addr[8];
    uint8_t phy_id;
    uint8_t reserved_4[7];
};

/*
 * SES-3 rev 11a Table 80 - Array Device Slot control element
 */
struct _ses_ar_dev_ctrl {
#if __BYTE_ORDER__ == __ORDER_LITTLE_ENDIAN__
    uint8_t we_dont_care_0      : 7;
    uint8_t select              : 1;
    uint8_t we_dont_care_1;
    uint8_t we_dont_care_2      : 1;
    uint8_t rqst_ident          : 1;
    uint8_t we_dont_care_3      : 6;
    uint8_t we_dont_care_4      : 5;
    uint8_t rqst_fault          : 1;
    uint8_t we_dont_care_5      : 2;
#else
    uint8_t select              : 1;
    uint8_t we_dont_care_0      : 7;

    uint8_t we_dont_care_1;

    uint8_t we_dont_care_3      : 6;
    uint8_t rqst_ident          : 1;
    uint8_t we_dont_care_2      : 1;

    uint8_t we_dont_care_5      : 2;
    uint8_t rqst_fault          : 1;
    uint8_t we_dont_care_4      : 5;
#endif
};

struct _ses_ctrl_diag_hdr {
    uint8_t page_code;
    uint8_t we_dont_care_0;
    uint16_t len_be;
    uint32_t gen_code_be;
    uint8_t ctrl_dp_list_begin;
};

struct _ses_st_hdr {
    uint8_t page_code;
    uint8_t we_dont_care_0;
    uint16_t len_be;
    uint32_t gen_code_be;
    uint8_t st_dp_list;
};

struct _ses_cfg_hdr {
    uint8_t page_code;
    uint8_t num_of_sec_enc;
    uint16_t len_be;
    uint32_t gen_code_be;
    uint8_t enc_dp_list;
};

struct _ses_cfg_enc_dp {
    uint8_t we_dont_care_0[2];
    uint8_t num_of_dp_hdr;
    uint8_t len;
    uint8_t we_dont_care_1[35];
    uint8_t vendor_info_begin;
};

struct _ses_cfg_dp_hdr {
    uint8_t element_type;
    uint8_t num_of_possible_element;
    uint8_t sub_enc_id;
    uint8_t dp_text_len;
};

#pragma pack(pop)

#define _set_array_bit(array, bytes, bit) \
    do { \
        array[bytes] |= 1 << bit; \
    } while(0)

#define _clear_array_bit(array, bytes, bit) \
    do { \
        array[bytes] &= ~(1 << bit); \
    } while(0)

/*
 * Get all /dev/bsg/<htbl>  which support SES via sysfs.
 * Assuming given pointer is not NULL.
 */
static int _ses_bsg_paths_get(char *err_msg, char ***bsg_paths,
                              uint32_t *bsg_count);

/*
 * Return element index of given SAS address. The index is including overall
 * status item in status page(0x02).
 * 'add_st_data' should be 'uint8_t [_SG_T10_SPC_RECV_DIAG_MAX_LEN]'
 * 'cfg_data' should be 'uint8_t [_SG_T10_SPC_RECV_DIAG_MAX_LEN]'
 * The SES-3 ELEMENT INDEX field which is uint8_t, we expand it to
 * int16_t in order to include -1 as 'not found' error.
 */
static int16_t _ses_find_sas_addr(const char *sas_addr, uint8_t *add_st_data,
                                  uint8_t *cfg_data);

/*
 * 'status' should be 'uint8_t [_T10_SES_DEV_SLOT_STATUS_LEN]'.
 */
static int _ses_raw_status_get(char *err_msg, uint8_t *status_data,
                               const int16_t element_index, uint8_t *status,
                               uint32_t *gen_code_be);

static int _ses_ctrl_data_gen(char *err_msg, uint8_t *status_data,
                              uint8_t *status, const int16_t element_index,
                              uint16_t *len);

/*
 * When EIIOE is set to zero, we need to add the overall element count into
 * given element_index. Quote of SES-3 rev 11a:
 *  An EIIOE (element index includes overall elements) bit set to one indicates
 *  that the ELEMENT INDEX field in table 31 is based on the position in the
 *  status descriptor list of the Enclosure Status diagnostic page (see 6.1.4)
 *  including overall status elements (i.e., is the same as the CONNECTOR
 *  ELEMENT INDEX fields (see table 43 and table 45) and the OTHER ELEMENT INDEX
 *  fields (see table 43 and table 45)).  An EIIOE bit set to zero indicates
 *  that the ELEMENT INDEX field is based on the position in the status
 *  descriptor list of the Enclosure Status diagnostic page excluding overall
 *  status elements. The device server should set the EIIOE bit to one.
 *
 *  The EIIOE is introduced by SES-3.
 */
static int16_t _ses_eiioe(uint8_t *cfg_data, int16_t element_index);


/*
 * Get the total count of 'Type descriptor header' and
 * the beginning pointer of 'Type descriptor header list' from config page.
 * If error, dp_hdr_begin will be NULL and total_dp_hdr_count will be 0.
 */
static void _ses_cfg_parse(uint8_t *cfg_data, uint8_t **dp_hdr_begin,
                           uint16_t *total_dp_hdr_count);

static int _ses_info_get_by_sas_addr(char *err_msg, const char *tp_sas_addr,
                                     uint8_t *cfg_data, uint8_t *status_data,
                                     uint8_t *add_st_data, int *fd,
                                     int16_t *element_index);

static void _ses_cfg_parse(uint8_t *cfg_data, uint8_t **dp_hdr_begin,
                           uint16_t *total_dp_hdr_count)
{
    struct _ses_cfg_hdr *cfg_hdr = NULL;
    struct _ses_cfg_enc_dp *enc_dp = NULL;
    uint8_t *end_p = NULL;
    uint8_t *tmp_p = NULL;
    uint8_t i = 0;

    assert(cfg_data != NULL);
    assert(dp_hdr_begin != NULL);
    assert(total_dp_hdr_count != NULL);

    *total_dp_hdr_count = 0;
    *dp_hdr_begin = NULL;

    cfg_hdr = (struct _ses_cfg_hdr *)cfg_data;
    end_p = cfg_data + be16toh(cfg_hdr->len_be) + 4;
    if (end_p >= cfg_data + _SG_T10_SPC_RECV_DIAG_MAX_LEN)
        /* Facing data boundary */
        return;

    tmp_p = &cfg_hdr->enc_dp_list;
    /* Check the "Enclosure descriptor list" section */
    for (; i <= cfg_hdr->num_of_sec_enc; ++i) {
        enc_dp = (struct _ses_cfg_enc_dp *) tmp_p;
        *total_dp_hdr_count += enc_dp->num_of_dp_hdr;
        tmp_p += enc_dp->len + 4;
        /* ENCLOSURE DESCRIPTOR LENGTH (m - 3) */
    }
    *dp_hdr_begin = tmp_p;
    return;
}

static int _ses_bsg_paths_get(char *err_msg, char ***bsg_paths,
                              uint32_t *bsg_count)
{
    int rc = LSM_ERR_OK;
    uint32_t i = 0;
    DIR *dir = NULL;
    struct dirent *dp = NULL;
    lsm_string_list *bsg_name_list = NULL;
    const char *bsg_name = NULL;
    char *sysfs_bsg_type_path = NULL;
    char dev_type[_LINUX_SCSI_DEV_TYPE_SES_LEN + 1];
    ssize_t dev_type_size = 0;
    char strerr_buff[_LSM_ERR_MSG_LEN];
    int tmp_rc = 0;

    assert(err_msg != NULL);
    assert(bsg_paths != NULL);
    assert(bsg_count != NULL);

    *bsg_paths = NULL;
    *bsg_count = 0;

    bsg_name_list = lsm_string_list_alloc(0 /* no pre-allocation */);
    _alloc_null_check(err_msg, bsg_name_list, rc, out);

    /* We don't use libudev here because libudev seems have no way to check
     * whether 'bsg' kernel module is loaded or not.
     */
    if (! _file_exists(_SYSFS_BSG_ROOT_PATH)) {
        rc = LSM_ERR_INVALID_ARGUMENT;
        _lsm_err_msg_set(err_msg, "Required kernel module 'bsg' not loaded");
        goto out;
    }

    dir = opendir(_SYSFS_BSG_ROOT_PATH);
    if (dir == NULL) {
        _lsm_err_msg_set(err_msg, "Cannot open %s: error (%d)%s",
                         _SYSFS_BSG_ROOT_PATH, errno,
                         strerror_r(errno, strerr_buff, _LSM_ERR_MSG_LEN));

        rc = LSM_ERR_LIB_BUG;
        goto out;
    }

    do {
        if ((dp = readdir(dir)) != NULL) {
            bsg_name = dp->d_name;
            if ((bsg_name == NULL) || (strlen(bsg_name) == 0))
                continue;
            sysfs_bsg_type_path = (char *)
                malloc(sizeof(char) * (strlen(_SYSFS_BSG_ROOT_PATH) +
                                       strlen("/") +
                                       strlen(bsg_name) +
                                       strlen("/device/type") +
                                       1 /* trailing \0 */));
            _alloc_null_check(err_msg, sysfs_bsg_type_path, rc, out);
            sprintf(sysfs_bsg_type_path, "%s/%s/device/type",
                    _SYSFS_BSG_ROOT_PATH, bsg_name);
            tmp_rc = _read_file(sysfs_bsg_type_path, (uint8_t *) dev_type,
                                &dev_type_size,
                                _LINUX_SCSI_DEV_TYPE_SES_LEN + 1);
            if ((tmp_rc != 0) && (tmp_rc != EFBIG)) {
                free(sysfs_bsg_type_path);
                continue;
            }
            if (strncmp(dev_type, _LINUX_SCSI_DEV_TYPE_SES,
                        _LINUX_SCSI_DEV_TYPE_SES_LEN) == 0) {
                if (lsm_string_list_append(bsg_name_list, bsg_name) != 0) {
                    free(sysfs_bsg_type_path);
                    _lsm_err_msg_set(err_msg, "No memory");
                    rc = LSM_ERR_NO_MEMORY;
                    goto out;
                }
            }
            free(sysfs_bsg_type_path);
        }
    } while(dp != NULL);

    *bsg_count = lsm_string_list_size(bsg_name_list);
    *bsg_paths = (char **) malloc(sizeof(char *) * (*bsg_count));
    _alloc_null_check(err_msg, bsg_name_list, rc, out);

    /* Initialize *bsg_paths */
    for (i = 0; i < *bsg_count; ++i)
        (*bsg_paths)[i] = NULL;

    _lsm_string_list_foreach(bsg_name_list, i, bsg_name) {
        (*bsg_paths)[i] = (char *)
            malloc(sizeof(char) * (strlen(bsg_name) + strlen("/dev/bsg/") +
                                   1 /* trailing \0 */));
        if ((*bsg_paths)[i] == NULL) {
            rc = LSM_ERR_NO_MEMORY;
            goto out;
        }
        sprintf((*bsg_paths)[i], "/dev/bsg/%s", bsg_name);
    }

 out:
    if (dir != NULL)
        closedir(dir);

    if (bsg_name_list != NULL)
        lsm_string_list_free(bsg_name_list);
    if (rc != LSM_ERR_OK) {
        if (*bsg_paths != NULL) {
            for (i = 0; i < *bsg_count; ++i)
                free((char *) (*bsg_paths)[i]);
            free(*bsg_paths);
        }
        *bsg_paths = NULL;
        *bsg_count = 0;
    }
    return rc;
}

static int16_t _ses_find_sas_addr(const char *sas_addr, uint8_t *add_st_data,
                                  uint8_t *cfg_data)
{
    struct _ses_add_st *add_st = NULL;
    struct _ses_add_st_dp *dp = NULL;
    struct _ses_add_st_dp_sas *dp_sas = NULL;
    struct _ses_add_st_sas_phy *phy = NULL;
    uint8_t *end_p = NULL;
    uint8_t *tmp_p = NULL;
    int16_t element_index = -1;
    uint8_t i = 0;
    char tmp_sas_addr[_SG_T10_SPL_SAS_ADDR_LEN];

    assert(sas_addr != NULL);
    assert(add_st_data != NULL);
    assert(cfg_data != NULL);

    add_st = (struct _ses_add_st *) add_st_data;
    end_p = add_st_data + be16toh(add_st->len_be) + 4;

    tmp_p = &add_st->dp_list_begin;
    while(tmp_p < end_p) {
        if (tmp_p + sizeof(struct _ses_add_st_dp) > end_p)
            goto out;
        dp = (struct _ses_add_st_dp *) tmp_p;
        tmp_p += dp->len + 2;

        /* Both SES-2 and SES-3 said 'The EIP bit should be set to one.'
         * The 'should' means 'is strongly recommended'.
         * When EIP == 0, the SES standard is in blur state for count element
         * index.
         * Hence we silently ignore the descriptor with EIP=0.
         */
        if ((dp->protocol_id != _SG_T10_SPC_PROTOCOL_ID_SAS) ||
            (dp->invalid == 1) ||
            (dp->eip == 0))
            continue;

        if (&dp->data_begin + sizeof(struct _ses_add_st_dp_sas) > end_p)
            goto out;
        dp_sas = (struct _ses_add_st_dp_sas *) &dp->data_begin;
        if (dp_sas->dp_type != _T10_SES_DESCRIPTOR_TYPE_DEV_SLOT)
            continue;
        if (dp_sas->phy_count == 0)
            continue;
        if (&dp_sas->phy_list + sizeof(struct _ses_add_st_sas_phy) > end_p)
            goto out;
        for (i = 0; i < dp_sas->phy_count; ++i) {
            phy = (struct _ses_add_st_sas_phy *)
                ((uint8_t *) (&dp_sas->phy_list) +
                 sizeof(struct _ses_add_st_sas_phy) * i);
            _be_raw_to_hex((uint8_t *) &phy->sas_addr,
                           _SG_T10_SPL_SAS_ADDR_LEN_BITS, tmp_sas_addr);
            if (strncmp(tmp_sas_addr, sas_addr,
                        _SG_T10_SPL_SAS_ADDR_LEN) == 0) {
                if (dp->eiioe == _T10_SES_ADD_DP_INCLUDE_OVERALL)
                    return dp->element_index;
                else
                    return _ses_eiioe(cfg_data, dp->element_index);
            }
        }
    }

 out:
    return element_index;
}

static int _ses_raw_status_get(char *err_msg, uint8_t *status_data,
                               const int16_t element_index, uint8_t *status,
                               uint32_t *gen_code_be)
{
    int rc = LSM_ERR_OK;
    struct _ses_st_hdr *st_hdr = NULL;
    uint8_t *end_p = NULL;
    uint8_t *status_p = NULL;

    assert(err_msg != NULL);
    assert(status_data != NULL);
    assert(status != NULL);
    assert(gen_code_be != NULL);

    st_hdr = (struct _ses_st_hdr *) status_data;
    end_p = status_data + be16toh(st_hdr->len_be) + 4;
    status_p = &st_hdr->st_dp_list +
        element_index * _T10_SES_DEV_SLOT_STATUS_LEN;

    if ((end_p >= status_data + _SG_T10_SPC_RECV_DIAG_MAX_LEN) ||
        (status_p >= end_p) ||
        (status_p + _T10_SES_DEV_SLOT_STATUS_LEN > end_p)) {
        rc = LSM_ERR_LIB_BUG;
        _lsm_err_msg_set(err_msg, "BUG: Got corrupted SES status page: "
                         "facing data boundary");
        goto out;
    }

    memcpy(status, status_p, _T10_SES_DEV_SLOT_STATUS_LEN);

    *gen_code_be = st_hdr->gen_code_be;

 out:
    if (rc != LSM_ERR_OK) {
        memset(status, 0, _T10_SES_DEV_SLOT_STATUS_LEN);
        *gen_code_be = 0;
    }

    return rc;
}

static int _ses_ctrl_data_gen(char *err_msg, uint8_t *status_data,
                              uint8_t *status, const int16_t element_index,
                              uint16_t *len)
{
    struct _ses_ctrl_diag_hdr *ctrl_hdr = NULL;
    uint8_t *tmp_p = NULL;
    uint8_t *end_p = NULL;

    assert(err_msg != NULL);
    assert(status_data != NULL);
    assert(status != NULL);
    assert(len != NULL);

    ctrl_hdr = (struct _ses_ctrl_diag_hdr *) (status_data);

    *len = be16toh(ctrl_hdr->len_be) + 4;

    /* set all element as not selected */
    tmp_p = &ctrl_hdr->ctrl_dp_list_begin;
    end_p = tmp_p + be16toh(ctrl_hdr->len_be);

    while(tmp_p < end_p) {
        _clear_array_bit(tmp_p, _T10_SES_CTRL_SELECT_BYTES,
                         _T10_SES_CTRL_SELECT_BIT);
        tmp_p += _T10_SES_DEV_SLOT_STATUS_LEN;
    }

    /* update the selected element */

    tmp_p = &ctrl_hdr->ctrl_dp_list_begin + \
            _T10_SES_DEV_SLOT_STATUS_LEN * element_index;

    memcpy(tmp_p, status, _T10_SES_DEV_SLOT_STATUS_LEN);

    return LSM_ERR_OK;
}

/*
 * Workflow:
 *  Parse config page(0x01)
 *      Loop enclosure descriptor list
 *          Loop descriptor header list
 *              Store 'NUMBER OF POSSIBLE ELEMENTS' of descriptor header in
 *              an array. The 0 indicates this element type only have overall
 *              element.
 */
static int16_t _ses_eiioe(uint8_t *cfg_data, int16_t element_index)
{
    struct _ses_cfg_hdr *cfg_hdr = NULL;
    struct _ses_cfg_dp_hdr *dp_hdr = NULL;
    uint8_t *end_p = NULL;
    uint8_t i = 0;
    uint16_t total_dp_hdr_count = 0;
    uint8_t add = 0;
    uint8_t *dp_hdr_begin = NULL;

    assert(cfg_data != NULL);

    cfg_hdr = (struct _ses_cfg_hdr *)cfg_data;
    end_p = cfg_data + be16toh(cfg_hdr->len_be) + 4;
    if (end_p >= cfg_data + _SG_T10_SPC_RECV_DIAG_MAX_LEN)
        /* Facing data boundary */
        return -1;

    _ses_cfg_parse(cfg_data, &dp_hdr_begin, &total_dp_hdr_count);
    if ((dp_hdr_begin == NULL) || (total_dp_hdr_count == 0))
        return -1;

    for (i = 0; i < total_dp_hdr_count; ++i) {
        dp_hdr = (struct _ses_cfg_dp_hdr *) dp_hdr_begin +
            (_T10_SES_CFG_DP_HDR_LEN * i);
        if ((uint8_t *) &dp_hdr >= end_p)
            /* Facing data boundary */
            return -1;
        add++;
        if (element_index <= dp_hdr->num_of_possible_element)
            break;
        element_index -= dp_hdr->num_of_possible_element;
    }

    return element_index + add;
}

/*
 * Workflow:
 *  1. Find all scsi generic paths that correspond to enclosures.
 *  2. Find which scsi generic path connects to the given SAS address via the
 *     following SES page:
 *      6.1.13 Additional Element Status diagnostic page
 *  3. Record the element index of the port that the given SAS address is
 *     connecting to.
 *  4. Retrieve status of above element index.
 *  5. Update status data of these SES pages with ctrl_value:
 *      6.1.3 Enclosure Control diagnostic page
 *      7.2.2 Control element format
 *      7.3.2 Device Slot element
 *  4. Invoke SEND DIAGNOSTICS command.
 */
int _ses_dev_slot_ctrl(char *err_msg, const char *tp_sas_addr,
                       int ctrl_value, int ctrl_type)
{
    int rc = LSM_ERR_OK;
    int fd = -1;
    uint8_t cfg_data[_SG_T10_SPC_RECV_DIAG_MAX_LEN];
    uint8_t status_data[_SG_T10_SPC_RECV_DIAG_MAX_LEN];
    uint8_t add_st_data[_SG_T10_SPC_RECV_DIAG_MAX_LEN];
    uint8_t status[_T10_SES_DEV_SLOT_STATUS_LEN];
    int16_t element_index = -1;
    uint8_t ctrl_bytes = 0;
    uint8_t ctrl_bit = 0;
    uint32_t gen_code_be;
    uint16_t ctrl_data_len = 0;

    _good(_ses_info_get_by_sas_addr(err_msg, tp_sas_addr, cfg_data, status_data,
                                    add_st_data, &fd, &element_index),
          rc, out);

    _good(_ses_raw_status_get(err_msg, status_data, element_index, status,
                              &gen_code_be),
          rc, out);

    /* Only keep the PRDFAIL bit */
    status[_T10_SES_CTRL_PRDFAIL_BYTES] &= 1 << _T10_SES_CTRL_PRDFAIL_BIT;

    /* Set the SELECT bit */
    _set_array_bit(status, _T10_SES_CTRL_SELECT_BYTES,
                   _T10_SES_CTRL_SELECT_BIT);

    if (ctrl_value == _SES_DEV_CTRL_RQST_IDENT) {
        ctrl_bytes = _T10_SES_CTRL_RQST_IDENT_BYTES;
        ctrl_bit = _T10_SES_CTRL_RQST_IDENT_BIT;
    } else if (ctrl_value == _SES_DEV_CTRL_RQST_FAULT) {
        ctrl_bytes = _T10_SES_CTRL_RQST_FAULT_BYTES;
        ctrl_bit = _T10_SES_CTRL_RQST_FAULT_BIT;
    } else {
        rc = LSM_ERR_LIB_BUG;
        _lsm_err_msg_set(err_msg, "Got invalid ctrl_value %d", ctrl_value);
        goto out;
    }

    if (ctrl_type == _SES_CTRL_SET)
        _set_array_bit(status, ctrl_bytes, ctrl_bit);
    else if (ctrl_type == _SES_CTRL_CLEAR)
        _clear_array_bit(status, ctrl_bytes, ctrl_bit);
    else {
        rc = LSM_ERR_LIB_BUG;
        _lsm_err_msg_set(err_msg, "Got invalid ctrl_type %d", ctrl_type);
        goto out;
    }


    _good(_ses_ctrl_data_gen(err_msg, status_data, status, element_index,
                             &ctrl_data_len),
          rc, out);

    /* TODO(Gris Ge): If gen_code_be not match, the SEND DIAGNOSTIC will fail,
     *                in that case, we should refresh status and retry.
     */
    _good(_sg_io_send_diag(err_msg, fd, status_data, ctrl_data_len), rc, out);

    /*
     * Verify whether certain action is supported
     */
    _good(_sg_io_recv_diag(err_msg, fd, _T10_SES_STATUS_PG_CODE,
                           status_data), rc, out);

    _good(_ses_raw_status_get(err_msg, status_data, element_index, status,
                              &gen_code_be),
          rc, out);

    if (((ctrl_type == _SES_CTRL_CLEAR) &&
         (status[ctrl_bytes] & (1 << ctrl_bit))) ||
        ((ctrl_type == _SES_CTRL_SET) &&
         !((status[ctrl_bytes] & (1 << ctrl_bit))))) {
            /* Control bit is still set */
            rc = LSM_ERR_NO_SUPPORT;
            _lsm_err_msg_set(err_msg, "Requested SES action is not supported "
                             "by vendor enclosure vendor or/and kernel driver");
    }

 out:
    if (fd >= 0)
        close(fd);
    return rc;
}

static int _ses_info_get_by_sas_addr(char *err_msg, const char *tp_sas_addr,
                                      uint8_t *cfg_data, uint8_t *status_data,
                                      uint8_t *add_st_data, int *fd,
                                      int16_t *element_index)
{
    int rc = LSM_ERR_OK;
    char  **bsg_paths = NULL;
    uint32_t bsg_count = 0;
    uint32_t i = 0;
    bool found = false;

    assert(tp_sas_addr != NULL);
    assert(cfg_data != NULL);
    assert(status_data != NULL);
    assert(add_st_data != NULL);
    assert(element_index != NULL);
    assert(fd != NULL);

    _good(_ses_bsg_paths_get(err_msg, &bsg_paths, &bsg_count), rc, out);

    for (i = 0; i < bsg_count; ++i) {
        _good(_sg_io_open_rw(err_msg, bsg_paths[i], fd), rc, out);
        _good(_sg_io_recv_diag(err_msg, *fd, _T10_SES_CFG_PG_CODE, cfg_data),
              rc, out);
        _good(_sg_io_recv_diag(err_msg, *fd, _T10_SES_STATUS_PG_CODE,
                               status_data), rc, out);
        _good(_sg_io_recv_diag(err_msg, *fd, _T10_SES_ADD_STATUS_PG_CODE,
                               add_st_data),
              rc, out);
        /* TODO(Gris Ge): We need to check "GENERATION CODE" of above four
         *                pages are identical, or we need retry.
         */

        *element_index = _ses_find_sas_addr(tp_sas_addr, add_st_data, cfg_data);
        if (*element_index != -1) {
            found = true;
            break;
        }

        if (*fd >= 0)
            close(*fd);
        *fd = -1;
    }
    if (found != true) {
        rc = LSM_ERR_NO_SUPPORT;
        _lsm_err_msg_set(err_msg, "Failed to find any SCSI enclosure with "
                         "given SAS address %s", tp_sas_addr);
        goto out;
    }

 out:
    if (bsg_paths != NULL) {
        for (i = 0; i < bsg_count; ++i) {
            free(bsg_paths[i]);
        }
        free(bsg_paths);
    }
    if (rc != LSM_ERR_OK) {
        *element_index = -1;
        if (*fd >= 0)
            close(*fd);
        *fd = -1;
    }
    return rc;
}

int _ses_status_get(char *err_msg, const char *tp_sas_addr,
                    struct _ses_dev_slot_status *status)
{
    int rc = LSM_ERR_OK;
    int fd = -1;
    uint8_t cfg_data[_SG_T10_SPC_RECV_DIAG_MAX_LEN];
    uint8_t status_data[_SG_T10_SPC_RECV_DIAG_MAX_LEN];
    uint8_t add_st_data[_SG_T10_SPC_RECV_DIAG_MAX_LEN];
    uint8_t raw_status[_T10_SES_DEV_SLOT_STATUS_LEN];
    int16_t element_index = -1;
    uint32_t gen_code_be = 0;

    assert(tp_sas_addr != NULL);
    assert(status != NULL);

    _good(_ses_info_get_by_sas_addr(err_msg, tp_sas_addr, cfg_data, status_data,
                                    add_st_data, &fd, &element_index),
          rc, out);

    _good(_ses_raw_status_get(err_msg, status_data, element_index, raw_status,
                              &gen_code_be),
          rc, out);

    memcpy(status, raw_status, _T10_SES_DEV_SLOT_STATUS_LEN);

 out:
    if (fd >= 0)
        close(fd);
    return rc;
}
