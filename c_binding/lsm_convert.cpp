/*
 * Copyright (C) 2011-2016 Red Hat, Inc.
 * (C) Copyright 2015-2016 Hewlett Packard Enterprise Development LP
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
 * Author: tasleson
 *         Joe Handzik <joseph.t.handzik@hpe.com>
 *         Gris Ge <fge@redhat.com>
 */

#include "lsm_convert.hpp"
#include "libstoragemgmt/libstoragemgmt_accessgroups.h"
#include "libstoragemgmt/libstoragemgmt_battery.h"
#include "libstoragemgmt/libstoragemgmt_blockrange.h"
#include "libstoragemgmt/libstoragemgmt_nfsexport.h"
#include "libstoragemgmt/libstoragemgmt_plug_interface.h"

bool std_map_has_key(const std::map<std::string, Value> &x, const char *key) {
    return x.find(key) != x.end();
}

bool is_expected_object(Value &obj, std::string class_name) {
    if (obj.valueType() == Value::object_t) {
        std::map<std::string, Value> i = obj.asObject();
        std::map<std::string, Value>::iterator iter = i.find("class");
        if (iter != i.end() && iter->second.asString() == class_name) {
            return true;
        }
    }
    return false;
}

lsm_volume *value_to_volume(Value &vol) {
    lsm_volume *rc = NULL;

    if (is_expected_object(vol, CLASS_NAME_VOLUME)) {
        std::map<std::string, Value> v = vol.asObject();

        rc = lsm_volume_record_alloc(
            v["id"].asString().c_str(), v["name"].asString().c_str(),
            v["vpd83"].asString().c_str(), v["block_size"].asUint64_t(),
            v["num_of_blocks"].asUint64_t(), v["admin_state"].asUint32_t(),
            v["system_id"].asString().c_str(), v["pool_id"].asString().c_str(),
            v["plugin_data"].asC_str());
    } else {
        throw ValueException("value_to_volume: Not correct type");
    }

    return rc;
}

Value volume_to_value(lsm_volume *vol) {
    if (LSM_IS_VOL(vol)) {
        std::map<std::string, Value> v;
        v["class"] = Value(CLASS_NAME_VOLUME);
        v["id"] = Value(vol->id);
        v["name"] = Value(vol->name);
        v["vpd83"] = Value(vol->vpd83);
        v["block_size"] = Value(vol->block_size);
        v["num_of_blocks"] = Value(vol->number_of_blocks);
        v["admin_state"] = Value(vol->admin_state);
        v["system_id"] = Value(vol->system_id);
        v["pool_id"] = Value(vol->pool_id);
        v["plugin_data"] = Value(vol->plugin_data);
        return Value(v);
    }
    return Value();
}

int value_array_to_volumes(Value &volume_values, lsm_volume **volumes[],
                           uint32_t *count) {
    int rc = LSM_ERR_OK;
    try {
        *count = 0;

        if (Value::array_t == volume_values.valueType()) {
            std::vector<Value> vol = volume_values.asArray();

            *count = vol.size();

            if (vol.size()) {
                *volumes = lsm_volume_record_array_alloc(vol.size());

                if (*volumes) {
                    for (size_t i = 0; i < vol.size(); ++i) {
                        (*volumes)[i] = value_to_volume(vol[i]);
                        if (!((*volumes)[i])) {
                            rc = LSM_ERR_NO_MEMORY;
                            goto error;
                        }
                    }
                } else {
                    rc = LSM_ERR_NO_MEMORY;
                }
            }
        }
    } catch (const ValueException &ve) {
        rc = LSM_ERR_LIB_BUG;
        goto error;
    }

out:
    return rc;

error:
    if (*volumes && *count) {
        lsm_volume_record_array_free(*volumes, *count);
        *volumes = NULL;
        *count = 0;
    }
    goto out;
}

