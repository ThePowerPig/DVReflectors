/*
 * vocoder_probe.cpp — test harness for the compiled-in vocoder.
 *
 * Uses the exact CBuiltinVocoder class linked into P25Reflector.
 *
 *   encode <pcm-raw-in> <imbe-out>
 *       raw signed-16-bit-LE 8 kHz samples -> stream of 11-byte IMBE words
 *   bursts <bursts-in> <pcm-out>
 *       stream of 33-byte DMRD voice payloads -> decoded raw PCM
 *       (each burst yields 3 x 160-sample frames via dmr_voice_parse_burst
 *        + ambeToPcm)
 *
 * Build (run from P25Reflector/):
 *   c++ -O2 -std=c++0x -DHAVE_MBELIB -I../OpenBridge -I../../mbelib-neo/include \
 *       vocoder_probe.cpp BuiltinVocoder.cpp Log.o \
 *       ../OpenBridge/obp.o ../OpenBridge/dmr_voice.o ../OpenBridge/crypto.o \
 *       ../OpenBridge/dmr/*.o ../../mbelib-neo/build/libmbe-neo.a \
 *       -lpthread -lm -o vocoder_probe
 */

#include "BuiltinVocoder.h"
#include "dmr_voice.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>

static long fileBytes(const char* f)
{
	FILE* fp = ::fopen(f, "rb");
	if (fp == NULL) {
		::fprintf(stderr, "cannot open %s\n", f);
		return -1L;
	}
	::fseek(fp, 0, SEEK_END);
	long n = ::ftell(fp);
	::fclose(fp);
	return n;
}

static int encodeMode(const char* inF, const char* outF)
{
	FILE* in = ::fopen(inF, "rb");
	FILE* out = ::fopen(outF, "wb");
	if (in == NULL || out == NULL) {
		::fprintf(stderr, "encode: cannot open files\n");
		return 1;
	}

	CBuiltinVocoder voc(false);
	if (!voc.open())
		return 1;

	short pcm[160];
	unsigned char imbe[11];
	unsigned int n = 0U;

	while (::fread(pcm, sizeof(short), 160U, in) == 160U) {
		if (!voc.pcmToImbe(pcm, imbe)) {
			::fprintf(stderr, "encode: pcmToImbe failed at frame %u\n", n);
			return 1;
		}
		::fwrite(imbe, 1U, 11U, out);
		n++;
	}

	::printf("encode: %u IMBE words\n", n);
	::fclose(in);
	::fclose(out);
	return 0;
}

static int burstsMode(const char* inF, const char* outF)
{
	FILE* in = ::fopen(inF, "rb");
	FILE* out = ::fopen(outF, "wb");
	if (in == NULL || out == NULL) {
		::fprintf(stderr, "bursts: cannot open files\n");
		return 1;
	}

	CBuiltinVocoder voc(false);
	if (!voc.open())
		return 1;

	unsigned char burst[33];
	unsigned char ambe21[21];
	short pcm[160];
	unsigned int nB = 0U;

	while (::fread(burst, 1U, 33U, in) == 33U) {
		dmr_voice_parse_burst(burst, ambe21);
		for (int i = 0; i < 3; i++) {
			if (!voc.ambeToPcm(ambe21 + i * 7, pcm)) {
				::fprintf(stderr, "bursts: ambeToPcm failed burst %u unit %d\n", nB, i);
				return 1;
			}
			::fwrite(pcm, sizeof(short), 160U, out);
		}
		nB++;
	}

	::printf("bursts: %u bursts -> %u samples\n", nB, nB * 480U);
	::fclose(in);
	::fclose(out);
	return 0;
}

int main(int argc, char** argv)
{
	if (argc == 4 && ::strcmp(argv[1], "encode") == 0)
		return encodeMode(argv[2], argv[3]);
	if (argc == 4 && ::strcmp(argv[1], "bursts") == 0)
		return burstsMode(argv[2], argv[3]);

	::fprintf(stderr, "usage: %s encode <pcm-in> <imbe-out> | %s bursts <bursts-in> <pcm-out>\n", argv[0], argv[0]);
	return 2;
}
