/*
 *   Copyright (C) 2026
 *
 *   This program is free software; you can redistribute it and/or modify
 *   it under the terms of the GNU General Public License as published by
 *   the Free Software Foundation; either version 2 of the License, or
 *   (at your option) any later version.
 */

#include "OpenBridge.h"
#include "DMRFullLC.h"
#include "DMRSlotType.h"
#include "DMREMB.h"
#include "DMRDefines.h"
#include "YSFDefines.h"
#include "YSFFICH.h"
#include "YSFPayload.h"
#include "Sync.h"
#include "Utils.h"
#include "Log.h"

#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <stdint.h>

namespace {

const unsigned int DMR_PACE_MS = 60U;
const unsigned int YSF_PACE_MS = 100U;

uint32_t rotl32(uint32_t value, int bits)
{
	return (value << bits) | (value >> (32 - bits));
}

void sha1Transform(uint32_t state[5], const unsigned char block[64])
{
	uint32_t w[80];
	for (unsigned int i = 0U; i < 16U; i++) {
		w[i]  = (uint32_t)block[i * 4U] << 24;
		w[i] |= (uint32_t)block[i * 4U + 1U] << 16;
		w[i] |= (uint32_t)block[i * 4U + 2U] << 8;
		w[i] |= (uint32_t)block[i * 4U + 3U];
	}

	for (unsigned int i = 16U; i < 80U; i++)
		w[i] = rotl32(w[i - 3U] ^ w[i - 8U] ^ w[i - 14U] ^ w[i - 16U], 1);

	uint32_t a = state[0];
	uint32_t b = state[1];
	uint32_t c = state[2];
	uint32_t d = state[3];
	uint32_t e = state[4];

	for (unsigned int i = 0U; i < 80U; i++) {
		uint32_t f;
		uint32_t k;
		if (i < 20U) {
			f = (b & c) | ((~b) & d);
			k = 0x5A827999U;
		} else if (i < 40U) {
			f = b ^ c ^ d;
			k = 0x6ED9EBA1U;
		} else if (i < 60U) {
			f = (b & c) | (b & d) | (c & d);
			k = 0x8F1BBCDCU;
		} else {
			f = b ^ c ^ d;
			k = 0xCA62C1D6U;
		}

		uint32_t temp = rotl32(a, 5) + f + e + k + w[i];
		e = d;
		d = c;
		c = rotl32(b, 30);
		b = a;
		a = temp;
	}

	state[0] += a;
	state[1] += b;
	state[2] += c;
	state[3] += d;
	state[4] += e;
}

struct SHA1Context {
	uint32_t     state[5];
	uint64_t     bits;
	unsigned char buffer[64];
};

void sha1Init(SHA1Context& ctx)
{
	ctx.state[0] = 0x67452301U;
	ctx.state[1] = 0xEFCDAB89U;
	ctx.state[2] = 0x98BADCFEU;
	ctx.state[3] = 0x10325476U;
	ctx.state[4] = 0xC3D2E1F0U;
	ctx.bits = 0U;
}

void sha1Update(SHA1Context& ctx, const unsigned char* data, unsigned int length)
{
	unsigned int index = (unsigned int)((ctx.bits / 8U) % 64U);
	ctx.bits += (uint64_t)length * 8U;

	unsigned int fill = 64U - index;
	unsigned int offset = 0U;

	if (length >= fill) {
		::memcpy(ctx.buffer + index, data, fill);
		sha1Transform(ctx.state, ctx.buffer);
		offset = fill;
		while (offset + 64U <= length) {
			sha1Transform(ctx.state, data + offset);
			offset += 64U;
		}
		index = 0U;
	}

	if (offset < length)
		::memcpy(ctx.buffer + index, data + offset, length - offset);
}

void sha1Final(SHA1Context& ctx, unsigned char digest[20])
{
	uint64_t bits = ctx.bits;
	unsigned char lengthBytes[8];
	for (unsigned int i = 0U; i < 8U; i++)
		lengthBytes[7U - i] = (unsigned char)(bits >> (i * 8U));

	unsigned int index = (unsigned int)((ctx.bits / 8U) % 64U);
	unsigned int padLen = (index < 56U) ? (56U - index) : (120U - index);

	unsigned char padding[64];
	::memset(padding, 0, sizeof(padding));
	padding[0] = 0x80U;
	sha1Update(ctx, padding, padLen);
	sha1Update(ctx, lengthBytes, 8U);

	for (unsigned int i = 0U; i < 5U; i++) {
		digest[i * 4U]      = (unsigned char)(ctx.state[i] >> 24);
		digest[i * 4U + 1U] = (unsigned char)(ctx.state[i] >> 16);
		digest[i * 4U + 2U] = (unsigned char)(ctx.state[i] >> 8);
		digest[i * 4U + 3U] = (unsigned char)(ctx.state[i]);
	}
}

void sha1(const unsigned char* data, unsigned int length, unsigned char digest[20])
{
	SHA1Context ctx;
	sha1Init(ctx);
	sha1Update(ctx, data, length);
	sha1Final(ctx, digest);
}

void hmacSha1(const unsigned char* key, unsigned int keyLen, const unsigned char* data, unsigned int dataLen, unsigned char out[20])
{
	unsigned char k[64];
	::memset(k, 0, sizeof(k));

	if (keyLen > 64U) {
		sha1(key, keyLen, k);
	} else if (key != NULL && keyLen > 0U) {
		::memcpy(k, key, keyLen);
	}

	unsigned char ipad[64];
	unsigned char opad[64];
	for (unsigned int i = 0U; i < 64U; i++) {
		ipad[i] = (unsigned char)(k[i] ^ 0x36U);
		opad[i] = (unsigned char)(k[i] ^ 0x5CU);
	}

	SHA1Context ctx;
	sha1Init(ctx);
	sha1Update(ctx, ipad, 64U);
	if (data != NULL && dataLen > 0U)
		sha1Update(ctx, data, dataLen);

	unsigned char inner[20];
	sha1Final(ctx, inner);

	sha1Init(ctx);
	sha1Update(ctx, opad, 64U);
	sha1Update(ctx, inner, 20U);
	sha1Final(ctx, out);
}

bool hmacEqual(const unsigned char* a, const unsigned char* b, unsigned int n)
{
	unsigned char diff = 0U;
	for (unsigned int i = 0U; i < n; i++)
		diff |= (unsigned char)(a[i] ^ b[i]);

	return diff == 0U;
}

std::string normalizeCall(const unsigned char* text, unsigned int length)
{
	std::string out;
	for (unsigned int i = 0U; i < length && text[i] != '\0'; i++) {
		unsigned char c = text[i];
		if (c == ' ')
			break;
		out.push_back((char)::toupper(c));
	}

	if (out.find('?') != std::string::npos)
		out.clear();

	return out;
}

void padCall(unsigned char out[10], const std::string& text)
{
	::memset(out, ' ', 10U);
	unsigned int n = (unsigned int)text.size();
	if (n > 10U)
		n = 10U;
	for (unsigned int i = 0U; i < n; i++)
		out[i] = (unsigned char)::toupper((unsigned char)text[i]);
}

std::string callFromField(const unsigned char* text, unsigned int length)
{
	return normalizeCall(text, length);
}

} // namespace

