# RTP (the tool, not the protocol)

The `rtp` tool reads and writes RTP sessions.
It uses the same dump format as 
[rtptools](https://github.com/columbia-irt/rtptools)
which it aims to replace.

## historical context

In the early days of RTP, Henning Schulzrinne wrote `rtptools`,
one of the earliest tools to inspect and debug RTP sessions
and the behaviour of various RTP implementations.
While being an invaluable tool, the rtptools code base
is rooted quite a bit in the past, still using GNU workarounds
from the nineties for things that have a modern equivalent.
After contributing to rtptools for some time,
I decided to rewrite from scratch - preserving the format.

Note that RTP sessions can nowadays also be inspected in wirweshark,
down to the actual audio payload.

## installation

```sh
$ git clone git@github.com:janstary/rtp.git && cd rtp
$ echo 'PREFIX="$HOME"' > configure.local
$ ./configure && make install test
$ man rtp
```

