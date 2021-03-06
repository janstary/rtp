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

#include <arpa/inet.h>
#include <sys/types.h>
#include <sys/time.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <stdio.h>
#include <err.h>

#include "format-dump.h"
#include "format-rtp.h"

/* Parse the #! dump line: check that the magic is there,
 * check that the addr/port is valid, and store them.
 * Return 0 on success, or -1 on error. */
int
read_dumpline(int fd, struct sockaddr_in *addr)
{
	char *a, *p;
	const char *e;
	char buf[1024];
	if ((read(fd, buf, DUMPMAGICLEN) != DUMPMAGICLEN)
	||  (strncmp(buf, DUMPMAGIC, DUMPMAGICLEN) != 0)) {
		warnx("'%s' not found", DUMPMAGIC);
		return -1;
	}
	for (a = p = buf; read(fd, p, 1) == 1; p++) {
		if (*p == '\n') {
			*p = '\0';
			break;
		}
	}
	if ((p = strchr(a, '/')) == NULL) {
		warnx("addr/port not found");
		return -1;
	}
	*p++ = '\0';
	if (!inet_aton(a, &(addr->sin_addr))) {
		warnx("'%s' is not a valid address", a);
		return -1;
	}
	if ((addr->sin_port = strtonum(p, 1, UINT16_MAX, &e)) == 0) {
		warnx("port number '%s' %s", p, e);
		return -1;
	}
	addr->sin_addr.s_addr = addr->sin_addr.s_addr;
	addr->sin_port = addr->sin_port;
	return 0;
}

/* Write the DUMPLINE, with the given addr/port.
 * Return 0 for success, -1 on error. */
int
write_dumpline(int fd, struct sockaddr_in *addr)
{
	int len = 0;
	char *line = NULL;
	len = asprintf(&line, "#!rtpplay1.0 %s/%u\n",
		inet_ntoa(addr->sin_addr), addr->sin_port);
	if (write(fd, line, len) != len)
		return -1;
	free(line);
	return len;
}

void
print_dumphdr(struct dumphdr *hdr)
{
	struct in_addr a;
	time_t start;
	if (hdr == NULL)
		return;
	start = hdr->time.sec;
	a.s_addr = hdr->addr;
	fprintf(stderr, "dump of %s:%d started %s",
		inet_ntoa(a), hdr->port, ctime(&start));
}

/* Read the global binary dumphdr from a file,
 * converting the values to local byte order.
 * Return bytes read, or -1 on error. */
ssize_t
read_dumphdr(int fd, void *buf, size_t len)
{
	struct dumphdr *dumphdr = (struct dumphdr*) buf;
	if (read(fd, buf, DUMPHDRSIZE) != DUMPHDRSIZE) {
		warnx("Error reading dump file header");
		return -1;
	}
	dumphdr->time.sec = ntohl(dumphdr->time.sec);
	dumphdr->time.usec = ntohl(dumphdr->time.usec);
	dumphdr->addr = ntohl(dumphdr->addr);
	dumphdr->port = ntohs(dumphdr->port);
	return DUMPHDRSIZE;
}

/* Write the global binary dumphdr into a file,
 * converting the values to network byte order.
 * Return bytes written, or -1 on error. */
ssize_t
write_dumphdr(int fd, struct sockaddr_in *addr, struct timeval *start)
{
	struct dumphdr hdr;
	hdr.time.sec = htonl(start->tv_sec);
	hdr.time.usec = htonl(start->tv_usec);
	hdr.addr = htonl(addr->sin_addr.s_addr);
	hdr.port = htons(addr->sin_port);
	if (write(fd, &hdr, DUMPHDRSIZE) != DUMPHDRSIZE) {
		warnx("Error writing dump header");
		return -1;
	}
	return DUMPHDRSIZE;
}

/* Check that the dumphdr is valid, and that it agreees with
 * the provided address and port (from the #! dump line).
 * Return 0 for OK, -1 for error. */
int
check_dumphdr(struct dumphdr* hdr, struct sockaddr_in *addr) {
	if (hdr == NULL)
		return -1;
	if (hdr->addr != (uint32_t) addr->sin_addr.s_addr) {
		warnx("%0x is not %0x", hdr->addr, addr->sin_addr.s_addr);
		return -1;
	}
	if (hdr->port != addr->sin_port) {
		warnx("%u is not %u", hdr->port, addr->sin_port);
		return -1;
	}
	return 0;
}

/* Print a captured packet header. */
void
print_dpkthdr(struct dpkthdr *dpkthdr)
{
	if (dpkthdr == NULL)
		return;
	fprintf(stderr, "%08.3f ", dpkthdr->msec / 1e3);
	if (dpkthdr->plen) {
		fprintf(stderr, "RTP  %u bytes (%zd captured)\n",
			dpkthdr->plen, dpkthdr->dlen - DPKTHDRSIZE);
	} else {
		fprintf(stderr, "RTCP\n");
	}
}

/* Read a captured packet header from a file,
 * converting values to the local byte order.
 * Return bytes read, or -1 on error. */
ssize_t
read_dpkthdr(int fd, void *buf, size_t len)
{
	ssize_t r = 0;
	struct dpkthdr *dpkthdr;
	if ((r = read(fd, buf, DPKTHDRSIZE)) == 0) {
		return 0;
	} else if (r != DPKTHDRSIZE) {
		warnx("Error reading dumped packet header");
		return -1;
	}
	dpkthdr = (struct dpkthdr*) buf;
	dpkthdr->dlen = ntohs(dpkthdr->dlen);
	dpkthdr->plen = ntohs(dpkthdr->plen);
	dpkthdr->msec = ntohl(dpkthdr->msec);
	return r;
}

/* Write a captured packet header into a file,
 * converting the values to network byte order.
 * Return bytes written, or -1 on error. */
ssize_t
write_dpkthdr(int fd, uint16_t plen, uint32_t msec)
{
	ssize_t w = 0;
	struct dpkthdr hdr;
	hdr.msec = htonl(msec);
	hdr.plen = htons(plen);
	hdr.dlen = htons(plen + DPKTHDRSIZE);
	if ((w = write(fd, &hdr, DPKTHDRSIZE)) != DPKTHDRSIZE) {
		warnx("Error writing packet header");
		return -1;
	}
	return w;
}

/* Read record from a dump file into a buffer.
 * This means the dpkthdr, the rtphdr, and the payload (as much as stored).
 * Return bytes read, or -1 on error. */
ssize_t
read_dump(int fd, void *buf, size_t len)
{
	ssize_t want, r, have = 0;
	struct dpkthdr *dpkthdr;
	if ((r = read_dpkthdr(fd, buf, DPKTHDRSIZE)) == 0)
		return 0;
	else if (r != DPKTHDRSIZE)
		return -1;
	have += r, len -= r;
	dpkthdr = (struct dpkthdr*) buf;
	want = dpkthdr->dlen - DPKTHDRSIZE;
	if ((r = read(fd, buf + have, want)) != want) {
		warnx("Error reading %zd bytes of RTP packet", want);
		return -1;
	}
	have += r, len -= r;
	return have;
}

ssize_t
write_dump(int fd, void *buf, size_t len)
{
	return write(fd, buf, len);
}