void COpenBridgeWire::mac(const std::string& key, const unsigned char* data, unsigned int length, unsigned char out[20])
{
	hmacSha1(reinterpret_cast<const unsigned char*>(key.data()), (unsigned int)key.size(), data, length, out);
}

void COpenBridgeWire::sign(const unsigned char* dmr, unsigned int dmrLen, const std::string& passphrase, unsigned char* out)
{
	::memset(out, 0, OB_LENGTH);
	if (dmr == NULL || out == NULL || dmrLen < DMRD_LENGTH)
		return;

	::memcpy(out, dmr, DMRD_LENGTH);
	hmacSha1(reinterpret_cast<const unsigned char*>(passphrase.data()), (unsigned int)passphrase.size(), out, DMRD_LENGTH, out + DMRD_LENGTH);
}

bool COpenBridgeWire::verify(const unsigned char* packet, unsigned int len, const std::string& passphrase, unsigned char* dmrOut)
{
	if (packet == NULL || dmrOut == NULL || len < OB_LENGTH)
		return false;

	if (::memcmp(packet, "DMRD", 4U) != 0)
		return false;

	unsigned char expect[20];
	hmacSha1(reinterpret_cast<const unsigned char*>(passphrase.data()), (unsigned int)passphrase.size(), packet, DMRD_LENGTH, expect);
	if (!hmacEqual(expect, packet + DMRD_LENGTH, 20U))
		return false;

	::memcpy(dmrOut, packet, DMRD_LENGTH);
	return true;
}

