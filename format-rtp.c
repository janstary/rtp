#include <stdio.h>
#include "format-rtp.h"

void
print_rtphdr(struct rtphdr* rtphdr)
{
	if (rtphdr == NULL)
		return;
	fprintf(stderr, " %c version %u, ts %u, seq %u, ssrc %#x, pt %u%s%s\n",
		rtphdr->m ? '*' : ' ', rtphdr->v,
		rtphdr->ts, rtphdr->seq, rtphdr->ssrc, rtphdr->pt,
		rtphdr->p ? ", padding" : "",
		rtphdr->x ? ", extension" : "");
}
