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
/* FIXME: This should be enough for each and every packet we read,
 * but we are still wrong: mind the buflen in the reading routines. */

extern const char* __progname;
struct ifaddrs *ifaces = NULL;

typedef enum {
	FORMAT_DUMP,
	FORMAT_NET,
	FORMAT_RAW,
	FORMAT_TXT,
	FORMAT_NONE
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

static int remote = 0;
static int verbose = 0;
static format_t ifmt = FORMAT_NONE;
static format_t ofmt = FORMAT_NONE;
static int (*convert)(int ifd, int ofd) = NULL;

static void
usage()
{
	fprintf(stderr, "%s [-v] [-i format] [-o format] [input] [output]\n",
		__progname);
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

int
islocal(struct sockaddr_in *a)
{
	struct ifaddrs *i;
	if (remote)
		return 0;
	for (i = ifaces; i; i = i->ifa_next)
         	if (a->sin_addr.s_addr ==
		((struct sockaddr_in*)i->ifa_addr)->sin_addr.s_addr)
			return 1;
	return 0;
}

/* Open a path for reading or writing (the flags say which).
 * Return a file descriptor the convertor can read/recv from or write/sent to,
 * or -1 for failure. Also, set the input/output format, if not set alrady. */
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
		/* FIXME setsockopt */
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
				 * wait till we recvfrom() something first,
				 * to let us know where to send() later. */
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
				 * where to send(2) later. */
				char b[2] = "1";
				if (send(fd, b, 1, 0) != 1) {
					warn("send");
					goto bad;
				}
			}
		}
		freeaddrinfo(res);
		if (flags & O_CREAT) {
			if (ofmt == FORMAT_NONE) {
				ofmt = FORMAT_NET;
			} else if (ofmt != FORMAT_NET) {
				warnx("Only net output allowed for %s", path);
				goto bad;
			}
		} else {
			if (ifmt == FORMAT_NONE) {
				ifmt = FORMAT_NET;
			} else if (ifmt != FORMAT_NET) {
				warnx("Only net input allowed for %s", path);
				goto bad;
			}
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
		/* Be compatible with rtptools */
		if ((flags & O_CREAT) && (ofmt == FORMAT_NONE)) {
			ofmt = (fmt != FORMAT_NONE) ? fmt : FORMAT_TXT;
		} else if (ifmt == FORMAT_NONE) {
			ifmt = (fmt != FORMAT_NONE) ? fmt : FORMAT_DUMP;
		}
	}
	return fd;
bad:
	freeaddrinfo(res);
	close(fd);
	return -1;
}

/* Read a dump file from input, send the RTP output via net.
 * FIXME: mind the timing, as opposed to sending as you read.
 * Return 0 for success, -1 for error. */
int
dump2net(int ifd, int ofd)
{
	ssize_t r, w;
	int error = 0;
	struct dumphdr hdr;
	struct dpkthdr *pkt;
	struct rtphdr  *rtp;
	unsigned char buf[BUFLEN];
	if (read_dumpline(ifd, buf, BUFLEN) == -1) {
		warnx("Error reading dump file line");
		return -1;
	}
	if (read_dumphdr(ifd, &hdr, DUMPHDRSIZE) == -1) {
		warnx("Error reading %zd bytes of dump header", DUMPHDRSIZE);
		return -1;
	}
	if (verbose)
		print_dumphdr(&hdr);
	while ((r = read_dump(ifd, buf, BUFLEN)) > 0) {
		pkt = (struct dpkthdr*) buf;
		rtp = (struct rtphdr*) (buf + DPKTHDRSIZE);
		if (verbose)
			print_dpkthdr(pkt);
		if (parse_rtphdr(rtp) == -1) {
			warnx("Error parsing RTP header");
			error = -1;
			continue;
		}
		if (verbose)
			print_rtphdr(rtp);
		if (pkt->plen == 0) /* FIXME: that's RTCP. Currently, we don't
			send these, because receiving zero size confuses the
			reader, who considers that an end. But a RTCP packet
			does not actualy have zero size. We need to properly
			read the RTPC header, which we don't, yet. */
			continue;
		if ((w = send(ofd, rtp, pkt->plen, 0)) == -1) {
			warnx("Error sending %u bytes of RTP", pkt->plen);
			error = -1;
			continue;
		} else if (w < pkt->plen) {
			warnx("Only sent %zd < %u bytes of RTP", w, pkt->plen);
			error = -1;
			continue;
		}
		sleep(1); /* FIXME: propper timing as per rtp->ts */
	}
	return r == -1 ? -1 : error;
}

