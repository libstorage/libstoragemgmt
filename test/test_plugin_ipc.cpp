/*
 * SPDX-License-Identifier: LGPL-2.1-or-later
 *
 * Copyright (C) 2026 Red Hat, Inc.
 *
 * Regression tests for fae61750 ("Fix memory leak and NULL deref in plugin
 * IPC handlers"):
 *
 *   1. handle_volume_ident_led_on/off and handle_system_read_cache_pct_update
 *      called straight into p->ops_v1_3->...() with whatever
 *      value_to_volume()/value_to_system() returned, without checking for
 *      NULL.  Both converters return NULL (without throwing) when the
 *      underlying lsm_*_record_alloc() fails to allocate - rare, but exactly
 *      the kind of failure a real plug-in's handler must not be handed a
 *      NULL object for.  These tests force that allocation failure with a
 *      linker-level --wrap=strdup that fails only for a sentinel id, and
 *      assert the RPC gets back an error reply (LSM_ERR_NO_MEMORY) instead
 *      of crashing, and that the plug-in operation is never invoked.
 *
 *   2. ss_list() (dispatched as "fs_snapshots") freed its `lsm_fs *fs` only
 *      inside the `if (LSM_ERR_OK == rc)` branch, leaking it whenever the
 *      plug-in's fs_ss_list() callback returned an error.  Built with
 *      AddressSanitizer's leak detector (see test/Makefile.am), a run that
 *      exercises the error path trips an ASan leak report if the fix
 *      regresses.
 *
 * This drives the real lsm_plugin_init_v1()/lsm_register_plugin_v1_3() IPC
 * server loop (the same entry point every C plug-in uses) over a
 * socketpair, with a minimal in-test plug-in, rather than depending on any
 * specific shipped C plug-in implementing these v1.3 ops.
 */

#include "libstoragemgmt/libstoragemgmt_error.h"
#include "libstoragemgmt/libstoragemgmt_fs.h"
#include "libstoragemgmt/libstoragemgmt_plug_interface.h"
#include "libstoragemgmt/libstoragemgmt_systems.h"
#include "libstoragemgmt/libstoragemgmt_volumes.h"
#include "lsm_convert.hpp"
#include "lsm_datatypes.hpp"
#include "lsm_ipc.hpp"
#include "lsm_plugin_ipc.hpp"

#include <atomic>
#include <cerrno>
#include <cstring>
#include <map>
#include <string>
#include <sys/socket.h>
#include <thread>
#include <unistd.h>

/* check.h's "fail" macro clashes with std::basic_ios::fail(), so it must be
 * included only after all standard library headers are pulled in. */
#include <check.h>

/*
 * An id string containing this needle makes __wrap_strdup() below report
 * ENOMEM, standing in for a real allocation failure inside
 * lsm_volume_record_alloc()/lsm_system_record_alloc() without having to
 * fail allocations globally (which would also break plug-in setup).
 */
static const char FORCE_STRDUP_FAIL_NEEDLE[] = "LSM_TEST_FORCE_STRDUP_FAIL";

extern "C" char *__real_strdup(const char *s);

extern "C" char *__wrap_strdup(const char *s) {
    if (s != NULL && strstr(s, FORCE_STRDUP_FAIL_NEEDLE) != NULL) {
        errno = ENOMEM;
        return NULL;
    }
    return __real_strdup(s);
}

/* Set by the fake ops_v1_3 callbacks below if they are ever actually
 * invoked, so tests can assert a rejected request never reached them. */
static std::atomic<bool> g_volume_ident_on_called(false);
static std::atomic<bool> g_volume_ident_off_called(false);
static std::atomic<bool> g_sys_read_cache_pct_update_called(false);

/* If the fix regresses and a NULL volume/system reaches here, a real plug-in
 * implementation dereferencing ->id (the real struct, not the public opaque
 * type) the way these do would crash.  The result is stashed in a global
 * rather than merely discarded so the read cannot be optimized away: the
 * primary, compiler-independent proof the fix enforces is
 * g_*_called/the RPC error below, this is corroborating evidence of what a
 * real plug-in would experience. */
static volatile const char *g_last_seen_id;

static int fake_vol_ident_on(lsm_plugin_ptr c, lsm_volume *volume,
                             lsm_flag flags) {
    (void)c;
    (void)flags;
    g_volume_ident_on_called = true;
    g_last_seen_id = volume->id;
    return LSM_ERR_OK;
}

static int fake_vol_ident_off(lsm_plugin_ptr c, lsm_volume *volume,
                              lsm_flag flags) {
    (void)c;
    (void)flags;
    g_volume_ident_off_called = true;
    g_last_seen_id = volume->id;
    return LSM_ERR_OK;
}

static int fake_sys_read_cache_pct_update(lsm_plugin_ptr c, lsm_system *system,
                                          uint32_t read_pct, lsm_flag flags) {
    (void)c;
    (void)read_pct;
    (void)flags;
    g_sys_read_cache_pct_update_called = true;
    g_last_seen_id = system->id;
    return LSM_ERR_OK;
}