void COpenBridgeWire::build(unsigned char* dmr, unsigned char seq, unsigned int srcId, unsigned int dstId, unsigned int networkId, unsigned char control, const unsigned char streamId[4], const unsigned char frame[33])
{
	::memset(dmr, 0, DMRD_LENGTH);
	dmr[0] = 'D';
	dmr[1] = 'M';
	dmr[2] = 'R';
	dmr[3] = 'D';
	dmr[4] = seq;

	srcId &= 0xFFFFFFU;
	dstId &= 0xFFFFFFU;
	dmr[5]  = (unsigned char)(srcId >> 16);
	dmr[6]  = (unsigned char)(srcId >> 8);
	dmr[7]  = (unsigned char)(srcId);
	dmr[8]  = (unsigned char)(dstId >> 16);
	dmr[9]  = (unsigned char)(dstId >> 8);
	dmr[10] = (unsigned char)(dstId);

	dmr[11] = (unsigned char)(networkId >> 24);
	dmr[12] = (unsigned char)(networkId >> 16);
	dmr[13] = (unsigned char)(networkId >> 8);
	dmr[14] = (unsigned char)(networkId);
	dmr[15] = control;

	::memcpy(dmr + 16U, streamId, 4U);
	::memcpy(dmr + 20U, frame, 33U);
}

COpenBridge::COpenBridge(const COpenBridgeConfig& conf) :
m_conf(conf),
m_socket(conf.localPort),
m_peer(),
m_peerLen(0U),
m_ids(conf.idFile, conf.fallbackId),
m_conv(),
m_embedded(),
m_watchdog(1000U, 0U, 1500U),
m_active(false),
m_state(ST_IDLE),
m_ending(false),
m_termQueued(false),
m_gotHeader(false),
m_srcId(0U),
m_voiceN(0U),
m_rxFrames(0U),
m_ysfCnt(0U),
m_seq(0U),
m_txStream(),
m_rxStream(),
m_txStreams(),
m_txStreamNext(0U),
m_txStreamCount(0U),
m_gateway(),
m_rxCall(),
m_dmrAccum(DMR_PACE_MS),
m_ysfAccum(YSF_PACE_MS),
m_dmrQueue(),
m_ysfQueue(),
m_ysfReady(),
m_lastTx(),
m_lastTxLen(0U),
m_loggedUnsupported(false),
m_loggedAuth(false)
{
	::memset(&m_peer, 0, sizeof(m_peer));
	::memset(m_txStream, 0, sizeof(m_txStream));
	::memset(m_rxStream, 0, sizeof(m_rxStream));
	::memset(m_txStreams, 0, sizeof(m_txStreams));
	::memset(m_gateway, ' ', sizeof(m_gateway));
	::memset(m_rxCall, ' ', sizeof(m_rxCall));
	::memset(m_lastTx, 0, sizeof(m_lastTx));
}

COpenBridge::~COpenBridge()
{
	close();
}

