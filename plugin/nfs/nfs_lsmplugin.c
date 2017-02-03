/*
 * Plugin to handle local system NFS exports
 */

#define _GNU_SOURCE
#include <libstoragemgmt/libstoragemgmt_plug_interface.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <stdbool.h>
#include <stdarg.h>
#include <string.h>
#include <errno.h>
#include <limits.h>
#include <openssl/md5.h>
#include <sys/types.h>
#include <sys/statvfs.h>
#include <sys/stat.h>
#include <unistd.h>
#include <ctype.h>


#define MOUNTS      "/proc/self/mounts"
#define EXPORTS     "/etc/exports.d/libstoragemgmt.exports"
#define SYSID       "local"

#define _UNUSED(x) (void)(x)
#define _BUFF_SIZE  4096
#define MD5_HASHLEN ((MD5_DIGEST_LENGTH * 2) + 1)

static char name[] = "NFS Plugin";
static char version [] = "0.1";

struct plugin_data {
    uint32_t tmo;
    /* All your other variables as needed */
};

/* Create the functions you plan on implementing that
    match the callback signatures */
static int tmoSet(lsm_plugin_ptr c, uint32_t timeout, lsm_flag flags )
{
    int rc = LSM_ERR_OK;
    struct plugin_data *pd = (struct plugin_data*)lsm_private_data_get(c);
    /* Do something with state to set timeout */
    _UNUSED(flags);
    pd->tmo = timeout;
    return rc;
}

static int tmoGet(lsm_plugin_ptr c, uint32_t *timeout, lsm_flag flags )
{
    int rc = LSM_ERR_OK;
    struct plugin_data *pd = (struct plugin_data*)lsm_private_data_get(c);
    /* Do something with state to get timeout */
    _UNUSED(flags);
    *timeout = pd->tmo;
    return rc;
}

/* md5 hash a string */
static void md5(const char *plaintext, char * result)
{
    unsigned char hash[MD5_DIGEST_LENGTH];
    if (plaintext == NULL || result == NULL) return;

    MD5((const unsigned char *)plaintext, strlen(plaintext), hash);

    char * out = result;
    for (int i=0; i<MD5_DIGEST_LENGTH; i++) {
        sprintf(out, "%02X", hash[i]);
        out += 2;
    }
}

/* take a bunch of formatted string inputs, md5 them */
static void md5fmt(char * result, char * format, ...) __attribute__((format(gnu_printf,2,3)));
static void md5fmt(char * result, char * format, ...)
{
    char * input = NULL;
    va_list va;

    va_start(va, format);
    vasprintf(&input, format, va);
    va_end(va);

    md5(input, result);
    free(input);
}

/* NFS specific callback functions */
int list_authtypes(lsm_plugin_ptr c, lsm_string_list **types, lsm_flag flags)
{
    int rc = LSM_ERR_OK;
    _UNUSED(c);
    _UNUSED(flags);
    *types = lsm_string_list_alloc(4);
    if (*types) {
        rc = lsm_string_list_elem_set(*types, 0, "sys");
        rc = lsm_string_list_elem_set(*types, 1, "krb5");
        rc = lsm_string_list_elem_set(*types, 2, "krb5i");
        rc = lsm_string_list_elem_set(*types, 3, "krb5p");
    } else {
        rc = LSM_ERR_NO_MEMORY;
    }
    return rc;
}

/* log the error */
int lsm_perror(lsm_plugin_ptr c, lsm_error_number code, const char * msg, ...) __attribute__((format(gnu_printf,3,4)));
int lsm_perror(lsm_plugin_ptr c, lsm_error_number code, const char * msg, ...)
{
    va_list va;
    char * buff = NULL;

    va_start(va, msg);
    if (vasprintf(&buff, msg, va) != -1) {
        lsm_log_error_basic(c, code, buff);
        free(buff);
    }
    va_end(va);
    return code;
}

