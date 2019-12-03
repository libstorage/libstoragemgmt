/*
 * Scan a SCSI target given a uevent path to one of its devices
 * Author: Ewan D. Milne <emilne@redhat.com>
 *
 * Copyright (C) 2013, Red Hat Inc.
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU
 * General Public License for more details.
 */

#include <stdio.h>
#include <unistd.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <errno.h>
#include <getopt.h>

/*
 * Example SCSI uevent device path:
 *
 * /devices/pseudo_0/adapter0/host3/target3:0:0/3:0:0:0
 *
 * Desired sysfs action:
 *
 * write "<channel> <id> -" to "/sys/devices/pseudo_0/adapter0/host3/scsi_host/host3/scan"
 *
 * Note:  Per kernel Documentation/sysfs-rules.txt, sysfs is always mounted at /sys
 */

static void __attribute__ ((__noreturn__)) usage(char **argv, int err)
{
	fprintf(stderr, "\nUsage:\n");
	fprintf(stderr, "%s <uevent DEVPATH of SCSI device>\n", argv[0]);
	fprintf(stderr, "\nOptions:\n");
	fprintf(stderr, "  -h, --help     display this help and exit\n");
	exit(err);
}

static void __attribute__ ((__noreturn__)) invalid(char **argv, char *devpath)
{
	fprintf(stderr, "Invalid DEVPATH '%s'.\n", devpath);
	usage(argv, 1);
}

int main(int argc, char **argv)
{
	int	c;
	char	*devpath;

	char	*sysfs_path;
	char	*sysfs_data;
	struct stat	sysfs_stat;
	int	fd;

	char	*host_str;
	int	host_pos;
	size_t	host_len;
	char	*host_next_str;
	int	host_next_pos;
	size_t	host_next_len;
	char	*target_str;
	int	target_pos;
	size_t	target_len;

	char	*channel_str;
	int	channel_pos;
	int	channel_len;
	char	*id_str;
	int	id_pos;
	int	id_len;

	char	*dir_str;

	static const struct option longopts[] = {
		{"help", no_argument, 0, 'h'},
		{NULL, no_argument, 0, '0'},
	};


	while ((c = getopt_long(argc, argv, "rh", longopts, NULL)) != -1) {
		switch (c) {
		case 'h':
			usage(argv, 0);
		default:
			usage(argv, 1);
		}
	}

	if (optind >= argc) {
		usage(argv, 1);
	}
	devpath = argv[optind++];

	/*
	 * Make sure SCSI device uevent DEVPATH was supplied, and that it exists.
	 * Also verify that it is a directory, to provide some argument validation.
	 * Note:  the devpath does not include the "/sys" prefix, so we must add it.
	 */
	if (devpath == NULL) {
		usage(argv, 1);
	}

	sysfs_path = malloc(strlen("/sys") + strlen(devpath) + 1);
	strcpy(sysfs_path, "/sys");
	strcat(sysfs_path, devpath);

	if (stat(sysfs_path, &sysfs_stat) < 0) {
		fprintf(stderr, "Cannot stat '%s': %s\n", sysfs_path, strerror(errno));
		usage(argv, 1);
	}
	if (!S_ISDIR(sysfs_stat.st_mode))
		invalid(argv, devpath);

	free(sysfs_path);

	/*
	 * Construct the path to the "scan" entry in the Scsi_Host sysfs object.
	 */
	if ((host_str = strstr(devpath, "/host")) == NULL)
		invalid(argv, devpath);
	host_pos = strlen(devpath) - strlen(host_str);

	if ((host_next_str = strstr(&devpath[host_pos + 1], "/")) == NULL)
		invalid(argv, devpath);
	host_next_pos = strlen(devpath) - strlen(host_next_str);

	if ((target_str = strstr(devpath, "/target")) == NULL)
		invalid(argv, devpath);
	target_pos = strlen(devpath) - strlen(target_str);

	host_len = host_next_pos - host_pos;
	if (host_len <= strlen("/host"))
		invalid(argv, devpath);

	host_next_len = strlen(&devpath[host_next_pos]);
	if (host_next_len <= strlen("/"))
		invalid(argv, devpath);

	target_len = strlen(&devpath[target_pos]);
	if (target_len <= strlen("/target"))
		invalid(argv, devpath);

	sysfs_path = malloc(strlen("/sys") + strlen(devpath) - host_next_len + strlen("/scsi_host") + host_len + strlen("/scan") + 1);

	strcpy(sysfs_path, "/sys");
	strncat(sysfs_path, devpath, host_next_pos);
	strcat(sysfs_path, "/scsi_host");
	snprintf(sysfs_path + strlen(sysfs_path), host_len + 1, "%s", host_str);
	strcat(sysfs_path, "/scan");

	/*
	 * Obtain the SCSI channel and ID, and construct the string to write to the "scan" entry.
	 */
	if ((channel_str = strstr(&devpath[target_pos], ":")) == NULL)
		invalid(argv, devpath);
	channel_pos = strlen(&devpath[target_pos]) - strlen(channel_str) + 1;

	if ((id_str = strstr(&devpath[target_pos + channel_pos], ":")) == NULL)
		invalid(argv, devpath);
	id_pos = strlen(&devpath[target_pos + channel_pos]) - strlen(id_str) + 1;

	if ((dir_str = strstr(&devpath[target_pos + channel_pos + id_pos], "/")) == NULL)
		invalid(argv, devpath);

	channel_len = strlen(&devpath[target_pos + channel_pos]) - strlen(id_str);
	if (channel_len < 1)
		invalid(argv, devpath);

	id_len = strlen(&devpath[target_pos + channel_pos + id_pos]) - strlen(dir_str);
	if (id_len < 1)
		invalid(argv, devpath);

	sysfs_data = malloc(channel_len + strlen(" ") + id_len + strlen(" -") + 1);

	sysfs_data[0] = '\0';
	strncat(sysfs_data, &devpath[target_pos + channel_pos], channel_len);
	strcat(sysfs_data, " ");
	strncat(sysfs_data, &devpath[target_pos + channel_pos + id_pos], id_len);
	strcat(sysfs_data, " -");

	/*
	 * Tell the kernel to rescan the SCSI target for new LUNs.
	 */
	if ((fd = open(sysfs_path, O_WRONLY)) < 0) {
		fprintf(stderr, "Cannot open '%s': %s\n", sysfs_path, strerror(errno));
		usage(argv, 1);
	}
	if (write(fd, sysfs_data, strlen(sysfs_data)) < 0) {
		fprintf(stderr, "Cannot write '%s': %s\n", sysfs_path, strerror(errno));
		usage(argv, 1);
	}
	close(fd);
	free(sysfs_path);
	free(sysfs_data);
	return 0;
}
