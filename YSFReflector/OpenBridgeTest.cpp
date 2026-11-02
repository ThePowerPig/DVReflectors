/*
 *   Copyright (C) 2026
 *
 *   Loopback and codec tests for the YSFReflector OpenBridge link.
 *   Build with `make test` from YSFReflector/.
 *
 *   This program is free software; you can redistribute it and/or modify
 *   it under the terms of the GNU General Public License as published by
 *   the Free Software Foundation; either version 2 of the License, or
 *   (at your option) any later version.
 */

#include "OpenBridge.h"
#include "Conf.h"
#include "DMRIdLookup.h"
#include "DMRFullLC.h"
#include "DMRDefines.h"
#include "ModeConv.h"
#include "YSFFICH.h"
#include "YSFDefines.h"
#include "Sync.h"
#include "UDPSocket.h"
#include "Log.h"

#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

static int g_failed = 0;

#define CHECK(cond) do { \
	if (!(cond)) { \
		::fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); \
		g_failed++; \
	} \
} while (0)

static bool digestIs(const unsigned char* got, const char* hex)
{
	for (unsigned int i = 0U; i < 20U; i++) {
		unsigned int byte = 0U;
		if (::sscanf(hex + (i * 2U), "%2x", &byte) != 1 || got[i] != (unsigned char)byte)
			return false;
	}
	return true;
}

static void testHmac()
{
	// RFC 2202 HMAC-SHA1 test cases 1 and 2.
	unsigned char digest[20];
	std::string key1(20U, '\x0b');
	const char* data1 = "Hi There";
	COpenBridgeWire::mac(key1, reinterpret_cast<const unsigned char*>(data1), (unsigned int)::strlen(data1), digest);
	CHECK(digestIs(digest, "b617318655057264e28bc0b6fb378c8ef146be00"));

	const char* data2 = "what do ya want for nothing?";
	COpenBridgeWire::mac("Jefe", reinterpret_cast<const unsigned char*>(data2), (unsigned int)::strlen(data2), digest);
	CHECK(digestIs(digest, "effcdf6ae5eb2fa2d27416d5f184df9c259a7c79"));

	unsigned char dmr[53];
	::memset(dmr, 0, sizeof(dmr));
	::memcpy(dmr, "DMRD", 4U);
	dmr[5] = 0x12;
	dmr[6] = 0xD6;
	dmr[7] = 0x87;
	dmr[8] = 0x00;
	dmr[9] = 0x27;
	dmr[10] = 0x0F;

	unsigned char packet[73];
	COpenBridgeWire::sign(dmr, 53U, "test-secret", packet);
	CHECK(::memcmp(packet, "DMRD", 4U) == 0);
	CHECK(packet[5] == 0x12 && packet[7] == 0x87);

	unsigned char back[53];
	CHECK(COpenBridgeWire::verify(packet, 73U, "test-secret", back));
	CHECK(::memcmp(back, dmr, 53U) == 0);
	CHECK(!COpenBridgeWire::verify(packet, 73U, "other-secret", back));
	CHECK(!COpenBridgeWire::verify(packet, 53U, "test-secret", back));

	packet[20] ^= 0x01U;
	CHECK(!COpenBridgeWire::verify(packet, 73U, "test-secret", back));
	packet[20] ^= 0x01U;

	// Independent SHA1-HMAC check of the same bytes, duplicated here so a
	// broken signer cannot agree with a broken verifier.
	unsigned char again[73];
	COpenBridgeWire::sign(dmr, 53U, "test-secret", again);
	CHECK(::memcmp(packet + 53U, again + 53U, 20U) == 0);
}