/* parse string for uint64 value
 * return 0 on success
 */                                
static int str_to_uint64(const char *arg, uint64_t * val)
{
    int64_t ret;
    char * end;

    ret = strtoll(arg, &end, 10);

    if (ret == 0 && end==arg) {
        // no conversion
        return 1;
    }
    if ((ret == LLONG_MIN || ret == LLONG_MAX) && errno == ERANGE) {
        // out of range Error
        return 1;
    }
    *val = ret;
    return 0;
}
        
/* turn a hash table of strings into a single 'sep' delimited string */
static char * hash_to_str(lsm_hash * hash, const char * sep)
{
    lsm_string_list * list = NULL;
    int seplen = 0;

    lsm_hash_keys(hash, &list);
    if (list == NULL) return NULL;
    int count = lsm_string_list_size(list);
    if (sep != NULL) seplen = strlen(sep);

    // how big is this string going to be ?
    size_t size = 0;
    for (int i=0; i<count; i++) {
        const char * item = lsm_string_list_elem_get(list, i);
        const char * val = lsm_hash_string_get(hash, item);
        if (item == NULL) continue;
        size += strlen(item);
        if (val != NULL && strcmp(val, "true")!=0) size += strlen(val)+1;
        if (i < count-1) size += seplen;
    }

    if (size == 0) return NULL;

    char * result = calloc(size+1, 1);
    for (int i=0; i<count; i++) {
        const char * item = lsm_string_list_elem_get(list, i);
        const char * val = lsm_hash_string_get(hash, item);
        if (item == NULL) continue;
        strcat(result, item);
        if (val != NULL && strcmp(val, "true")!=0) {
            strcat(result, "=");
            strcat(result, val);
        }
        if (i < count-1 && sep!=NULL) strcat(result, sep);
    }
    lsm_string_list_free(list);
    return result;
}

/* derive a filesystem id from a path name
 * simple one for now, stat and use the dev id
 */
static char * path_to_fsid(const char *path)
{
    struct statvfs st;
    char * answer = NULL;
    if (path == NULL) return NULL;

    if ((statvfs(path, &st))== -1) {
        return NULL;
    }
    asprintf(&answer, "%" PRIx64, st.f_fsid);
    return answer;
}
            
/* export-id string generated from path+hostname */
static char * nfs_makeid(const char *path, const char * host)
{
    if (path == NULL || host==NULL) return NULL;
    char * result = calloc(MD5_HASHLEN, 1);
    md5fmt(result, "%s%s", path, host);
    return result;
}

/* nul terminated strings, remove \000 encodings */
static void unescape_string(char * text)
{
    if (text == NULL) return;
    char * p = text;
    while (*p != 0 && (p=strchr(p, '\\'))!=NULL) {
        int val = 0;
        int n = 1;
        while (n<4 && p[n]!=0) {
            if (p[n] < '0' && p[n] > '7') break;
            val <<= 3;
            val |= p[n] - '0';
            n++;
        }
        if (n>1) {
            size_t len = strlen(p);
            p[0] = val;
            memmove(&p[1], &p[n], len - n);
            memset(&p[len - (n-1)], 0, n-1);
        } else {
            p++;
        }
    }
}

static lsm_hash * options_to_hash(const char * optstext)
{
    lsm_hash * options_list = lsm_hash_alloc();

    if (options_list == NULL) return NULL;
    if (optstext == NULL) return options_list;

    char * tmp = strdup(optstext);
    if (tmp == NULL) {
        lsm_hash_free(options_list);
        return NULL;
    }

    char * next = tmp;
    while (next != NULL) {
        char *opt = strsep(&next, ",");
        char *arg = opt;
        strsep(&arg, "=");

        lsm_hash_string_set(options_list, opt, arg==NULL?"true":arg);
    }
    free(tmp);

    return options_list;
}