bool COpenBridge::open()
{
	if (!m_conf.enable)
		return true;

	if (m_conf.address.empty() || m_conf.port == 0U || m_conf.localPort == 0U) {
		LogError("OpenBridge needs Address, Port and LocalPort");
		return false;
	}

	if (m_conf.networkId == 0U) {
		LogError("OpenBridge NetworkId must be non-zero");
		return false;
	}

	if (m_conf.passphrase.empty()) {
		LogError("OpenBridge Passphrase is empty");
		return false;
	}

	if (m_conf.talkgroup == 0U || m_conf.talkgroup > 0xFFFFFFU) {
		LogError("OpenBridge Talkgroup must be 1..16777215");
		return false;
	}

	if (m_conf.colorCode > 15U)
		m_conf.colorCode = 1U;

	bool ipv6 = m_conf.address.find(':') != std::string::npos;

	struct addrinfo hints;
	::memset(&hints, 0, sizeof(hints));
	hints.ai_family = ipv6 ? AF_INET6 : AF_INET;
	hints.ai_socktype = SOCK_DGRAM;

	int err = CUDPSocket::lookup(m_conf.address, m_conf.port, m_peer, m_peerLen, hints);
	if (err != 0 || CUDPSocket::isNone(m_peer)) {
		LogError("OpenBridge cannot resolve %s", m_conf.address.c_str());
		return false;
	}

	if (!m_socket.open(ipv6 ? AF_INET6 : AF_INET)) {
		LogError("OpenBridge cannot bind local port %u", m_conf.localPort);
		return false;
	}

	std::string gateway = m_conf.gateway.empty() ? std::string("YSFREFLECT") : m_conf.gateway;
	padCall(m_gateway, gateway);

	m_ids.load();
	::srand((unsigned int)::time(NULL));

	m_active = true;
	LogMessage("OpenBridge listening on %u, peer %s:%u, TG %u, network %u", m_conf.localPort, m_conf.address.c_str(), m_conf.port, m_conf.talkgroup, m_conf.networkId);

	return true;
}

void COpenBridge::close()
{
	if (m_active)
		m_socket.close();

	m_active = false;
	m_state = ST_IDLE;
}

bool COpenBridge::isActive() const
{
	return m_active;
}

void COpenBridge::rememberStream(const unsigned char id[4])
{
	::memcpy(m_txStreams[m_txStreamNext], id, 4U);
	m_txStreamNext = (m_txStreamNext + 1U) % 16U;
	if (m_txStreamCount < 16U)
		m_txStreamCount++;
}

bool COpenBridge::isOwnStream(const unsigned char id[4]) const
{
	for (unsigned int i = 0U; i < m_txStreamCount; i++) {
		if (::memcmp(m_txStreams[i], id, 4U) == 0)
			return true;
	}

	return false;
}

bool COpenBridge::beginYSF(const std::string& callsign)
{
	unsigned int id = m_ids.findId(callsign);
	if (id == 0U) {
		LogWarning("OpenBridge has no DMR ID for %s; set FallbackId or add it to the lookup file", callsign.c_str());
		return false;
	}

	m_conv.reset();
	m_srcId = id & 0xFFFFFFU;
	m_seq = 0U;
	m_voiceN = 0U;
	m_ending = false;
	m_termQueued = false;

	unsigned int stream = (unsigned int)::rand();
	if (stream == 0U)
		stream = 1U;
	m_txStream[0] = (unsigned char)(stream >> 24);
	m_txStream[1] = (unsigned char)(stream >> 16);
	m_txStream[2] = (unsigned char)(stream >> 8);
	m_txStream[3] = (unsigned char)(stream);
	rememberStream(m_txStream);

	m_conv.putYSFHeader();
	m_state = ST_YSF_TO_DMR;
	m_watchdog.start();

	LogMessage("OpenBridge YSF->DMR %s as %u on TG %u", callsign.c_str(), m_srcId, m_conf.talkgroup);
	return true;
}

void COpenBridge::endYSF()
{
	if (m_state != ST_YSF_TO_DMR || m_ending)
		return;

	m_ending = true;
	m_watchdog.stop();

	// Pull the header and any whole AMBE triplets first. putYSFEOT pads using
	// the converter's running count, which must not still include that header.
	drainDMR();
	m_conv.putYSFEOT();
	drainDMR();
	LogMessage("OpenBridge YSF end of transmission");
}

void COpenBridge::finishDMR()
{
	if (m_state != ST_DMR_TO_YSF || m_ending)
		return;

	drainYSF();
	m_conv.putDMREOT();
	m_ending = true;
	m_watchdog.stop();
	drainYSF();
	LogMessage("OpenBridge DMR end of transmission, %u frames", m_rxFrames);
}