lsm_disk *value_to_disk(Value &disk) {
    lsm_disk *rc = NULL;
    if (is_expected_object(disk, CLASS_NAME_DISK)) {
        std::map<std::string, Value> d = disk.asObject();

        rc = lsm_disk_record_alloc(
            d["id"].asString().c_str(), d["name"].asString().c_str(),
            (lsm_disk_type)d["disk_type"].asInt32_t(),
            d["block_size"].asUint64_t(), d["num_of_blocks"].asUint64_t(),
            d["status"].asUint64_t(), d["system_id"].asString().c_str());
        if ((rc != NULL) && std_map_has_key(d, "vpd83") &&
            (d["vpd83"].asC_str()[0] != '\0') &&
            (lsm_disk_vpd83_set(rc, d["vpd83"].asC_str()) != LSM_ERR_OK)) {

            lsm_disk_record_free(rc);
            rc = NULL;
            throw ValueException("value_to_disk: failed to update 'vpd83'");
        }

        if ((rc != NULL) && std_map_has_key(d, "location") &&
            (d["location"].asC_str()[0] != '\0')) {

            if (lsm_disk_location_set(rc, d["location"].asC_str()) !=
                LSM_ERR_OK) {
                lsm_disk_record_free(rc);
                rc = NULL;
                throw ValueException("value_to_disk: failed to update "
                                     "location");
            }
        }
        if ((rc != NULL) && std_map_has_key(d, "rpm") &&
            (d["rpm"].asInt32_t() != LSM_DISK_RPM_NO_SUPPORT)) {
            if (lsm_disk_rpm_set(rc, d["rpm"].asInt32_t()) != LSM_ERR_OK) {
                lsm_disk_record_free(rc);
                rc = NULL;
                throw ValueException("value_to_disk: failed to update rpm");
            }
        }
        if ((rc != NULL) && std_map_has_key(d, "link_type") &&
            (d["link_type"].asInt32_t() != LSM_DISK_LINK_TYPE_NO_SUPPORT)) {
            if (lsm_disk_link_type_set(
                    rc, (lsm_disk_link_type)d["link_type"].asInt32_t()) !=
                LSM_ERR_OK) {
                lsm_disk_record_free(rc);
                rc = NULL;
                throw ValueException("value_to_disk: failed to update "
                                     "link_type");
            }
        }
    } else {
        throw ValueException("value_to_disk: Not correct type");
    }
    return rc;
}

Value disk_to_value(lsm_disk *disk) {
    if (LSM_IS_DISK(disk)) {
        std::map<std::string, Value> d;
        d["class"] = Value(CLASS_NAME_DISK);
        d["id"] = Value(disk->id);
        d["name"] = Value(disk->name);
        d["disk_type"] = Value(disk->type);
        d["block_size"] = Value(disk->block_size);
        d["num_of_blocks"] = Value(disk->number_of_blocks);
        d["status"] = Value(disk->status);
        d["system_id"] = Value(disk->system_id);
        if (disk->location != NULL)
            d["location"] = Value(disk->location);
        if (disk->rpm != LSM_DISK_RPM_NO_SUPPORT)
            d["rpm"] = Value(disk->rpm);
        if (disk->link_type != LSM_DISK_LINK_TYPE_NO_SUPPORT)
            d["link_type"] = Value(disk->link_type);
        if (disk->vpd83 != NULL)
            d["vpd83"] = Value(disk->vpd83);

        return Value(d);
    }
    return Value();
}

int value_array_to_disks(Value &disk_values, lsm_disk **disks[],
                         uint32_t *count) {
    int rc = LSM_ERR_OK;
    try {
        *count = 0;

        if (Value::array_t == disk_values.valueType()) {
            std::vector<Value> d = disk_values.asArray();

            *count = d.size();

            if (d.size()) {
                *disks = lsm_disk_record_array_alloc(d.size());

                if (*disks) {
                    for (size_t i = 0; i < d.size(); ++i) {
                        (*disks)[i] = value_to_disk(d[i]);
                        if (!((*disks)[i])) {
                            rc = LSM_ERR_NO_MEMORY;
                            goto error;
                        }
                    }
                } else {
                    rc = LSM_ERR_NO_MEMORY;
                }
            }
        }
    } catch (const ValueException &ve) {
        rc = LSM_ERR_LIB_BUG;
        goto error;
    }

out:
    return rc;

error:
    if (*disks && *count) {
        lsm_disk_record_array_free(*disks, *count);
        *disks = NULL;
        *count = 0;
    }
    goto out;
}