static lsm_nfs_export * make_record(lsm_plugin_ptr c, const char * path, const char * host, lsm_hash * options)
{
    char const * auth = NULL;
    uint64_t anon_uid = 0;
    uint64_t anon_gid = 0;
    bool readonly = false;
    bool root_squash = false;
    char * other_options = NULL;
    lsm_string_list * olist = NULL;

    if (host == NULL || path == NULL) {
        lsm_perror(c, LSM_ERR_INVALID_ARGUMENT, "make_record requires both path and host");
        return NULL;
    }

    if (options) {
        lsm_hash * extra_opts = lsm_hash_alloc();

        lsm_hash_keys(options, &olist);
        for (unsigned int i=0; i<lsm_string_list_size(olist); i++) {
            const char * item = lsm_string_list_elem_get(olist, i);
            const char * val = lsm_hash_string_get(options, item);

            if (strcmp(item, "sec")==0) {
                auth = val;
            } else
            if (strcmp(item, "anonuid")==0) {
                if (val) {
                    if (strcmp(val, "-1")==0) {
                        anon_uid = -1;
                    } else {
                        if (str_to_uint64(val, &anon_uid)) {
                            lsm_perror(c, LSM_ERR_INVALID_ARGUMENT, "Invalid number conversion for anon_uid from '%s'", val);
                        }
                    }
                }
            } else
            if (strcmp(item, "anongid")==0) {
                if (val) {
                    if (strcmp(val, "-1")==0) {
                        anon_gid = -1;
                    } else {
                        if (str_to_uint64(val, &anon_gid)) {
                            lsm_perror(c, LSM_ERR_INVALID_ARGUMENT, "Invalid number conversion for anon_gid from '%s'", val);
                        }
                    }
                }
            } else
            if (strcmp(item, "ro")==0) {
                readonly = true;
            } else
            if (strcmp(item, "rw")==0) {
                readonly = false;
            } else
            if (strcmp(item, "no_root_squash")==0) {
                root_squash = false;
            } else
            if (strcmp(item, "root_squash")==0) {
                root_squash = true;
            } else {
                lsm_hash_string_set(extra_opts, item, val==NULL?"true":val);
            }
        }

        /* any other options */
        other_options = hash_to_str(extra_opts, ",");
        lsm_hash_free(extra_opts);
    }

    /* unique export id from path+host */
    char * expid = nfs_makeid(path, host);

    /* unique id for the filesystem that the path is on */
    char * fsid = path_to_fsid(path);
    if (fsid == NULL) fsid=strdup("Unknown");

    // create the nfs object and add this to the list 
    lsm_string_list * root_hosts = lsm_string_list_alloc(0);
    lsm_string_list * rw_hosts = lsm_string_list_alloc(0);
    lsm_string_list * ro_hosts = lsm_string_list_alloc(0);

    if (!root_squash) lsm_string_list_append(root_hosts, host);
    if (readonly) {
        lsm_string_list_append(ro_hosts, host);
    } else {
        lsm_string_list_append(rw_hosts, host);
    }

    lsm_nfs_export * lsm_nfs_obj = lsm_nfs_export_record_alloc(expid, fsid, path, auth, root_hosts, rw_hosts, ro_hosts, anon_uid, anon_gid, other_options, NULL);

    lsm_string_list_free(root_hosts);
    lsm_string_list_free(rw_hosts);
    lsm_string_list_free(ro_hosts);

    if (fsid) free(fsid);
    if (expid) free(expid);

    if (other_options) free(other_options);
    if (olist) lsm_string_list_free(olist);

    return lsm_nfs_obj;
}

/*
 * Parse an 'exports' formatted line
 * only checks for single host per line entries like etab
 * NB: modifies the input line
 */