void COpenBridge::beginDMR(unsigned int srcId, const unsigned char streamId[4])
{
	m_conv.reset();
	::memcpy(m_rxStream, streamId, 4U);
	std::string callsign = m_ids.findCallsign(srcId);
	padCall(m_rxCall, callsign);
	m_state = ST_DMR_TO_YSF;
	m_ending = false;
	m_gotHeader = false;
	m_ysfCnt = 0U;
	m_rxFrames = 0U;
	m_watchdog.start();

	LogMessage("OpenBridge DMR->YSF %u (%s) on TG %u", srcId, callsign.c_str(), m_conf.talkgroup);
}

void COpenBridge::writeYSF(const unsigned char* data, unsigned int length)
{
	if (!m_active || data == NULL || length < 155U)
		return;

	if (::memcmp(data, "YSFD", 4U) != 0)
		return;

	// Local audio is still repeated to YSF clients by the reflector. While a
	// DMR transmission is being played out, do not send it back to the peer.
	if (m_state == ST_DMR_TO_YSF)
		return;

	if (m_state == ST_YSF_TO_DMR && m_ending)
		return;

	unsigned char frame[120U];
	::memcpy(frame, data + 35U, 120U);

	CYSFFICH fich;
	bool valid = fich.decode(frame);
	bool eot = (data[34U] & 0x01U) == 0x01U;

	if (valid) {
		unsigned char fi = fich.getFI();
		unsigned char dt = fich.getDT();

		if (dt != YSF_DT_VD_MODE2) {
			if (!m_loggedUnsupported && (fi == YSF_FI_HEADER || fi == YSF_FI_COMMUNICATIONS)) {
				LogInfo("OpenBridge only converts YSF DN (V/D mode 2); data type %u is not bridged", dt);
				m_loggedUnsupported = true;
			}
			if (eot || fi == YSF_FI_TERMINATOR)
				endYSF();
			return;
		}

		std::string callsign = callFromField(data + 14U, YSF_CALLSIGN_LENGTH);
		if (callsign.empty() && fi == YSF_FI_HEADER) {
			unsigned char copy[120U];
			::memcpy(copy, frame, 120U);
			CYSFPayload payload;
			if (payload.processHeaderData(copy)) {
				std::string source = payload.getSource();
				callsign = callFromField(reinterpret_cast<const unsigned char*>(source.c_str()), (unsigned int)source.size());
			}
		}

		if (fi == YSF_FI_HEADER) {
			if (m_state != ST_YSF_TO_DMR) {
				if (!callsign.empty())
					beginYSF(callsign);
			} else {
				m_watchdog.start();
			}
		} else if (fi == YSF_FI_TERMINATOR) {
			endYSF();
		} else if (fi == YSF_FI_COMMUNICATIONS) {
			if (m_state != ST_YSF_TO_DMR) {
				if (callsign.empty() || !beginYSF(callsign))
					return;
			}
			if (!m_ending) {
				m_conv.putYSF(frame);
				m_watchdog.start();
			}
		}
	}

	if (eot)
		endYSF();
}

void COpenBridge::queueDMR(unsigned char control, const unsigned char frame[33])
{
	unsigned char dmr[COpenBridgeWire::DMRD_LENGTH];
	COpenBridgeWire::build(dmr, m_seq, m_srcId, m_conf.talkgroup, m_conf.networkId, control, m_txStream, frame);
	m_seq++;

	OBPacket packet;
	::memset(packet.data, 0, sizeof(packet.data));
	COpenBridgeWire::sign(dmr, COpenBridgeWire::DMRD_LENGTH, m_conf.passphrase, packet.data);
	packet.length = COpenBridgeWire::OB_LENGTH;
	m_dmrQueue.push_back(packet);

	::memcpy(m_lastTx, packet.data, COpenBridgeWire::OB_LENGTH);
	m_lastTxLen = COpenBridgeWire::OB_LENGTH;

	if (m_conf.debug)
		CUtils::dump(1U, "OpenBridge TX", packet.data, packet.length);
}

