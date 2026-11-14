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

#include "Vocoder.h"
#include "StopWatch.h"
#include "Thread.h"
#include "Log.h"
#include "Utils.h"

#include <cstdio>
#include <cstring>
#include <cassert>

CVocoder::CVocoder(const std::string& address, unsigned short ambePort, unsigned short imbePort, bool debug) :
m_address(address),
m_ambePort(ambePort),
m_imbePort(imbePort),
m_debug(debug),
m_ambeSock(),
m_imbeSock(),
m_ambeAddr(),
m_ambeAddrLen(0U),
m_imbeAddr(),
m_imbeAddrLen(0U),
m_timeoutMs(200U)
{
}

CVocoder::~CVocoder()
{
}

bool CVocoder::open()
{
	LogInfo("Opening vocoder connection to %s (AMBE port %u, IMBE port %u)", m_address.c_str(), m_ambePort, m_imbePort);

	int err = CUDPSocket::lookup(m_address, m_ambePort, m_ambeAddr, m_ambeAddrLen);
	if (err != 0) {
		LogError("Vocoder: cannot resolve AMBE address %s", m_address.c_str());
		return false;
	}

	err = CUDPSocket::lookup(m_address, m_imbePort, m_imbeAddr, m_imbeAddrLen);
	if (err != 0) {
		LogError("Vocoder: cannot resolve IMBE address %s", m_address.c_str());
		return false;
	}

	// Client only: no local bind (port 0).
	if (!m_ambeSock.open())
		return false;

	if (!m_imbeSock.open())
		return false;

	return true;
}

void CVocoder::close()
{
	m_ambeSock.close();
	m_imbeSock.close();

	LogInfo("Closing vocoder connection");
}

bool CVocoder::exchange(CUDPSocket& sock, const sockaddr_storage& addr, unsigned int addrLen,
	const unsigned char* req, unsigned int reqLen,
	unsigned char* out, unsigned int expLen)
{
	assert(req != NULL);
	assert(out != NULL);

	if (m_debug)
		CUtils::dump(2U, "Vocoder Request", req, reqLen);

	CStopWatch stopWatch;
	stopWatch.start();

	if (!sock.write(req, reqLen, addr, addrLen)) {
		LogError("Vocoder: failed to send request");
		return false;
	}

	unsigned char buffer[400U];
	sockaddr_storage from;
	unsigned int fromLen;

	// The vocoder replies with a single datagram of exactly expLen bytes.
	while (stopWatch.elapsed() < m_timeoutMs) {
		int n = sock.read(buffer, sizeof(buffer), from, fromLen);
		if (n > 0) {
			if ((unsigned int)n >= expLen) {
				if (m_debug)
					CUtils::dump(2U, "Vocoder Reply", buffer, expLen);

				::memcpy(out, buffer, expLen);
				return true;
			}
		} else if (n < 0) {
			return false;
		}

		CThread::sleep(1U);
	}

	LogError("Vocoder: timeout waiting for %u-byte reply", expLen);
	return false;
}

bool CVocoder::ambeToPcm(const unsigned char ambe7[7], short pcm160[160])
{
	return exchange(m_ambeSock, m_ambeAddr, m_ambeAddrLen, ambe7, 7U, (unsigned char*)pcm160, 320U);
}

bool CVocoder::pcmToAmbe(const short pcm160[160], unsigned char ambe7[7])
{
	return exchange(m_ambeSock, m_ambeAddr, m_ambeAddrLen, (const unsigned char*)pcm160, 320U, ambe7, 7U);
}

bool CVocoder::imbeToPcm(const unsigned char imbe11[11], short pcm160[160])
{
	return exchange(m_imbeSock, m_imbeAddr, m_imbeAddrLen, imbe11, 11U, (unsigned char*)pcm160, 320U);
}

bool CVocoder::pcmToImbe(const short pcm160[160], unsigned char imbe11[11])
{
	return exchange(m_imbeSock, m_imbeAddr, m_imbeAddrLen, (const unsigned char*)pcm160, 320U, imbe11, 11U);
}