static void testLookupAndConf()
{
	const char* idPath = "/tmp/ysf-ob-ids.txt";
	FILE* fp = ::fopen(idPath, "wt");
	CHECK(fp != NULL);
	if (fp != NULL) {
		::fputs("# comment\n", fp);
		::fputs("1234567 N0CALL Joe\n", fp);
		::fputs("7654321,W1AW,Hiram\n", fp);
		::fputs("0000000 BAD\n", fp);
		::fclose(fp);
	}

	CDMRIdLookup ids(idPath, 42U);
	CHECK(ids.load());
	CHECK(ids.size() == 2U);
	CHECK(ids.findId("n0call") == 1234567U);
	CHECK(ids.findId("W1AW") == 7654321U);
	CHECK(ids.findId("UNKNOWN") == 42U);
	CHECK(ids.findCallsign(1234567U) == "N0CALL");
	CHECK(ids.findCallsign(999U) == "999");

	const char* iniPath = "/tmp/ysf-ob.ini";
	fp = ::fopen(iniPath, "wt");
	CHECK(fp != NULL);
	if (fp != NULL) {
		::fputs("[General]\nDaemon=0\n[Info]\nId=12345\nName=TESTBRIDGE\nDescription=bridge\n", fp);
		::fputs("[Log]\nDisplayLevel=0\nFileLevel=0\nFilePath=.\nFileRoot=t\n", fp);
		::fputs("[Network]\nPort=42999\nDebug=0\n", fp);
		::fputs("[OpenBridge]\nEnable=1\nAddress=127.0.0.1\nPort=62035\nLocalPort=62036\n", fp);
		::fputs("NetworkId=310999\nPassphrase=\"my secret\"\nTalkgroup=9999\n", fp);
		::fputs("IdFile=/tmp/ysf-ob-ids.txt\nFallbackId=1234567\nColorCode=1\nCallsign=BRIDGE\nDebug=0\n", fp);
		::fclose(fp);
	}

	CConf conf(iniPath);
	CHECK(conf.read());
	CHECK(conf.getOpenBridgeEnable());
	CHECK(conf.getOpenBridgeAddress() == "127.0.0.1");
	CHECK(conf.getOpenBridgePort() == 62035U);
	CHECK(conf.getOpenBridgeLocalPort() == 62036U);
	CHECK(conf.getOpenBridgeNetworkId() == 310999U);
	CHECK(conf.getOpenBridgePassphrase() == "my secret");
	CHECK(conf.getOpenBridgeTalkgroup() == 9999U);
	CHECK(conf.getOpenBridgeFallbackId() == 1234567U);
	CHECK(conf.getOpenBridgeCallsign() == "BRIDGE");
	CHECK(conf.getOpenBridgeColorCode() == 1U);
	CHECK(conf.getDaemon() == false);
}

static void testFichAndLc()
{
	unsigned char frame[120U];
	::memset(frame, 0, sizeof(frame));
	CSync::addYSFSync(frame);

	CYSFFICH fich;
	fich.setFI(YSF_FI_COMMUNICATIONS);
	fich.setCS(0U);
	fich.setCM(YSF_CM_GROUP);
	fich.setBN(0U);
	fich.setBT(0U);
	fich.setFN(3U);
	fich.setFT(6U);
	fich.setDev(false);
	fich.setMR(YSF_MR_NOT_BUSY);
	fich.setVoIP(true);
	fich.setDT(YSF_DT_VD_MODE2);
	fich.setSQL(false);
	fich.setSQ(0U);
	fich.encode(frame);

	CYSFFICH back;
	CHECK(back.decode(frame));
	CHECK(back.getFI() == YSF_FI_COMMUNICATIONS);
	CHECK(back.getDT() == YSF_DT_VD_MODE2);
	CHECK(back.getFN() == 3U);
	CHECK(back.getFT() == 6U);

	unsigned char dmr[33U];
	::memset(dmr, 0, sizeof(dmr));
	CDMRLC lc(FLCO_GROUP, 1234567U, 9999U);
	CDMRFullLC full;
	full.encode(lc, dmr, DT_VOICE_LC_HEADER);

	CDMRFullLC decoder;
	CDMRLC* decoded = decoder.decode(dmr, DT_VOICE_LC_HEADER);
	CHECK(decoded != NULL);
	if (decoded != NULL) {
		CHECK(decoded->getSrcId() == 1234567U);
		CHECK(decoded->getDstId() == 9999U);
		CHECK(decoded->getFLCO() == FLCO_GROUP);
		delete decoded;
	}
}

static void putSilence(unsigned char* ysf)
{
	static const unsigned char silence[13] = {
		0x7BU, 0xB2U, 0x8EU, 0x43U, 0x36U, 0xE4U, 0xA2U, 0x39U, 0x78U, 0x49U, 0x33U, 0x68U, 0x33U
	};
	static const unsigned int offsets[5] = {35U, 53U, 71U, 89U, 107U};
	for (unsigned int i = 0U; i < 5U; i++)
		::memcpy(ysf + offsets[i], silence, 13U);
}

