EXTRA_DIST=kernel-doc split-man.pl doc-preclean.pl

API_MAN_PAGES = \
	api_man/lsm_local_disk_vpd83_search.3 \
	api_man/lsm_local_disk_serial_num_get.3 \
	api_man/lsm_local_disk_vpd83_get.3 \
	api_man/lsm_local_disk_rpm_get.3 \
	api_man/lsm_local_disk_list.3 \
	api_man/lsm_local_disk_link_type_get.3 \
	api_man/lsm_local_disk_ident_led_on.3 \
	api_man/lsm_local_disk_ident_led_off.3 \
	api_man/lsm_local_disk_fault_led_on.3 \
	api_man/lsm_local_disk_fault_led_off.3 \
	api_man/lsm_local_disk_led_status_get.3 \
	api_man/lsm_local_disk_link_speed_get.3 \
	api_man/lsm_local_disk_health_status_get.3 \
	api_man/lsm_system_record_copy.3 \
	api_man/lsm_system_record_free.3 \
	api_man/lsm_system_record_array_free.3 \
	api_man/lsm_system_id_get.3 \
	api_man/lsm_system_name_get.3 \
	api_man/lsm_system_read_cache_pct_get.3 \
	api_man/lsm_system_status_get.3 \
	api_man/lsm_system_fw_version_get.3 \
	api_man/lsm_system_mode_get.3 \
	api_man/lsm_pool_record_array_free.3 \
	api_man/lsm_pool_record_free.3 \
	api_man/lsm_pool_record_copy.3 \
	api_man/lsm_pool_name_get.3 \
	api_man/lsm_pool_id_get.3 \
	api_man/lsm_pool_total_space_get.3 \
	api_man/lsm_pool_free_space_get.3 \
	api_man/lsm_pool_status_get.3 \
	api_man/lsm_pool_status_info_get.3 \
	api_man/lsm_pool_system_id_get.3 \
	api_man/lsm_pool_element_type_get.3 \
	api_man/lsm_pool_unsupported_actions_get.3 \
	api_man/lsm_volume_record_free.3 \
	api_man/lsm_volume_record_copy.3 \
	api_man/lsm_volume_record_array_free.3 \
	api_man/lsm_volume_id_get.3 \
	api_man/lsm_volume_name_get.3 \
	api_man/lsm_volume_vpd83_get.3 \
	api_man/lsm_volume_block_size_get.3 \
	api_man/lsm_volume_number_of_blocks_get.3 \
	api_man/lsm_volume_admin_state_get.3 \
	api_man/lsm_volume_system_id_get.3 \
	api_man/lsm_volume_pool_id_get.3 \
	api_man/lsm_access_group_record_free.3 \
	api_man/lsm_access_group_record_array_free.3 \
	api_man/lsm_access_group_record_copy.3 \
	api_man/lsm_access_group_id_get.3 \
	api_man/lsm_access_group_name_get.3 \
	api_man/lsm_access_group_system_id_get.3 \
	api_man/lsm_access_group_initiator_id_get.3 \
	api_man/lsm_access_group_init_type_get.3 \
	api_man/lsm_battery_record_free.3 \
	api_man/lsm_battery_record_copy.3 \
	api_man/lsm_battery_record_array_free.3 \
	api_man/lsm_battery_id_get.3 \
	api_man/lsm_battery_name_get.3 \
	api_man/lsm_battery_type_get.3 \
	api_man/lsm_battery_status_get.3 \
	api_man/lsm_battery_system_id_get.3 \
	api_man/lsm_capability_record_free.3 \
	api_man/lsm_capability_get.3 \
	api_man/lsm_capability_supported.3 \
	api_man/lsm_block_range_record_alloc.3 \
	api_man/lsm_block_range_record_free.3 \
	api_man/lsm_block_range_record_copy.3 \
	api_man/lsm_block_range_record_array_alloc.3 \
	api_man/lsm_block_range_record_array_free.3 \
	api_man/lsm_block_range_source_start_get.3 \
	api_man/lsm_block_range_dest_start_get.3 \
	api_man/lsm_block_range_block_count_get.3 \
	api_man/lsm_string_list_alloc.3 \
	api_man/lsm_string_list_free.3 \
	api_man/lsm_string_list_copy.3 \
	api_man/lsm_string_list_elem_set.3 \
	api_man/lsm_string_list_elem_get.3 \
	api_man/lsm_string_list_size.3 \
	api_man/lsm_string_list_append.3 \
	api_man/lsm_string_list_delete.3 \
	api_man/lsm_initiator_id_verify.3 \
	api_man/lsm_volume_vpd83_verify.3 \
	api_man/lsm_disk_record_free.3 \
	api_man/lsm_disk_record_copy.3 \
	api_man/lsm_disk_record_array_free.3 \
	api_man/lsm_disk_id_get.3 \
	api_man/lsm_disk_name_get.3 \
	api_man/lsm_disk_type_get.3 \
	api_man/lsm_disk_number_of_blocks_get.3 \
	api_man/lsm_disk_block_size_get.3 \
	api_man/lsm_disk_status_get.3 \
	api_man/lsm_disk_location_get.3 \
	api_man/lsm_disk_rpm_get.3 \
	api_man/lsm_disk_link_type_get.3 \
	api_man/lsm_disk_system_id_get.3 \
	api_man/lsm_disk_vpd83_get.3 \
	api_man/lsm_error_last_get.3 \
	api_man/lsm_error_free.3 \
	api_man/lsm_error_number_get.3 \
	api_man/lsm_error_message_get.3 \
	api_man/lsm_error_exception_get.3 \
	api_man/lsm_error_debug_get.3 \
	api_man/lsm_error_debug_data_get.3 \
	api_man/lsm_fs_record_free.3 \
	api_man/lsm_fs_record_copy.3 \
	api_man/lsm_fs_record_array_free.3 \
	api_man/lsm_fs_id_get.3 \
	api_man/lsm_fs_name_get.3 \
	api_man/lsm_fs_system_id_get.3 \
	api_man/lsm_fs_pool_id_get.3 \
	api_man/lsm_fs_total_space_get.3 \
	api_man/lsm_fs_free_space_get.3 \
	api_man/lsm_connect_password.3 \
	api_man/lsm_connect_close.3 \
	api_man/lsm_plugin_info_get.3 \
	api_man/lsm_available_plugins_list.3 \
	api_man/lsm_connect_timeout_set.3 \
	api_man/lsm_connect_timeout_get.3 \
	api_man/lsm_job_status_get.3 \
	api_man/lsm_job_status_pool_get.3 \
	api_man/lsm_job_status_volume_get.3 \
	api_man/lsm_job_status_fs_get.3 \
	api_man/lsm_job_status_ss_get.3 \
	api_man/lsm_job_free.3 \
	api_man/lsm_capabilities.3 \
	api_man/lsm_pool_list.3 \
	api_man/lsm_volume_list.3 \
	api_man/lsm_disk_list.3 \
	api_man/lsm_volume_create.3 \
	api_man/lsm_volume_resize.3 \
	api_man/lsm_volume_replicate.3 \
	api_man/lsm_volume_replicate_range_block_size.3 \
	api_man/lsm_volume_replicate_range.3 \
	api_man/lsm_volume_delete.3 \
	api_man/lsm_volume_enable.3 \
	api_man/lsm_volume_disable.3 \
	api_man/lsm_iscsi_chap_auth.3 \
	api_man/lsm_access_group_list.3 \
	api_man/lsm_access_group_create.3 \
	api_man/lsm_access_group_delete.3 \
	api_man/lsm_access_group_initiator_add.3 \
	api_man/lsm_access_group_initiator_delete.3 \
	api_man/lsm_volume_mask.3 \
	api_man/lsm_volume_unmask.3 \
	api_man/lsm_volumes_accessible_by_access_group.3 \
	api_man/lsm_access_groups_granted_to_volume.3 \
	api_man/lsm_volume_child_dependency.3 \
	api_man/lsm_volume_child_dependency_delete.3 \
	api_man/lsm_system_list.3 \
	api_man/lsm_fs_list.3 \
	api_man/lsm_fs_create.3 \
	api_man/lsm_fs_delete.3 \
	api_man/lsm_fs_clone.3 \
	api_man/lsm_fs_child_dependency.3 \
	api_man/lsm_fs_child_dependency_delete.3 \
	api_man/lsm_fs_resize.3 \
	api_man/lsm_fs_file_clone.3 \
	api_man/lsm_fs_ss_list.3 \
	api_man/lsm_fs_ss_create.3 \
	api_man/lsm_fs_ss_delete.3 \
	api_man/lsm_fs_ss_restore.3 \
	api_man/lsm_nfs_auth_types.3 \
	api_man/lsm_nfs_list.3 \
	api_man/lsm_nfs_export_fs.3 \
	api_man/lsm_nfs_export_delete.3 \
	api_man/lsm_target_port_list.3 \
	api_man/lsm_volume_raid_info.3 \
	api_man/lsm_pool_member_info.3 \
	api_man/lsm_volume_raid_create_cap_get.3 \
	api_man/lsm_volume_raid_create.3 \
	api_man/lsm_volume_ident_led_on.3 \
	api_man/lsm_volume_ident_led_off.3 \
	api_man/lsm_system_read_cache_pct_update.3 \
	api_man/lsm_battery_list.3 \
	api_man/lsm_volume_cache_info.3 \
	api_man/lsm_volume_physical_disk_cache_update.3 \
	api_man/lsm_volume_write_cache_policy_update.3 \
	api_man/lsm_volume_read_cache_policy_update.3 \
	api_man/lsm_nfs_export_record_free.3 \
	api_man/lsm_nfs_export_record_array_free.3 \
	api_man/lsm_nfs_export_record_copy.3 \
	api_man/lsm_nfs_export_id_get.3 \
	api_man/lsm_nfs_export_fs_id_get.3 \
	api_man/lsm_nfs_export_export_path_get.3 \
	api_man/lsm_nfs_export_auth_type_get.3 \
	api_man/lsm_nfs_export_root_get.3 \
	api_man/lsm_nfs_export_read_write_get.3 \
	api_man/lsm_nfs_export_read_only_get.3 \
	api_man/lsm_nfs_export_anon_uid_get.3 \
	api_man/lsm_nfs_export_anon_gid_get.3 \
	api_man/lsm_nfs_export_options_get.3 \
	api_man/lsm_fs_ss_record_free.3 \
	api_man/lsm_fs_ss_record_copy.3 \
	api_man/lsm_fs_ss_record_array_free.3 \
	api_man/lsm_fs_ss_id_get.3 \
	api_man/lsm_fs_ss_name_get.3 \
	api_man/lsm_fs_ss_time_stamp_get.3 \
	api_man/lsm_target_port_copy.3 \
	api_man/lsm_target_port_record_free.3 \
	api_man/lsm_target_port_record_array_free.3 \
	api_man/lsm_target_port_id_get.3 \
	api_man/lsm_target_port_type_get.3 \
	api_man/lsm_target_port_service_address_get.3 \
	api_man/lsm_target_port_network_address_get.3 \
	api_man/lsm_target_port_physical_address_get.3 \
	api_man/lsm_target_port_physical_name_get.3 \
	api_man/lsm_target_port_system_id_get.3


