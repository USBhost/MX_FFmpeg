#!/bin/sh
gcc -Wall -I../include speex_jitter_buffer.c speexclient.c alsa_device.c -o speexclient -lspeex -lspeexdsp -lasound -lm