lsm_pool *value_to_pool(Value &pool) {
    lsm_pool *rc = NULL;

    if (is_expected_object(pool, CLASS_NAME_POOL)) {
        std::map<std::string, Value> i = pool.asObject();

        rc = lsm_pool_record_alloc(
            i["id"].asString().c_str(), i["name"].asString().c_str(),
            i["element_type"].asUint64_t(),
            i["unsupported_actions"].asUint64_t(),
            i["total_space"].asUint64_t(), i["free_space"].asUint64_t(),
            i["status"].asUint64_t(), i["status_info"].asString().c_str(),
            i["system_id"].asString().c_str(), i["plugin_data"].asC_str());
    } else {
        throw ValueException("value_to_pool: Not correct type");
    }
    return rc;
}

Value pool_to_value(lsm_pool *pool) {
    if (LSM_IS_POOL(pool)) {
        std::map<std::string, Value> p;
        p["class"] = Value(CLASS_NAME_POOL);
        p["id"] = Value(pool->id);
        p["name"] = Value(pool->name);
        p["element_type"] = Value(pool->element_type);
        p["unsupported_actions"] = Value(pool->unsupported_actions);
        p["total_space"] = Value(pool->total_space);
        p["free_space"] = Value(pool->free_space);
        p["status"] = Value(pool->status);
        p["status_info"] = Value(pool->status_info);
        p["system_id"] = Value(pool->system_id);
        p["plugin_data"] = Value(pool->plugin_data);
        return Value(p);
    }
    return Value();
}

lsm_system *value_to_system(Value &system) {
    lsm_system *rc = NULL;
    if (is_expected_object(system, CLASS_NAME_SYSTEM)) {
        std::map<std::string, Value> i = system.asObject();

        rc = lsm_system_record_alloc(
            i["id"].asString().c_str(), i["name"].asString().c_str(),
            i["status"].asUint32_t(), i["status_info"].asString().c_str(),
            i["plugin_data"].asC_str());
        if ((rc != NULL) && std_map_has_key(i, "fw_version") &&
            (i["fw_version"].asC_str()[0] != '\0')) {

            if (lsm_system_fw_version_set(rc, i["fw_version"].asC_str()) !=
                LSM_ERR_OK) {
                lsm_system_record_free(rc);
                rc = NULL;
                throw ValueException("value_to_system: failed to update "
                                     "fw_version");
            }
        }
        if ((rc != NULL) && std_map_has_key(i, "mode") &&
            (i["mode"].asInt32_t() != LSM_SYSTEM_MODE_NO_SUPPORT) &&
            (lsm_system_mode_set(
                rc, (lsm_system_mode_type)i["mode"].asInt32_t()))) {

            lsm_system_record_free(rc);
            rc = NULL;
            throw ValueException("value_to_system: failed to update 'mode'");
        }
        if ((rc != NULL) && std_map_has_key(i, "read_cache_pct") &&
            (i["read_cache_pct"].asInt32_t() !=
             LSM_SYSTEM_READ_CACHE_PCT_NO_SUPPORT)) {

            if (lsm_system_read_cache_pct_set(
                    rc, i["read_cache_pct"].asInt32_t()) != LSM_ERR_OK) {
                lsm_system_record_free(rc);
                rc = NULL;
                throw ValueException("value_to_system: failed to update "
                                     "read_cache_pct");
            }
        }
    } else {
        throw ValueException("value_to_system: Not correct type");
    }
    return rc;
}

Value system_to_value(lsm_system *system) {
    if (LSM_IS_SYSTEM(system)) {
        std::map<std::string, Value> s;
        s["class"] = Value(CLASS_NAME_SYSTEM);
        s["id"] = Value(system->id);
        s["name"] = Value(system->name);
        s["status"] = Value(system->status);
        s["status_info"] = Value(system->status_info);
        s["plugin_data"] = Value(system->plugin_data);
        if (system->fw_version != NULL)
            s["fw_version"] = Value(system->fw_version);
        if (system->mode != LSM_SYSTEM_MODE_NO_SUPPORT)
            s["mode"] = Value(system->mode);
        if (system->read_cache_pct != LSM_SYSTEM_READ_CACHE_PCT_NO_SUPPORT)
            s["read_cache_pct"] = Value(system->read_cache_pct);
        return Value(s);
    }
    return Value();
}

