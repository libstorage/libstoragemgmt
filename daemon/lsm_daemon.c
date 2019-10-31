/*
 * Copyright (C) 2011-2016 Red Hat, Inc.
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
 */

#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <getopt.h>
#include <string.h>
#include <ctype.h>
#include <signal.h>
#include <sys/types.h>
#include <pwd.h>
#include <errno.h>
#include <unistd.h>
#include <sys/types.h>
#include <syslog.h>
#include <stdarg.h>
#include <dirent.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <sys/queue.h>
#include <sys/wait.h>
#include <sys/time.h>
#include <libgen.h>
#include <assert.h>
#include <grp.h>
#include <limits.h>
#include <libconfig.h>

#define BASE_DIR  "/var/run/lsm"
#define SOCKET_DIR BASE_DIR"/ipc"
#define PLUGIN_DIR "/usr/bin"
#define LSM_USER "libstoragemgmt"
#define LSM_CONF_DIR "/etc/lsm/"
#define LSM_PLUGIN_CONF_DIR_NAME "pluginconf.d"
#define LSMD_CONF_FILE "lsmd.conf"
#define LSM_CONF_ALLOW_ROOT_OPT_NAME "allow-plugin-root-privilege"
#define LSM_CONF_REQUIRE_ROOT_OPT_NAME "require-root-privilege"

#define max(a,b) \
   ({ __typeof__ (a) _a = (a); \
       __typeof__ (b) _b = (b); \
     _a > _b ? _a : _b; })

int verbose_flag = 0;
int systemd = 0;

const char *socket_dir = SOCKET_DIR;
const char *plugin_dir = PLUGIN_DIR;
const char *conf_dir = LSM_CONF_DIR;

char plugin_extension[] = "_lsmplugin";

char plugin_conf_extension[] = ".conf";

typedef enum { RUNNING, RESTART, EXIT } serve_type;
serve_type serve_state = RUNNING;

int plugin_mem_debug = 0;

int allow_root_plugin = 0;
int has_root_plugin = 0;

/**
 * Each item in plugin list contains this information
 */
struct plugin {
    char *file_path;
    int require_root;
    int fd;
    LIST_ENTRY(plugin) pointers;
};

/**
 * Linked list of plug-ins
 */
LIST_HEAD(plugin_list, plugin) head;

/**
 * Logs messages to the appropriate place
 * @param severity      Severity of message, LOG_ERR causes daemon to exit
 * @param fmt           String with format
 * @param ...           Format parameters
 */
void logger(int severity, const char *fmt, ...)
{
    char buf[2048];

    if (verbose_flag || LOG_WARNING == severity || LOG_ERR == severity) {
        va_list arg;
        va_start(arg, fmt);
        vsnprintf(buf, sizeof(buf), fmt, arg);
        va_end(arg);

        if (!systemd) {
            if (verbose_flag) {
                syslog(LOG_ERR, "%s", buf);
            } else {
                syslog(severity, "%s", buf);
            }
        } else {
            fprintf(stdout, "%s", buf);
            fflush(stdout);
        }

        if (LOG_ERR == severity) {
            exit(1);
        }
    }
}

#define log_and_exit(fmt, ...)  logger(LOG_ERR, fmt, ##__VA_ARGS__)
#define warn(fmt, ...)  logger(LOG_WARNING, fmt, ##__VA_ARGS__)
#define info(fmt, ...)  logger(LOG_INFO, fmt, ##__VA_ARGS__)

/**
 * Our signal handler.
 * @param s     Received signal
 */
void signal_handler(int s)
{
    if (SIGTERM == s) {
        serve_state = EXIT;
    } else if (SIGHUP == s) {
        serve_state = RESTART;
    }
}

/**
 * Installs our signal handler
 */
void install_sh(void)
{
    if (signal(SIGTERM, signal_handler) == SIG_ERR) {
        log_and_exit("Can't catch signal SIGTERM\n");
    }

    if (signal(SIGHUP, signal_handler) == SIG_ERR) {
        log_and_exit("Can't catch signal SIGHUP\n");
    }
}

/**
 * If we are running as root, we will try to drop our privs. to our default
 * user.
 */