void COpenBridge::queueHeader(unsigned char dataType)
{
	unsigned char frame[33U];
	::memset(frame, 0, sizeof(frame));

	CSync::addDMRDataSync(frame, false);

	CDMRSlotType slotType;
	slotType.setColorCode(m_conf.colorCode);
	slotType.setDataType(dataType);
	slotType.getData(frame);

	CDMRLC lc(FLCO_GROUP, m_srcId, m_conf.talkgroup);
	CDMRFullLC full;
	full.encode(lc, frame, dataType);
	if (dataType == DT_VOICE_LC_HEADER)
		m_embedded.setLC(lc);

	queueDMR((unsigned char)(0x20U | dataType), frame);
}

void COpenBridge::queueVoice(unsigned char* frame, unsigned int n)
{
	n %= 6U;

	if (n == 0U) {
		CSync::addDMRAudioSync(frame, false);
		CDMRLC lc(FLCO_GROUP, m_srcId, m_conf.talkgroup);
		m_embedded.setLC(lc);
		queueDMR(0x10U, frame);
	} else {
		unsigned char lcss = m_embedded.getData(frame, (unsigned char)n);
		CDMREMB emb;
		emb.setColorCode(m_conf.colorCode);
		emb.setLCSS(lcss);
		emb.getData(frame);
		queueDMR((unsigned char)n, frame);
	}

	m_voiceN++;
}

void COpenBridge::drainDMR()
{
	for (;;) {
		unsigned char frame[33U];
		::memset(frame, 0, sizeof(frame));

		unsigned int tag = m_conv.getDMR(frame);
		if (tag == TAG_NODATA)
			break;

		if (tag == TAG_HEADER) {
			for (unsigned int i = 0U; i < 3U; i++)
				queueHeader(DT_VOICE_LC_HEADER);
		} else if (tag == TAG_EOT) {
			while ((m_voiceN % 6U) != 0U) {
				unsigned char silence[33U];
				::memcpy(silence, DMR_SILENCE_DATA, 33U);
				queueVoice(silence, m_voiceN);
			}
			queueHeader(DT_TERMINATOR_WITH_LC);
			m_termQueued = true;
		} else if (tag == TAG_DATA) {
			queueVoice(frame, m_voiceN);
		}
	}
}

void COpenBridge::enqueueYSF(unsigned int tag, const unsigned char* raw)
{
	OBPacket packet;
	::memset(packet.data, 0, sizeof(packet.data));
	::memcpy(packet.data + 0U, "YSFD", 4U);
	::memcpy(packet.data + 4U, m_gateway, YSF_CALLSIGN_LENGTH);
	::memcpy(packet.data + 14U, m_rxCall, YSF_CALLSIGN_LENGTH);
	::memcpy(packet.data + 24U, "ALL       ", YSF_CALLSIGN_LENGTH);

	unsigned char* ysf = packet.data + 35U;
	if (tag == TAG_DATA && raw != NULL) {
		static const unsigned int offsets[5] = {35U, 53U, 71U, 89U, 107U};
		for (unsigned int i = 0U; i < 5U; i++)
			::memcpy(ysf + offsets[i], raw + offsets[i], 13U);
	}

	CSync::addYSFSync(ysf);

	unsigned char fn = 0U;
	if (tag == TAG_DATA) {
		fn = (unsigned char)(m_ysfCnt % 7U);
		packet.data[34U] = (unsigned char)((m_ysfCnt & 0x7FU) << 1);
		m_ysfCnt++;
	} else if (tag == TAG_EOT) {
		packet.data[34U] = 0x01U;
	} else {
		packet.data[34U] = 0x00U;
	}

	CYSFPayload payload;
	if (tag == TAG_DATA) {
		unsigned char dch[10U];
		::memset(dch, ' ', sizeof(dch));
		switch (fn) {
			case 0U:
				::memset(dch, '*', 5U);
				::memcpy(dch + 5U, "OBRID", 5U);
				payload.writeVDMode2Data(ysf, dch);
				break;
			case 1U:
				payload.writeVDMode2Data(ysf, m_rxCall);
				break;
			case 2U:
				payload.writeVDMode2Data(ysf, (const unsigned char*)"ALL       ");
				break;
			default:
				payload.writeVDMode2Data(ysf, dch);
				break;
		}
	} else {
		unsigned char csd1[20U];
		unsigned char csd2[20U];
		::memset(csd1, ' ', sizeof(csd1));
		::memset(csd1, '*', 5U);
		::memcpy(csd1 + 5U, "OBRID", 5U);
		::memcpy(csd1 + 10U, m_rxCall, YSF_CALLSIGN_LENGTH);
		::memset(csd2, ' ', sizeof(csd2));
		payload.writeHeader(ysf, csd1, csd2);
	}

	CYSFFICH fich;
	fich.setCS(0U);
	fich.setCM(YSF_CM_GROUP);
	fich.setBN(0U);
	fich.setBT(0U);
	fich.setFN(fn);
	fich.setFT(6U);
	fich.setDev(false);
	fich.setMR(YSF_MR_NOT_BUSY);
	fich.setVoIP(true);
	fich.setDT(YSF_DT_VD_MODE2);
	fich.setSQL(false);
	fich.setSQ(0U);
	if (tag == TAG_HEADER)
		fich.setFI(YSF_FI_HEADER);
	else if (tag == TAG_EOT)
		fich.setFI(YSF_FI_TERMINATOR);
	else
		fich.setFI(YSF_FI_COMMUNICATIONS);
	fich.encode(ysf);

	packet.length = 155U;
	m_ysfQueue.push_back(packet);
}

