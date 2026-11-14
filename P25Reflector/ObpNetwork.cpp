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

#include "ObpNetwork.h"
#include "Log.h"
#include "Utils.h"
#include "BuiltinVocoder.h"

#include "obp.h"
#include "dmr_voice.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <cassert>

// P25 voice frame templates — byte-for-byte identical to the REC62..REC80
// records in P25Gateway's Voice.cpp (the P25 wire ground truth).  Each record
// is a template: the 11-byte IMBE word is spliced in at the per-type offset.
static const unsigned char REC62[] = {
	0x62U, 0x02U, 0x02U, 0x0CU, 0x0BU, 0x12U, 0x64U, 0x00U, 0x00U, 0x80U, 0x00U, 0x00U, 0x00U, 0x00U, 0x00U, 0x00U, 0x00U,
	0x00U, 0x00U, 0x00U, 0x00U, 0x00U };

static const unsigned char REC63[] = {
	0x63U, 0x00U, 0x00U, 0x00U, 0x00U, 0x00U, 0x00U, 0x00U, 0x00U, 0x00U, 0x00U, 0x00U, 0x00U, 0x02U };

static const unsigned char REC64[] = {
	0x64U, 0x00U, 0x00U, 0x00U, 0x00U, 0x00U, 0x00U, 0x00U, 0x00U, 0x00U, 0x00U, 0x00U, 0x00U, 0x00U, 0x00U, 0x00U, 0x02U };

static const unsigned char REC65[] = {
	0x65U, 0x00U, 0x00U, 0x00U, 0x00U, 0x00U, 0x00U, 0x00U, 0x00U, 0x00U, 0x00U, 0x00U, 0x00U, 0x00U, 0x00U, 0x00U, 0x02U };

static const unsigned char REC66[] = {
	0x66U, 0x00U, 0x00U, 0x00U, 0x00U, 0x00U, 0x00U, 0x00U, 0x00U, 0x00U, 0x00U, 0x00U, 0x00U, 0x00U, 0x00U, 0x00U, 0x02U };

static const unsigned char REC67[] = {
	0x67U, 0xF0U, 0x9DU, 0x6AU, 0x00U, 0x00U, 0x00U, 0x00U, 0x00U, 0x00U, 0x00U, 0x00U, 0x00U, 0x00U, 0x00U, 0x00U, 0x02U };

static const unsigned char REC68[] = {
	0x68U, 0x19U, 0xD4U, 0x26U, 0x00U, 0x00U, 0x00U, 0x00U, 0x00U, 0x00U, 0x00U, 0x00U, 0x00U, 0x00U, 0x00U, 0x00U, 0x02U };

static const unsigned char REC69[] = {
	0x69U, 0xE0U, 0xEBU, 0x7BU, 0x00U, 0x00U, 0x00U, 0x00U, 0x00U, 0x00U, 0x00U, 0x00U, 0x00U, 0x00U, 0x00U, 0x00U, 0x02U };

static const unsigned char REC6A[] = {
	0x6AU, 0x00U, 0x00U, 0x02U, 0x00U, 0x00U, 0x00U, 0x00U, 0x00U, 0x00U, 0x00U, 0x00U, 0x00U, 0x00U, 0x00U, 0x00U };

static const unsigned char REC6B[] = {
	0x6BU, 0x02U, 0x02U, 0x0CU, 0x0BU, 0x12U, 0x64U, 0x00U, 0x00U, 0x80U, 0x00U, 0x00U, 0x00U, 0x00U, 0x00U, 0x00U, 0x00U,
	0x00U, 0x00U, 0x00U, 0x00U, 0x00U };

static const unsigned char REC6C[] = {
	0x6CU, 0x00U, 0x00U, 0x00U, 0x00U, 0x00U, 0x00U, 0x00U, 0x00U, 0x00U, 0x00U, 0x00U, 0x00U, 0x02U };

