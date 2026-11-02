/*
 *   Copyright (C) 2026
 *
 *   Optional OpenBridge (HBlink3 / FreeDMR) peer for YSFReflector.
 *   Audio is repacked between YSF DN (V/D mode 2) and DMR AMBE+2; it is
 *   not transcoded. The wire format is a 53-byte DMRD packet plus a
 *   20-byte HMAC-SHA1, on timeslot 1.
 *
 *   Frame conversion follows MMDVM / YSF2DMR (Jonathan Naylor G4KLX,
 *   Andy Uribe CA6JAU), which is also GPL v2.
 *
 *   This program is free software; you can redistribute it and/or modify
 *   it under the terms of the GNU General Public License as published by
 *   the Free Software Foundation; either version 2 of the License, or
 *   (at your option) any later version.
 */

#if !defined(OPENBRIDGE_H)
#define	OPENBRIDGE_H

#include "DMREmbeddedData.h"
#include "DMRIdLookup.h"
#include "ModeConv.h"
#include "UDPSocket.h"
#include "Timer.h"

#include <cstdint>
#include <deque>
#include <string>

struct COpenBridgeConfig {
	bool           enable;
	std::string    address;
	unsigned short port;
	unsigned short localPort;
	unsigned int   networkId;
	std::string    passphrase;
	unsigned int   talkgroup;
	std::string    idFile;
	unsigned int   fallbackId;
	bool           debug;
	std::string    gateway;
	unsigned char  colorCode;

	COpenBridgeConfig() :
	enable(false),
	address(),
	port(0U),
	localPort(0U),
	networkId(0U),
	passphrase(),
	talkgroup(0U),
	idFile(),
	fallbackId(0U),
	debug(false),
	gateway(),
	colorCode(1U)
	{
	}
};

class COpenBridgeWire {
public:
	static const unsigned int DMRD_LENGTH = 53U;
	static const unsigned int OB_LENGTH   = 73U;

	static void sign(const unsigned char* dmr, unsigned int dmrLen, const std::string& passphrase, unsigned char* out);
	static bool verify(const unsigned char* packet, unsigned int len, const std::string& passphrase, unsigned char* dmrOut);
	static void mac(const std::string& key, const unsigned char* data, unsigned int length, unsigned char out[20]);

	static void build(unsigned char* dmr, unsigned char seq, unsigned int srcId, unsigned int dstId, unsigned int networkId, unsigned char control, const unsigned char streamId[4], const unsigned char frame[33]);
};

struct OBPacket {
	unsigned char data[155U];
	unsigned int  length;
};

class COpenBridge {
public:
	explicit COpenBridge(const COpenBridgeConfig& conf);
	~COpenBridge();

	bool open();
	void close();

	bool isActive() const;

	// A real YSF client frame (155-byte YSFD). Ignored when the link is idle
	// in the other direction, or when the frame is not DN.
	void writeYSF(const unsigned char* data, unsigned int length);

	// Advance pacing, read the peer socket, and end streams that go quiet.
	void clock(unsigned int ms);

	// One bridged YSFD frame ready for every linked YSF client, if any.
	bool readYSF(unsigned char* data);

	// Re-submit a datagram as though the configured peer sent it.
	void injectFromPeer(const unsigned char* data, unsigned int length);

	const unsigned char* lastPacket(unsigned int& length) const;

private:
	enum STATE {
		ST_IDLE,
		ST_YSF_TO_DMR,
		ST_DMR_TO_YSF
	};

	COpenBridge(const COpenBridge&);
	COpenBridge& operator=(const COpenBridge&);

	bool beginYSF(const std::string& callsign);
	void endYSF();
	void beginDMR(unsigned int srcId, const unsigned char streamId[4]);
	void finishDMR();

	void processDatagram(const unsigned char* data, unsigned int length, const sockaddr_storage& addr);
	void drainDMR();
	void drainYSF();
	void pace(unsigned int ms);

	void queueHeader(unsigned char dataType);
	void queueVoice(unsigned char* frame, unsigned int n);
	void queueDMR(unsigned char control, const unsigned char frame[33]);
	void enqueueYSF(unsigned int tag, const unsigned char* raw);
	void rememberStream(const unsigned char id[4]);
	bool isOwnStream(const unsigned char id[4]) const;

	void sendFront();

	COpenBridgeConfig          m_conf;
	CUDPSocket                 m_socket;
	sockaddr_storage           m_peer;
	unsigned int               m_peerLen;
	CDMRIdLookup               m_ids;
	CModeConv                  m_conv;
	CDMREmbeddedData           m_embedded;
	CTimer                     m_watchdog;
	bool                       m_active;
	STATE                      m_state;
	bool                       m_ending;
	bool                       m_termQueued;
	bool                       m_gotHeader;
	unsigned int               m_srcId;
	unsigned int               m_voiceN;
	unsigned int               m_rxFrames;
	unsigned int               m_ysfCnt;
	unsigned char              m_seq;
	unsigned char              m_txStream[4U];
	unsigned char              m_rxStream[4U];
	unsigned char              m_txStreams[16U][4U];
	unsigned int               m_txStreamNext;
	unsigned int               m_txStreamCount;
	unsigned char              m_gateway[10U];
	unsigned char              m_rxCall[10U];
	unsigned int               m_dmrAccum;
	unsigned int               m_ysfAccum;
	std::deque<OBPacket>       m_dmrQueue;
	std::deque<OBPacket>       m_ysfQueue;
	std::deque<OBPacket>       m_ysfReady;
	unsigned char              m_lastTx[73U];
	unsigned int               m_lastTxLen;
	bool                       m_loggedUnsupported;
	bool                       m_loggedAuth;
};

#endif