lsm_string_list *value_to_string_list(Value &v) {
    lsm_string_list *il = NULL;

    if (Value::array_t == v.valueType()) {
        std::vector<Value> vl = v.asArray();
        uint32_t size = vl.size();
        il = lsm_string_list_alloc(size);

        if (il) {
            for (uint32_t i = 0; i < size; ++i) {
                if (LSM_ERR_OK !=
                    lsm_string_list_elem_set(il, i, vl[i].asC_str())) {
                    lsm_string_list_free(il);
                    il = NULL;
                    break;
                }
            }
        }
    } else {
        throw ValueException("value_to_string_list: Not correct type");
    }
    return il;
}

Value string_list_to_value(lsm_string_list *sl) {
    std::vector<Value> rc;
    if (LSM_IS_STRING_LIST(sl)) {
        uint32_t size = lsm_string_list_size(sl);
        rc.reserve(size);

        for (uint32_t i = 0; i < size; ++i) {
            rc.push_back(Value(lsm_string_list_elem_get(sl, i)));
        }
    }
    return Value(rc);
}

lsm_access_group *value_to_access_group(Value &group) {
    lsm_string_list *il = NULL;
    lsm_access_group *ag = NULL;

    if (is_expected_object(group, CLASS_NAME_ACCESS_GROUP)) {
        std::map<std::string, Value> vAg = group.asObject();
        il = value_to_string_list(vAg["init_ids"]);

        if (il) {
            ag = lsm_access_group_record_alloc(
                vAg["id"].asString().c_str(), vAg["name"].asString().c_str(),
                il, (lsm_access_group_init_type)vAg["init_type"].asInt32_t(),
                vAg["system_id"].asString().c_str(),
                vAg["plugin_data"].asC_str());
        }
        /* This stuff is copied in lsm_access_group_record_alloc */
        lsm_string_list_free(il);
    } else {
        throw ValueException("value_to_access_group: Not correct type");
    }
    return ag;
}

Value access_group_to_value(lsm_access_group *group) {
    if (LSM_IS_ACCESS_GROUP(group)) {
        std::map<std::string, Value> ag;
        ag["class"] = Value(CLASS_NAME_ACCESS_GROUP);
        ag["id"] = Value(group->id);
        ag["name"] = Value(group->name);
        ag["init_ids"] = Value(string_list_to_value(group->initiators));
        ag["init_type"] = Value(group->init_type);
        ag["system_id"] = Value(group->system_id);
        ag["plugin_data"] = Value(group->plugin_data);
        return Value(ag);
    }
    return Value();
}

int value_array_to_access_groups(Value &group, lsm_access_group **ag_list[],
                                 uint32_t *count) {
    int rc = LSM_ERR_OK;

    try {
        std::vector<Value> ag = group.asArray();
        *count = ag.size();

        if (*count) {
            *ag_list = lsm_access_group_record_array_alloc(*count);
            if (*ag_list) {
                uint32_t i;
                for (i = 0; i < *count; ++i) {
                    (*ag_list)[i] = value_to_access_group(ag[i]);
                    if (!((*ag_list)[i])) {
                        rc = LSM_ERR_NO_MEMORY;
                        goto error;
                    }
                }
            } else {
                rc = LSM_ERR_NO_MEMORY;
            }
        }
    } catch (const ValueException &ve) {
        rc = LSM_ERR_LIB_BUG;
        goto error;
    }

out:
    return rc;

error:
    if (*ag_list && *count) {
        lsm_access_group_record_array_free(*ag_list, *count);
        *ag_list = NULL;
        *count = 0;
    }
    goto out;
}

Value access_group_list_to_value(lsm_access_group **group, uint32_t count) {
    std::vector<Value> rc;

    if (group && count) {
        uint32_t i;
        rc.reserve(count);
        for (i = 0; i < count; ++i) {
            rc.push_back(access_group_to_value(group[i]));
        }
    }
    return Value(rc);
}