static const unsigned char REC6D[] = {
	0x6DU, 0x00U, 0x00U, 0x00U, 0x00U, 0x00U, 0x00U, 0x00U, 0x00U, 0x00U, 0x00U, 0x00U, 0x00U, 0x00U, 0x00U, 0x00U, 0x02U };

static const unsigned char REC6E[] = {
	0x6EU, 0x00U, 0x00U, 0x00U, 0x00U, 0x00U, 0x00U, 0x00U, 0x00U, 0x00U, 0x00U, 0x00U, 0x00U, 0x00U, 0x00U, 0x00U, 0x02U };

static const unsigned char REC6F[] = {
	0x6FU, 0x00U, 0x00U, 0x00U, 0x00U, 0x00U, 0x00U, 0x00U, 0x00U, 0x00U, 0x00U, 0x00U, 0x00U, 0x00U, 0x00U, 0x00U, 0x02U };

static const unsigned char REC70[] = {
	0x70U, 0x00U, 0x00U, 0x00U, 0x00U, 0x00U, 0x00U, 0x00U, 0x00U, 0x00U, 0x00U, 0x00U, 0x00U, 0x00U, 0x00U, 0x00U, 0x02U };

static const unsigned char REC71[] = {
	0x71U, 0xACU, 0xB8U, 0xA4U, 0x00U, 0x00U, 0x00U, 0x00U, 0x00U, 0x00U, 0x00U, 0x00U, 0x00U, 0x00U, 0x00U, 0x00U, 0x02U };

static const unsigned char REC72[] = {
	0x72U, 0x9BU, 0xDCU, 0x75U, 0x00U, 0x00U, 0x00U, 0x00U, 0x00U, 0x00U, 0x00U, 0x00U, 0x00U, 0x00U, 0x00U, 0x00U, 0x02U };

static const unsigned char REC73[] = {
	0x73U, 0x00U, 0x00U, 0x02U, 0x00U, 0x00U, 0x00U, 0x00U, 0x00U, 0x00U, 0x00U, 0x00U, 0x00U, 0x00U, 0x00U, 0x00U };

static const unsigned char REC80[] = {
	0x80U, 0x00U, 0x00U, 0x00U, 0x00U, 0x00U, 0x00U, 0x00U, 0x00U, 0x00U, 0x00U, 0x00U, 0x00U, 0x00U, 0x00U, 0x00U, 0x00U };

// Standard P25 silence IMBE word (same constant as P25Gateway Voice.cpp).
static const unsigned char SILENCE_IMBE[] = {
	0x04U, 0x0CU, 0xFDU, 0x7BU, 0xFBU, 0x7DU, 0xF2U, 0x7BU, 0x3DU, 0x9EU, 0x44U };

static const unsigned int IMBE_LENGTH = 11U;

// Defensive cap on the RX output queue — drop the oldest frame if the
// reflector main loop ever falls behind.
static const unsigned int MAX_OUT_QUEUE = 250U;

// IMBE offset within each local P25 voice frame type (0xFF = no IMBE).
static unsigned int imbeOffset(unsigned char type)
{
	switch (type) {
	case 0x62U:
	case 0x6BU:
		return 10U;
	case 0x63U:
	case 0x6CU:
		return 1U;
	case 0x6AU:
	case 0x73U:
		return 4U;
	case 0x64U:
	case 0x65U:
	case 0x66U:
	case 0x67U:
	case 0x68U:
	case 0x69U:
	case 0x6DU:
	case 0x6EU:
	case 0x6FU:
	case 0x70U:
	case 0x71U:
	case 0x72U:
		return 5U;
	default:
		return 0xFFU;
	}
}

CObpNetwork::CObpNetwork(const std::string& address, unsigned short peerPort, unsigned short port,
	const std::string& passphrase, unsigned int netid, const std::string& callsign,
	bool vocoderEnabled, bool vocoderBuiltin, const std::string& vocoderAddress,
	unsigned short vocoderAmbePort, unsigned short vocoderImbePort,
	bool debug) :