/* Controls what the fake fs_ss_list callback returns, so a single plug-in
 * can drive both the success and the error (leak) path. */
static std::atomic<int> g_fs_ss_list_rc(LSM_ERR_OK);

static int fake_fs_ss_list(lsm_plugin_ptr c, lsm_fs *fs, lsm_fs_ss **ss[],
                           uint32_t *ss_count, lsm_flag flags) {
    (void)c;
    (void)fs;
    (void)flags;
    *ss = NULL;
    *ss_count = 0;
    return g_fs_ss_list_rc;
}

static int fake_register(lsm_plugin_ptr c, const char *uri,
                         const char *password, uint32_t timeout,
                         lsm_flag flags) {
    (void)uri;
    (void)password;
    (void)timeout;
    (void)flags;

    static struct lsm_ops_v1_3 ops_v1_3;
    memset(&ops_v1_3, 0, sizeof(ops_v1_3));
    ops_v1_3.vol_ident_on = fake_vol_ident_on;
    ops_v1_3.vol_ident_off = fake_vol_ident_off;
    ops_v1_3.sys_read_cache_pct_update = fake_sys_read_cache_pct_update;

    static struct lsm_fs_ops_v1 fs_ops;
    memset(&fs_ops, 0, sizeof(fs_ops));
    fs_ops.fs_ss_list = fake_fs_ss_list;

    return lsm_register_plugin_v1_3(c, NULL, NULL, NULL, &fs_ops, NULL, NULL,
                                    &ops_v1_3);
}

static int fake_unregister(lsm_plugin_ptr c, lsm_flag flags) {
    (void)c;
    (void)flags;
    return LSM_ERR_OK;
}

/*
 * Runs the real plug-in-side IPC server loop (lsm_plugin_init_v1(), the same
 * entry point every C plug-in's main() calls) on 'fd' until the client sends
 * plugin_unregister or disconnects.
 */
static void run_plugin(int fd) {
    char fd_str[32];
    snprintf(fd_str, sizeof(fd_str), "%d", fd);
    char prog[] = "test-plugin";
    char *argv[2];
    argv[0] = prog;
    argv[1] = fd_str;

    lsm_plugin_init_v1(2, argv, fake_register, fake_unregister,
                       "test plug-in for IPC handler regression tests",
                       "0.0.1");
}

namespace {

/* One client + plug-in pair, torn down with plugin_unregister. */
class PluginFixture {
  public:
    PluginFixture() {
        ck_assert_int_eq(socketpair(AF_UNIX, SOCK_STREAM, 0, fds_), 0);
        server_ = std::thread(run_plugin, fds_[0]);
        client_ = new Ipc(fds_[1]);

        std::map<std::string, Value> params;
        params["uri"] = Value("test://");
        params["password"] = Value();
        params["timeout"] = Value((uint32_t)30000);
        params["flags"] = Value((uint64_t)0);

        client_->rpc("plugin_register", Value(params));
    }

    ~PluginFixture() {
        std::map<std::string, Value> params;
        params["flags"] = Value((uint64_t)0);
        try {
            client_->rpc("plugin_unregister", Value(params));
        } catch (...) {
            /* best effort */
        }
        if (server_.joinable()) {
            server_.join();
        }
        delete client_;
        close(fds_[1]);
    }

    Ipc *client() { return client_; }

  private:
    int fds_[2];
    std::thread server_;
    Ipc *client_;
};

} // namespace

/* Builds a "Volume" object Value; 'id' is the knob used to trigger the
 * forced allocation failure. */
static Value make_volume(const std::string &id) {
    std::map<std::string, Value> v;
    v["class"] = Value("Volume");
    v["id"] = Value(id);
    v["name"] = Value("vol1");
    v["vpd83"] = Value("2000c50012345678");
    v["block_size"] = Value((uint64_t)512);
    v["num_of_blocks"] = Value((uint64_t)2048);
    v["admin_state"] = Value((uint32_t)1);
    v["system_id"] = Value("sys1");
    v["pool_id"] = Value("pool1");
    v["plugin_data"] = Value();
    return Value(v);
}

static Value make_system(const std::string &id) {
    std::map<std::string, Value> s;
    s["class"] = Value("System");
    s["id"] = Value(id);
    s["name"] = Value("sys1");
    s["status"] = Value((uint32_t)1);
    s["status_info"] = Value("");
    s["plugin_data"] = Value();
    return Value(s);
}

static Value make_fs(const std::string &id) {
    std::map<std::string, Value> f;
    f["class"] = Value("FileSystem");
    f["id"] = Value(id);
    f["name"] = Value("fs1");
    f["total_space"] = Value((uint64_t)1024 * 1024);
    f["free_space"] = Value((uint64_t)512 * 1024);
    f["pool_id"] = Value("pool1");
    f["system_id"] = Value("sys1");
    f["plugin_data"] = Value();
    return Value(f);
}