void drop_privileges(void)
{
    int err = 0;
    struct passwd *pw = NULL;

    pw = getpwnam(LSM_USER);
    if (pw) {
        if (!geteuid()) {

            if (-1 == setgid(pw->pw_gid)) {
                err = errno;
                log_and_exit("Unexpected error on setgid(errno %d)\n", err);
            }

            if (-1 == setgroups(1, &pw->pw_gid)) {
                err = errno;
                log_and_exit("Unexpected error on setgroups(errno %d)\n", err);
            }

            if (-1 == setuid(pw->pw_uid)) {
                err = errno;
                log_and_exit("Unexpected error on setuid(errno %d)\n", err);
            }
        } else if (pw->pw_uid != getuid()) {
            warn("Daemon not running as correct user\n");
        }
    } else {
        info("Warn: Missing %s user, running as existing user!\n", LSM_USER);
    }
}

/**
 * Check to make sure we have access to the directories of interest
 */
void flight_check(void)
{
    int err = 0;
    if (-1 == access(socket_dir, R_OK | W_OK)) {
        err = errno;
        log_and_exit("Unable to access socket directory %s, errno= %d\n",
                     socket_dir, err);
    }

    if (-1 == access(plugin_dir, R_OK | X_OK)) {
        err = errno;
        log_and_exit("Unable to access plug-in directory %s, errno= %d\n",
                     plugin_dir, err);
    }
}

/**
 * Print help.
 */
void usage(void)
{
    printf("libStorageMgmt plug-in daemon.\n");
    printf("lsmd [--plugindir <directory>] [--socketdir <dir>] [-v] [-d]\n");
    printf("     --plugindir = The directory where the plugins are located\n");
    printf("     --socketdir = The directory where the Unix domain sockets will "
                                "be created\n");
    printf("     --confdir   = The directory where the config files are "
                               "located\n");
    printf("     -v          = Verbose logging\n");
    printf("     -d          = New style daemon (systemd)\n");
}

/**
 * Concatenates a path and a file name.
 * @param path      Fully qualified path
 * @param name      File name
 * @return Concatenated string, caller must call free when done
 */
char *path_form(const char *path, const char *name)
{
    size_t s = strlen(path) + strlen(name) + 2;
    char *full = calloc(1, s);
    if (full) {
        snprintf(full, s, "%s/%s", path, name);
    } else {
        log_and_exit("malloc failure while trying to allocate %d bytes\n", s);
    }
    return full;
}

/* Call back signature */
typedef int (*file_op) (void *p, char *full_file_path);

/**
 * For a given directory iterate through each directory item and exec the
 * callback, recursively process nested directories too.
 * @param   dir         Directory to transverse
 * @param   p           Pointer to user data (Optional)
 * @param   call_back   Function to call against file
 * @return
 */
void process_directory(const char *dir, void *p, file_op call_back)
{
    int err = 0;

    if (call_back && dir && strlen(dir)) {
        DIR *dp = NULL;
        struct dirent *entry = NULL;
        char *full_name = NULL;
        dp = opendir(dir);

        if (dp) {
            while ((entry = readdir(dp)) != NULL) {
                struct stat entry_st;
                free(full_name);
                full_name = path_form(dir, entry->d_name);

                if (lstat(full_name, &entry_st) != 0) {
                    continue;
                }

                if (S_ISDIR(entry_st.st_mode)) {
                    if (strncmp(entry->d_name, ".", 1) == 0) {
                        continue;
                    }
                    process_directory(full_name, p, call_back);
                } else {
                    if (call_back(p, full_name)) {
                        break;
                    }
                }
            }

            free(full_name);

            if (closedir(dp)) {
                err = errno;
                log_and_exit("Error on closing dir %s: %s\n", dir,
                             strerror(err));
            }
        } else {
            err = errno;
            log_and_exit("Error on processing directory %s: %s\n", dir,
                         strerror(err));
        }
    }
}

/**
 * Callback to remove a unix domain socket by deleting it.
 * @param p             Call back data
 * @param full_name     Full path an and file name
 * @return 0 to continue processing, anything else to stop.
 */