m_address(),
m_socket(),
m_port(port),
m_peer(),
m_peerLen(0U),
m_passphrase(passphrase),
m_netid(netid),
m_callsign(callsign),
m_seq(0U),
m_debug(debug),
m_vocoderEnabled(vocoderEnabled),
m_vocoderBuiltin(vocoderBuiltin),
m_vocoderAddress(vocoderAddress),
m_vocoderAmbePort(vocoderAmbePort),
m_vocoderImbePort(vocoderImbePort),
m_rxActive(false),
m_rxStream(),
m_rxSrc(0U),
m_rxDst(0U),
m_rxEmb(),
m_rxDmrId(0U),
m_rxTimer(1000U, 5U),
m_rxP25Type(0x62U),
m_rxP25Count(0U),
m_rxHdr64(false),
m_rxHdr65(false),
m_rxHdr66(false),
m_outQueue(),
m_txActive(false),
m_txStream(),
m_txDst(0U),
m_txSrc(0U),
m_txEmb(),
m_txLcf(0U),
m_txStarted(false),
m_txAmbe(),
m_txAmbeCount(0U),
m_txPos(0U),
m_txFailTimer(1000U, 3U),
m_vocoderFailLogged(false),
m_vocoder(NULL)
{
	::memset(&m_peer, 0, sizeof(struct sockaddr_storage));
	::memset(m_rxStream, 0, sizeof(m_rxStream));
	::memset(m_txStream, 0, sizeof(m_txStream));
	::memset(m_rxEmb, 0, sizeof(m_rxEmb));
	::memset(m_txEmb, 0, sizeof(m_txEmb));
	::memset(m_txAmbe, 0, sizeof(m_txAmbe));

	// The ctor address/peerPort are resolved in open(); keep them in members.
	m_address = address;
	m_peerPort = peerPort;
}

CObpNetwork::~CObpNetwork()
{
	close();
}

bool CObpNetwork::open()
{
	int err = CUDPSocket::lookup(m_address, m_peerPort, m_peer, m_peerLen);
	if (err != 0) {
		LogError("OBP: cannot resolve the OpenBridge peer address %s", m_address.c_str());
		return false;
	}

	// Bind the local OBP port so the server can send DMRD datagrams to us
	// (port 0 = ephemeral, effectively TX-only unless the peer learns the port).
	m_socket = CUDPSocket(m_port);
	if (!m_socket.open()) {
		LogError("OBP: cannot open the local UDP port %hu", m_port);
		return false;
	}

	LogMessage("Opened the OpenBridge link to %s:%u (local port %hu)", m_address.c_str(), m_peerPort, m_port);

	if (m_vocoderEnabled) {
		if (m_vocoderBuiltin) {
			m_vocoder = new CBuiltinVocoder(m_debug);
			if (!m_vocoder->open()) {
				LogError("OBP: cannot initialise the builtin vocoder, voice conversion disabled");
				delete m_vocoder;
				m_vocoder = NULL;
			}
		} else {
			m_vocoder = new CVocoder(m_vocoderAddress, m_vocoderAmbePort, m_vocoderImbePort, m_debug);
			if (!m_vocoder->open()) {
				LogError("OBP: cannot open the vocoder connection, voice conversion disabled");
				delete m_vocoder;
				m_vocoder = NULL;
			}
		}
	} else {
		LogMessage("OBP: vocoder disabled, streams are tracked but voice is not bridged");
	}

	return true;
}

void CObpNetwork::close()
{
	if (m_vocoder != NULL) {
		m_vocoder->close();
		delete m_vocoder;
		m_vocoder = NULL;
	}

	m_socket.close();
}

// ---------------------------------------------------------------------------
// RX: OBP (DMRD) -> P25
// ---------------------------------------------------------------------------