static int parse_export(char * line, char **path, char **host, char **options)
{
    if (line == NULL || path==NULL || host==NULL || options == NULL)
        return 1;

    char * p;
    char * host_ptr;
    char * path_ptr;
    char * opt_ptr;

    // seperate the host from the path
    path_ptr = line;
    host_ptr = line;
    if (*path_ptr == '"') {
        // quoted path
        path_ptr++;
        host_ptr = path_ptr;
        strsep(&host_ptr, "\"");
    } else {
        // normal tab/space seperated path
        strsep(&host_ptr, "\t ");
    }
    if (host_ptr == NULL) return 1;

    while (isspace(*host_ptr)) host_ptr++;

    // seperate options string from host
    opt_ptr = host_ptr;
    strsep(&opt_ptr, "(");

    if (opt_ptr != NULL) {
        // trim trailing bracket
        if ((p=strchr(opt_ptr, ')'))!=NULL) *p=0;
    }

    // any further cleanup of host
    unescape_string(path_ptr);

    // hand out the results
    *path = path_ptr;
    *host = host_ptr;
    *options = opt_ptr;
    return 0;
}

/* clean up a nul terminated line, 
 * remove comments, leading and trailing whitespace 
 */
static void trim(char * line)
{
    char * p;

    if (line == NULL) return;

    // trim whitespace from both ends to see whats left
    if ((p = strchr(line, '\n'))!=NULL) *p=0;

    // strip out comments
    if ((p = strchr(line, '#'))!=NULL) *p=0;

    // any other trailing junk
    p = &line[ strlen(line) - 1 ];
    while (p>line && isspace(*p)) *(p--)=0;

    // any leading whitespace
    p = line;
    while (*p!=0 && isspace(*p)) p++;

    // there was leading space, shuffle down (include terminator)
    if (p != line) memmove(line, p, strlen(p)+1);
}

/* list the nfs exports one per line */
int list_exports(lsm_plugin_ptr c, const char *search_key, const char *search_value,
                lsm_nfs_export **exports[], uint32_t *count, lsm_flag flags)
{
    _UNUSED(flags);

    if (search_key) {
        return lsm_perror(c, LSM_ERR_NO_SUPPORT, "Search keys not supported: %s=%s", search_key, search_value==NULL?"NULL":search_value);
    }

    FILE *f = NULL;
    char buff[_BUFF_SIZE];

    if ((f=fopen(EXPORTS, "r")) == NULL) {
        return lsm_perror(c, LSM_ERR_PERMISSION_DENIED, "Error opening %s: %s\n", EXPORTS, strerror(errno));
    }

    int ecount = 0;
    int elimit = 8;
    lsm_nfs_export **export_list = calloc(elimit, sizeof(lsm_nfs_export *));

    if (export_list == NULL) {
        return lsm_perror(c, LSM_ERR_NO_MEMORY, "Error allocating results");
    }

    while (fgets(buff, sizeof(buff), f) != NULL) {
        char * path = NULL;
        char * host = NULL;
        char * options = NULL;
        lsm_hash * options_list = NULL;

        trim(buff);
        if (buff[0] == 0) continue;
        if (parse_export(buff, &path, &host, &options)) continue;

        options_list = options_to_hash(options);
        if (options_list == NULL) {
            return lsm_perror(c, LSM_ERR_NO_MEMORY, "Error allocating results");
        }

        lsm_nfs_export * lsm_nfs_obj = make_record(c, path, host, options_list);

        if (ecount+1 > elimit) {
            elimit *= 2;
            export_list = realloc(export_list, elimit * sizeof(lsm_nfs_export *));
            if (export_list == NULL) {
                lsm_hash_free(options_list);
                lsm_nfs_export_record_free(lsm_nfs_obj);
                return lsm_perror(c, LSM_ERR_NO_MEMORY, "Error allocating results");
            }
        }
        export_list[ecount++]=lsm_nfs_obj;
        lsm_hash_free(options_list);
    }

    fclose(f);

    _UNUSED(exports);
    *count = ecount;
    *exports = export_list;
    return LSM_ERR_OK;
}

/* load our own exports file into a manipulable form 
 * We assume its still in a nice one-entry-per-line format 
 */