int delete_socket(void *p, char *full_name)
{
    struct stat statbuf;
    int err;

    assert(p == NULL);

    if (!lstat(full_name, &statbuf)) {
        if (S_ISSOCK(statbuf.st_mode)) {
            if (unlink(full_name)) {
                err = errno;
                log_and_exit("Error on unlinking file %s: %s\n",
                             full_name, strerror(err));
            }
        }
    }
    return 0;
}

/**
 * Walk the IPC socket directory and remove the socket files.
 */
void clean_sockets(void)
{
    process_directory(socket_dir, NULL, delete_socket);
}


/**
 * Given a socket file name, create the IPC socket.
 * @param name     socket file name for plug-in
 * @return  listening socket descriptor for IPC
 */
int setup_socket(char *name)
{
    int err = 0;

    char *socket_file = path_form(socket_dir, name);
    delete_socket(NULL, socket_file);

    int fd = socket(AF_UNIX, SOCK_STREAM, 0);
    if (-1 != fd) {
        struct sockaddr_un addr;
        memset(&addr, 0, sizeof(addr));
        addr.sun_family = AF_UNIX;

        strncpy(addr.sun_path, socket_file, sizeof(addr.sun_path) - 1);

        if (-1 ==
            bind(fd, (struct sockaddr *) &addr, sizeof(struct sockaddr_un))) {
            err = errno;
            log_and_exit("Error on binding socket %s: %s\n", socket_file,
                         strerror(err));
        }

        if (-1 == chmod(socket_file, S_IREAD | S_IWRITE | S_IRGRP | S_IWGRP
                        | S_IROTH | S_IWOTH)) {
            err = errno;
            log_and_exit("Error on chmod socket file %s: %s\n", socket_file,
                         strerror(err));
        }

        if (-1 == listen(fd, 5)) {
            err = errno;
            log_and_exit("Error on listening %s: %s\n", socket_file,
                         strerror(err));
        }

    } else {
        err = errno;
        log_and_exit("Error on socket create %s: %s\n",
                     socket_file, strerror(err));
    }

    free(socket_file);
    return fd;
}

/**
 * Closes all the listening sockets and re-claims memory in linked list.
 * @param list
 */
void empty_plugin_list(struct plugin_list *list)
{
    int err;
    struct plugin *item = NULL;

    while (!LIST_EMPTY(list)) {
        item = LIST_FIRST(list);
        LIST_REMOVE(item, pointers);

        if (-1 == close(item->fd)) {
            err = errno;
            info("Error on closing fd %d for file %s: %s\n", item->fd,
                 item->file_path, strerror(err));
        }

        free(item->file_path);
        item->file_path = NULL;
        item->fd = INT_MAX;
        free(item);
    }
}

/**
 * Parse config and seeking provided key name bool
 *  1. Keep value untouched if file not exist
 *  2. If file is not readable, abort via log_and_exit()
 *  3. Keep value untouched if provided key not found
 *  4. Abort via log_and_exit() if no enough memory.
 * @param conf_path     config file path
 * @param key_name      string, searching key
 * @param value         int, output, value of this config key
 */

void parse_conf_bool(const char *conf_path, const char *key_name, int *value)
{
    if (access(conf_path, F_OK) == -1) {
        /* file not exist. */
        return;
    }
    config_t *cfg = (config_t *) malloc(sizeof(config_t));
    if (cfg) {
        config_init(cfg);
        if (CONFIG_TRUE == config_read_file(cfg, conf_path)) {
            config_lookup_bool(cfg, key_name, value);
        } else {
            log_and_exit("configure %s parsing failed: %s at line %d\n",
                         conf_path, config_error_text(cfg),
                         config_error_line(cfg));
        }
    } else {
        log_and_exit
            ("malloc failure while trying to allocate memory for config_t\n");
    }

    config_destroy(cfg);
    free(cfg);
}

/**
 * Load plugin config for root privilege setting.
 * If config not found, return 0 for no root privilege required.
 * @param plugin_name plugin name.
 * @return 1 for require root privilege, 0 or not.
 */

