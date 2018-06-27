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

#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <ctype.h>
#include <stdio.h>
#include <errno.h>
#include <err.h>

#include <sys/types.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <ifaddrs.h>
#include <netdb.h>

#include "rtp.h"
#define BUFLEN 1024

extern const char* __progname;
struct ifaddrs *ifaces = NULL;

ssize_t (*reader)   (int fd, void *buf, size_t len);
ssize_t (*writer)   (int fd, void *buf, size_t len);

ssize_t read_dump   (int fd, void *buf, size_t len);
ssize_t read_rtp    (int fd, void *buf, size_t len);
ssize_t read_pay    (int fd, void *buf, size_t len);
ssize_t read_ascii  (int fd, void *buf, size_t len);

ssize_t write_dump  (int fd, void *buf, size_t len);
ssize_t write_rtp   (int fd, void *buf, size_t len);
ssize_t write_pay   (int fd, void *buf, size_t len);
ssize_t write_ascii (int fd, void *buf, size_t len);

struct name {
	const char *suff;
	ssize_t (*reader)(int fd, void *buf, size_t len);
	ssize_t (*writer)(int fd, void *buf, size_t len);
} names[] = {
	{ "dump",	read_dump,	write_dump  },
	{ "rtp",	read_rtp,	write_rtp   },
	{ "raw",	read_pay,	write_pay   },
	{ "asc",	read_ascii,	write_ascii },
};

static void
usage()
{
	fprintf(stderr, "%s [-v] [input] [output]\n", __progname);
}

void
praddr(const char* msg, struct sockaddr_in* a)
{
	if (a == NULL)
		return;
	printf("%s %s:%d\n", msg, inet_ntoa(a->sin_addr), ntohs(a->sin_port));
}

int
islocal(struct sockaddr_in *a)
{
	struct ifaddrs *i;
	for (i = ifaces; i; i = i->ifa_next)
         	if (a->sin_addr.s_addr ==
		((struct sockaddr_in*)i->ifa_addr)->sin_addr.s_addr)
			return 1;
	return 0;
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

ssize_t
read_rtp(int fd, void *buf, size_t len)
{
	return read(fd, buf, len);
}

ssize_t
write_rtp(int fd, void *buf, size_t len)
{
	return write(fd, buf, len);
}

ssize_t
read_pay(int fd, void *buf, size_t len)
{
	return read(fd, buf, len);
}

ssize_t
write_pay(int fd, void *buf, size_t len)
{
	return write(fd, buf, len);
}

ssize_t
read_ascii(int fd, void *buf, size_t len)
{
	return read(fd, buf, len);
}

ssize_t
write_ascii(int fd, void *buf, size_t len)
{
	return write(fd, buf, len);
}

/* Open and prepare the input for reading, or the output for writing.
 * Return a suitable file escriptor (FIXME: FILE*) that the reader
 * can read from and the writer can write to. Based on the type of
 * input/output (file or addr:port), set the reader and writer,
 * if not set already by options (FIXME). */
int
rtpopen(const char *input, int flags)
{
	int e;
	char* p;
	int fd = -1;
	uint32_t port;
	struct addrinfo info;
	struct addrinfo *res;
	struct sockaddr_in *a;
	if (strcmp(input, "-") == 0) {
		reader = read_dump;
		writer = write_ascii;
		return (flags & O_CREAT) ? STDOUT_FILENO : STDIN_FILENO;
	} else if ((p = strchr(input, ':'))) {
		*p++ = '\0';
		if ((port = atoi(p)) == 0) { /* FIXME strtonum */
			warnx("%s is not a valid port number", p);
			return -1;
		}
		memset(&info, 0, sizeof(info));
		info.ai_family = PF_INET;
		info.ai_socktype = SOCK_DGRAM;
		info.ai_protocol = IPPROTO_UDP;
		info.ai_flags = AI_ADDRCONFIG | AI_NUMERICSERV;
		if ((e = getaddrinfo(input, p, &info, &res))) {
			warnx("%s", gai_strerror(e));
			return -1;
		}
		if (res == NULL) {
			warn("getaddrinfo %s", input);
			goto bad;
		}
		if (-1 == (fd =
		socket(res->ai_family, res->ai_socktype, res->ai_protocol))) {
			warn("socket");
			goto bad;
		}
		reader = read_rtp;
		writer = write_rtp;
		if (islocal(a = (struct sockaddr_in*) res->ai_addr)) {
			/* If the local socket is an input, we will read on it;
			 * if it's an output, we want to receive a message first
			 * to know who to write to. So bind(2) in any case. */
			a->sin_port = htons(port);
			if (bind(fd, res->ai_addr, res->ai_addrlen) == -1) {
				warn("bind");
				goto bad;
			}
			if (flags & O_CREAT) {
				/* If this local address is an output,
			 	 * wait till someone sends something here,
		         	 * to let us know where to write later. */
				char b[2];
				socklen_t len;
				struct sockaddr r;
				while (recvfrom(fd, b, 1, 0, &r, &len) != 1)
					;
				if (connect(fd, &r, len) == -1) {
					warn("connect");
					goto bad;
				}
			}
		} else {
			if (connect(fd, res->ai_addr, res->ai_addrlen) == -1) {
				warn("connect");
				goto bad;
			}
			if (!(flags & O_CREAT)) {
				/* If the remote address is an input,
			 	 * send a byte there to let them know
				 * where to write later. */
				char b[2] = "1";
				if (send(fd, b, 1, 0) != 1) {
					warn("send");
					goto bad;
				}

			}
		}
		freeaddrinfo(res);
	} else {
		fd = (flags & O_CREAT)
			? open(input, flags, 0644)
			: open(input, flags);
		if (fd == -1)
			warn("%s", input);
		reader = read_dump;
		writer = write_ascii;
		/* FIXME: consult the file name */
	}
	return fd;
bad:
	freeaddrinfo(res);
	close(fd);
	return -1;
}

int
main(int argc, char** argv)
{
	int c;
	ssize_t r, w;
	int ifd = STDIN_FILENO;
	int ofd = STDOUT_FILENO;
	unsigned char buf[BUFLEN];

	reader = NULL;
	writer = NULL;

	while ((c = getopt(argc, argv, "v")) != -1) switch (c) {
		case 'v':
			break;
		default:
			usage();
		return -1;
	}
	argc -= optind;
	argv += optind;

	if (getifaddrs(&ifaces) == -1)
		err(1, NULL);

	if (argc) {
		if ((ifd = rtpopen(*argv, O_RDONLY)) == -1) {
			warn("Cannot open %s for reading", *argv);
			return -1;
		}
		argv++;
		argc--;
	}
	if (argc) {
		if ((ofd = rtpopen(*argv, O_WRONLY|O_CREAT|O_TRUNC)) == -1) {
			warn("Cannot open %s for writing", *argv);
			return -1;
		}
		argv++;
		argc--;
	}
	freeifaddrs(ifaces);

	while ((r = reader(ifd, buf, BUFLEN)) > 0) {
		if ((w = writer(ofd, buf, r)) != r) {
			warn("write");
			warnx("%zd != %zd bytes written", w, r);
		}
	}
	if (r == -1)
		warnx("error while trying to read %d bytes", BUFLEN);

	return 0;
}