lsm_hash * load_exports(lsm_plugin_ptr c, const char * filename)
{
    FILE *f = NULL;
    char buff[_BUFF_SIZE];

    if (filename == NULL) {
        lsm_perror(c, LSM_ERR_PLUGIN_BUG, "load_exports filename missing");
        return NULL;
    }

    lsm_hash * list = lsm_hash_alloc();

    if ((f=fopen(filename, "r"))==NULL) {
        if (errno == ENOENT) {
            /* its just not there, not an error, return empty list */
            return list;
        }

        lsm_perror(c, LSM_ERR_PLUGIN_BUG, "Error reading %s: %s", filename, strerror(errno));
        lsm_hash_free(list);
        return NULL;
    }

    while (fgets(buff, sizeof(buff), f)!=NULL) {
        char * path = NULL;
        char * host = NULL;
        char * options = NULL;

        trim(buff);
        if (buff[0] == 0) continue;

        parse_export(buff, &path, &host, &options);

        if (path == NULL || host == NULL || options == NULL) {
            continue;
        }

        char * expid = nfs_makeid(path, host);
        char * line = NULL;

        if (strchr(path, ' ')!=NULL)
            asprintf(&line, "\"%s\"\t%s(%s)", path, host, options);
        else
            asprintf(&line, "%s\t%s(%s)", path, host, options);

        lsm_hash_string_set(list, expid, line);
    }

    if (f) fclose(f);
    return list;
}
        
static int lsm_string_list_find(lsm_string_list * haystack, const char * needle)
{
    for (unsigned int i=0; i<lsm_string_list_size(haystack); i++) {
        const char * item = lsm_string_list_elem_get(haystack, i);
        if (strcmp(item, needle)==0) return i;
    }
    return -1;
}


/* clean up the options list before writing out 
 * esp. remove supressed or empty options
 */
static lsm_hash * filter_options(lsm_hash * in)
{
    lsm_string_list * list = NULL;

    lsm_hash_keys(in, &list);
    if (list == NULL) return in;

    lsm_hash * out = lsm_hash_alloc();
    for (unsigned int i=0; i<lsm_string_list_size(list); i++) {
        const char * key = lsm_string_list_elem_get(list, i);
        const char * val = lsm_hash_string_get(in, key);

        /* suppress options that are defaulted */
        if (strcmp(key, "anonuid")==0 || strcmp(key, "anongid")==0) {
            if (strcmp(val, "-1")==0) continue;
        }
        /* you cant delete from hashes, so empty marks gone */
        if (*val==0) continue;

        lsm_hash_string_set(out, key, val);
    }
    lsm_string_list_free(list);
    lsm_hash_free(in);
    return out;
}

static int write_exports(lsm_plugin_ptr c, lsm_hash * exports, const char * filename)
{
    int ret = LSM_ERR_OK;
    char * tmpfile = NULL;
    asprintf(&tmpfile, "%s.tmp", filename);

    FILE *f = NULL;
    if ((f=fopen(tmpfile, "w"))==NULL) {
        return lsm_perror(c, LSM_ERR_PLUGIN_BUG, "Error writing to exports file %s: %s", tmpfile, strerror(errno));
    }

    lsm_string_list * list = NULL;
    lsm_hash_keys(exports, &list);

    fprintf(f, "# NFS exports managed by libstoragemgmt. do not edit.\n");

    unsigned int count = 0;
    if (list != NULL) count=lsm_string_list_size(list);

    for (unsigned int i=0; i<count; i++) {
        const char * item = lsm_string_list_elem_get(list, i);
        const char * val = lsm_hash_string_get(exports, item);

        if (val==NULL || *val == 0) continue;

        fprintf(f, "%s\n", val);
    }
    fclose(f);
    lsm_string_list_free(list);

    if (rename(tmpfile, filename)) {
        ret = lsm_perror(c, LSM_ERR_PLUGIN_BUG, "Error renaming exports file: %s", strerror(errno));
    }
    return ret;
}

