# Unit tests for the OpenBridge library.
#   make          - build test_dsp and test_openbridge
#   make run      - build and run both
#   make clean
CC      = cc
CFLAGS  = -g -O2 -Wall -Wextra -std=c11
TOP     = ..

LIBOB   = $(TOP)/obp.c \
          $(TOP)/dmr_voice.c \
          $(TOP)/crypto.c \
          $(TOP)/dmr/dmr_bits.c \
          $(TOP)/dmr/ambe.c \
          $(TOP)/dmr/bptc.c \
          $(TOP)/dmr/crc.c \
          $(TOP)/dmr/dmr_const.c \
          $(TOP)/dmr/golay.c \
          $(TOP)/dmr/hamming.c \
          $(TOP)/dmr/rs129.c

all: test_dsp test_openbridge xcheck

test_dsp: test_dsp.c $(LIBOB)
	$(CC) $(CFLAGS) -o $@ test_dsp.c $(LIBOB)

test_openbridge: test_openbridge.c $(LIBOB)
	$(CC) $(CFLAGS) -o $@ test_openbridge.c $(LIBOB)

xcheck: xcheck.c $(LIBOB)
	$(CC) $(CFLAGS) -o $@ xcheck.c $(LIBOB)

run: all
	./test_dsp dsp_vectors.txt
	./test_openbridge
	python3 interop_check.py

clean:
	rm -f test_dsp test_openbridge xcheck

.PHONY: all run clean
