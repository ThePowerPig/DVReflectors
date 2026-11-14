/*
 *   Copyright (C) 2026 by the DVReflectors contributors
 *
 *   This program is free software; you can redistribute it and/or modify
 *   it under the terms of the GNU General Public License as published by
 *   the Free Software Foundation; either version 2 of the License, or
 *   (at your option) any later version.
 *
 *   This program is distributed in the hope that it will be useful,
 *   but WITHOUT ANY WARRANTY; without even the implied warranty of
 *   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *   GNU General Public License for more details.
 *
 *   You should have received a copy of the GNU General Public License
 *   along with this program; if not, write to the Free Software
 *   Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
 */

// ObpNetwork.h — the OpenBridge (OBP / DMRD) link for P25Reflector.
//
// Mirrors NXDNReflector's CIcomNetwork: a separate class that owns the peer
// socket, is polled in the main loop next to the local CNetwork, and does the
// audio interchange.  Unlike Icom (which copies 33-byte C4FM blocks
// unchanged), OBP carries DMR voice (AMBE+2) while the P25 side carries IMBE,
// so this class performs the P25<->DMR voice translation through a CVocoder.
//
// Wire summary (see OpenBridge/obp.h + dmr_voice.h):
//   * A packet is a 73-byte DMRD datagram: 53-byte body + 20-byte HMAC-SHA1.
//   * A voice call is a VHEAD (DATASYNC/VHEAD) datagram, N voice-burst
//     datagrams (each carries 3 x 7-byte AMBE+2 units = one 60 ms slot), and a
//     VTERM (DATASYNC/VTERM) datagram.
//
// RX (OBP -> P25):  read+verify DMRD, track the stream, convert each burst's
//   3 AMBE units to 3 P25 IMBE voice frames, and emit them as P25 voice frames
//   (18-frame 0x62..0x73 cycle, with 0x64/0x65/0x66 header on the first three)
//   plus a 0x80 terminator at VTERM.
//
// TX (P25 -> OBP):  feed() each local P25 voice frame; extract the 11-byte IMBE
//   word, convert to an AMBE unit, accumulate three units per burst, and emit
//   VHEAD, voice bursts, and VTERM as the local transmission progresses.

#if !defined(ObpNetwork_H)
#define	ObpNetwork_H

#include "UDPSocket.h"
#include "Vocoder.h"
#include "Timer.h"

#include <cstdint>
#include <deque>
#include <string>
#include <vector>

// One queued P25 voice frame to deliver to the local repeaters.
struct P25Frame {
	unsigned char  type;
	unsigned int   length;
	unsigned char  data[22U];
};

class CObpNetwork {
public:
	CObpNetwork(const std::string& address, unsigned short peerPort, unsigned short port,
		const std::string& passphrase, unsigned int netid, const std::string& callsign,
		bool vocoderEnabled, const std::string& vocoderAddress,
		unsigned short vocoderAmbePort, unsigned short vocoderImbePort,
		bool debug);
	~CObpNetwork();

	bool open();
	void close();

	// Non-blocking: drain any pending DMRD datagrams (verify + convert to P25
	// frames) and return one P25 frame from the output queue, or 0 if empty.
	// The caller forwards the returned frame to every local repeater.
	unsigned int readP25Frame(unsigned char* data);

	// Feed one P25 voice frame received from a local repeater (the frame the
	// main loop is about to forward).  The bridge extracts the IMBE word,
	// converts to AMBE, and assembles OBP DMRD bursts.  No-op for poll/unlink.
	void feedP25Frame(const unsigned char* data, unsigned int length);

	// Advance timers (RX silence watchdog, stream tracking).  Call every loop.
	void clock(unsigned int ms);

private:
	// --- OBP link ----------------------------------------------------------
	std::string         m_address;        // OBP peer address (resolved in open())
	unsigned short      m_peerPort;
	CUDPSocket          m_socket;
	unsigned short      m_port;           // local UDP bind port (0 = ephemeral)
	sockaddr_storage    m_peer;
	unsigned int        m_peerLen;
	std::string         m_passphrase;
	unsigned int        m_netid;
	std::string         m_callsign;
	unsigned char       m_seq;          // TX DMRD sequence counter
	bool                m_debug;

	// --- Vocoder (owns the conversion engine) -----------------------------
	bool                m_vocoderEnabled;
	std::string         m_vocoderAddress;
	unsigned short      m_vocoderAmbePort;
	unsigned short      m_vocoderImbePort;

	// --- RX stream tracking (OBP -> P25) -----------------------------------
	bool                m_rxActive;
	unsigned char       m_rxStream[4U];
	unsigned int        m_rxSrc;
	unsigned int        m_rxDst;
	unsigned char       m_rxEmb[4U][4U];
	unsigned int        m_rxDmrId;      // DMR ID shown in logs
	CTimer              m_rxTimer;

	// P25 synthesis state for the RX direction.
	unsigned char       m_rxP25Type;    // current cycling frame type (0x62..0x73)
	unsigned int        m_rxP25Count;   // IMBE words emitted so far this call
	bool                m_rxHdr64;      // 0x64 LCF header emitted
	bool                m_rxHdr65;      // 0x65 dst header emitted
	bool                m_rxHdr66;      // 0x66 src header emitted

	std::deque<P25Frame> m_outQueue;    // P25 frames waiting to be delivered

	// --- TX stream state (P25 -> OBP) --------------------------------------
	bool                m_txActive;
	unsigned char       m_txStream[4U];
	unsigned int        m_txDst;
	unsigned int        m_txSrc;
	unsigned char       m_txEmb[4U][4U];
	unsigned char       m_txLcf;
	bool                m_txStarted;    // VHEAD sent
	unsigned char       m_txAmbe[21U];  // accumulate 3 x 7-byte AMBE units
	unsigned int        m_txAmbeCount;  // 0..3
	unsigned int        m_txPos;        // 0..5 embed cycle position
	CTimer              m_txFailTimer;  // TX idle watchdog (restarted by each fed frame)
	bool                m_vocoderFailLogged;

	CVocoder*           m_vocoder;

	// --- helpers -----------------------------------------------------------
	void queueP25Frame(const P25Frame& frame);
	void emitRXHeader();                 // build + queue 0x64/0x65/0x66 (silence)
	void emitRXVoice(const unsigned char imbe11[11]);  // one IMBE -> one P25 frame
	void emitRXEnd();                    // queue 0x80
	void sendDMRD(const unsigned char flags, const unsigned char payload33[33]);
	void flushTXBurst();                 // send accumulated 3 AMBE units as a burst
	void startTXStream();
	void endTXStream();
	void resetTX();                      // clear all TX state
	void processTxImbe(const unsigned char* imbe11);  // convert + accumulate one IMBE
	void buildTxLc(unsigned char lc[9U]) const;       // Full LC from current TX ids
	bool ambeUnitToImbe(const unsigned char ambe7[7], unsigned char imbe11[11]);
	bool imbeToAmbeUnit(const unsigned char imbe11[11], unsigned char ambe7[7]);
	static void randomStream(unsigned char stream[4U]);
};

#endif