int chk_pconf_root_pri(char *plugin_name)
{
    int require_root = 0;
    size_t plugin_name_len = strlen(plugin_name);
    size_t conf_ext_len = strlen(plugin_conf_extension);
    ssize_t conf_file_name_len = plugin_name_len  + conf_ext_len + 1;
    char *plugin_conf_filename = (char *) malloc(conf_file_name_len);

    if (plugin_conf_filename) {
        snprintf(plugin_conf_filename, conf_file_name_len, "%s%s", plugin_name,
                 plugin_conf_extension);

        char *plugin_conf_dir_path = path_form(conf_dir,
                                               LSM_PLUGIN_CONF_DIR_NAME);

        char *plugin_conf_path = path_form(plugin_conf_dir_path,
                                           plugin_conf_filename);
        parse_conf_bool(plugin_conf_path, LSM_CONF_REQUIRE_ROOT_OPT_NAME,
                        &require_root);

        if (require_root == 1 && allow_root_plugin == 0) {
            warn("Plugin %s require root privilege while %s disable globally\n",
                 plugin_name, LSMD_CONF_FILE);
        }
        free(plugin_conf_dir_path);
        free(plugin_conf_filename);
        free(plugin_conf_path);
    } else {
        log_and_exit("malloc failure while trying to allocate %d "
                     "bytes\n", conf_file_name_len);
    }
    return require_root;
}

/**
 * Call back for plug-in processing.
 * @param p             Private data
 * @param full_name     Full path and file name
 * @return 0 to continue, else abort directory processing
 */
int process_plugin(void *p, char *full_name)
{
    char * base_nm = NULL;
    size_t base_nm_len = 0;
    size_t no_ext_len = 0;
    char plugin_name[128];
    size_t ext_len = strlen(plugin_extension);
    size_t plugin_name_max_len = sizeof(plugin_name)/sizeof(char);

    if (full_name == NULL)
        return 0;

    base_nm = basename(full_name);
    base_nm_len = strlen(base_nm);

    if (base_nm_len <= ext_len)
        return 0;

    if (strncmp(base_nm + base_nm_len - ext_len, plugin_extension, ext_len))
        return 0;

    struct plugin *item = calloc(1, sizeof(struct plugin));
    if (item == NULL) {
        log_and_exit("Memory allocation failure!\n");
        return 0; // no use, just trick covscan;
    }

    /* Strip off _lsmplugin from the file name, not sure
     * why I chose to do this */
    memset(plugin_name, 0, plugin_name_max_len);
    strncpy(plugin_name, base_nm, plugin_name_max_len - 1);
    no_ext_len = base_nm_len - ext_len;
    // Already check, no_ext_len is bigger than 0 here.
    if (no_ext_len < plugin_name_max_len - 1)
        plugin_name[no_ext_len] = '\0';

    item->file_path = strdup(full_name);
    item->fd = setup_socket(plugin_name);
    item->require_root = chk_pconf_root_pri(plugin_name);
    has_root_plugin |= item->require_root;

    if (item->file_path && item->fd >= 0) {
        LIST_INSERT_HEAD((struct plugin_list *) p, item,
                         pointers);
        info("Plugin %s added\n", full_name);
    } else {
        /* The only real way to get here is failed strdup as
           setup_socket will exit on error. */
        free(item);
        item = NULL;
        log_and_exit("strdup failed %s\n", full_name);
    }
    return 0;
}

/**
 * Cleans up any children that have exited.
 */
void child_cleanup(void)
{
    int rc;
    int err;

    do {
        siginfo_t si;
        memset(&si, 0, sizeof(siginfo_t));

        rc = waitid(P_ALL, 0, &si, WNOHANG | WEXITED);

        if (-1 == rc) {
            err = errno;
            if (err != ECHILD) {
                info("waitid %d - %s\n", err, strerror(err));
            }
            break;
        } else {
            if (0 == rc && si.si_pid == 0) {
                break;
            } else {
                if (si.si_code == CLD_EXITED && si.si_status != 0) {
                    info("Plug-in process %d exited with %d\n", si.si_pid,
                         si.si_status);
                }
            }
        }
    } while (1);
}

/**
 * Closes and frees memory and removes Unix domain sockets.
 */
void clean_up(void)
{
    empty_plugin_list(&head);
    clean_sockets();
}