/* Read a dump file from input, write raw audio payload to output.
 * Return 0 for success, -1 for error. */
int
dump2raw(int ifd, int ofd)
{
	struct rtphdr *rtp;
	struct dpkthdr *pkt;
	unsigned char buf[BUFLEN];
	unsigned char *p = buf;
	ssize_t s, w, hlen;
	int error = 0;
	if (read_dumpline(ifd, buf, BUFLEN) == -1) {
		warnx("Invalid dump line");
		return -1;
	}
	if (read_dumphdr(ifd, buf, BUFLEN) == -1) {
		warnx("Invalid dump file header");
		return -1;
	}
	while ((s = read_dump(ifd, buf, BUFLEN)) > 0) {
		pkt = (struct dpkthdr*) (p = buf);
		if (pkt->plen == 0) /* not RTP */
			continue;
		if (verbose)
			print_dpkthdr(pkt);
		if (pkt->dlen - DPKTHDRSIZE < pkt->plen) {
			warnx("%lu bytes of RTP payload missing",
				pkt->plen - pkt->dlen + DPKTHDRSIZE);
		}
		rtp = (struct rtphdr*) (buf + DPKTHDRSIZE);
		if ((hlen = parse_rtphdr(rtp)) == -1) {
			warnx("Error parsing RTP header");
			error = -1;
			continue;
		}
		if (verbose)
			print_rtphdr(rtp);
		p += DPKTHDRSIZE + hlen;
		s -= DPKTHDRSIZE + hlen;
		if ((w = write(ofd, p, s)) == -1) {
			warnx("Error writing %zd bytes of payload", s);
			error = -1;
			continue;
		} else if (w < s) {
			warnx("Only wrote %zd < %zd bytes of payload", w, s);
			error = -1;
			continue;
		}
	}
	return s == -1 ? -1 : error;
}

int
dump2txt(int ifd, int ofd)
{
	return 0;
}

/* Read RTP packets from the net, write a dump file.
 * Return 0 for success, -1 for error. */
int
net2dump(int ifd, int ofd)
{
	ssize_t r, w;
	int error = 0;
	struct rtphdr *rtp;
	struct dpkthdr hdr;
	unsigned char buf[BUFLEN];
	if (write_dumpline(ofd) == -1) {
		warnx("Error writing dump line");
		return -1;
	}
	if (write_dumphdr(ofd) == -1) {
		warnx("Error writing dump header");
		return -1;
	}
	while ((r = recv(ifd, buf, BUFLEN, 0)) > 0) {
		if (verbose)
			fprintf(stderr, "%zd bytes of RTP received\n", r);
		rtp = (struct rtphdr*) buf;
		if (parse_rtphdr(rtp) == -1) {
			warnx("Error parsing RTP header");
			error = -1;
			continue;
		}
		if (verbose)
			print_rtphdr(rtp);
		hdr.plen = r;
		hdr.dlen = r + DPKTHDRSIZE;
		if (verbose)
			print_dpkthdr(&hdr);
		if ((w = write_dpkthdr(ofd, &hdr, DPKTHDRSIZE)) == -1) {
			warnx("Error writing dump packet header");
			error = -1;
			continue;
		}
		/* TODO: -s size of RTP to save */
		if ((w = write(ofd, buf, r)) != r) {
			warnx("Error writing %zd bytes of RTP", r);
			error = -1;
			continue;
		}
	}
	fprintf(stderr, "read %zd bytes\n", r);
	return r == -1 ? -1 : error;
}

/* Read RTP packets from the net, write RTP packets to the net.
 * No timing is considered here: write them as you read them.
 * Return 0 for success, -1 for error. */
