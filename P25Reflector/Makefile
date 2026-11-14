CC      = cc
CXX     = c++
CFLAGS  = -g -O3 -Wall -DHAVE_LOG_H -DUDP_SOCKET_MAX=4 -std=c++0x -pthread -I../OpenBridge
CFLAGS_C = -g -O3 -Wall -std=c11
LIBS    = -lpthread
LDFLAGS = -g

OPENBRIDGE = ../OpenBridge

OBPOBJS = $(OPENBRIDGE)/obp.o $(OPENBRIDGE)/dmr_voice.o $(OPENBRIDGE)/crypto.o \
	$(OPENBRIDGE)/dmr/dmr_bits.o $(OPENBRIDGE)/dmr/ambe.o $(OPENBRIDGE)/dmr/bptc.o \
	$(OPENBRIDGE)/dmr/crc.o $(OPENBRIDGE)/dmr/dmr_const.o $(OPENBRIDGE)/dmr/golay.o \
	$(OPENBRIDGE)/dmr/hamming.o $(OPENBRIDGE)/dmr/rs129.o

OBJECTS = Conf.o DMRLookup.o Log.o Mutex.o Network.o ObpNetwork.o P25Reflector.o StopWatch.o Thread.o Timer.o UDPSocket.o Utils.o Vocoder.o

# --- builtin vocoder (mbelib-neo) -------------------------------------------
# When the mbelib-neo static library is present (looked for at ../mbelib-neo
# or ../../mbelib-neo, or set MBELIB_LIB/override directly), CBuiltinVocoder
# is compiled in and selected at runtime with [Vocoder] Builtin=1.
# Build mbelib-neo once with:  mkdir -p build && cd build && cmake .. && make
MBELIB_LIB ?= $(firstword $(wildcard ../mbelib-neo/build/libmbe-neo.a ../../mbelib-neo/build/libmbe-neo.a))
MBELIB_INC ?= $(patsubst %/build/libmbe-neo.a,%/include,$(MBELIB_LIB))

ifneq ($(strip $(MBELIB_LIB)),)
CFLAGS  += -DHAVE_MBELIB -I$(MBELIB_INC)
OBJECTS += BuiltinVocoder.o
MBELIB_LINK = $(MBELIB_LIB) -lm
endif

all:		P25Reflector

P25Reflector:	$(OBJECTS) $(OBPOBJS)
		$(CXX) $(OBJECTS) $(OBPOBJS) $(CFLAGS) $(LIBS) $(MBELIB_LINK) -o P25Reflector

%.o: %.cpp
		$(CXX) $(CFLAGS) -c -o $@ $<

$(OPENBRIDGE)/%.o: $(OPENBRIDGE)/%.c
		$(CC) $(CFLAGS_C) -c -o $@ $<

install:
		install -m 755 P25Reflector /usr/local/bin/

clean:
		$(RM) P25Reflector *.o *.d *.bak *~
		$(RM) $(OBPOBJS)
