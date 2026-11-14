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

all:		P25Reflector

P25Reflector:	$(OBJECTS) $(OBPOBJS)
		$(CXX) $(OBJECTS) $(OBPOBJS) $(CFLAGS) $(LIBS) -o P25Reflector

%.o: %.cpp
		$(CXX) $(CFLAGS) -c -o $@ $<

$(OPENBRIDGE)/%.o: $(OPENBRIDGE)/%.c
		$(CC) $(CFLAGS_C) -c -o $@ $<

install:
		install -m 755 P25Reflector /usr/local/bin/

clean:
		$(RM) P25Reflector *.o *.d *.bak *~
		$(RM) $(OBPOBJS)