unsigned int CObpNetwork::readP25Frame(unsigned char* data)
{
	assert(data != NULL);

	unsigned char pkt[OBP_DMRD_PKT_LEN];
	sockaddr_storage from;
	unsigned int fromLen;

	// Drain every pending DMRD datagram, converting to queued P25 frames.
	for (;;) {
		int n = m_socket.read(pkt, OBP_DMRD_PKT_LEN, from, fromLen);
		if (n <= 0)
			break;

		if ((unsigned int)n != OBP_DMRD_PKT_LEN) {
			if (m_debug)
				LogWarning("OBP: datagram of %d bytes (expected %u), dropped", n, OBP_DMRD_PKT_LEN);
			continue;
		}

		int v = obp_verify(pkt, m_passphrase.c_str());
		if (v != 0) {
			LogWarning("OBP: datagram failed verification (%d), dropped", v);
			continue;
		}

		obp_frame f;
		obp_parse_body(pkt, &f);

		// Reflector policy: unit (private) calls and vcsbk are dropped.
		if (f.is_unit_call || f.is_vcsbk)
			continue;

		if (f.is_head) {
			// A new head while one is active: close the old stream first.
			if (m_rxActive) {
				emitRXEnd();
				m_rxActive = false;
				m_rxTimer.stop();
			}

			::memcpy(m_rxStream, f.stream_id, 4U);
			m_rxSrc      = f.src;
			m_rxDst      = f.dst;
			m_rxDmrId    = f.src;

			unsigned char lc[9U];
			dmr_voice_build_group_lc(lc, f.dst, f.src);
			dmr_encode_emblc(lc, m_rxEmb);

			m_rxP25Type  = 0x62U;
			m_rxP25Count = 0U;
			m_rxHdr64 = m_rxHdr65 = m_rxHdr66 = false;
			m_rxActive = true;
			m_rxTimer.start();

			if (m_vocoder != NULL)
				m_vocoder->resetRx();

			emitRXHeader();

			LogMessage("OBP transmission from %u to TG %u", m_rxSrc, m_rxDst);
		} else if (f.is_term) {
			if (!m_rxActive || ::memcmp(m_rxStream, f.stream_id, 4U) != 0)
				continue;

			emitRXEnd();
			m_rxActive = false;
			m_rxTimer.stop();

			LogMessage("OBP end of transmission from %u to TG %u", m_rxSrc, m_rxDst);
		} else if (f.is_voicesync || f.is_voice) {
			if (!m_rxActive || ::memcmp(m_rxStream, f.stream_id, 4U) != 0)
				continue;

			unsigned char ambe21[21U];
			dmr_voice_parse_burst(f.payload, ambe21);

			for (unsigned int i = 0U; i < 3U; i++) {
				unsigned char imbe11[IMBE_LENGTH];
				if (ambeUnitToImbe(ambe21 + (i * 7U), imbe11))
					emitRXVoice(imbe11);
				else if (!m_vocoderFailLogged) {
					LogWarning("OBP: vocoder conversion failed, voice not bridged (streams still tracked)");
					m_vocoderFailLogged = true;
				}
			}

			m_rxTimer.start();
		}
	}

	if (m_outQueue.empty())
		return 0U;

	P25Frame frame = m_outQueue.front();
	m_outQueue.pop_front();

	::memcpy(data, frame.data, frame.length);

	return frame.length;
}

void CObpNetwork::queueP25Frame(const P25Frame& frame)
{
	if (m_outQueue.size() >= MAX_OUT_QUEUE)
		m_outQueue.pop_front();

	m_outQueue.push_back(frame);
}