lsm_block_range *value_to_block_range(Value &br) {
    lsm_block_range *rc = NULL;
    if (is_expected_object(br, CLASS_NAME_BLOCK_RANGE)) {
        std::map<std::string, Value> range = br.asObject();

        rc = lsm_block_range_record_alloc(range["src_block"].asUint64_t(),
                                          range["dest_block"].asUint64_t(),
                                          range["block_count"].asUint64_t());
    } else {
        throw ValueException("value_to_block_range: Not correct type");
    }
    return rc;
}

Value block_range_to_value(lsm_block_range *br) {
    if (LSM_IS_BLOCK_RANGE(br)) {
        std::map<std::string, Value> r;
        r["class"] = Value(CLASS_NAME_BLOCK_RANGE);
        r["src_block"] = Value(br->source_start);
        r["dest_block"] = Value(br->dest_start);
        r["block_count"] = Value(br->block_count);
        return Value(r);
    }
    return Value();
}

lsm_block_range **value_to_block_range_list(Value &brl, uint32_t *count) {
    lsm_block_range **rc = NULL;
    std::vector<Value> r = brl.asArray();
    *count = r.size();
    if (*count) {
        rc = lsm_block_range_record_array_alloc(*count);
        if (rc) {
            for (uint32_t i = 0; i < *count; ++i) {
                rc[i] = value_to_block_range(r[i]);
                if (!rc[i]) {
                    lsm_block_range_record_array_free(rc, i);
                    rc = NULL;
                    break;
                }
            }
        }
    }
    return rc;
}

Value block_range_list_to_value(lsm_block_range **brl, uint32_t count) {
    std::vector<Value> r;
    if (brl && count) {
        uint32_t i = 0;
        r.reserve(count);
        for (i = 0; i < count; ++i) {
            r.push_back(block_range_to_value(brl[i]));
        }
    }
    return Value(r);
}

lsm_fs *value_to_fs(Value &fs) {
    lsm_fs *rc = NULL;
    if (is_expected_object(fs, CLASS_NAME_FILE_SYSTEM)) {
        std::map<std::string, Value> f = fs.asObject();

        rc = lsm_fs_record_alloc(
            f["id"].asString().c_str(), f["name"].asString().c_str(),
            f["total_space"].asUint64_t(), f["free_space"].asUint64_t(),
            f["pool_id"].asString().c_str(), f["system_id"].asString().c_str(),
            f["plugin_data"].asC_str());
    } else {
        throw ValueException("value_to_fs: Not correct type");
    }
    return rc;
}

Value fs_to_value(lsm_fs *fs) {
    if (LSM_IS_FS(fs)) {
        std::map<std::string, Value> f;
        f["class"] = Value(CLASS_NAME_FILE_SYSTEM);
        f["id"] = Value(fs->id);
        f["name"] = Value(fs->name);
        f["total_space"] = Value(fs->total_space);
        f["free_space"] = Value(fs->free_space);
        f["pool_id"] = Value(fs->pool_id);
        f["system_id"] = Value(fs->system_id);
        f["plugin_data"] = Value(fs->plugin_data);
        return Value(f);
    }
    return Value();
}

lsm_fs_ss *value_to_ss(Value &ss) {
    lsm_fs_ss *rc = NULL;
    if (is_expected_object(ss, CLASS_NAME_FS_SNAPSHOT)) {
        std::map<std::string, Value> f = ss.asObject();

        rc = lsm_fs_ss_record_alloc(
            f["id"].asString().c_str(), f["name"].asString().c_str(),
            f["ts"].asUint64_t(), f["plugin_data"].asC_str());
    } else {
        throw ValueException("value_to_ss: Not correct type");
    }
    return rc;
}

Value ss_to_value(lsm_fs_ss *ss) {
    if (LSM_IS_SS(ss)) {
        std::map<std::string, Value> f;
        f["class"] = Value(CLASS_NAME_FS_SNAPSHOT);
        f["id"] = Value(ss->id);
        f["name"] = Value(ss->name);
        f["ts"] = Value(ss->time_stamp);
        f["plugin_data"] = Value(ss->plugin_data);
        return Value(f);
    }
    return Value();
}

