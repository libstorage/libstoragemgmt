/*
 * Copyright (C) 2011-2013 Red Hat, Inc.
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
 * License along with this library; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301 USA
 *
 * Author: tasleson
 */

#include <stdio.h>
#include <stdlib.h>
#include <getopt.h>
#define _GNU_SOURCE
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

#define BASE_DIR  "/var/run/lsm"
#define SOCKET_DIR BASE_DIR"/ipc"
#define PLUGIN_DIR "/usr/bin"
#define LSM_USER "libstoragemgmt"

#define min(a,b) \
   ({ __typeof__ (a) _a = (a); \
       __typeof__ (b) _b = (b); \
     _a < _b ? _a : _b; })

#define max(a,b) \
   ({ __typeof__ (a) _a = (a); \
       __typeof__ (b) _b = (b); \
     _a > _b ? _a : _b; })

int verbose_flag = 0;
int systemd = 0;

char *socket_dir = SOCKET_DIR;
char *plugin_dir = PLUGIN_DIR;

char plugin_extension[] = "_lsmplugin";

typedef enum { RUNNING, RESTART, EXIT } serve_type;
serve_type serve_state = RUNNING;

/**
 * Each item in plugin list contains this information
 */
struct plugin {
    char *file_path;
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

    if( (LOG_INFO == severity && verbose_flag) || LOG_ERR == LOG_WARNING
            || LOG_ERR == severity) {
        va_list arg;
        va_start(arg, fmt);
        vsnprintf(buf, sizeof(buf), fmt, arg);
        va_end(arg);

        if( !systemd ) {
            syslog(LOG_ERR, "%s", buf);
        } else {
            fprintf(stdout, "%s", buf);
        }

        if( LOG_ERR == severity) {
            exit(1);
        }
    }
}
#define loud(fmt, ...)  logger(LOG_ERR, fmt, ##__VA_ARGS__)
#define warn(fmt, ...)  logger(LOG_WARNING, fmt, ##__VA_ARGS__)
#define info(fmt, ...)  logger(LOG_INFO, fmt, ##__VA_ARGS__)

/**
 * Our signal handler.
 * @param s     Received signal
 */
void signal_handler(int s)
{
    if( SIGTERM == s) {
        serve_state = EXIT;
    } else if( SIGHUP == s ) {
        serve_state = RESTART;
    }
}

/**
 * Installs our signal handler
 */
void install_sh(void)
{
    signal(SIGTERM, signal_handler);
    signal(SIGHUP, signal_handler);
}

/**
 * If we are running as root, we will try to drop our privs. to our default
 * user.
 */
void drop_privileges(void)
{
    int err = 0;
    struct passwd *pw = NULL;

    if( !systemd ) {
        pw = getpwnam(LSM_USER);
        if( pw ) {
            if ( !geteuid() ) {

                if( -1 == setgid(pw->pw_gid) ) {
                    err = errno;
                    loud("Unexpected error on setgid(errno %d)\n", err);
                }

                if( -1 == setuid(pw->pw_uid) ) {
                    err = errno;
                    loud("Unexpected error on setuid(errno %d)\n", err);
                }
            } else if ( pw->pw_uid != getuid() ) {
                warn("Daemon not running as correct user\n");
            }
        } else {
            info("Warn: Missing %s user, running as existing user!\n",
                LSM_USER);
        }
    }
}

/**
 * Check to make sure we have access to the directories of interest
 */
void flight_check(void)
{
    int err = 0;
    if( -1 == access(socket_dir, R_OK|W_OK )) {
        err = errno;
        loud("Unable to access socket directory %s, errno= %d\n",
                socket_dir, err);
    }

    if( -1 == access(plugin_dir, R_OK|X_OK)) {
        err = errno;
        loud("Unable to access plug-in directory %s, errno= %d\n",
                plugin_dir, err);
    }
}

/**
 * Print help.
 */
void usage(void)
{
    printf("libStorageMgmt plug-in daemon.\n");
    printf("lsmd --plugindir <directory> --socketdir <dir> -v -d\n");
    printf("    --plugindir = The directory where the plugins are located\n");
    printf("    --socketdir = The directory where the Unix domain sockets will "
                                "be created\n");
    printf("    -v          = Verbose logging\n");
    printf("    -d          = new style daemon (systemd)\n");
}

/**
 * Concatenates a path and a file name.
 * @param path      Fully qualified path
 * @param name      File name
 * @return Concatenated string, caller must call free when done
 */
char *path_form(const char* path, const char *name)
{
    size_t s = strlen(path) + strlen(name) + 2;
    char *full = malloc(s);
    if( full ) {
        snprintf(full, s, "%s/%s", path, name);
    } else {
        loud("malloc failure while trying to allocate %d bytes\n", s);
    }
    return full;
}