void CObpNetwork::emitRXHeader()
{
	P25Frame f;

	// 0x64 — LCF (0x00 = group call), silence IMBE
	f.type   = 0x64U;
	f.length = 17U;
	::memset(f.data, 0x00U, 22U);
	::memcpy(f.data, REC64, 17U);
	::memcpy(f.data + 5U, SILENCE_IMBE, IMBE_LENGTH);
	queueP25Frame(f);
	m_rxHdr64 = true;

	// 0x65 — destination ID
	f.type   = 0x65U;
	f.length = 17U;
	::memset(f.data, 0x00U, 22U);
	::memcpy(f.data, REC65, 17U);
	f.data[1U] = (m_rxDst >> 16) & 0xFFU;
	f.data[2U] = (m_rxDst >> 8) & 0xFFU;
	f.data[3U] = (m_rxDst >> 0) & 0xFFU;
	::memcpy(f.data + 5U, SILENCE_IMBE, IMBE_LENGTH);
	queueP25Frame(f);
	m_rxHdr65 = true;

	// 0x66 — source ID
	f.type   = 0x66U;
	f.length = 17U;
	::memset(f.data, 0x00U, 22U);
	::memcpy(f.data, REC66, 17U);
	f.data[1U] = (m_rxSrc >> 16) & 0xFFU;
	f.data[2U] = (m_rxSrc >> 8) & 0xFFU;
	f.data[3U] = (m_rxSrc >> 0) & 0xFFU;
	::memcpy(f.data + 5U, SILENCE_IMBE, IMBE_LENGTH);
	queueP25Frame(f);
	m_rxHdr66 = true;

	// The voice cycle resumes at the frame type following 0x66.
	m_rxP25Type = 0x67U;
}

