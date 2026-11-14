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

// Vocoder.h — synchronous UDP client for an external AMBE/IMBE vocoder.
//
// Speaks the md380tools/emulator wire protocol (7-byte AMBE+2 2450 parameter
// set <-> 320-byte PCM, 8 kHz/16-bit, 160 samples) plus the VocoderEmu IMBE
// extension (11-byte raw IMBE 7200x4400 word <-> 320-byte PCM).  Each service
// lives on its own UDP port; the request/response is distinguished by the
// length of the request datagram:
//
//   AMBE port:   7-byte request  -> 320-byte reply   (AMBE decode)
//                320-byte request -> 7-byte reply    (AMBE encode)
//   IMBE port:   11-byte request  -> 320-byte reply  (IMBE decode)
//                320-byte request -> 11-byte reply   (IMBE encode)
//
// All conversions are blocking: send the request, wait for the reply with a
// select() timeout, and return false on timeout/short read.

#if !defined(VOCODER_H)
#define	VOCODER_H

#include "UDPSocket.h"

#include <string>

class CVocoder {
public:
	CVocoder(const std::string& address, unsigned short ambePort, unsigned short imbePort, bool debug);
	~CVocoder();

	bool open();
	void close();

	// AMBE (7 bytes) -> PCM (320 bytes = 160 x int16 LE samples).
	bool ambeToPcm(const unsigned char ambe7[7], short pcm160[160]);

	// PCM (320 bytes) -> AMBE (7 bytes).
	bool pcmToAmbe(const short pcm160[160], unsigned char ambe7[7]);

	// IMBE (11 bytes) -> PCM (320 bytes).
	bool imbeToPcm(const unsigned char imbe11[11], short pcm160[160]);

	// PCM (320 bytes) -> IMBE (11 bytes).
	bool pcmToImbe(const short pcm160[160], unsigned char imbe11[11]);

private:
	// Generic request/response: send reqLen bytes, wait up to m_timeoutMs for
	// exactly expLen bytes from the vocoder, copy into out.  Returns true on
	// success.  sock is either m_ambeSock or m_imbeSock; addr/addrLen is the
	// matching resolved peer address.
	bool exchange(CUDPSocket& sock, const sockaddr_storage& addr, unsigned int addrLen,
		const unsigned char* req, unsigned int reqLen,
		unsigned char* out, unsigned int expLen);

	std::string    m_address;
	unsigned short m_ambePort;
	unsigned short m_imbePort;
	bool           m_debug;

	CUDPSocket     m_ambeSock;
	CUDPSocket     m_imbeSock;

	sockaddr_storage m_ambeAddr;
	unsigned int     m_ambeAddrLen;
	sockaddr_storage m_imbeAddr;
	unsigned int     m_imbeAddrLen;

	unsigned int     m_timeoutMs;   // vocoder reply timeout
};

#endif