/* Call back signature */
typedef int (*file_op)(void *p, char *full_file_path);

/**
 * For a given directory iterate through each directory item and exec the
 * callback.
 * @param   dir     Directory to transverse
 * @param   p       Pointer to user data (Optional)
 * @param   file_op Function to call against file
 * @return
 */
void process_directory( char *dir, void *p, file_op call_back)
{
    int err = 0;

    if( call_back && dir && strlen(dir) ) {
        DIR *dp = NULL;
        dp = opendir(dir);
        struct dirent *entry = NULL;

        if( dp ) {
            while(( entry = readdir(dp)) != NULL) {
                char *full_name = path_form(dir, entry->d_name);
                int result = call_back(p, full_name);
                free(full_name);

                if( result ) {
                    break;
                }
            }

            if( closedir(dp) ) {
                err = errno;
                loud("closedir %s, error %s\n", socket_dir, strerror(err));
            }
        } else {
            err = errno;
            loud("Error processing socket directory %s, error %s\n", socket_dir,
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

    assert(p==NULL);

    if( !lstat(full_name, &statbuf) ) {
        if( S_ISSOCK(statbuf.st_mode)) {
            if( unlink(full_name) ) {
                err = errno;
                loud("Error unlinking file %s, error %s\n",
                        full_name, strerror(err));
            }
        }
    }
    return 0;
}

/**
 * Walk the IPC socket directory and remove the socket files.
 */
void clean_sockets()
{
    process_directory(socket_dir, NULL, delete_socket);
}


/**
 * Given a fully qualified path and name to a plug-in, create the IPC socket.
 * @param full_name     Full name and path for plug-in
 * @return  listening socket descriptor for IPC
 */
int setup_socket(char *full_name)
{
    int err = 0;
    char name[128];

    /* Strip off _lsmplugin from the file name, not sure
     * why I chose to do this */
    memset(name, 0, sizeof(name));

    char *base_nm = basename(full_name);
    strncpy(name, base_nm, min(abs(strlen(base_nm) - 10), (sizeof(name)-1)));

    char *socket_file = path_form(socket_dir, name);
    delete_socket(NULL, socket_file);

    int fd = socket(AF_UNIX, SOCK_STREAM, 0);
    if( -1 != fd ) {
        struct sockaddr_un addr;
        memset(&addr, 0, sizeof(addr));
        addr.sun_family = AF_UNIX;

        strncpy(addr.sun_path, socket_file, sizeof(addr.sun_path) - 1);

        if( -1 == bind(fd, (struct sockaddr *)&addr, sizeof(struct sockaddr_un))) {
            err = errno;
            loud("bind %s, error %s\n", socket_file, strerror(err));
        }

        if( -1 == chmod(socket_file, S_IREAD|S_IWRITE|S_IRGRP|S_IWGRP
                                    |S_IROTH|S_IWOTH)) {
            err = errno;
            loud("chmod %s, error %s\n", socket_file, strerror(err));
        }

        if( -1 == listen(fd, 5)) {
            err = errno;
            loud("listen %s, error %s\n", socket_file, strerror(err));
        }

    } else {
        err = errno;
        loud("Error unlinking file %s, error %s\n",
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

    while(!LIST_EMPTY(list))
    {
        item = LIST_FIRST(list);
        LIST_REMOVE(item, pointers);

        if( -1 == close(item->fd) ) {
            err = errno;
            info("Error on closing fd %d for %s, error= %s\n", item->fd,
                    item->file_path, strerror(err));
        }

        free(item->file_path);
        item->file_path = NULL;
        item->fd = -1;
        free(item);
    }
}

/**
 * Call back for plug-in processing.
 * @param p             Private data
 * @param full_name     Full path and file name
 * @return 0 to continue, else abort directory processing
 */
int process_plugin(void *p, char *full_name)
{
    if( full_name ) {
        size_t ext_len = strlen(plugin_extension);
        size_t full_len = strlen(full_name);

        if( full_len > ext_len) {
            if( strncmp(full_name + full_len - ext_len, plugin_extension, ext_len) == 0) {
                struct plugin *item = malloc(sizeof(struct plugin));
                if( item ) {
                    item->file_path = strdup(full_name);
                    item->fd = setup_socket(full_name);

                    if( item->file_path && item->fd >= 0 ) {
                        LIST_INSERT_HEAD((struct plugin_list*)p, item, pointers);
                        info("Plugin %s added\n", full_name);
                    } else {
                        loud("strdup failed %s\n", item->file_path);
                    }
                } else {
                    loud("Memory allocation failure!\n");
                }
            }
        }
    }
    return 0;
}

/**
 * Cleans up any children that have existed.
 */
void child_cleanup(void)
{
    int rc;

    do {
        siginfo_t si;
        memset(&si, 0, sizeof(siginfo_t));

        rc = waitid(P_ALL, 0, &si, WNOHANG|WEXITED);

        if( rc > 0 ) {
            if( si.si_code == CLD_EXITED && si.si_status != 0 ) {
                info("Plug-in process %d exited with %d\n", rc, si.si_status);
            }
        } else {
            break;
        }
    } while(1);
}

/**
 * Closes and frees memory and removes Unix domain sockets.
 */
void clean_up(void )
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
    return 0;
}

/**
 * Given a socket descriptor looks it up and returns the plug-in
 * @param fd        Socket descriptor to lookup
 * @return Character string
 */
char *plugin_lookup(int fd)
{
    struct plugin *plug = NULL;
    LIST_FOREACH(plug, &head, pointers) {
        if( plug->fd == fd ) {
            return plug->file_path;
        }
    }
    return NULL;
}

/**
 * Does the actual fork and exec of the plug-in
 * @param plugin        Full filename and path of plug-in to exec.
 * @param client_fd     Client connected file descriptor
 */
void exec_plugin( char *plugin, int client_fd )
{
    int err = 0;

    info("Exec'ing plug-in = %s\n", plugin);

    pid_t process = fork();
    if( process ) {
        /* Parent */
        int rc = close(client_fd);
        if( -1 == rc ) {
            err = errno;
            info("Error on accepted socket being closed in parent %s\n",
                     strerror(err));
        }

    } else {
        /* Child */
        char fd_str[12];
        char *plugin_argv[3];

        /* Make copy of plug-in string as once we call empty_plugin_list it
         * will be deleted :-) */
        char *p_copy = strdup(plugin);

        empty_plugin_list(&head);
        sprintf(fd_str, "%d", client_fd);

        plugin_argv[0] = basename(p_copy);
        plugin_argv[1] = fd_str;
        plugin_argv[2] = NULL;
        extern char **environ;

        if( -1 == execve(p_copy, plugin_argv, environ)) {
            int err = errno;
            loud("Plugin failed to exececute %s, error %s\n",
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

    while( serve_state == RUNNING ) {
        FD_ZERO(&readfds);
        nfds = 0;

        tmo.tv_sec = 15;
        tmo.tv_usec = 0;

        LIST_FOREACH(plug, &head, pointers) {
            nfds = max(plug->fd, nfds);
            FD_SET(plug->fd, &readfds);
        }

        if( !nfds ) {
            loud("No plugins found in directory %s\n", plugin_dir);
        }

        nfds += 1;
        int ready = select(nfds, &readfds, NULL, NULL, &tmo);

        if( -1 == ready ) {
            if( serve_state != RUNNING ) {
                return;
            } else {
                err = errno;
                loud("Error on select: %s", strerror(err));
            }
        } else if( ready > 0 ) {
            int fd = 0;
            for( fd = 0; fd < nfds; fd++ ) {
                if( FD_ISSET(fd, &readfds) ) {
                    int cfd = accept(fd, NULL, NULL);
                    if( -1 != cfd ) {
                        char *p = plugin_lookup(fd);
                        exec_plugin(p, cfd);
                    } else {
                        err = errno;
                        info("Error on accept %s", strerror(err));
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
    while( serve_state != EXIT ) {
        if( serve_state == RESTART ) {
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
    while(1) {
        static struct option l_options[] =
        {
            {"help", no_argument, 0, 'h'},                  //Index 0
            {"plugindir", required_argument, 0, 0},         //Index 1
            {"socketdir", required_argument, 0, 0},         //Index 2
            {0, 0, 0, 0}
        };

        int option_index = 0;
        c = getopt_long(argc, argv, "hvd", l_options, &option_index);

        if ( c == -1 ) {
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
               }
               break;

             case 'h':
                usage();
                break;

             case 'v':
                verbose_flag = 1;
                break;

             case 'd':
                systemd = 1;
                break;

             case '?':
               break;

             default:
               abort ();
             }
         }

    /* Print any remaining command line arguments (not options). */
    if (optind < argc) {
        printf ("non-option ARGV-elements: ");
        while (optind < argc) {
            printf ("%s \n", argv[optind++]);
        }
        printf("\n");
        exit(1);
    }

    /* Setup syslog if needed */
    if( !systemd ) {
        openlog("lsmd", LOG_ODELAY, LOG_USER);
    }

    install_sh();
    drop_privileges();
    flight_check();

    /* Become a daemon if told we are not using systemd */
    if( !systemd ) {
        if ( -1 == daemon(0, 0) ) {
            int err = errno;
            loud("daemon call failed, errno %d\n", err);
        }
    }

    serve();
    return EXIT_SUCCESS;
}