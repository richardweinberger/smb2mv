// SPDX-License-Identifier: GPL-2.0+
/*
 * smb2mv - A simple mv-alike tool to perform server side file move on SMB2 shares.
 * (c) 2024 - Richard Weinberger <richard@nod.at>
 */
/*
 * Inspired by xfstests cloner.c:
 *  Tiny program to perform file (range) clones using raw Btrfs and CIFS ioctls.
 *  Copyright (C) 2014 SUSE Linux Products GmbH. All Rights Reserved.
 */

#include <errno.h>
#include <fcntl.h>
#include <libgen.h>
#include <linux/magic.h>
#include <stdarg.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/vfs.h>
#include <unistd.h>

#ifndef CIFS_SUPER_MAGIC
#define CIFS_SUPER_MAGIC 0xFF534D42
#endif
#ifndef SMB2_SUPER_MAGIC
#define SMB2_SUPER_MAGIC 0xFE534D42
#endif
#define CIFS_IOCTL_MAGIC 0xCF
#define CIFS_IOC_COPYCHUNK_FILE _IOW(CIFS_IOCTL_MAGIC, 3, int)

static int xasprintf(char **strp, const char *fmt, ...)
{
	va_list ap;
	int n;

	va_start(ap, fmt);
	n = vasprintf(strp, fmt, ap);
	va_end(ap);

	if (n == -1) {
		fprintf(stderr, "out of memory!\n");
		abort();
	}

	return n;
}

static char *xstrdup(const char *s)
{
	char *d = strdup(s);

	if (d == NULL) {
		fprintf(stderr, "out of memory!\n");
		abort();
	}

	return d;
}

static int clone_file_cifs(int src_fd, int dst_fd)
{
	int ret;

	ret = ioctl(dst_fd, CIFS_IOC_COPYCHUNK_FILE, src_fd);
	if (ret) {
		ret = -1;
		fprintf(stderr, "server-side-copy failed: %m\n");
	}

	return ret;
}

static bool check_is_cifs(int src_fd, int dest_fd)
{
	struct statfs sfs;
	int ret;

	ret = fstatfs(src_fd, &sfs);
	if (ret == -1) {
		fprintf(stderr, "failed to stat source file system: %m\n");
		return false;
	}

	if (sfs.f_type != CIFS_SUPER_MAGIC && sfs.f_type != SMB2_SUPER_MAGIC) {
		fprintf(stderr, "source file system is not CIFS/SMB2!\n");
		return false;
	}

	ret = fstatfs(dest_fd, &sfs);
	if (ret != 0) {
		fprintf(stderr, "failed to stat destination file system: %m\n");
		return false;
	}

	if (sfs.f_type != CIFS_SUPER_MAGIC && sfs.f_type != SMB2_SUPER_MAGIC) {
		fprintf(stderr, "destination file system is not CIFS/SMB2!\n");
		return false;
	}

	return true;
}

static int open_dest(char *src, char *dst)
{
	struct stat st;
	int ret = -1;

	ret = stat(dst, &st);
	if (ret == -1) {
		if (errno == ENOENT) {
			ret = open(dst, O_CREAT | O_WRONLY, 0644);
			if (ret != -1) {
				// All good, file created
				goto out;
			}
		}

		// Unable to stat or create
		fprintf(stderr, "failed to create %s: %m\n", dst);
	} else {
		if ((st.st_mode & S_IFMT) == S_IFDIR) {
			const char *fname = basename(src);
			char *new_dst;

			xasprintf(&new_dst, "%s/%s", dst, fname);
			ret = open(new_dst, O_CREAT | O_WRONLY, 0644);
			free(new_dst);

			if (ret == -1) {
				fprintf(stderr, "failed to create %s: %m\n", new_dst);
			}
		} else {
			//file exists or is something else, we don't overwrite
			fprintf(stderr, "refusing to overwrite %s\n", dst);
			ret = -1;
		}
	}

out:
	return ret;
}

int main(int argc, char **argv)
{
	char *src_file = NULL, *dst_file = NULL;
	int src_fd, dst_fd, ret;

	if (argc != 3) {
		fprintf(stderr, "Usage: %s SRC DST\n", argv[0]);
		goto err_out;
	}

	src_file = xstrdup(argv[1]);
	dst_file = xstrdup(argv[2]);

	src_fd = open(src_file, O_RDWR);
	if (src_fd == -1) {
		ret = 1;
		fprintf(stderr, "failed to open %s: %m\n", src_file);
		goto err_out;
	}

	dst_fd = open_dest(src_file, dst_file);
	if (dst_fd == -1) {
		ret = 1;
		goto err_src_close;
	}

	if (!check_is_cifs(src_fd, dst_fd)) {
		ret = 1;
		goto err_dst_close;
	}

	if (clone_file_cifs(src_fd, dst_fd)) {
		ret = 1;
		goto err_dst_close;
	}

	if (unlink(src_file) == -1) {
		fprintf(stderr, "unable to remove source file: %m\n");
		goto err_dst_close;
	}

	ret = 0;

err_dst_close:
	if (close(dst_fd)) {
		ret = 1;
		fprintf(stderr, "failed to close dst file: %m\n");
	}

err_src_close:
	if (close(src_fd)) {
		ret = 1;
		fprintf(stderr, "failed to close src file: %m\n");
	}

err_out:
	free(src_file);
	free(dst_file);
	return ret;
}