void COpenBridge::drainYSF()
{
	for (;;) {
		unsigned char raw[120U];
		::memset(raw, 0, sizeof(raw));

		unsigned int tag = m_conv.getYSF(raw);
		if (tag == TAG_NODATA)
			break;

		if (tag == TAG_EOT) {
			enqueueYSF(TAG_EOT, raw);
			// Stay in ST_DMR_TO_YSF until the YSF frames have been handed to
			// clients, so a gateway that echoes them is not bridged back.
			m_ending = true;
			m_gotHeader = false;
			m_watchdog.stop();
			m_conv.reset();
			break;
		}

		enqueueYSF(tag, raw);
	}
}

void COpenBridge::sendFront()
{
	if (m_dmrQueue.empty())
		return;

	const OBPacket& packet = m_dmrQueue.front();
	m_socket.write(packet.data, packet.length, m_peer, m_peerLen);
	m_dmrQueue.pop_front();
}

void COpenBridge::pace(unsigned int ms)
{
	m_dmrAccum += ms;
	while (m_dmrAccum >= DMR_PACE_MS && !m_dmrQueue.empty()) {
		sendFront();
		m_dmrAccum -= DMR_PACE_MS;
	}
	if (m_dmrQueue.empty() && m_dmrAccum > DMR_PACE_MS)
		m_dmrAccum = DMR_PACE_MS;

	if (m_state == ST_YSF_TO_DMR && m_ending && m_termQueued && m_dmrQueue.empty()) {
		m_state = ST_IDLE;
		m_ending = false;
		m_termQueued = false;
		m_conv.reset();
	}

	if (m_state == ST_DMR_TO_YSF && m_ending && m_ysfQueue.empty() && m_ysfReady.empty()) {
		m_state = ST_IDLE;
		m_ending = false;
	}

	m_ysfAccum += ms;
	while (m_ysfAccum >= YSF_PACE_MS && !m_ysfQueue.empty()) {
		m_ysfReady.push_back(m_ysfQueue.front());
		m_ysfQueue.pop_front();
		m_ysfAccum -= YSF_PACE_MS;
	}
	if (m_ysfQueue.empty() && m_ysfAccum > YSF_PACE_MS)
		m_ysfAccum = YSF_PACE_MS;
}

