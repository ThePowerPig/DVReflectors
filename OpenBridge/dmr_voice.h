/*
 * dmr_voice.h — DMR voice burst assembly for OBP DMRD payloads (33 bytes).
 *
 * Built on the vendored DMR DSP in dmr/ (ported from dmr_utils3 / N0MJS,
 * GPLv3). Layout facts confirmed against the C reference (cc2obp
 * translate.c, GPLv3):
 *
 *  Voice HEAD/TERM payload (33 bytes = 264 bits):
 *    [0..98)     full_lc_BPTC[0..98)
 *    [98..108)   slot_type(20b)[0..10)
 *    [108..156)  DMR_BS_DATA_SYNC (48 bits)
 *    [156..166)  slot_type(20b)[10..20)
 *    [166..264)  full_lc_BPTC[98..196)
 *    slot_type = DMR_SLOT_TYPE_VHEAD (head) / DMR_SLOT_TYPE_VTERM (term).
 *
 *  Voice burst payload (33 bytes = 264 bits):
 *    a1_72(72) | a2_72[0..36) | EMBED(48) | a2_72[36..72) | a3_72(72)
 *    where aN_72 = dmr_ambe_49_to_72(49-bit AMBE+2 2450 params).
 *
 *  EMBED by burst position pos 0..5 (6-frame cycle):
 *    pos 0: DMR_BS_VOICE_SYNC (48 bits)
 *    pos 1..4: DMR_EMB[pos-1][0..8) + emb_lc_fragment[pos-1] (32 bits) +
 *              DMR_EMB[pos-1][8..16)
 *    pos 5: DMR_EMB[4][0..8) + 32 zero bits + DMR_EMB[4][8..16)
 *
 *  49-bit AMBE parameter units use the md380tools/emulator 7-byte wire
 *  packing: bits 0..47 in bytes 0..5 (MSB-first), bit 48 in the MSB of byte
 *  6, low 7 bits of byte 6 zero. That is the same bit order as mbelib-neo's
 *  char ambe_d[49] (one bit per byte, index 0 = first bit) — see the
 *  md380-vocoder notes. A voice burst carries 3 such units = 21 bytes
 *  (a1, a2, a3).
 *
 * Copyright (C) 2026 DVReflectors contributors.
 * SPDX-License-Identifier: GPL-2.0-or-later
 */
#ifndef OBP_DMR_VOICE_H
#define OBP_DMR_VOICE_H

#include <stdint.h>
#include <stddef.h>
#include "dmr/dmr.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Build the 9-byte Full LC for a group call:
 *   lc[0..3) = 00 00 00 (group), lc[3..6) = TGID (3 bytes BE),
 *   lc[6..9) = source ID (3 bytes BE). */
void dmr_voice_build_group_lc(uint8_t lc[9], uint32_t tgid, uint32_t src);

/* Build the 33-byte DMRD payload for a VOICE_HEAD (is_term=0) or
 * VOICE_TERM (is_term=1) from the 9-byte Full LC. */
void dmr_voice_build_head_term(const uint8_t lc[9], int is_term, uint8_t out33[33]);

/* Decode the 9-byte Full LC out of a 33-byte VOICE_HEAD/TERM payload
 * (BPTC full-LC decode, no FEC correction). Returns 0. */
int dmr_voice_parse_head_term(const uint8_t in33[33], uint8_t lc_out[9]);

/* 49-bit AMBE parameter set <-> 7-byte wire unit (md380 packing). */
void dmr_voice_pack_ambe49(const dmr_bit in49[49], uint8_t out7[7]);
void dmr_voice_unpack_ambe49(const uint8_t in7[7], dmr_bit out49[49]);

/* Build the 48-bit EMBED field for burst position pos (0..5).
 * emb = the four 32-bit embedded-LC fragments from dmr_encode_emblc
 * (out[0]=burst B .. out[3]=burst E). Output: 6 bytes, MSB-first. */
void dmr_voice_build_embed(int pos, const uint8_t emb[4][4], uint8_t out6[6]);

/* Build the 33-byte voice-burst payload.
 * ambe21 = three consecutive 7-byte AMBE units (a1, a2, a3).
 * pos = burst position 0..5 (the 6-frame embed cycle). */
void dmr_voice_build_burst(const uint8_t ambe21[21], int pos,
                           const uint8_t emb[4][4], uint8_t out33[33]);

/* Parse a 33-byte voice-burst payload into three 7-byte AMBE units
 * (a1, a2, a3), in the same 21-byte layout as dmr_voice_build_burst.
 * The EMBED field is not checked. */
void dmr_voice_parse_burst(const uint8_t in33[33], uint8_t out21[21]);

#ifdef __cplusplus
}
#endif

#endif /* OBP_DMR_VOICE_H */
