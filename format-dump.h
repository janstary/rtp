/*
 * Copyright (c) 2018 Jan Stary <hans@stare.cz>
 *
 * See the individual source files for information about contributors.
 * The distribution as a whole is distributed under the following license:
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
#include <stdint.h>

struct dumphdr {
	struct {
		uint32_t sec;
		uint32_t usec;
	}		time;
	uint32_t	addr;
	uint16_t	port;
	uint16_t	zero;
};

struct dpkthdr {
	uint16_t dlen; /* length of the dumped packet, including dpkthdr */
	uint16_t plen; /* length of the original RTP header + payload    */
	/* Note that dlen < plen if payload was truncated during dump.   */
	uint32_t usec; /* usec since start */
};

#define DUMPMAGIC    "#!rtpplay1.0 "
#define DUMPMAGICLEN strlen(DUMPMAGIC)
#define DUMPHDRSIZE ((size_t) sizeof(struct dumphdr))
#define DPKTHDRSIZE ((size_t) sizeof(struct dpkthdr))

int	read_dumpline	(int, struct in_addr*, uint16_t*);
ssize_t	write_dumpline	(int);

void	print_dumphdr	(struct dumphdr*);
int	check_dumphdr	(struct dumphdr*, struct in_addr, uint16_t);
ssize_t	read_dumphdr	(int, void*, size_t);
ssize_t	write_dumphdr	(int);

void	print_dpkthdr	(struct dpkthdr*);
ssize_t	read_dpkthdr	(int, void*, size_t);
ssize_t	write_dpkthdr	(int, void*, size_t);

ssize_t	read_dump	(int, void*, size_t);
ssize_t	write_dump	(int, void*, size_t);
