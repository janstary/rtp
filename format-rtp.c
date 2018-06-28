#include <stdio.h>
#include "format-rtp.h"

void
print_rtphdr(struct rtphdr* rtp)
{
	if (rtp == NULL)
		return;
	fprintf(stderr, " %c version %u, ts %u, seq %u, ssrc %#x, pt %u\n",
		rtp->m ? '*' : ' ', rtp->v, rtp->ts,
		rtp->seq, rtp->ssrc, rtp->pt);
	if (rtp->cc) {
		fprintf(stderr, "   (sources: ");
		for (unsigned c = 0; c < rtp->cc; c++) {
		}
		fprintf(stderr, ")\n");
	}
	/* FIXME pad rtp->p */
	/* FIXME ext rtp->x */
}

/* Parse a RTP header, return size, or -1 for error. */
ssize_t
parse_rtphdr(struct rtphdr* rtp)
{
	ssize_t size = 0;
	struct rtpext* ext;
	if (rtp == NULL)
		return -1;
	size += 12 + rtp->cc * 4;
	if (rtp->x) {
		ext = (struct rtpext*)((unsigned char*)rtp + size);
		size += 4 + ext->elen * 4;
	}
	return size;
}