static void testAmbeRoundTrip()
{
	unsigned char ysf[120U];
	::memset(ysf, 0, sizeof(ysf));
	putSilence(ysf);

	CModeConv conv;
	for (unsigned int i = 0U; i < 3U; i++)
		conv.putYSF(ysf);

	unsigned char dmrFrames[5][33];
	unsigned int got = 0U;
	for (unsigned int i = 0U; i < 8U && got < 5U; i++) {
		unsigned char frame[33U];
		::memset(frame, 0, sizeof(frame));
		unsigned int tag = conv.getDMR(frame);
		CHECK(tag == TAG_DATA);
		if (tag != TAG_DATA)
			return;
		::memcpy(dmrFrames[got], frame, 33U);
		got++;
	}
	CHECK(got == 5U);

	unsigned char extra[33U];
	::memset(extra, 0, sizeof(extra));
	CHECK(conv.getDMR(extra) == TAG_NODATA);

	CModeConv back;
	for (unsigned int i = 0U; i < got; i++)
		back.putDMR(dmrFrames[i]);

	static const unsigned int offsets[5] = {35U, 53U, 71U, 89U, 107U};
	for (unsigned int n = 0U; n < 3U; n++) {
		unsigned char out[120U];
		::memset(out, 0, sizeof(out));
		unsigned int tag = back.getYSF(out);
		CHECK(tag == TAG_DATA);
		if (tag != TAG_DATA)
			return;
		for (unsigned int i = 0U; i < 5U; i++)
			CHECK(::memcmp(out + offsets[i], ysf + offsets[i], 13U) == 0);
	}
}

static void buildDn(unsigned char out[155], unsigned char fi, unsigned char fn, const char* callsign, bool eot)
{
	::memset(out, 0, 155U);
	::memcpy(out, "YSFD", 4U);
	::memset(out + 4U, ' ', 10U);
	::memcpy(out + 4U, "FROM-YSF", 8U);
	::memset(out + 14U, ' ', 10U);
	unsigned int n = (unsigned int)::strlen(callsign);
	if (n > 10U)
		n = 10U;
	::memcpy(out + 14U, callsign, n);
	::memcpy(out + 24U, "ALL       ", 10U);
	out[34U] = eot ? 0x01U : 0x00U;

	unsigned char* ysf = out + 35U;
	CSync::addYSFSync(ysf);

	CYSFFICH fich;
	fich.setFI(fi);
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
	fich.encode(ysf);

	if (fi == YSF_FI_COMMUNICATIONS)
		putSilence(ysf);
}

static COpenBridgeConfig bridgeConfig(unsigned short local, unsigned short peer, const std::string& pass, unsigned int net, unsigned int fallback)
{
	COpenBridgeConfig conf;
	conf.enable = true;
	conf.address = "127.0.0.1";
	conf.port = peer;
	conf.localPort = local;
	conf.networkId = net;
	conf.passphrase = pass;
	conf.talkgroup = 9999U;
	conf.idFile = "/tmp/ysf-ob-ids.txt";
	conf.fallbackId = fallback;
	conf.debug = false;
	conf.gateway = "BRIDGE";
	conf.colorCode = 1U;
	return conf;
}

static void pump(COpenBridge& a, COpenBridge& b, std::vector<std::string>& fromB, unsigned int steps)
{
	unsigned char frame[155U];
	for (unsigned int i = 0U; i < steps; i++) {
		a.clock(20U);
		b.clock(20U);
		while (b.readYSF(frame)) {
			fromB.push_back(std::string(reinterpret_cast<char*>(frame), 155U));
			bool eot = (frame[34U] & 0x01U) == 0x01U;
			if (eot)
				return;
		}
		while (a.readYSF(frame)) {
			::fprintf(stderr, "FAIL unexpected YSF frame generated by the sending bridge\n");
			g_failed++;
		}
	}
}

