/*
 * Copyright (c) 2018 Jan Stary <hans@stare.cz>
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

#include <sys/time.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <ifaddrs.h>
#include <netdb.h>

#include "format-dump.h"
#include "format-rtp.h"

#define BUFLEN 8192
/* FIXME: This should be enough for each and every packet we read,
 * but we are still wrong: mind the buflen in the reading routines. */

extern const char* __progname;
struct ifaddrs *ifaces = NULL;
struct sockaddr_in *addr;

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
#define NUMFORMATS (sizeof(formats) / sizeof(struct format))

struct pt {
	const char*	enc;  /* encoding name */
	uint32_t	rate; /* sampling rate (audio) or clock rate (video) */
	uint8_t		ch;   /* audio channels; 0 for video */
} payload[] = {
	{ "PCMU",	 8000,	1 },
	{ "reserved",	    0,	0 },
	{ "reserved",	    0,	0 },
	{ "GSM ",	 8000,	1 },
	{ "G723",	 8000,	1 },
	{ "DVI4",	 8000,	1 },
	{ "DVI4",	16000,	1 },
	{ "LPC ",	 8000,	1 },
	{ "PCMA",	 8000,	1 },
	{ "G722",	 8000,	1 },
	{ "L16 ",	44100,	2 },
	{ "L16 ",	44100,	1 },
	{ "QCELP",	 8000,	1 },
	{ "CN  ",	 8000,	0 },
	{ "MPA ",	90000,	0 },
	{ "G728",	 8000,	1 },
	{ "DVI4",	11025,	1 },
	{ "DVI4",	22050,	1 },
	{ "G729",	 8000,	1 },
	{ "reserved",	    0,	0 },
	{ "unassigned",	    0,	0 },
	{ "unassigned",	    0,	0 },
	{ "unassigned",	    0,	0 },
	{ "unassigned",	    0,	0 },
	{ "unassigned",	    0,	0 },
	{ "CelB",	90000,	0 },
	{ "JPEG",	90000,	0 },
	{ "unassigned",	    0,	0 },
	{ "nv  ",	90000,	0 },
	{ "unassigned",	    0,	0 },
	{ "unassigned",	    0,	0 },
	{ "H261",	90000,	0 },
	{ "MPV ",	90000,	0 },
	{ "MP2T",	90000,	0 },
	{ "H263",	90000,	0 },
	{ NULL,		    0,	0 }
};
#define NUMPAYLOAD ((sizeof(payload)) / (sizeof(struct pt)))

static int remote = 0;
static int dumptime = 0;
static int verbose = 0;
static format_t ifmt = FORMAT_NONE;
static format_t ofmt = FORMAT_NONE;