int
net2net(int ifd, int ofd)
{
	ssize_t s, w;
	int error = 0;
	struct rtphdr *rtp;
	unsigned char buf[BUFLEN];
	while ((s = recv(ifd, buf, BUFLEN, 0)) > 0) {
		if (verbose)
			fprintf(stderr, "%zd bytes of RTP received\n", s);
		rtp = (struct rtphdr*) buf;
		if (parse_rtphdr(rtp) == -1) {
			error = -1;
			warnx("Error parsing RTP header");
			continue;
		}
		if (verbose)
			print_rtphdr(rtp);
		if ((w = write(ofd, buf, s)) == -1) {
			warnx("Error writing %zd bytes of payload", s);
			error = -1;
		} else if (w < s) {
			warnx("Only wrote %zd < %zd bytes of payload", w, s);
			error = -1;
		}
	}
	return s == -1 ? -1 : error;
}

/* Read RTP packets from the ifd, write raw audio payload to ofd.
 * Return 0 for success, -1 for error. */
int
net2raw(int ifd, int ofd)
{
	unsigned char *p;
	unsigned char buf[BUFLEN];
	struct rtphdr *rtp;
	ssize_t s, w, hlen;
	int error = 0;
	while ((s = recv(ifd, buf, BUFLEN, 0)) > 0) {
		if (verbose)
			fprintf(stderr, "%zd bytes of RTP received\n", s);
		rtp = (struct rtphdr*) (p = buf);
		if ((hlen = parse_rtphdr(rtp)) == -1) {
			error = -1;
			warnx("Error parsing RTP header");
			continue;
		}
		if (verbose)
			print_rtphdr(rtp);
		p += hlen;
		s -= hlen;
		if ((w = write(ofd, p, s)) == -1) {
			warnx("Error writing %zd bytes of payload", s);
			error = -1;
		} else if (w < s) {
			warnx("Only wrote %zd < %zd bytes of payload", w, s);
			error = -1;
		}
	}
	return s == -1 ? -1 : error;
}

int
net2txt(int ifd, int ofd)
{
	return 0;
}

int
main(int argc, char** argv)
{
	int c;
	int ifd = STDIN_FILENO;
	int ofd = STDOUT_FILENO;

	while ((c = getopt(argc, argv, "i:o:rv")) != -1) switch (c) {
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
		case 'r':
			remote = 1;
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
	if (-1 == (ifd = (*argv
	? rtpopen(*argv++, O_RDONLY)
	: rtpopen("-",     O_RDONLY)))) {
		warnx("Cannot open input for reading");
		return -1;
	}
	if (-1 == (ofd = (*argv
	? rtpopen(*argv++, O_WRONLY|O_CREAT|O_TRUNC)
	: rtpopen("-",     O_WRONLY|O_CREAT|O_TRUNC)))) {
		warnx("Cannot open output for writing");
		return -1;
	}
	freeifaddrs(ifaces);

	if (ifmt == FORMAT_NONE) {
		warnx("Input format not determined");
	} else if (ifmt == FORMAT_DUMP) {
		if (ofmt == FORMAT_DUMP) {
			warnx("No point converting dump to dump.");
		} else if (ofmt == FORMAT_NET) {
			convert = dump2net;
		} else if (ofmt == FORMAT_RAW) {
			convert = dump2raw;
		} else if (ofmt == FORMAT_TXT) {
			convert = dump2txt;
		}
	} else if (ifmt == FORMAT_NET) {
		if (ofmt == FORMAT_DUMP) {
			convert = net2dump;
		} else if (ofmt == FORMAT_NET) {
			convert = net2net;
		} else if (ofmt == FORMAT_RAW) {
			convert = net2raw;
		} else if (ofmt == FORMAT_TXT) {
			convert = net2txt;
		}
	} else if (ifmt == FORMAT_RAW) {
		warnx("Only output can be raw");
		return -1;
	} else if (ifmt == FORMAT_TXT) {
	}

	return convert ? convert(ifd, ofd) : -1;
}
