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

#include "format-dump.h"
#include "format-udp.h"

#define BUFLEN 2048

extern const char* __progname;
struct ifaddrs *ifaces = NULL;

ssize_t (*reader)   (int fd, void *buf, size_t len);
ssize_t (*writer)   (int fd, void *buf, size_t len);

ssize_t read_raw    (int fd, void *buf, size_t len);
ssize_t read_ascii  (int fd, void *buf, size_t len);

ssize_t write_raw   (int fd, void *buf, size_t len);
ssize_t write_ascii (int fd, void *buf, size_t len);

struct format {
	const char *name;
	const char *suff;
	ssize_t (*reader)(int fd, void *buf, size_t len);
	ssize_t (*writer)(int fd, void *buf, size_t len);
} formats[] = {
	{ "dump",	"rtp",	read_dump,	write_dump  },
	{ "udp",	"udp",	read_udp,	write_udp   },
	{ "raw",	"raw",	read_raw,	write_raw   },
	{ "ascii",	"txt",	read_ascii,	write_ascii },
	{ NULL,		NULL,	NULL,		NULL        }
};

#define FORMAT_DUMP  (&(formats[0]))
#define FORMAT_RTP   (&(formats[1]))
#define FORMAT_RAW   (&(formats[2]))
#define FORMAT_ASCII (&(formats[3]))

struct format *ifmt = NULL;
struct format *ofmt = NULL;

static void
usage()
{
	fprintf(stderr, "%s [-v] [input] [output]\n", __progname);
}

struct format*
getfmt(const char *name)
{
	struct format *fmt;
	for (fmt = formats; fmt && fmt->name; fmt++)
		if (strcmp(fmt->name, name) == 0)
			return fmt;
	return NULL;
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
read_raw(int fd, void *buf, size_t len)
{
	return read(fd, buf, len);
}

ssize_t
write_raw(int fd, void *buf, size_t len)
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
 * Return a suitable file descriptor (FIXME? FILE*) that the reader
 * can read from and the writer can write to. Based on the type of
 * input/output, set the input/output format if not set already. */
int
rtpopen(const char *input, int flags)
{
	int e;
	int fd = -1;
	uint32_t port;
	char* p = NULL;
	struct format *fmt = NULL;
	struct addrinfo *res = NULL;
	struct addrinfo info;
	struct sockaddr_in *a;
	if (strcmp(input, "-") == 0) {
		/* stdin/stdout */
		if (flags & O_CREAT) {
			ofmt = ofmt ? ofmt : FORMAT_ASCII;
			return STDOUT_FILENO;
		} else {
			ifmt = ifmt ? ifmt : FORMAT_DUMP;
			return STDIN_FILENO;
		}
	} else if ((p = strchr(input, ':'))) {
		/* addr:port */
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
				/* If the remote address is our input,
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
		if (flags & O_CREAT) {
			ofmt = ofmt ? ofmt : FORMAT_RTP;
			if (ofmt != FORMAT_RTP && ofmt != FORMAT_RAW) {
				warnx("output only supports rtp or raw format");
				goto bad;
			}
		} else {
			ifmt = ifmt ? ifmt : FORMAT_RTP;
			if (ifmt != FORMAT_RTP && ifmt != FORMAT_RAW) {
				warnx("input only supports rtp or raw format");
				goto bad;
			}
		}
	} else {
		if (-1 == (fd = (flags & O_CREAT)
		? open(input, flags, 0644)
		: open(input, flags))) {
			warn("%s", input);
			goto bad;
		}
		if ((p = strrchr(input, '.')))
			fmt = getfmt(++p);
		if ((flags & O_CREAT) && (ofmt == NULL)) {
			ofmt = fmt ? fmt : FORMAT_ASCII;
		} else if (ifmt == NULL) {
			ifmt = fmt ? fmt : FORMAT_DUMP;
			if (ifmt == FORMAT_DUMP) {
				if (read_dumpline(fd) == -1) {
					warnx("Invalid dump file header");
					goto bad;
				}
				if (read_dumphdr(fd) == -1) {
					warnx("Invalid dump file header");
					goto bad;
				}
			}
		}
	}
	/* raw input can ony produce raw output */
	if (ifmt == FORMAT_RAW && ofmt != FORMAT_RAW) {
		warnx("Using raw output with raw input");
		ofmt = FORMAT_RAW;
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

	while ((c = getopt(argc, argv, "i:o:v")) != -1) switch (c) {
		case 'i':
			if (((ifmt = getfmt(optarg))) == NULL) {
				warnx("unknown format: %s", optarg);
				return -1;
			}
			break;
		case 'o':
			if ((ofmt = getfmt(optarg)) == NULL) {
				warnx("unknown format: %s", optarg);
				return -1;
			}
			break;
		case 'v':
			break;
		default:
			usage();
			return -1;
			break;
	}
	argc -= optind;
	argv += optind;

	if (argc > 2) {
		usage();
		return -1;
	}

	if (getifaddrs(&ifaces) == -1)
		err(1, NULL);
	if (-1 == (ifd = *argv
	? rtpopen(*argv++, O_RDONLY)
	: rtpopen("-",     O_RDONLY))) {
		warnx("Cannot open input for reading");
		return -1;
	}
	if (-1 == (ofd = *argv
	? rtpopen(*argv++, O_WRONLY|O_CREAT|O_TRUNC)
	: rtpopen("-",     O_WRONLY|O_CREAT|O_TRUNC))) {
		warnx("Cannot open output for writing");
		return -1;
	}
	freeifaddrs(ifaces);

	if (ifmt == NULL) {
		warnx("Input format not specified");
		return -1;
	}
	if (ofmt == NULL) {
		warnx("Output format not specified");
		return -1;
	}

	reader = ifmt->reader;
	writer = ofmt->writer;
	while ((r = reader(ifd, buf, BUFLEN)) > 0) {
		if ((w = writer(ofd, buf, r)) != r) {
			warn("write");
			warnx("%zd != %zd bytes written", w, r);
		}
	}

	if (r == -1) {
		warnx("error while trying to read %d bytes", BUFLEN);
		return -1;
	}

	return 0;
}