/**
 * Walks the plugin directory creating IPC sockets for each one.
 * @return
 */
int process_plugins(void)
{
    clean_up();
    info("Scanning plug-in directory %s\n", plugin_dir);
    process_directory(plugin_dir, &head, process_plugin);
    if (allow_root_plugin == 1 && has_root_plugin == 0) {
        info("No plugin requires root privilege, dropping root privilege\n");
        flight_check();
        drop_privileges();
    }
    return 0;
}

/**
 * Given a socket descriptor looks it up and returns the plug-in
 * @param fd        Socket descriptor to lookup
 * @return struct plugin
 */
struct plugin *plugin_lookup(int fd)
{
    struct plugin *plug = NULL;
    LIST_FOREACH(plug, &head, pointers) {
        if (plug->fd == fd) {
            return plug;
        }
    }
    return NULL;
}

/**
 * Does the actual fork and exec of the plug-in
 * @param plugin        Full filename and path of plug-in to exec.
 * @param client_fd     Client connected file descriptor
 * @param require_root  int, indicate whether this plugin require root
 *                      privilege or not
 */
void exec_plugin(char *plugin, int client_fd, int require_root)
{
    int err = 0;

    info("Exec'ing plug-in = %s\n", plugin);

    pid_t process = fork();
    if (process) {
        /* Parent */
        int rc = close(client_fd);
        if (-1 == rc) {
            err = errno;
            info("Error on closing accepted socket in parent: %s\n",
                 strerror(err));
        }

    } else {
        /* Child */
        int exec_rc = 0;
        char fd_str[12];
        const char *plugin_argv[7];
        extern char **environ;
        struct ucred cli_user_cred;
        socklen_t cli_user_cred_len = sizeof(cli_user_cred);

        /*
         * The plugin will still run no matter with root privilege or not.
         * so that client could get detailed error message.
         */
        if (require_root == 0) {
            drop_privileges();
        } else {
            if (getuid()) {
                warn("Plugin %s require root privilege, but lsmd daemon "
                     "is not run as root user\n", plugin);
            } else if (allow_root_plugin == 0) {
                warn("Plugin %s require root privilege, but %s disabled "
                     "it globally\n", LSMD_CONF_FILE);
                drop_privileges();
            } else {
                /* Check socket client uid */
                int rc_get_cli_uid =
                    getsockopt(client_fd, SOL_SOCKET, SO_PEERCRED,
                               &cli_user_cred, &cli_user_cred_len);
                if (0 == rc_get_cli_uid) {
                    if (cli_user_cred.uid != 0) {
                        warn("Plugin %s require root privilege, but "
                             "client is not run as root user\n", plugin);
                        drop_privileges();
                    } else {
                        info("Plugin %s is running as root privilege\n",
                             plugin);
                    }
                } else {
                    warn("Failed to get client socket uid, getsockopt() "
                         "error: %d\n", errno);
                    drop_privileges();
                }
            }

        }

        /* Make copy of plug-in string as once we call empty_plugin_list it
         * will be deleted :-) */
        char *p_copy = strdup(plugin);

        empty_plugin_list(&head);
        sprintf(fd_str, "%d", client_fd);

        if (plugin_mem_debug) {
            char debug_out[64];
            snprintf(debug_out, (sizeof(debug_out) - 1),
                     "--log-file=/tmp/leaking_%d-%d", getppid(), getpid());

            plugin_argv[0] = "valgrind";
            plugin_argv[1] = "--leak-check=full";
            plugin_argv[2] = "--show-reachable=no";
            plugin_argv[3] = debug_out;
            plugin_argv[4] = p_copy;
            plugin_argv[5] = fd_str;
            plugin_argv[6] = NULL;

            exec_rc = execve("/usr/bin/valgrind", (char * const*) plugin_argv,
                             environ);
        } else {
            plugin_argv[0] = basename(p_copy);
            plugin_argv[1] = fd_str;
            plugin_argv[2] = NULL;
            exec_rc = execve(p_copy, (char * const*) plugin_argv, environ);
        }

        if (-1 == exec_rc) {
            err = errno;
            log_and_exit("Error on exec'ing Plugin %s: %s\n",
                         p_copy, strerror(err));
        }
    }
}