lsm_nfs_export *value_to_nfs_export(Value &exp) {
    lsm_nfs_export *rc = NULL;
    if (is_expected_object(exp, CLASS_NAME_FS_EXPORT)) {
        int ok = 0;
        lsm_string_list *root = NULL;
        lsm_string_list *rw = NULL;
        lsm_string_list *ro = NULL;

        std::map<std::string, Value> i = exp.asObject();

        /* Check all the arrays for successful allocation */
        root = value_to_string_list(i["root"]);
        if (root) {
            rw = value_to_string_list(i["rw"]);
            if (rw) {
                ro = value_to_string_list(i["ro"]);
                if (!ro) {
                    lsm_string_list_free(rw);
                    lsm_string_list_free(root);
                    rw = NULL;
                    root = NULL;
                } else {
                    ok = 1;
                }
            } else {
                lsm_string_list_free(root);
                root = NULL;
            }
        }

        if (ok) {
            rc = lsm_nfs_export_record_alloc(
                i["id"].asC_str(), i["fs_id"].asC_str(),
                i["export_path"].asC_str(), i["auth"].asC_str(), root, rw, ro,
                i["anonuid"].asUint64_t(), i["anongid"].asUint64_t(),
                i["options"].asC_str(), i["plugin_data"].asC_str());

            lsm_string_list_free(root);
            lsm_string_list_free(rw);
            lsm_string_list_free(ro);
        }
    } else {
        throw ValueException("value_to_nfs_export: Not correct type");
    }
    return rc;
}

Value nfs_export_to_value(lsm_nfs_export *exp) {
    if (LSM_IS_NFS_EXPORT(exp)) {
        std::map<std::string, Value> f;
        f["class"] = Value(CLASS_NAME_FS_EXPORT);
        f["id"] = Value(exp->id);
        f["fs_id"] = Value(exp->fs_id);
        f["export_path"] = Value(exp->export_path);
        f["auth"] = Value(exp->auth_type);
        f["root"] = Value(string_list_to_value(exp->root));
        f["rw"] = Value(string_list_to_value(exp->read_write));
        f["ro"] = Value(string_list_to_value(exp->read_only));
        if (exp->anon_uid == UINT64_MAX)
            f["anonuid"] = Value(-1);
        else if (exp->anon_uid == UINT64_MAX - 1)
            f["anonuid"] = Value(-2);
        else
            f["anonuid"] = Value(exp->anon_uid);
        if (exp->anon_gid == UINT64_MAX)
            f["anongid"] = Value(-1);
        else if (exp->anon_gid == UINT64_MAX - 1)
            f["anongid"] = Value(-2);
        else
            f["anongid"] = Value(exp->anon_gid);
        f["options"] = Value(exp->options);
        f["plugin_data"] = Value(exp->plugin_data);
        return Value(f);
    }
    return Value();
}

lsm_storage_capabilities *value_to_capabilities(Value &exp) {
    lsm_storage_capabilities *rc = NULL;
    if (is_expected_object(exp, CLASS_NAME_CAPABILITIES)) {
        const char *val = exp["cap"].asC_str();
        rc = lsm_capability_record_alloc(val);
    } else {
        throw ValueException("value_to_capabilities: Not correct type");
    }
    return rc;
}

Value capabilities_to_value(lsm_storage_capabilities *cap) {
    if (LSM_IS_CAPABILITY(cap)) {
        std::map<std::string, Value> c;
        char *t = capability_string(cap);
        c["class"] = Value(CLASS_NAME_CAPABILITIES);
        c["cap"] = Value(t);
        free(t);
        return Value(c);
    }
    return Value();
}

lsm_target_port *value_to_target_port(Value &tp) {
    lsm_target_port *rc = NULL;
    if (is_expected_object(tp, CLASS_NAME_TARGET_PORT)) {
        rc = lsm_target_port_record_alloc(
            tp["id"].asC_str(),
            (lsm_target_port_type)tp["port_type"].asInt32_t(),
            tp["service_address"].asC_str(), tp["network_address"].asC_str(),
            tp["physical_address"].asC_str(), tp["physical_name"].asC_str(),
            tp["system_id"].asC_str(), tp["plugin_data"].asC_str());
    } else {
        throw ValueException("value_to_target_port: Not correct type");
    }
    return rc;
}