static void
usage(void)
{
	fprintf(stderr,
		"%s [-r] [-v] [-i format] [-o format] [input] [output]\n",
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
 * Set the input/output format, set addr/port if applicable.
 * Return a file descriptor, or -1 for failure. */
int
rtpopen(const char *path, int flags)
{
	int e;
	int fd = -1;
	uint16_t port;
	const char* er;
	char* p = NULL;
	format_t fmt = FORMAT_NONE;;
	struct addrinfo *res = NULL;
	struct addrinfo info;
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
		if ((port = strtonum(p, 1, UINT16_MAX, &er)) == 0) {
			warnx("port number '%s' %s", p, er);
			return -1;
		}
		if (*path == '\0')
			path = NULL;
		memset(&info, 0, sizeof(info));
		info.ai_family = PF_INET;
		info.ai_socktype = SOCK_DGRAM;
		info.ai_protocol = IPPROTO_UDP;
		info.ai_flags = AI_ADDRCONFIG | AI_NUMERICSERV;
		if ((e = getaddrinfo(path, p, &info, &res))) {
			warnx("'%s': %s", path, gai_strerror(e));
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
		if (-1 == setsockopt(fd,
		SOL_SOCKET, SO_REUSEADDR, &fd, sizeof(fd)))
			warn("REUSEADDR");
		/* TODO: SO_SNDTIMEO SO_RCVTIMEO SO_TIMESTAMP */
		addr = (struct sockaddr_in*) res->ai_addr;
		addr->sin_port = port;
		if (islocal(addr)) {
			/* If the local socket is an input, we will read on it;
			 * if it's an output, we want to receive a message first
			 * to know who to write to. So bind(2) in any case. */
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
					/* Debian: EAFNOSUPPORT for localhost */
					warn("connect to output");
					goto bad;
				}
			}
		} else {
			if (connect(fd, res->ai_addr, res->ai_addrlen) == -1) {
				warn("connect to remote");
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
	if (res)
		freeaddrinfo(res);
	close(fd);
	return -1;
}

/* Compute the time interval from old to new timeval.
 * Return the difference in usec, or 0 for error.
 * This is used to time outgoing packets,
 * so the 0 means "send immediately". */
uint32_t
tvdiff(struct timeval *old, struct timeval *new)
{
	uint32_t usec;
	if (old == NULL || new == NULL)
		return 0;
	if ((old->tv_sec > new->tv_sec)
	|| ((old->tv_sec == new->tv_sec) && (old->tv_usec > new->tv_usec)))
		return 0;
	usec = (new->tv_sec - old->tv_sec) * 1000000;
	usec += new->tv_usec - old->tv_usec;
	return usec;
}

/* Compute the offset since the given timeval.
 * Return the difference in msec, or 0 for error. */
uint32_t
offset(struct timeval *since)
{
	struct timeval now;
	if (gettimeofday(&now, NULL) == -1) {
		warn("gettimeofday");
		return 0;
	}
	return tvdiff(since, &now) / 1000;
}

/* The 'zero' describes the start of the dump,
 * the 'when' says (in msec since zero) when the next packet goes out.
 * Sleep for the appropriate time; then return 0, or -1 if interrupted. */
int
dumpsleep(struct timeval *zero, uint32_t when)
{
	struct timeval now;
	struct timespec nap;
	uint32_t diff, usec = when * 1000;
	/* some time has already elapsed */
	if (gettimeofday(&now, NULL) == -1) {
		warnx("gettimeofday");
		return -1;
	}
	if ((diff = tvdiff(zero, &now)) > usec) {
		/* we are late already */
		return 0;
	}
	usec -= diff;
	nap.tv_sec = usec / 1000000;
	nap.tv_nsec = (usec % 1000000) * 1000;
	if (nanosleep(&nap, NULL) != 0) {
		warn("nanosleep");
		return -1;
	}
	return 0;
}

/* The meaning of the rtp timestamp is payload dependent.
 * Use the global paylod type table to compute the appropriate
 * time to sleep, where 'last' is the rtp timestamp of the last
 * rtp packet we sent, and 'next' is the timestamp of the next.
 * For example, a typical audio application uses a 8000 kHz rate
 * and the RTP timestamp increments by 160. This means a step of
 * 160/8000 = 1/50 of a second = 20 ms.
 * Return 0 for success, -1 for error. */
int
rtpsleep(uint32_t *last, uint32_t next, uint8_t pt)
{
	double step;
	uint32_t diff;
	struct timespec nap;
	if (*last == 0) {
		/* first packet */
		*last = next;
		return 0;
	}
	if (*last > next) {
		warnx("packets out of timestamp order: %u > %u", *last, next);
		return -1;
	}
	if (pt > NUMPAYLOAD) {
		warnx("unknown payload %u: sending immediately", pt);
		return -1;
	}
	if (payload[pt].rate == 0) {
		warnx("unknown rate for '%s': sending now", payload[pt].enc);
		return -1;
	}
	diff = next - *last; /* FIXME: minus time already elapsed. */
	*last = next;
	step = (1.0 * diff) / payload[pt].rate;
	nap.tv_sec = step;
	nap.tv_nsec = (step - nap.tv_sec) * 1000000000;
	if (nanosleep(&nap, NULL) != 0) {
		warn("nanosleep");
		return -1;
	}
	return 0;
}

/* Read a dump file from input, send the RTP output via net.
 * Return 0 for success, -1 for error. */
int
dump2net(int ifd, int ofd)
{
	ssize_t r, w;
	int error = 0;
	struct sockaddr_in addr;
	struct dumphdr hdr;
	struct dpkthdr *pkt;
	struct rtphdr  *rtp;
	struct timeval zero;
	uint32_t last = 0;
	unsigned char buf[BUFLEN];
	if (read_dumpline(ifd, &addr) == -1) {
		warnx("Error reading dump line");
		return -1;
	}
	if (read_dumphdr(ifd, &hdr, DUMPHDRSIZE) == -1) {
		warnx("Error reading %zd bytes of dump header", DUMPHDRSIZE);
		return -1;
	}
	if (check_dumphdr(&hdr, &addr) == -1)
		warnx("Dump file header is inconsistent");
	if (verbose)
		print_dumphdr(&hdr);
	if (dumptime && gettimeofday(&zero, NULL) == -1) {
		warnx("gettimeofday");
		return -1;
	}
	while ((r = read_dump(ifd, buf, BUFLEN)) > 0) {
		pkt = (struct dpkthdr*) buf;
		rtp = (struct rtphdr*) (buf + DPKTHDRSIZE);
		if (pkt->plen == 0) { /* FIXME: that's RTCP. Currently, we don't
			send these, because receiving zero size confuses the
			reader, who considers that an end. But a RTCP packet
			does not actualy have zero size. We need to properly
			read the RTCP header, which we don't, yet. */
			continue;
		}
		if ((dumptime
		? dumpsleep(&zero, pkt->msec)
		: rtpsleep(&last, ntohl(rtp->ts), rtp->pt)) == -1) {
		/* FIXME: notice how we use pkt->msec, because that's
		 * already converted to the local byte order by
		 * read_dump() -> read_dpkthdr(); but we convert the rtp->ts,
		 * because we have not properly parsed the RTP packet;
		 * which we have to, e.g. to change the timestamp. */
			warnx("packet timing failed");
			error = -1;
			continue;
		}
		if (verbose)
			print_dpkthdr(pkt);
		if (parse_rtphdr(rtp) == -1) {
			warnx("Error parsing RTP header");
			error = -1;
			continue;
		}
		if (verbose)
			print_rtphdr(rtp);
		if ((w = send(ofd, rtp, pkt->plen, 0)) == -1) {
			warnx("Error sending %u bytes of RTP", pkt->plen);
			error = -1;
			continue;
		} else if (w < pkt->plen) {
			warnx("Only sent %zd < %u bytes of RTP", w, pkt->plen);
			error = -1;
			continue;
		}
	}
	return r == -1 ? -1 : error;
}

/* Read a dump file from input, write raw audio payload to output.
 * Return 0 for success, -1 for error. */
int
dump2raw(int ifd, int ofd)
{
	struct sockaddr_in addr;
	struct dumphdr hdr;
	struct dpkthdr *pkt;
	struct rtphdr *rtp;
	unsigned char buf[BUFLEN];
	unsigned char *p = buf;
	ssize_t r, w, hlen;
	int error = 0;
	if (read_dumpline(ifd, &addr) == -1) {
		warnx("Error reading dump line");
		return -1;
	}
	if (read_dumphdr(ifd, &hdr, DUMPHDRSIZE) == -1) {
		warnx("Error reading dump header");
		return -1;
	}
	if (check_dumphdr(&hdr, &addr) == -1)
		warnx("Dump file header is inconsistent");
	if (verbose)
		print_dumphdr(&hdr);
	while ((r = read_dump(ifd, buf, BUFLEN)) > 0) {
		pkt = (struct dpkthdr*) (p = buf);
		rtp = (struct rtphdr*) (buf + DPKTHDRSIZE);
		if (pkt->plen == 0) /* not RTP */
			continue;
		if (verbose)
			print_dpkthdr(pkt);
		if ((hlen = parse_rtphdr(rtp)) == -1) {
			warnx("Error parsing RTP header");
			error = -1;
			continue;
		}
		if (verbose)
			print_rtphdr(rtp);
		if (pkt->dlen - DPKTHDRSIZE < pkt->plen) {
			warnx("%lu bytes of RTP payload missing",
				pkt->plen - pkt->dlen + DPKTHDRSIZE);
		}
		p += DPKTHDRSIZE + hlen;
		r -= DPKTHDRSIZE + hlen;
		if ((w = write(ofd, p, r)) == -1) {
			warnx("Error writing %zd bytes of payload", r);
			error = -1;
			continue;
		} else if (w < r) {
			warnx("Only wrote %zd < %zd bytes of payload", w, r);
			error = -1;
			continue;
		}
	}
	return r == -1 ? -1 : error;
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
	unsigned char buf[BUFLEN];
	struct timeval start;
	if (gettimeofday(&start, NULL) == -1) {
		warn("gettimeofday");
		return -1;
	}
	if (write_dumpline(ofd, addr) == -1) {
		warnx("Error writing dump line");
		return -1;
	}
	if (write_dumphdr(ofd, addr, &start) == -1) {
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
		if ((w = write_dpkthdr(ofd, r, offset(&start))) == -1) {
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
txt2dump(int ifd, int ofd)
{
	return 0;
}

int
txt2net(int ifd, int ofd)
{
	return 0;
}

int
txt2raw(int ifd, int ofd)
{
	return 0;
}

int
main(int argc, char** argv)
{
	int c;
	int ifd = STDIN_FILENO;
	int ofd = STDOUT_FILENO;

	int (*convert)(int ifd, int ofd) = NULL;
	int (*converter[NUMFORMATS][NUMFORMATS])(int, int) = {
		{ NULL,     dump2net, dump2raw, dump2txt, NULL },
		{ net2dump, net2net,  net2raw,  net2txt,  NULL },
		{ NULL,     NULL,     NULL,	NULL,     NULL },
		{ txt2dump, txt2net,  txt2raw,  NULL,     NULL },
		{ NULL,     NULL,     NULL,     NULL,     NULL },
	};

	while ((c = getopt(argc, argv, "i:o:rtv")) != -1) switch (c) {
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
		case 't':
			dumptime = 1;
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
		return -1;
	}
	if (ofmt == FORMAT_NONE) {
		warnx("Output format not determined");
		return -1;
	}
	if (ifmt == FORMAT_RAW) {
		warnx("Only output can be raw");
		return -1;
	}
	if ((convert = converter[ifmt][ofmt]) == NULL) {
		warnx("No converter for this input/output combination");
		return -1;
	}
	return convert ? convert(ifd, ofd) : -1;
}