void CObpNetwork::emitRXVoice(const unsigned char imbe11[11])
{
	P25Frame f;

	::memset(f.data, 0x00U, 22U);
	f.type = m_rxP25Type;

	switch (m_rxP25Type) {
	case 0x62U:
		::memcpy(f.data, REC62, 22U);
		::memcpy(f.data + 10U, imbe11, IMBE_LENGTH);
		f.length = 22U;
		m_rxP25Type = 0x63U;
		break;
	case 0x63U:
		::memcpy(f.data, REC63, 14U);
		::memcpy(f.data + 1U, imbe11, IMBE_LENGTH);
		f.length = 14U;
		m_rxP25Type = 0x64U;
		break;
	case 0x64U:
		::memcpy(f.data, REC64, 17U);
		f.data[1U] = 0x00U;                      // LCF: group
		::memcpy(f.data + 5U, imbe11, IMBE_LENGTH);
		f.length = 17U;
		m_rxP25Type = 0x65U;
		break;
	case 0x65U:
		::memcpy(f.data, REC65, 17U);
		f.data[1U] = (m_rxDst >> 16) & 0xFFU;
		f.data[2U] = (m_rxDst >> 8) & 0xFFU;
		f.data[3U] = (m_rxDst >> 0) & 0xFFU;
		::memcpy(f.data + 5U, imbe11, IMBE_LENGTH);
		f.length = 17U;
		m_rxP25Type = 0x66U;
		break;
	case 0x66U:
		::memcpy(f.data, REC66, 17U);
		f.data[1U] = (m_rxSrc >> 16) & 0xFFU;
		f.data[2U] = (m_rxSrc >> 8) & 0xFFU;
		f.data[3U] = (m_rxSrc >> 0) & 0xFFU;
		::memcpy(f.data + 5U, imbe11, IMBE_LENGTH);
		f.length = 17U;
		m_rxP25Type = 0x67U;
		break;
	case 0x67U:
		::memcpy(f.data, REC67, 17U);
		::memcpy(f.data + 5U, imbe11, IMBE_LENGTH);
		f.length = 17U;
		m_rxP25Type = 0x68U;
		break;
	case 0x68U:
		::memcpy(f.data, REC68, 17U);
		::memcpy(f.data + 5U, imbe11, IMBE_LENGTH);
		f.length = 17U;
		m_rxP25Type = 0x69U;
		break;
	case 0x69U:
		::memcpy(f.data, REC69, 17U);
		::memcpy(f.data + 5U, imbe11, IMBE_LENGTH);
		f.length = 17U;
		m_rxP25Type = 0x6AU;
		break;
	case 0x6AU:
		::memcpy(f.data, REC6A, 16U);
		::memcpy(f.data + 4U, imbe11, IMBE_LENGTH);
		f.length = 16U;
		m_rxP25Type = 0x6BU;
		break;
	case 0x6BU:
		::memcpy(f.data, REC6B, 22U);
		::memcpy(f.data + 10U, imbe11, IMBE_LENGTH);
		f.length = 22U;
		m_rxP25Type = 0x6CU;
		break;
	case 0x6CU:
		::memcpy(f.data, REC6C, 14U);
		::memcpy(f.data + 1U, imbe11, IMBE_LENGTH);
		f.length = 14U;
		m_rxP25Type = 0x6DU;
		break;
	case 0x6DU:
		::memcpy(f.data, REC6D, 17U);
		::memcpy(f.data + 5U, imbe11, IMBE_LENGTH);
		f.length = 17U;
		m_rxP25Type = 0x6EU;
		break;
	case 0x6EU:
		::memcpy(f.data, REC6E, 17U);
		::memcpy(f.data + 5U, imbe11, IMBE_LENGTH);
		f.length = 17U;
		m_rxP25Type = 0x6FU;
		break;
	case 0x6FU:
		::memcpy(f.data, REC6F, 17U);
		::memcpy(f.data + 5U, imbe11, IMBE_LENGTH);
		f.length = 17U;
		m_rxP25Type = 0x70U;
		break;
	case 0x70U:
		::memcpy(f.data, REC70, 17U);
		::memcpy(f.data + 5U, imbe11, IMBE_LENGTH);
		f.length = 17U;
		m_rxP25Type = 0x71U;
		break;
	case 0x71U:
		::memcpy(f.data, REC71, 17U);
		::memcpy(f.data + 5U, imbe11, IMBE_LENGTH);
		f.length = 17U;
		m_rxP25Type = 0x72U;
		break;
	case 0x72U:
		::memcpy(f.data, REC72, 17U);
		::memcpy(f.data + 5U, imbe11, IMBE_LENGTH);
		f.length = 17U;
		m_rxP25Type = 0x73U;
		break;
	default:                                        // 0x73
		::memcpy(f.data, REC73, 16U);
		::memcpy(f.data + 4U, imbe11, IMBE_LENGTH);
		f.length = 16U;
		m_rxP25Type = 0x62U;
		break;
	}

	m_rxP25Count++;

	queueP25Frame(f);
}

void CObpNetwork::emitRXEnd()
{
	P25Frame f;

	f.type   = 0x80U;
	f.length = 17U;
	::memset(f.data, 0x00U, 22U);
	::memcpy(f.data, REC80, 17U);

	queueP25Frame(f);
}

// ---------------------------------------------------------------------------
// TX: P25 -> OBP (DMRD)
// ---------------------------------------------------------------------------