/* add a new export to our exports file */
int add_export(lsm_plugin_ptr c, const char *fs_id, const char *export_path,
                  lsm_string_list *root_list, lsm_string_list *rw_list,
                  lsm_string_list *ro_list, uint64_t anon_uid,
                  uint64_t anon_gid, const char *auth_type, const char *options,
                  lsm_nfs_export **exported, lsm_flag flags)
{
    int ret = LSM_ERR_OK;

    _UNUSED(fs_id);
    _UNUSED(flags);

    if (exported == NULL) {
        return lsm_perror(c, LSM_ERR_INVALID_ARGUMENT, "Missing exported argument");
    }
    if (export_path == NULL) {
        return lsm_perror(c, LSM_ERR_INVALID_ARGUMENT, "Missing export_path argument");
    }

    if (geteuid() != 0) {
        return lsm_perror(c, LSM_ERR_INVALID_ARGUMENT, "This action requies the plugin to have root privilege");
    }

    struct stat st;
    if (stat(export_path, &st)) {
        return lsm_perror(c, LSM_ERR_INVALID_ARGUMENT, "export_path not found");
    }
    if (!S_ISDIR(st.st_mode)) {
        return lsm_perror(c, LSM_ERR_INVALID_ARGUMENT, "export_path is not a directory");
    }

    lsm_hash * export_list = load_exports(c, EXPORTS);

    // get a list of all the hostnames we have been given
    // use a hash to squash dupes quickly
    lsm_hash * hostlist = lsm_hash_alloc();
    for (unsigned int i=0; i<lsm_string_list_size(root_list); i++) {
        const char * item = lsm_string_list_elem_get(root_list, i);
        lsm_hash_string_set(hostlist, item, "1");
    }
    for (unsigned int i=0; i<lsm_string_list_size(rw_list); i++) {
        const char * item = lsm_string_list_elem_get(rw_list, i);
        lsm_hash_string_set(hostlist, item, "1");
    }
    for (unsigned int i=0; i<lsm_string_list_size(ro_list); i++) {
        const char * item = lsm_string_list_elem_get(ro_list, i);
        lsm_hash_string_set(hostlist, item, "1");
    }

    /* build the common options list for all hosts */
    lsm_hash * common_opts = options_to_hash(options);
    
    if (auth_type) lsm_hash_string_set(common_opts, "sec", auth_type);

    if (anon_uid == (uint64_t)-1) {
        lsm_hash_string_set(common_opts, "anonuid", "-1");
    } else {
        char * number = NULL;
        asprintf(&number, "%" PRIu64, anon_uid);
        if (number != NULL) {
            lsm_hash_string_set(common_opts, "anonuid", number);
            free(number);
        }
    }
    if (anon_gid == (uint64_t)-1) {
        lsm_hash_string_set(common_opts, "anongid", "-1");
    } else {
        char * number = NULL;
        asprintf(&number, "%" PRIu64, anon_gid);
        if (number != NULL) {
            lsm_hash_string_set(common_opts, "anongid", number);
            free(number);
        }
    }

    lsm_string_list * hostkeys;
    lsm_hash_keys(hostlist, &hostkeys);
    for (unsigned int i=0; i<lsm_string_list_size(hostkeys); i++) {
        /* add each seperate host as an entry */
        const char * thishost = lsm_string_list_elem_get(hostkeys, i);
        lsm_hash * thisopts = lsm_hash_copy(common_opts);

        /* add the options unique to this host */
        if (lsm_string_list_find(root_list, thishost)!=-1) {
            lsm_hash_string_set(thisopts, "no_root_squash", "true");
        } else {
            lsm_hash_string_set(thisopts, "root_squash", "true");
        }

        if (lsm_string_list_find(rw_list, thishost)!=-1) {
            lsm_hash_string_set(thisopts, "rw", "true");
        } else
        if (lsm_string_list_find(ro_list, thishost)!=-1) {
            lsm_hash_string_set(thisopts, "ro", "true");
        } else {
            /* host should have been in rw or ro as well as root, assume rw then */
            lsm_hash_string_set(thisopts, "rw", "true");
        }

        thisopts = filter_options(thisopts);
        char * options = hash_to_str(thisopts, ",");
        char * expid = nfs_makeid(export_path, thishost);
        char * line = NULL;
        if (strchr(export_path, ' ')!=NULL)
            asprintf(&line, "\"%s\"\t%s(%s)", export_path, thishost, options);
        else
            asprintf(&line, "%s\t%s(%s)", export_path, thishost, options);

        lsm_hash_string_set(export_list, expid, line);

        free(line);
        free(expid);
        free(options);
    }
    lsm_string_list_free(hostkeys);

    if ((ret=write_exports(c, export_list, EXPORTS))==LSM_ERR_OK) {
        *exported = lsm_nfs_export_record_alloc(NULL, fs_id, export_path, auth_type, root_list, rw_list, ro_list, anon_uid, anon_gid, options, NULL);
    }

    lsm_hash_free(export_list);
    lsm_hash_free(hostlist);

    return ret;
}

