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
#include "format-net.h"
#include "format-rtp.h"

#define BUFLEN 8192

extern const char* __progname;
struct ifaddrs *ifaces = NULL;

typedef enum {
	FORMAT_NONE,
	FORMAT_DUMP,
	FORMAT_NET,
	FORMAT_RAW,
	FORMAT_TXT
} format_t;

struct format {
	format_t	type;
	const char*	name;
	const char*	suff;
} formats[] = {
	{ FORMAT_DUMP,	"dump",	"rtp"	},
	{ FORMAT_NET,	"net",	NULL	},
	{ FORMAT_RAW,	"raw",	"raw"	},
	{ FORMAT_TXT,	"txt",	"txt"	},
	{ FORMAT_NONE,	NULL,	NULL	}
};

format_t ifmt = FORMAT_NONE;
format_t ofmt = FORMAT_NONE;
int (*convert)(int ifd, int ofd);

static int verbose = 0;

static void
usage()
{
	fprintf(stderr, "%s [-v] [input] [output]\n", __progname);
}

format_t
fmtbyname(const char *name)
{
	struct format *fmt;
	for (fmt = formats; fmt && fmt->name; fmt++)
		if (strcmp(fmt->name, name) == 0)
			return fmt->type;
	return FORMAT_NONE;
}

format_t
fmtbysuff(const char *suff)
{
	struct format *fmt;
	for (fmt = formats; fmt && fmt->name; fmt++)
		if (fmt->suff && strcmp(fmt->suff, suff) == 0)
			return fmt->type;
	return FORMAT_NONE;
}

/*
void
print_addr(const char* msg, struct sockaddr_in* a)
{
	if (a == NULL)
		return;
	printf("%s %s:%d\n", msg, inet_ntoa(a->sin_addr), ntohs(a->sin_port));
}
*/

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

/* Open the input for reading, or the output for writing.
 * Return a file descriptor the convertor can read from or write to, or -1.
 * Based on the type of input/output, set the input/output format if not set. */
int
rtpopen(const char *path, int flags)
{
	int e;
	int fd = -1;
	uint32_t port;
	char* p = NULL;
	format_t fmt = FORMAT_NONE;;
	struct addrinfo *res = NULL;
	struct addrinfo info;
	struct sockaddr_in *a;
	if (strcmp(path, "-") == 0) {
		/* stdin/stdout */
		if (flags & O_CREAT) {
			if (ofmt == FORMAT_NONE)
				ofmt = FORMAT_TXT;
			return STDOUT_FILENO;
		} else {
			if (ifmt == FORMAT_NONE)
				ifmt = FORMAT_DUMP;
			return STDIN_FILENO;
		}
	} else if ((p = strchr(path, ':'))) {
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
		if ((e = getaddrinfo(path, p, &info, &res))) {
			warnx("%s", gai_strerror(e));
			return -1;
		}
		if (res == NULL) {
			warn("getaddrinfo %s", path);
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
		if (ifmt == FORMAT_NONE)
			ifmt = FORMAT_NET;
		if (ofmt == FORMAT_NONE)
			ofmt = FORMAT_NET;
		if (ifmt != FORMAT_NET || ofmt != FORMAT_NET) {
			warnx("Only net format allowed for %s", path);
			goto bad;
		}
	} else {
		if (-1 == (fd = (flags & O_CREAT)
		? open(path, flags, 0644)
		: open(path, flags))) {
			warn("%s", path);
			goto bad;
		}
		if ((p = strrchr(path, '.')))
			fmt = fmtbysuff(++p);
		if ((flags & O_CREAT) && (ofmt == FORMAT_NONE)) {
			ofmt = fmt ? fmt : FORMAT_TXT;
		} else if (ifmt == FORMAT_NONE) {
			ifmt = fmt ? fmt : FORMAT_DUMP;
		}
	}
	return fd;
bad:
	freeaddrinfo(res);
	close(fd);
	return -1;
}

int
dump2net(int ifd, int ofd)
{
	return 0;
}

/* Read a dump file from ifd, write raw audio payload to ofd.
 * Return 0 for success, -1 for error. */
int
dump2raw(int ifd, int ofd)
{
	struct rtphdr *rtp;
	struct dpkthdr *pkt;
	unsigned char buf[BUFLEN];
	unsigned char *p = buf;
	ssize_t s, w, hlen;
	int e = 0;
	/* read and ignore the dump line */
	if (read_dumpline(ifd, buf, BUFLEN) == -1) {
		warnx("Invalid dump file header");
		return -1;
	}
	/* read and ignore the dump hdr */
	if (read_dumphdr(ifd, buf, BUFLEN) == -1) {
		warnx("Invalid dump file header");
		return -1;
	}
	/* parse each captured packet in turn,
	 * writing its payload to the output. */
	while ((s = read_dump(ifd, buf, BUFLEN)) > 0) {
		pkt = (struct dpkthdr*) buf;
		if (verbose)
			print_dpkthdr(pkt);
		if (pkt->dlen - DPKTHDRSIZE < pkt->plen) {
			warnx("%lu bytes missing from captured RTP",
				pkt->plen - pkt->dlen + DPKTHDRSIZE);
		}
		rtp = (struct rtphdr*) (buf + DPKTHDRSIZE);
		if ((hlen = parse_rtphdr(rtp)) == -1) {
			e = -1;
			warnx("Error parsing RTP header");
			continue;
		}
		if (verbose)
			print_rtphdr(rtp);
		p += DPKTHDRSIZE + hlen;
		s -= DPKTHDRSIZE + hlen;
		if ((w = write(ofd, p, s)) == -1) {
			warnx("Error writing %zd bytes of payload", s);
			e = -1;
		} else if (w < s) {
			warnx("Only wrote %zd < %zd bytes of payload", w, s);
			e = -1;
		}
	}
	return e;
}

int
dump2txt(int ifd, int ofd)
{
	return 0;
}

int
main(int argc, char** argv)
{
	int c;
	int ifd = STDIN_FILENO;
	int ofd = STDOUT_FILENO;

	while ((c = getopt(argc, argv, "i:o:v")) != -1) switch (c) {
		case 'i':
			if (((ifmt = fmtbyname(optarg))) == FORMAT_NONE) {
				warnx("unknown format: %s", optarg);
				return -1;
			}
			break;
		case 'o':
			if ((ofmt = fmtbyname(optarg)) == FORMAT_NONE) {
				warnx("unknown format: %s", optarg);
				return -1;
			}
			break;
		case 'v':
			verbose = 1;
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

	if (ifmt == FORMAT_NONE) {
		warnx("Input format not determined");
	} else if (ifmt == FORMAT_DUMP) {
		if (ofmt == FORMAT_DUMP) {
			warnx("No point cnverting dump to dump.");
		} else if (ofmt == FORMAT_NET) {
			convert = dump2net;
		} else if (ofmt == FORMAT_RAW) {
			convert = dump2raw;
		} else if (ofmt == FORMAT_TXT) {
			convert = dump2txt;
		}
	} else if (ifmt == FORMAT_NET) {
	} else if (ifmt == FORMAT_RAW) {
		warnx("raw can only be an output");
		return -1;
	} else if (ifmt == FORMAT_TXT) {
	}

	return convert ? convert(ifd, ofd) : -1;

	/*
	while ((r = reader(ifd, buf, BUFLEN)) > 0) {
		if ((w = writer(ofd, buf, r)) != r) {
			warn("write");
			warnx("%zd != %zd bytes written", w, r);
		}
	}
	*/
}