/**
 * Main event loop
 */
void _serving(void)
{
    struct plugin *plug = NULL;
    struct timeval tmo;
    fd_set readfds;
    int nfds = 0;
    int err = 0;

    process_plugins();

    while (serve_state == RUNNING) {
        FD_ZERO(&readfds);
        nfds = 0;

        tmo.tv_sec = 15;
        tmo.tv_usec = 0;

        LIST_FOREACH(plug, &head, pointers) {
            nfds = max(plug->fd, nfds);
            FD_SET(plug->fd, &readfds);
        }

        if (!nfds) {
            log_and_exit("No plugins found in directory %s\n", plugin_dir);
        }

        nfds += 1;
        int ready = select(nfds, &readfds, NULL, NULL, &tmo);

        if (-1 == ready) {
            if (serve_state != RUNNING) {
                return;
            } else {
                err = errno;
                log_and_exit("Error on selecting Plugin: %s", strerror(err));
            }
        } else if (ready > 0) {
            int fd = 0;
            for (fd = 0; fd < nfds; fd++) {
                if (FD_ISSET(fd, &readfds)) {
                    int cfd = accept(fd, NULL, NULL);
                    if (-1 != cfd) {
                        struct plugin *p = plugin_lookup(fd);
                        exec_plugin(p->file_path, cfd, p->require_root);
                    } else {
                        err = errno;
                        info("Error on accepting request: %s", strerror(err));
                    }
                }
            }
        }
        child_cleanup();
    }
    clean_up();
}

/**
 * Main entry for daemon to work
 */
void serve(void)
{
    while (serve_state != EXIT) {
        if (serve_state == RESTART) {
            info("Reloading plug-ins\n");
            serve_state = RUNNING;
        }
        _serving();
    }
    clean_up();
}

int main(int argc, char *argv[])
{
    int c = 0;

    LIST_INIT(&head);

    /* Process command line arguments */
    while (1) {
        static struct option l_options[] = {
            {"help", no_argument, 0, 'h'},  //Index 0
            {"plugindir", required_argument, 0, 0}, //Index 1
            {"socketdir", required_argument, 0, 0}, //Index 2
            {"confdir", required_argument, 0, 0},   //Index 3
            {0, 0, 0, 0}
        };

        int option_index = 0;
        c = getopt_long(argc, argv, "hvd", l_options, &option_index);

        if (c == -1) {
            break;
        }

        switch (c) {
        case 0:
            switch (option_index) {
            case 1:
                plugin_dir = optarg;
                break;
            case 2:
                socket_dir = optarg;
                break;
            case 3:
                conf_dir = optarg;
                break;
            }
            break;

        case 'h':
            usage();
            return EXIT_SUCCESS;

        case 'v':
            verbose_flag = 1;
            break;

        case 'd':
            systemd = 1;
            break;

        case '?':
            break;

        default:
            abort();
        }
    }

    /* Print any remaining command line arguments (not options). */
    if (optind < argc) {
        printf("non-option ARGV-elements: ");
        while (optind < argc) {
            printf("%s \n", argv[optind++]);
        }
        printf("\n");
        exit(1);
    }

    /* Setup syslog if needed */
    if (!systemd) {
        openlog("lsmd", LOG_ODELAY, LOG_USER);
    }

    /* Check lsmd.conf */
    char *lsmd_conf_path = path_form(conf_dir, LSMD_CONF_FILE);
    parse_conf_bool(lsmd_conf_path, (char *) LSM_CONF_ALLOW_ROOT_OPT_NAME,
                    &allow_root_plugin);
    free(lsmd_conf_path);

    /* Check to see if we want to check plugin for memory errors */
    if (getenv("LSM_VALGRIND")) {
        plugin_mem_debug = 1;
    }

    install_sh();
    if (allow_root_plugin == 0) {
        drop_privileges();
    }
    flight_check();

    /* Become a daemon if told we are not using systemd */
    if (!systemd) {
        if (-1 == daemon(0, 0)) {
            int err = errno;
            log_and_exit("Error on calling daemon: %s\n", strerror(err));
        }
    }

    serve();
    return EXIT_SUCCESS;
}