void COpenBridge::processDatagram(const unsigned char* data, unsigned int length, const sockaddr_storage& addr)
{
	if (!CUDPSocket::match(addr, m_peer)) {
		if (m_conf.debug)
			LogDebug("OpenBridge dropped a packet from an unexpected address");
		return;
	}

	unsigned char dmr[COpenBridgeWire::DMRD_LENGTH];
	if (!COpenBridgeWire::verify(data, length, m_conf.passphrase, dmr)) {
		if (!m_loggedAuth) {
			LogWarning("OpenBridge authentication failed; further failures are not logged");
			m_loggedAuth = true;
		}
		return;
	}

	unsigned int srcId = ((unsigned int)dmr[5] << 16) | ((unsigned int)dmr[6] << 8) | (unsigned int)dmr[7];
	unsigned int dstId = ((unsigned int)dmr[8] << 16) | ((unsigned int)dmr[9] << 8) | (unsigned int)dmr[10];
	unsigned char control = dmr[15];
	const unsigned char* streamId = dmr + 16U;

	bool slot2 = (control & 0x80U) != 0U;
	bool unit = (control & 0x40U) != 0U;
	bool dataSync = (control & 0x20U) != 0U;
	bool voiceSync = (control & 0x10U) != 0U;
	unsigned char n = (unsigned char)(control & 0x0FU);

	if (slot2 || unit || dstId != m_conf.talkgroup)
		return;

	if (isOwnStream(streamId))
		return;

	// Half duplex: do not accept the peer while we are still sending to it.
	if (m_state == ST_YSF_TO_DMR)
		return;

	unsigned char dataType;
	if (dataSync)
		dataType = n;
	else if (voiceSync)
		dataType = DT_VOICE_SYNC;
	else
		dataType = DT_VOICE;

	if (m_conf.debug)
		CUtils::dump(1U, "OpenBridge RX", data, length < 73U ? length : 73U);

	if (dataType == DT_VOICE_LC_HEADER || dataType == DT_VOICE_SYNC || dataType == DT_VOICE) {
		if (m_state != ST_DMR_TO_YSF) {
			beginDMR(srcId, streamId);
		} else if (::memcmp(m_rxStream, streamId, 4U) != 0) {
			return;
		}

		if (!m_gotHeader) {
			m_conv.putDMRHeader();
			m_gotHeader = true;
		}

		if (dataType == DT_VOICE_SYNC || dataType == DT_VOICE) {
			unsigned char frame[33U];
			::memcpy(frame, dmr + 20U, 33U);
			m_conv.putDMR(frame);
			m_rxFrames++;
		}

		m_watchdog.start();
	} else if (dataType == DT_TERMINATOR_WITH_LC) {
		if (m_state == ST_DMR_TO_YSF && ::memcmp(m_rxStream, streamId, 4U) == 0)
			finishDMR();
	}
}

void COpenBridge::clock(unsigned int ms)
{
	if (!m_active)
		return;

	unsigned char buffer[256U];
	for (;;) {
		sockaddr_storage addr;
		unsigned int addrLen = 0U;
		::memset(&addr, 0, sizeof(addr));
		int len = m_socket.read(buffer, sizeof(buffer), addr, addrLen);
		if (len <= 0)
			break;
		processDatagram(buffer, (unsigned int)len, addr);
	}

	if (m_state == ST_YSF_TO_DMR && !m_ending) {
		m_watchdog.clock(ms);
		if (m_watchdog.isRunning() && m_watchdog.hasExpired()) {
			LogMessage("OpenBridge YSF watchdog expired");
			endYSF();
		}
	}

	if (m_state == ST_DMR_TO_YSF && !m_ending) {
		m_watchdog.clock(ms);
		if (m_watchdog.isRunning() && m_watchdog.hasExpired()) {
			LogMessage("OpenBridge DMR watchdog expired");
			finishDMR();
		}
	}

	if (m_state == ST_YSF_TO_DMR)
		drainDMR();

	if (m_state == ST_DMR_TO_YSF)
		drainYSF();

	pace(ms);
}

bool COpenBridge::readYSF(unsigned char* data)
{
	if (data == NULL || m_ysfReady.empty())
		return false;

	::memcpy(data, m_ysfReady.front().data, 155U);
	m_ysfReady.pop_front();
	return true;
}

void COpenBridge::injectFromPeer(const unsigned char* data, unsigned int length)
{
	if (!m_active || data == NULL)
		return;

	processDatagram(data, length, m_peer);
}

const unsigned char* COpenBridge::lastPacket(unsigned int& length) const
{
	length = m_lastTxLen;
	return m_lastTx;
}
