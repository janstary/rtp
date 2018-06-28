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

#include <stdint.h>
#include "config.h"

#define RTPVERSION 2

struct rtphdr {
#if HAVE_BIGENDIAN
	uint8_t		v:2;
	uint8_t		p:1;
	uint8_t		x:1;
	uint8_t		cc:4;
	uint8_t		m:1;
	uint8_t		pt:7;
#else
	uint8_t		cc:4;
	uint8_t		p:1;
	uint8_t		x:1;
	uint8_t		v:2;
	uint8_t		pt:7;
	uint8_t		m:1;
#endif
	uint16_t	seq;
	uint32_t	ts;
	uint32_t	ssrc;
	uint32_t	csrc[];
};

struct rtpext {
	uint16_t ehid;	/* extension header ID; profile-dependent */
	uint16_t elen;	/* extension length in 32-bit words */
};

void	print_rtphdr(struct rtphdr*);