int del_export(lsm_plugin_ptr c, lsm_nfs_export *e, lsm_flag flags)
{
    int ret = LSM_ERR_OK;

    _UNUSED(flags);

    if (geteuid() != 0) {
        return lsm_perror(c, LSM_ERR_INVALID_ARGUMENT, "This action requies the plugin to have root privilege");
    }

    lsm_hash * export_list = load_exports(c, EXPORTS);

    const char *eid = lsm_nfs_export_id_get(e);

    int found = 0;
    lsm_string_list * ekeys = NULL;
    lsm_hash_keys(export_list, &ekeys);
    for (unsigned int i=0; i<lsm_string_list_size(ekeys); i++) {
        const char * key = lsm_string_list_elem_get(ekeys, i);

        if (strcasecmp(key, eid)==0) {
            lsm_hash_string_set(export_list, key, "");
            found++;
        }
    }
    lsm_string_list_free(ekeys);

    if (found == 0) {
        ret = lsm_perror(c, LSM_ERR_PLUGIN_BUG, "Export %s not found", eid);
    } else {
        write_exports(c, export_list, EXPORTS);
    }
    lsm_hash_free(export_list);
    return ret;
}

int capList(lsm_plugin_ptr c, lsm_system *sys, lsm_storage_capabilities **cap, lsm_flag flags)
{
    int rc = LSM_ERR_NO_MEMORY;

    if (cap==NULL) return lsm_perror(c, LSM_ERR_INVALID_ARGUMENT, "Got NULL in arguments");

     _UNUSED(flags);
     _UNUSED(sys);

     *cap = lsm_capability_record_alloc(NULL);
     if (*cap == NULL) return lsm_perror(c, LSM_ERR_NO_MEMORY, "Capabilities malloc failed");

     rc = lsm_capability_set_n(*cap, LSM_CAP_SUPPORTED,
         LSM_CAP_EXPORT_AUTH,
         LSM_CAP_EXPORTS,
         LSM_CAP_EXPORT_FS,
         LSM_CAP_EXPORT_REMOVE,
         LSM_CAP_EXPORT_CUSTOM_PATH,
         -1);

     if (rc != LSM_ERR_OK) {
         lsm_capability_record_free(*cap);
         lsm_perror(c, rc, "lsm_capability_set_n failed");
     }
     return rc;
}

/* we need to be able to list filesystems in order for lsmcli
 * to select one of them for export 
 */
