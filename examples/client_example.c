#include <libstoragemgmt/libstoragemgmt.h>
#include <stdio.h>

/*
 * If you have the development library package installed
 *
 *  $ gcc -Wall client_example.c -lstoragemgmt -o client_example
 *
 *
 * If building out of source tree
 *
 *  $ gcc -Wall -g -O0 client_example.c -I../c_binding/include/ \
 *          -L../c_binding/.libs -lstoragemgmt -o client_example
 *
 * If using autotools:
 *  * configure.ac
 *
 *      PKG_CHECK_MODULES(
 *          [LSM], [libstoragemgmt >= 1.0.0],,
 *          AC_MSG_ERROR([libstoragemgmt 1.0.0 or newer not found.]))
 *
 *  * Makefile.am
 *
 *      bin_PROGRAMS = lsm_client_example
 *      lsm_client_example_SOURCES = client_example.c
 *      lsm_client_example_CFLAGS = $(LSM_CFLAGS)
 *      lsm_client_example_LDADD = $(LSM_LIBS)
 *
 */

void error(char *msg, int rc, lsm_error *e) {
    if (rc) {
        printf("%s: error: %d\n", msg, rc);

        if (e && lsm_error_message_get(e)) {
            printf("Msg: %s\n", lsm_error_message_get(e));
            lsm_error_free(e);
        }
    }
}

void list_pools(lsm_connect *c) {
    lsm_pool **pools = NULL;
    int rc = 0;
    uint32_t count = 0;

    rc = lsm_pool_list(c, NULL, NULL, &pools, &count, LSM_CLIENT_FLAG_RSVD);
    if (LSM_ERR_OK == rc) {
        uint32_t i;
        for (i = 0; i < count; ++i) {
            printf("pool name: %s freespace: %" PRIu64 "\n",
                   lsm_pool_name_get(pools[i]),
                   lsm_pool_free_space_get(pools[i]));
        }

        lsm_pool_record_array_free(pools, count);
    } else {
        error("Volume list", rc, lsm_error_last_get(c));
    }
}

int main() {
    lsm_connect *c = NULL;
    lsm_error *e = NULL;
    int rc = 0;

    const char *uri = "sim://";

    rc = lsm_connect_password(uri, NULL, &c, 30000, &e, LSM_CLIENT_FLAG_RSVD);

    if (LSM_ERR_OK == rc) {
        printf("We connected...\n");

        list_pools(c);

        rc = lsm_connect_close(c, LSM_CLIENT_FLAG_RSVD);
        if (LSM_ERR_OK != rc) {
            error("Close", rc, lsm_error_last_get(c));
        } else {
            printf("We closed\n");
        }
    } else {
        error("Connect", rc, e);
    }

    return rc;
}
