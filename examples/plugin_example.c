/*
 * Web example.
 * http://libstorage.github.io/libstoragemgmt-doc/doc/c_plugin_dev_guide.html
 *
 *
  gcc -Wall -g -O0 plugin_example.c -I../include/ -L../src/.libs \
       -lstoragemgmt -o c_example_lsmplugin
 */

#include <libstoragemgmt/libstoragemgmt_plug_interface.h>
#include <stdint.h>
#include <stdlib.h>

static char name[] = "Simple limited plug-in example";
static char version[] = "0.01";

struct plugin_data {
    uint32_t tmo;
    /* All your other variables as needed */
};

/* Create the functions you plan on implementing that
    match the callback signatures */
static int tmoSet(lsm_plugin_ptr c, uint32_t timeout, lsm_flag flags) {
    int rc = LSM_ERR_OK;
    struct plugin_data *pd = (struct plugin_data *)lsm_private_data_get(c);
    /* Do something with state to set timeout */
    pd->tmo = timeout;
    return rc;
}

static int tmoGet(lsm_plugin_ptr c, uint32_t *timeout, lsm_flag flags) {
    int rc = LSM_ERR_OK;
    struct plugin_data *pd = (struct plugin_data *)lsm_private_data_get(c);
    /* Do something with state to get timeout */
    *timeout = pd->tmo;
    return rc;
}

/* Setup the function addresses in the appropriate
    required callback structure */
static struct lsm_mgmt_ops_v1 mgmOps = {tmoSet, tmoGet, NULL, NULL,
                                        NULL,   NULL,   NULL};

int load(lsm_plugin_ptr c, const char *uri, const char *password,
         uint32_t timeout, lsm_flag flags) {
    /* Do plug-in specific init. and setup callback structures */
    struct plugin_data *data =
        (struct plugin_data *)malloc(sizeof(struct plugin_data));

    if (!data) {
        return LSM_ERR_NO_MEMORY;
    }

    /* Call back into the framework */
    int rc = lsm_register_plugin_v1(c, data, &mgmOps, NULL, NULL, NULL);
    return rc;
}

int unload(lsm_plugin_ptr c, lsm_flag flags) {
    /* Get a handle to your private data and do clean-up */
    struct plugin_data *pd = (struct plugin_data *)lsm_private_data_get(c);
    free(pd);
    return LSM_ERR_OK;
}

int main(int argc, char *argv[]) {
    return lsm_plugin_init_v1(argc, argv, load, unload, name, version);
}
