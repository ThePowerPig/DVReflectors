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

#include "BuiltinVocoder.h"
#include "Log.h"

#include <cstring>

#if defined(HAVE_MBELIB)

// Wire units <-> mbelib-neo one-bit-per-byte parameter arrays (MSB-first —
// same order as the md380tools 7-byte packing and the OpenBridge dmr_voice
// helpers).
static void unpackBits(const unsigned char* in, unsigned int nBytes, char* out, unsigned int nBits)
{
	for (unsigned int i = 0U; i < nBits; i++)
		out[i] = (in[i >> 3] >> (7 - (i & 7))) & 1;
}

static void packBits(const char* in, unsigned int nBits, unsigned char* out, unsigned int nBytes)
{
	::memset(out, 0, nBytes);
	for (unsigned int i = 0U; i < nBits; i++)
		if (in[i])
			out[i >> 3] |= (unsigned char)(1 << (7 - (i & 7)));
}

CBuiltinVocoder::CBuiltinVocoder(bool debug) :
m_debug(debug),
m_ambeEnc(NULL),
m_imbeEnc(NULL)
{
}

CBuiltinVocoder::~CBuiltinVocoder()
{
	close();
}

bool CBuiltinVocoder::open()
{
	m_ambeEnc = mbe_ambe2450EncoderAlloc();
	m_imbeEnc = mbe_imbe4400EncoderAlloc();

	if (m_ambeEnc == NULL || m_imbeEnc == NULL) {
		LogError("BuiltinVocoder: cannot allocate encoder contexts");
		close();
		return false;
	}

	// NOTE: mbe_initMbeParms() is a no-op if ANY argument is NULL, so every
	// state set needs a third (enhanced) parms struct passed by address.
	mbe_initMbeParms(&m_ambeDecCur, &m_ambeDecPrev, &m_ambeDecEnh);
	mbe_initMbeParms(&m_ambeEncCur, &m_ambeEncPrev, &m_ambeEncEnh);
	mbe_initMbeParms(&m_imbeDecCur, &m_imbeDecPrev, &m_imbeDecEnh);
	mbe_initMbeParms(&m_imbeEncCur, &m_imbeEncPrev, &m_imbeEncEnh);

	LogInfo("BuiltinVocoder: mbelib-neo engine ready (AMBE+2 2450 + IMBE 7200x4400)");
	return true;
}

void CBuiltinVocoder::close()
{
	mbe_ambe2450EncoderFree(m_ambeEnc);
	mbe_imbe4400EncoderFree(m_imbeEnc);
	m_ambeEnc = NULL;
	m_imbeEnc = NULL;
}

void CBuiltinVocoder::resetRx()
{
	mbe_initMbeParms(&m_ambeDecCur, &m_ambeDecPrev, &m_ambeDecEnh);
	mbe_imbe4400EncoderReset(m_imbeEnc);
	mbe_initMbeParms(&m_imbeEncCur, &m_imbeEncPrev, &m_imbeEncEnh);
}

void CBuiltinVocoder::resetTx()
{
	mbe_initMbeParms(&m_imbeDecCur, &m_imbeDecPrev, &m_imbeDecEnh);
	mbe_ambe2450EncoderReset(m_ambeEnc);
	mbe_initMbeParms(&m_ambeEncCur, &m_ambeEncPrev, &m_ambeEncEnh);
}

bool CBuiltinVocoder::ambeToPcm(const unsigned char ambe7[7], short pcm160[160])
{
	char ambe_d[49];
	unpackBits(ambe7, 7U, ambe_d, 49U);

	mbe_process_result result = {};

	int rc = mbe_processAmbe2450Data(pcm160, &result, ambe_d,
	                                 &m_ambeDecCur, &m_ambeDecPrev, &m_ambeDecEnh);

	mbe_moveMbeParms(&m_ambeDecCur, &m_ambeDecPrev);

	return rc >= 0;
}

bool CBuiltinVocoder::pcmToAmbe(const short pcm160[160], unsigned char ambe7[7])
{
	char ambe_d[49];

	// Returns the frame class (voice or tone); errors are negative.
	int rc = mbe_encodeAmbe2450ParmsShort(m_ambeEnc, pcm160, ambe_d,
	                                      &m_ambeEncCur, &m_ambeEncPrev);

	packBits(ambe_d, 49U, ambe7, 7U);

	mbe_moveMbeParms(&m_ambeEncCur, &m_ambeEncPrev);

	return rc >= 0;
}

bool CBuiltinVocoder::imbeToPcm(const unsigned char imbe11[11], short pcm160[160])
{
	char imbe_d[88];
	unpackBits(imbe11, 11U, imbe_d, 88U);

	mbe_process_result result = {};

	int rc = mbe_processImbe4400Data(pcm160, &result, imbe_d,
	                                 &m_imbeDecCur, &m_imbeDecPrev, &m_imbeDecEnh);

	mbe_moveMbeParms(&m_imbeDecCur, &m_imbeDecPrev);

	return rc >= 0;
}

bool CBuiltinVocoder::pcmToImbe(const short pcm160[160], unsigned char imbe11[11])
{
	char imbe_d[88];

	int rc = mbe_encodeImbe4400ParmsShort(m_imbeEnc, pcm160, imbe_d,
	                                      &m_imbeEncCur, &m_imbeEncPrev);

	packBits(imbe_d, 88U, imbe11, 11U);

	mbe_moveMbeParms(&m_imbeEncCur, &m_imbeEncPrev);

	return rc >= 0;
}

#else  /* !HAVE_MBELIB */

// Graceful stub: this translation unit only builds when the Makefile finds
// mbelib-neo; if HAVE_MBELIB is missing the class reports unusable so the
// caller can fall back (should not normally happen — the Makefile then also
// omits this object).
CBuiltinVocoder::CBuiltinVocoder(bool debug) : m_debug(debug) {}
CBuiltinVocoder::~CBuiltinVocoder() {}
bool CBuiltinVocoder::open() { LogError("BuiltinVocoder: built without mbelib-neo support"); return false; }
void CBuiltinVocoder::close() {}
bool CBuiltinVocoder::ambeToPcm(const unsigned char[7], short[160]) { return false; }
bool CBuiltinVocoder::pcmToAmbe(const short[160], unsigned char[7]) { return false; }
bool CBuiltinVocoder::imbeToPcm(const unsigned char[11], short[160]) { return false; }
bool CBuiltinVocoder::pcmToImbe(const short[160], unsigned char[11]) { return false; }
void CBuiltinVocoder::resetRx() {}
void CBuiltinVocoder::resetTx() {}

#endif /* HAVE_MBELIB */
