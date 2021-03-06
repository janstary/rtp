#include <arpa/inet.h>
#include <stdio.h>

#include "format-rtp.h"

void
print_rtphdr(struct rtphdr* rtp)
{
	if (rtp == NULL)
		return;
	fprintf(stderr, " %c version %u, ts %u, seq %u, ssrc %#x, pt %u\n",
		rtp->m ? '*' : ' ', rtp->v, ntohl(rtp->ts),
		ntohs(rtp->seq), ntohl(rtp->ssrc), rtp->pt);
	if (rtp->cc) {
		fprintf(stderr, "   (sources: ");
		/* FIXME */
		fprintf(stderr, ")\n");
	}
	/* FIXME pad rtp->p */
	/* FIXME ext rtp->x */
	/* FIXME csrc's */
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
