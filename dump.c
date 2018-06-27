/*
 * Copyright (c) 2018 Jan stary <hans@stare.cz>
 *
 * Permission to use, copy, modify, and distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 * 
 * THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
 * WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
 * ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
 * WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN
 * ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
 * OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 */

#include <sys/types.h>
#include <unistd.h>
#include <string.h>
#include <stdio.h>
#include <err.h>

#include "dump.h"

/* Read over the DUMPHDR line.
 * Check that the version is there, ignore the addr/port.
 * Return bytes read, or -1 on error. */
ssize_t
read_dumpline(int fd)
{
	char p;
	ssize_t r;
	char buf[16];
	if ((read(fd, buf, DUMPHDRLEN) != DUMPHDRLEN)
	||  (strncmp(buf, DUMPHDR, DUMPHDRLEN) != 0)
	||  (strncmp(buf+9, "1.0", 3) != 0)) {
		warnx("Invalid dump file header");
		return -1;
	}
	r = DUMPHDRLEN;
	while (read(fd, &p, 1) == 1) {
		r++;
		if (p == '\n')
			break;
	}
	if (p != '\n') {
		warnx("Invalid dump file header");
		return -1;
	}
	return r;
}

void
print_dumphdr(struct dumphdr *dumphdr)
{
	if (dumphdr == NULL)
		return;
	printf("dump starts on %u:%u\n",
		dumphdr->start.sec, dumphdr->start.usec);
}

/* Read the binary dumphdr.
 * Return bytes read, or -1 on error. */
ssize_t
read_dumphdr(int fd)
{
	struct dumphdr dumphdr;
	if (read(fd, (void*) &dumphdr, DUMPHDRSIZE) != DUMPHDRSIZE) {
		warnx("Broken dump header");
		return -1;
	}
	dumphdr.start.sec = ntohl(dumphdr.start.sec);
	dumphdr.start.usec = ntohl(dumphdr.start.usec);
	dumphdr.source = ntohl(dumphdr.source);
	dumphdr.port = ntohs(dumphdr.port);
	/*print_dumphdr(&dumphdr);*/
	return DUMPHDRSIZE;
}

ssize_t
read_dpkthdr(int fd, void *buf, size_t len)
{
	return read(fd, buf, len);
}

ssize_t
read_dump(int fd, void *buf, size_t len)
{
	return read(fd, buf, len);
}

ssize_t
write_dump(int fd, void *buf, size_t len)
{
	return write(fd, buf, len);
}
