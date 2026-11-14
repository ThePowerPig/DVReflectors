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

// BuiltinVocoder.h — vocoder backend compiled into the reflector, backed by
// mbelib-neo (AMBE+2 2450 for the DMR/OBP side, IMBE 7200x4400 for P25).
//
// No external vocoder process or UDP round-trips; conversion is a direct
// function call.  Codec prediction state is kept per direction (four
// independent state sets: AMBE-decode, AMBE-encode, IMBE-decode, IMBE-encode)
// and reset per stream via resetRx()/resetTx().
//
// Requires MBELIB (mbelib-neo) at build time; the Makefile enables this file
// only when the mbelib-neo static library is found (-DHAVE_MBELIB).

#if !defined(BUILTIN_VOCODER_H)
#define	BUILTIN_VOCODER_H

#include "Vocoder.h"

#if defined(HAVE_MBELIB)
#include <mbelib-neo/mbelib.h>
#endif

class CBuiltinVocoder : public IVocoder {
public:
	CBuiltinVocoder(bool debug);
	~CBuiltinVocoder();

	bool open();
	void close();

	bool ambeToPcm(const unsigned char ambe7[7], short pcm160[160]);
	bool pcmToAmbe(const short pcm160[160], unsigned char ambe7[7]);
	bool imbeToPcm(const unsigned char imbe11[11], short pcm160[160]);
	bool pcmToImbe(const short pcm160[160], unsigned char imbe11[11]);

	void resetRx();
	void resetTx();

private:
	bool m_debug;

#if defined(HAVE_MBELIB)
	// Encoder contexts (own FFT plans).
	mbe_ambe2450_encoder* m_ambeEnc;
	mbe_imbe4400_encoder* m_imbeEnc;

	// Prediction state, one set per conversion direction.
	mbe_parms m_ambeDecCur, m_ambeDecPrev, m_ambeDecEnh;
	mbe_parms m_ambeEncCur, m_ambeEncPrev, m_ambeEncEnh;
	mbe_parms m_imbeDecCur, m_imbeDecPrev, m_imbeDecEnh;
	mbe_parms m_imbeEncCur, m_imbeEncPrev, m_imbeEncEnh;
#endif
};

#endif
