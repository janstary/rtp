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

Note that RTP sessions can nowadays also be inspected in wireshark,
down to the actual audio payload.

## installation

Short version:

```sh
$ git clone git@github.com:janstary/rtp.git && cd rtp
$ echo 'PREFIX="$HOME"' > configure.local
$ ./configure && make install test
$ man rtp
```

Long version: `rtp` tries to be very portable.
The `./configure` script will configure the build for your machine,
producing `config.log`, `config.h` and `Makefile.local`.
Inspect them to see of they contaion what you would expect.

Note that `./configure` is written by hand (mostly stolen from
[mandoc](http://mandoc.bsd.lv/), actually) and avoids the
GNU `auto*`tools horror. The actual features of the system
are tested with the `have-*.c` tests, and the missing
functionality is provided in `compat-*.c`.

If the autodetection fails on your system, write a `configure.local`;
see `configure.local.example` for what can be tweaked.

Currently, `rtp` is tested on the following platforms:

* OpenBSD 6.3 @ amd64
* OpenBSD 6.3 @ macppc
* OpenBSD 6.3 @ armv7
* SunOS 5.11 @ sparc
* MacOS 10.13 @ x64
* Debian 7.11 @ x64
* Gentoo 2.4.1 @ x64
