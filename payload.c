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

#include <stdint.h>
#include <stdio.h>
#include "payload.h"

struct pt payload[] = {
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