void CObpNetwork::feedP25Frame(const unsigned char* data, unsigned int length)
{
	assert(data != NULL);

	if (length < 1U)
		return;

	// Poll/unlink frames are never forwarded by the reflector, but guard anyway.
	if (data[0U] == 0xF0U || data[0U] == 0xF1U || data[0U] == 0x80U) {
		if (data[0U] == 0x80U && m_txActive) {
			if (m_txStarted)
				endTXStream();
			resetTX();          // helper resets the TX state
		}
		return;
	}

	switch (data[0U]) {
	case 0x64U:
		if (length < 17U)
			return;
		m_txActive = true;
		m_txLcf    = data[1U];
		processTxImbe(data + 5U);
		break;
	case 0x65U:
		if (length < 17U)
			return;
		m_txActive = true;
		m_txDst = (data[1U] << 16) & 0x00FF0000U;
		m_txDst |= (data[2U] << 8) & 0x0000FF00U;
		m_txDst |= (data[3U] << 0) & 0x000000FFU;
		processTxImbe(data + 5U);
		break;
	case 0x66U:
		if (length < 17U)
			return;
		m_txActive = true;
		m_txSrc = (data[1U] << 16) & 0x00FF0000U;
		m_txSrc |= (data[2U] << 8) & 0x0000FF00U;
		m_txSrc |= (data[3U] << 0) & 0x000000FFU;
		if (!m_txStarted)
			startTXStream();
		processTxImbe(data + 5U);
		break;
	default: {
		if (!m_txActive)
			return;
		unsigned int off = imbeOffset(data[0U]);
		if (off == 0xFFU || length < off + IMBE_LENGTH)
			return;
		processTxImbe(data + off);
		break;
	}
	}

	// TX idle watchdog: any local voice frame keeps the stream alive.
	if (m_txActive)
		m_txFailTimer.start();
}

void CObpNetwork::processTxImbe(const unsigned char* imbe11)
{
	unsigned char ambe7[7U];

	if (!imbeToAmbeUnit(imbe11, ambe7)) {
		if (!m_vocoderFailLogged) {
			LogWarning("OBP: vocoder conversion failed, voice not bridged (streams still tracked)");
			m_vocoderFailLogged = true;
		}
		return;
	}

	::memcpy(m_txAmbe + (m_txAmbeCount * 7U), ambe7, 7U);
	m_txAmbeCount++;

	if (m_txAmbeCount == 3U && m_txStarted)
		flushTXBurst();
}

void CObpNetwork::resetTX()
{
	m_txActive         = false;
	m_txStarted        = false;
	m_txAmbeCount      = 0U;
	m_txPos            = 0U;
	m_txLcf            = 0U;
	m_txDst            = 0U;
	m_txSrc            = 0U;
	m_vocoderFailLogged = false;
	m_txFailTimer.stop();
}

void CObpNetwork::buildTxLc(unsigned char lc[9U]) const
{
	if (m_txLcf == 0x00U) {
		dmr_voice_build_group_lc(lc, m_txDst, m_txSrc);
	} else {
		// Individual call: DMR Full LC with the individual (radio-id) service
		// option.  Experimental path — group calls use the standard layout.
		lc[0U] = 0x90U;
		lc[1U] = 0x00U;
		lc[2U] = 0x00U;
		lc[3U] = (m_txDst >> 16) & 0xFFU;
		lc[4U] = (m_txDst >> 8) & 0xFFU;
		lc[5U] = (m_txDst >> 0) & 0xFFU;
		lc[6U] = (m_txSrc >> 16) & 0xFFU;
		lc[7U] = (m_txSrc >> 8) & 0xFFU;
		lc[8U] = (m_txSrc >> 0) & 0xFFU;
	}
}

void CObpNetwork::startTXStream()
{
	if (m_txDst == 0U)
		return;                                  // no destination yet — wait

	if (m_txSrc == 0U)
		m_txSrc = m_netid & 0x00FFFFFFU;         // fall back to our network ID

	randomStream(m_txStream);

	if (m_vocoder != NULL)
		m_vocoder->resetTx();

	unsigned char lc[9U];
	buildTxLc(lc);
	dmr_encode_emblc(lc, m_txEmb);

	unsigned char out33[OBP_PAYLOAD_LEN];
	dmr_voice_build_head_term(lc, 0, out33);

	sendDMRD(OBPF_FRAMETYPE_DATASYNC | OBPF_SLT_VHEAD, out33);

	m_txStarted = true;

	LogMessage("OBP transmission to TG %u from %u", m_txDst, m_txSrc);
}