Value target_port_to_value(lsm_target_port *tp) {
    if (LSM_IS_TARGET_PORT(tp)) {
        std::map<std::string, Value> p;
        p["class"] = Value(CLASS_NAME_TARGET_PORT);
        p["id"] = Value(tp->id);
        p["port_type"] = Value(tp->type);
        p["service_address"] = Value(tp->service_address);
        p["network_address"] = Value(tp->network_address);
        p["physical_address"] = Value(tp->physical_address);
        p["physical_name"] = Value(tp->physical_name);
        p["system_id"] = Value(tp->system_id);
        p["plugin_data"] = Value(tp->plugin_data);
        return Value(p);
    }
    return Value();
}

int values_to_uint32_array(Value &value, uint32_t **uint32_array,
                           uint32_t *count) {
    int rc = LSM_ERR_OK;
    *count = 0;
    try {
        std::vector<Value> data = value.asArray();
        *count = data.size();
        if (*count) {
            *uint32_array = (uint32_t *)malloc(sizeof(uint32_t) * *count);
            if (*uint32_array) {
                uint32_t i;
                for (i = 0; i < *count; i++) {
                    (*uint32_array)[i] = data[i].asUint32_t();
                }
            } else {
                rc = LSM_ERR_NO_MEMORY;
            }
        }
    } catch (const ValueException &ve) {
        if (*count) {
            free(*uint32_array);
            *uint32_array = NULL;
            *count = 0;
        }
        rc = LSM_ERR_LIB_BUG;
    }
    return rc;
}

Value uint32_array_to_value(uint32_t *uint32_array, uint32_t count) {
    std::vector<Value> rc;
    if (uint32_array && count) {
        uint32_t i;
        rc.reserve(count);
        for (i = 0; i < count; i++) {
            rc.push_back(uint32_array[i]);
        }
    }
    return rc;
}

lsm_battery *value_to_battery(Value &battery) {
    lsm_battery *rc = NULL;
    if (is_expected_object(battery, CLASS_NAME_BATTERY)) {
        std::map<std::string, Value> b = battery.asObject();

        rc = lsm_battery_record_alloc(
            b["id"].asString().c_str(), b["name"].asString().c_str(),
            (lsm_battery_type)b["type"].asInt32_t(), b["status"].asUint64_t(),
            b["system_id"].asString().c_str(),
            b["plugin_data"].asString().c_str());
    } else {
        throw ValueException("value_to_battery: Not correct type");
    }
    return rc;
}

Value battery_to_value(lsm_battery *battery) {
    if (LSM_IS_BATTERY(battery)) {
        std::map<std::string, Value> b;
        b["class"] = Value(CLASS_NAME_BATTERY);
        b["id"] = Value(battery->id);
        b["name"] = Value(battery->name);
        b["type"] = Value(battery->type);
        b["status"] = Value(battery->status);
        b["system_id"] = Value(battery->system_id);
        if (battery->plugin_data != NULL)
            b["plugin_data"] = Value(battery->plugin_data);
        return Value(b);
    }
    return Value();
}

int value_array_to_batteries(Value &battery_values, lsm_battery ***bs,
                             uint32_t *count) {
    int rc = LSM_ERR_OK;
    try {
        *count = 0;

        if (Value::array_t == battery_values.valueType()) {
            std::vector<Value> d = battery_values.asArray();

            *count = d.size();

            if (d.size()) {
                *bs = lsm_battery_record_array_alloc(d.size());

                if (*bs) {
                    for (size_t i = 0; i < d.size(); ++i) {
                        (*bs)[i] = value_to_battery(d[i]);
                        if (!((*bs)[i])) {
                            rc = LSM_ERR_NO_MEMORY;
                            goto error;
                        }
                    }
                } else {
                    rc = LSM_ERR_NO_MEMORY;
                }
            }
        }
    } catch (const ValueException &ve) {
        rc = LSM_ERR_LIB_BUG;
        goto error;
    }

out:
    return rc;

error:
    if (*bs && *count) {
        lsm_battery_record_array_free(*bs, *count);
        *bs = NULL;
        *count = 0;
    }
    goto out;
}
