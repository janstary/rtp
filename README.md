# RTP (the tool, not the protocol)

The `rtp` tools reads and writes RTP sessions.
It uses the same dump format as 
[https://github.com/columbia-irt/rtptools](rtptools)
which it aims to replace.

## installation

```sh
$ git clone git@github.com:janstary/rtp.git && cd rtp
$ echo 'PREFIX="$HOME"' > configure.local
$ ./configure && make install test
$ man rtp
```