notrans_dist_man3_MANS = $(API_MAN_PAGES)

HEADER_FOLDER = $(top_srcdir)/c_binding/include/

HEADER_FILES = \
	$(HEADER_FOLDER)/libstoragemgmt/libstoragemgmt_local_disk.h \
	$(HEADER_FOLDER)/libstoragemgmt/libstoragemgmt_systems.h \
	$(HEADER_FOLDER)/libstoragemgmt/libstoragemgmt_pool.h \
	$(HEADER_FOLDER)/libstoragemgmt/libstoragemgmt_volumes.h \
	$(HEADER_FOLDER)/libstoragemgmt/libstoragemgmt_accessgroups.h \
	$(HEADER_FOLDER)/libstoragemgmt/libstoragemgmt_battery.h \
	$(HEADER_FOLDER)/libstoragemgmt/libstoragemgmt_capabilities.h \
	$(HEADER_FOLDER)/libstoragemgmt/libstoragemgmt_blockrange.h \
	$(HEADER_FOLDER)/libstoragemgmt/libstoragemgmt_common.h \
	$(HEADER_FOLDER)/libstoragemgmt/libstoragemgmt_disk.h \
	$(HEADER_FOLDER)/libstoragemgmt/libstoragemgmt_error.h \
	$(HEADER_FOLDER)/libstoragemgmt/libstoragemgmt_fs.h \
	$(HEADER_FOLDER)/libstoragemgmt/libstoragemgmt.h \
	$(HEADER_FOLDER)/libstoragemgmt/libstoragemgmt_nfsexport.h \
	$(HEADER_FOLDER)/libstoragemgmt/libstoragemgmt_snapshot.h \
	$(HEADER_FOLDER)/libstoragemgmt/libstoragemgmt_targetport.h

TEMPFILE := $(shell mktemp)

$(API_MAN_PAGES): $(HEADER_FILES)
	cat $(HEADER_FILES) | \
	    perl $(top_srcdir)/doc/doc-preclean.pl > $(TEMPFILE)
	perl $(top_srcdir)/doc/kernel-doc -man $(TEMPFILE) | \
	    perl $(top_srcdir)/doc/split-man.pl api_man
	-rm -f $(TEMPFILE)

CLEANFILES=$(API_MAN_PAGES)

SUBDIRS = man