int fs_list(lsm_plugin_ptr c, const char *search_key, const char *search_value,
                lsm_fs **results[], uint32_t *count, lsm_flag flags)
{
    _UNUSED(c);
    _UNUSED(flags);
    
    if (search_key) {
        return lsm_perror(c, LSM_ERR_NO_SUPPORT, "Search keys not supported: %s=%s", search_key, search_value==NULL?"NULL":search_value);
    }

    if (results == NULL) return lsm_perror(c, LSM_ERR_INVALID_ARGUMENT, "Got NULL in arguments");
    *count = 0;

    FILE * f = NULL;
    if ((f=fopen(MOUNTS, "r"))==NULL) {
        return lsm_perror(c, LSM_ERR_PLUGIN_BUG, "Error listing mounts: %s", strerror(errno));
    }

    lsm_hash * pathlist = lsm_hash_alloc();
    char buff[_BUFF_SIZE];
    lsm_fs ** fslist = NULL;
    int rcount = 0;
    while (fgets(buff, sizeof(buff), f)!=NULL) {
        char *source = buff;
        char *path = source;
        char *type = NULL;

        strsep(&path, " ");
        type = path;
        strsep(&type, " ");

        struct statvfs st;
        if ((statvfs(path, &st))== -1) {
            /* if we cant get stats, its not a valid fs */
            continue;
        }
        if (st.f_fsid == 0) {
            /* if fsid is zero then we should not be exporting it */
            continue;
        }
        char * fsid = NULL;
        asprintf(&fsid, "%" PRIx64, st.f_fsid);
        uint64_t total_space = st.f_frsize * st.f_blocks;
        uint64_t free_space = st.f_frsize * st.f_bavail;

        /* is this a duplicate ? */
        if (lsm_hash_string_get(pathlist, fsid)!=NULL) {
            free(fsid);
            continue;
        }
        lsm_hash_string_set(pathlist, fsid, path);

        lsm_fs * fsobj = lsm_fs_record_alloc(fsid, path, total_space, free_space, "none", SYSID, NULL);

        size_t isize = sizeof(lsm_fs *) * (rcount+1);
        fslist = (lsm_fs **)realloc(fslist, isize); 
        fslist[rcount++] = fsobj;

        free(fsid);
    }
    lsm_hash_free(pathlist);

    *count = rcount;
    *results = fslist;

    fclose(f);

    return LSM_ERR_OK;
}

/* Setup the function addresses in the appropriate
    required callback structure */
static struct lsm_mgmt_ops_v1 mgmOps = {
    tmoSet,
    tmoGet,
    capList,
    NULL,
    NULL,
    NULL,
    NULL
};

static struct lsm_nas_ops_v1 nfsOps = {
    list_authtypes,
    list_exports,
    add_export,
    del_export
};

static struct lsm_fs_ops_v1 fsOps = {
    fs_list,
    NULL,
    NULL,
    NULL,
    NULL,
    NULL,
    NULL,
    NULL,
    NULL,
    NULL,
    NULL,
    NULL
};

int load( lsm_plugin_ptr c, const char *uri, const char *password,
                        uint32_t timeout, lsm_flag flags )
{
    /* Do plug-in specific init. and setup callback structures */
    struct plugin_data *data = (struct plugin_data *)
                                malloc(sizeof(struct plugin_data));

    _UNUSED(uri);
    _UNUSED(password);
    _UNUSED(timeout);
    _UNUSED(flags);

    if (!data) {
        return LSM_ERR_NO_MEMORY;
    }

    /* Call back into the framework */
    int rc = lsm_register_plugin_v1( c, data, &mgmOps, NULL, &fsOps, &nfsOps);
    return rc;
}

int unload( lsm_plugin_ptr c, lsm_flag flags)
{
    /* Get a handle to your private data and do clean-up */
    struct plugin_data *pd = (struct plugin_data*)lsm_private_data_get(c);
    _UNUSED(flags);
    free(pd);
    return LSM_ERR_OK;
}

int main(int argc, char *argv[] )
{
    return lsm_plugin_init_v1(argc, argv, load, unload, name, version);
}
