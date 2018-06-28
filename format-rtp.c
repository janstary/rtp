#include <stdio.h>
#include "format-rtp.h"

void
print_rtphdr(struct rtphdr* rtphdr)
{
	if (rtphdr == NULL)
		return;
	fprintf(stderr, "\tts %u seq %u ssrc %#x\n",
		rtphdr->ts, rtphdr->seq, rtphdr->ssrc);
	/*
	uint16_t	v:2;
	uint16_t	p:1;
	uint16_t	x:1;
	uint16_t	cc:4;
	uint16_t	m:1;
	uint16_t	pt:7;
	uint32_t	csrc[];
	*/
}