void CObpNetwork::endTXStream()
{
	// A trailing partial burst (<3 AMBE units, <60 ms) is dropped — the
	// receiver's silence watchdog closes the call a moment later.
	if (m_txAmbeCount > 0U) {
		if (m_debug)
			LogInfo("OBP: dropping %u trailing AMBE unit(s)", m_txAmbeCount);
		m_txAmbeCount = 0U;
	}

	unsigned char lc[9U];
	buildTxLc(lc);

	unsigned char out33[OBP_PAYLOAD_LEN];
	dmr_voice_build_head_term(lc, 1, out33);

	sendDMRD(OBPF_FRAMETYPE_DATASYNC | OBPF_SLT_VTERM, out33);

	m_txStarted = false;

	LogMessage("OBP end of transmission to TG %u", m_txDst);
}

void CObpNetwork::flushTXBurst()
{
	unsigned char out33[OBP_PAYLOAD_LEN];
	dmr_voice_build_burst(m_txAmbe, m_txPos, m_txEmb, out33);

	unsigned char flags = (m_txPos == 0U) ? OBPF_FRAMETYPE_VOICESYNC : (unsigned char)(OBPF_FRAMETYPE_VOICE | m_txPos);

	sendDMRD(flags, out33);

	m_txPos       = (m_txPos + 1U) % 6U;
	m_txAmbeCount = 0U;
}

void CObpNetwork::sendDMRD(const unsigned char flags, const unsigned char payload33[33])
{
	unsigned char body[OBP_DMRD_BODY_LEN];
	unsigned char pkt[OBP_DMRD_PKT_LEN];

	obp_build_body(body, m_seq, m_txSrc, m_txDst, m_netid, flags, m_txStream, payload33);
	obp_sign(body, m_passphrase.c_str(), pkt);

	if (m_debug)
		CUtils::dump(2U, "OBP TX", pkt, OBP_DMRD_PKT_LEN);

	m_socket.write(pkt, OBP_DMRD_PKT_LEN, m_peer, m_peerLen);

	m_seq = (unsigned char)((m_seq + 1U) & 0xFFU);
}

// ---------------------------------------------------------------------------
// Vocoder conversion helpers
// ---------------------------------------------------------------------------

bool CObpNetwork::ambeUnitToImbe(const unsigned char ambe7[7], unsigned char imbe11[11])
{
	if (m_vocoder == NULL)
		return false;

	short pcm160[160];

	if (!m_vocoder->ambeToPcm(ambe7, pcm160))
		return false;

	return m_vocoder->pcmToImbe(pcm160, imbe11);
}

bool CObpNetwork::imbeToAmbeUnit(const unsigned char imbe11[11], unsigned char ambe7[7])
{
	if (m_vocoder == NULL)
		return false;

	short pcm160[160];

	if (!m_vocoder->imbeToPcm(imbe11, pcm160))
		return false;

	return m_vocoder->pcmToAmbe(pcm160, ambe7);
}

// ---------------------------------------------------------------------------
// Timers
// ---------------------------------------------------------------------------

void CObpNetwork::clock(unsigned int ms)
{
	m_rxTimer.clock(ms);
	if (m_rxActive && m_rxTimer.isRunning() && m_rxTimer.hasExpired()) {
		emitRXEnd();
		m_rxActive = false;
		m_rxTimer.stop();
		LogWarning("OBP RX stream silent, closed");
	}

	m_txFailTimer.clock(ms);
	if (m_txFailTimer.isRunning() && m_txFailTimer.hasExpired()) {
		m_txFailTimer.stop();
		if (m_txActive) {
			if (m_txStarted)
				endTXStream();
			resetTX();
			LogWarning("OBP TX stream timed out (no local frames)");
		}
	}
}

void CObpNetwork::randomStream(unsigned char stream[4U])
{
	static bool seeded = false;

	if (!seeded) {
		::srand((unsigned int)::time(NULL));
		seeded = true;
	}

	for (unsigned int i = 0U; i < 4U; i++)
		stream[i] = (unsigned char)(::rand() & 0xFF);
}
