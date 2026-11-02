CC      = cc
CXX     = c++
CFLAGS  = -g -O3 -Wall -std=c++0x -pthread -DHAVE_LOG_H -DUDP_SOCKET_MAX=2
LIBS    = -lpthread
LDFLAGS = -g

CODEC_OBJECTS = CRC.o Hamming.o BPTC19696.o RS129.o QR1676.o Golay2087.o Golay24128.o DMRLC.o \
		DMRSlotType.o DMRFullLC.o DMREMB.o DMREmbeddedData.o YSFConvolution.o YSFFICH.o \
		YSFPayload.o Sync.o ModeConv.o DMRIdLookup.o OpenBridge.o

OBJECTS = BlockList.o Conf.o Log.o Network.o StopWatch.o Thread.o Timer.o UDPSocket.o Utils.o YSFReflector.o $(CODEC_OBJECTS)

TEST_OBJECTS = Conf.o Log.o Timer.o UDPSocket.o Utils.o OpenBridgeTest.o $(CODEC_OBJECTS)

all:		YSFReflector

YSFReflector:	$(OBJECTS)
		$(CXX) $(OBJECTS) $(CFLAGS) $(LIBS) -o YSFReflector

OpenBridgeTest:	$(TEST_OBJECTS)
		$(CXX) $(TEST_OBJECTS) $(CFLAGS) $(LIBS) -o OpenBridgeTest

test:		OpenBridgeTest
		./OpenBridgeTest

%.o: %.cpp
		$(CXX) $(CFLAGS) -c -o $@ $<

YSFReflector.o: GitVersion.h FORCE

.PHONY: GitVersion.h

FORCE:

install:
		install -m 755 YSFReflector /usr/local/bin/

clean:
		$(RM) YSFReflector OpenBridgeTest *.o *.d *.bak *~ GitVersion.h
 
# Export the current git version if the index file exists, else 000...
GitVersion.h:
ifneq ("$(wildcard ../.git/index)","")
	echo "const char *gitversion = \"$(shell git rev-parse HEAD)\";" > $@
else
	echo "const char *gitversion = \"0000000000000000000000000000000000000000\";" > $@
endif