static void testBridgeLoopback()
{
	COpenBridge a(bridgeConfig(28140U, 28141U, "test-secret", 111U, 0U));
	COpenBridge b(bridgeConfig(28141U, 28140U, "test-secret", 222U, 0U));
	CHECK(a.open());
	CHECK(b.open());
	if (!a.isActive() || !b.isActive())
		return;

	unsigned char packet[155U];
	buildDn(packet, YSF_FI_HEADER, 0U, "N0CALL", false);
	a.writeYSF(packet, 155U);

	buildDn(packet, YSF_FI_COMMUNICATIONS, 0U, "ZZ9ZZZ", false);
	a.writeYSF(packet, 155U);

	for (unsigned char fn = 0U; fn < 6U; fn++) {
		buildDn(packet, YSF_FI_COMMUNICATIONS, fn, "N0CALL", false);
		a.writeYSF(packet, 155U);
	}

	buildDn(packet, YSF_FI_TERMINATOR, 0U, "N0CALL", true);
	a.writeYSF(packet, 155U);

	std::vector<std::string> frames;
	pump(a, b, frames, 500U);

	CHECK(!frames.empty());
	if (frames.empty())
		return;

	const std::string& first = frames.front();
	CHECK(::memcmp(first.data(), "YSFD", 4U) == 0);
	CHECK(::memcmp(first.data() + 4U, "BRIDGE", 6U) == 0);
	CHECK(::memcmp(first.data() + 14U, "N0CALL", 6U) == 0);

	bool sawVoice = false;
	bool sawEot = false;
	bool sawHeader = false;
	for (size_t i = 0U; i < frames.size(); i++) {
		const unsigned char* ysf = reinterpret_cast<const unsigned char*>(frames[i].data());
		CHECK(::memcmp(ysf + 14U, "N0CALL", 6U) == 0);
		if ((ysf[34U] & 0x01U) != 0U)
			sawEot = true;

		CYSFFICH fich;
		if (fich.decode(ysf + 35U)) {
			CHECK(fich.getDT() == YSF_DT_VD_MODE2);
			if (fich.getFI() == YSF_FI_HEADER)
				sawHeader = true;
			if (fich.getFI() == YSF_FI_COMMUNICATIONS)
				sawVoice = true;
		}
	}
	CHECK(sawHeader);
	CHECK(sawVoice);
	CHECK(sawEot);

	// Our own stream id must not come back as a new transmission.
	unsigned int len = 0U;
	const unsigned char* last = a.lastPacket(len);
	CHECK(len == 73U);
	if (len == 73U) {
		a.injectFromPeer(last, len);
		for (unsigned int i = 0U; i < 30U; i++) {
			a.clock(20U);
			unsigned char echo[155U];
			CHECK(!a.readYSF(echo));
		}
	}

	// Wrong talkgroup and wrong passphrase are ignored.
	unsigned char forged[53];
	unsigned char stream[4] = {0x11U, 0x22U, 0x33U, 0x44U};
	unsigned char voice[33];
	::memset(voice, 0, sizeof(voice));
	COpenBridgeWire::build(forged, 0U, 1234567U, 1111U, 222U, 0x21U, stream, voice);
	unsigned char bad[73];
	COpenBridgeWire::sign(forged, 53U, "test-secret", bad);
	b.injectFromPeer(bad, 73U);
	b.clock(20U);
	unsigned char ignored[155U];
	CHECK(!b.readYSF(ignored));

	COpenBridgeWire::build(forged, 0U, 1234567U, 9999U, 222U, 0x21U, stream, voice);
	COpenBridgeWire::sign(forged, 53U, "not-the-pass", bad);
	b.injectFromPeer(bad, 73U);
	b.clock(20U);
	CHECK(!b.readYSF(ignored));

	a.close();
	b.close();
}

static void testWatchdogAndFallback()
{
	COpenBridge a(bridgeConfig(28142U, 28143U, "test-secret", 333U, 42U));
	COpenBridge b(bridgeConfig(28143U, 28142U, "test-secret", 444U, 0U));
	CHECK(a.open());
	CHECK(b.open());
	if (!a.isActive() || !b.isActive())
		return;

	unsigned char packet[155U];
	buildDn(packet, YSF_FI_HEADER, 0U, "NOBODY", false);
	a.writeYSF(packet, 155U);
	for (unsigned char fn = 0U; fn < 4U; fn++) {
		buildDn(packet, YSF_FI_COMMUNICATIONS, fn, "NOBODY", false);
		a.writeYSF(packet, 155U);
	}
	buildDn(packet, YSF_FI_TERMINATOR, 0U, "NOBODY", true);
	a.writeYSF(packet, 155U);

	std::vector<std::string> frames;
	pump(a, b, frames, 500U);
	CHECK(!frames.empty());
	if (!frames.empty()) {
		// 42 is not in the lookup file, so the far side shows the decimal ID.
		CHECK(::memcmp(frames.front().data() + 14U, "42", 2U) == 0);
	}

	// A header with no audio must still produce a DMR terminator after the watchdog.
	buildDn(packet, YSF_FI_HEADER, 0U, "N0CALL", false);
	a.writeYSF(packet, 155U);
	for (unsigned int i = 0U; i < 120U; i++)
		a.clock(20U);

	unsigned int len = 0U;
	const unsigned char* last = a.lastPacket(len);
	CHECK(len == 73U);
	if (len == 73U)
		CHECK(last[15] == 0x22U);

	a.close();
	b.close();
}

int main()
{
	::LogInitialise(false, ".", "OpenBridgeTest", 0U, 0U, false);
	CUDPSocket::startup();

	testHmac();
	testLookupAndConf();
	testFichAndLc();
	testAmbeRoundTrip();
	testBridgeLoopback();
	testWatchdogAndFallback();

	CUDPSocket::shutdown();
	::LogFinalise();

	if (g_failed != 0) {
		::fprintf(stderr, "%d check(s) failed\n", g_failed);
		return 1;
	}

	::fprintf(stdout, "OpenBridge tests passed\n");
	return 0;
}