START_TEST(test_volume_ident_led_on_null_volume_handled) {
    PluginFixture fx;
    g_volume_ident_on_called = false;

    std::map<std::string, Value> params;
    params["volume"] = make_volume(FORCE_STRDUP_FAIL_NEEDLE);
    params["flags"] = Value((uint64_t)0);

    bool caught = false;
    try {
        fx.client()->rpc("volume_ident_led_on", Value(params));
    } catch (const LsmException &e) {
        caught = true;
        ck_assert_int_eq(e.error_code, (int)LSM_ERR_NO_MEMORY);
    }

    ck_assert_msg(caught, "Expected LSM_ERR_NO_MEMORY for a volume that "
                          "failed to convert");
    ck_assert_msg(!g_volume_ident_on_called,
                  "ops_v1_3->vol_ident_on was invoked with a NULL volume");
}
END_TEST

START_TEST(test_volume_ident_led_off_null_volume_handled) {
    PluginFixture fx;
    g_volume_ident_off_called = false;

    std::map<std::string, Value> params;
    params["volume"] = make_volume(FORCE_STRDUP_FAIL_NEEDLE);
    params["flags"] = Value((uint64_t)0);

    bool caught = false;
    try {
        fx.client()->rpc("volume_ident_led_off", Value(params));
    } catch (const LsmException &e) {
        caught = true;
        ck_assert_int_eq(e.error_code, (int)LSM_ERR_NO_MEMORY);
    }

    ck_assert_msg(caught, "Expected LSM_ERR_NO_MEMORY for a volume that "
                          "failed to convert");
    ck_assert_msg(!g_volume_ident_off_called,
                  "ops_v1_3->vol_ident_off was invoked with a NULL volume");
}
END_TEST

START_TEST(test_system_read_cache_pct_update_null_system_handled) {
    PluginFixture fx;
    g_sys_read_cache_pct_update_called = false;

    std::map<std::string, Value> params;
    params["system"] = make_system(FORCE_STRDUP_FAIL_NEEDLE);
    params["read_pct"] = Value((uint32_t)42);
    params["flags"] = Value((uint64_t)0);

    bool caught = false;
    try {
        fx.client()->rpc("system_read_cache_pct_update", Value(params));
    } catch (const LsmException &e) {
        caught = true;
        ck_assert_int_eq(e.error_code, (int)LSM_ERR_NO_MEMORY);
    }

    ck_assert_msg(caught, "Expected LSM_ERR_NO_MEMORY for a system that "
                          "failed to convert");
    ck_assert_msg(!g_sys_read_cache_pct_update_called,
                  "ops_v1_3->sys_read_cache_pct_update was invoked with a "
                  "NULL system");
}
END_TEST

/* Sanity check: a volume that allocates fine must still work normally. */
START_TEST(test_volume_ident_led_on_normal_volume_still_works) {
    PluginFixture fx;
    g_volume_ident_on_called = false;

    std::map<std::string, Value> params;
    params["volume"] = make_volume("vol-id-1");
    params["flags"] = Value((uint64_t)0);

    fx.client()->rpc("volume_ident_led_on", Value(params));

    ck_assert_msg(g_volume_ident_on_called,
                  "ops_v1_3->vol_ident_on was not invoked for a valid volume");
}
END_TEST

/*
 * fs_snapshots (ss_list()) must free its 'fs' conversion even when the
 * plug-in's own fs_ss_list() callback returns an error - otherwise 'fs'
 * leaks on every failed call.  Run under AddressSanitizer's leak detector
 * (see test/Makefile.am), this trips on the pre-fix code.
 */
START_TEST(test_fs_snapshots_error_path_does_not_leak_fs) {
    PluginFixture fx;
    g_fs_ss_list_rc = LSM_ERR_PLUGIN_BUG;

    std::map<std::string, Value> params;
    params["fs"] = make_fs("fs-id-1");
    params["flags"] = Value((uint64_t)0);

    bool caught = false;
    try {
        fx.client()->rpc("fs_snapshots", Value(params));
    } catch (const LsmException &e) {
        caught = true;
        ck_assert_int_eq(e.error_code, (int)LSM_ERR_PLUGIN_BUG);
    }
    ck_assert_msg(caught, "Expected LSM_ERR_PLUGIN_BUG from fs_ss_list()");
}
END_TEST

static Suite *plugin_ipc_suite(void) {
    Suite *s = suite_create("plugin_ipc");
    TCase *tc = tcase_create("core");

    tcase_add_test(tc, test_volume_ident_led_on_null_volume_handled);
    tcase_add_test(tc, test_volume_ident_led_off_null_volume_handled);
    tcase_add_test(tc, test_system_read_cache_pct_update_null_system_handled);
    tcase_add_test(tc, test_volume_ident_led_on_normal_volume_still_works);
    tcase_add_test(tc, test_fs_snapshots_error_path_does_not_leak_fs);
    suite_add_tcase(s, tc);
    return s;
}

int main(void) {
    int number_failed;
    Suite *s = plugin_ipc_suite();
    SRunner *sr = srunner_create(s);

    srunner_run_all(sr, CK_NORMAL);
    number_failed = srunner_ntests_failed(sr);
    srunner_free(sr);
    return (number_failed == 0) ? 0 : 1;
}
