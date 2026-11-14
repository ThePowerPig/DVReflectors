/*
 * dmr_voice.c — see dmr_voice.h.
 *
 * Burst assembly/decode ported from the C reference (cc2obp translate.c
 * build_head_term_payload/build_embed/extract_ambe, GPLv3); the 49-bit AMBE
 * 7-byte packing matches md380tools/emulator ambe.c encode/decode_amb_buffer.
 *
 * Copyright (C) 2026 DVReflectors contributors.
 * SPDX-License-Identifier: GPL-2.0-or-later
 */
#include "dmr_voice.h"
#include <string.h>

static void wr24(uint8_t *p, uint32_t v)
{
    p[0] = (uint8_t)(v >> 16);
    p[1] = (uint8_t)(v >> 8);
    p[2] = (uint8_t)v;
}

void dmr_voice_build_group_lc(uint8_t lc[9], uint32_t tgid, uint32_t src)
{
    memcpy(lc, DMR_LC_OPT, 3);      /* group call */
    wr24(lc + 3, tgid & 0xFFFFFF);
    wr24(lc + 6, src & 0xFFFFFF);
}

void dmr_voice_build_head_term(const uint8_t lc[9], int is_term, uint8_t out33[33])
{
    dmr_bit full_lc[196];
    dmr_bptc_encode_lc(lc, is_term, full_lc);

    dmr_bit fb[264];
    const dmr_bit *slot_type = is_term ? DMR_SLOT_TYPE_VTERM : DMR_SLOT_TYPE_VHEAD;
    int at = 0;
    memcpy(fb + at, full_lc, 98);          at += 98;
    memcpy(fb + at, slot_type, 10);        at += 10;
    memcpy(fb + at, DMR_BS_DATA_SYNC, 48); at += 48;
    memcpy(fb + at, slot_type + 10, 10);   at += 10;
    memcpy(fb + at, full_lc + 98, 98);     at += 98;

    dmr_bits_to_bytes(fb, 264, out33);
}

int dmr_voice_parse_head_term(const uint8_t in33[33], uint8_t lc_out[9])
{
    dmr_bit fb[264];
    dmr_bytes_to_bits(in33, 33, fb);

    /* Reassemble the 196-bit interleaved BPTC codeword: the payload stores
     * full_lc[0..98) at fb[0..98) and full_lc[98..196) at fb[166..264). */
    dmr_bit in196[196];
    for (int i = 0; i < 196; i++)
        in196[i] = fb[i < 98 ? i : i + 68];

    dmr_bptc_decode_full_lc(in196, lc_out);
    return 0;
}

void dmr_voice_pack_ambe49(const dmr_bit in49[49], uint8_t out7[7])
{
    dmr_bits_to_bytes(in49, 48, out7);            /* bits 0..47 -> bytes 0..5 */
    out7[6] = in49[48] ? 0x80 : 0x00;             /* bit 48 -> MSB of byte 6 */
}

void dmr_voice_unpack_ambe49(const uint8_t in7[7], dmr_bit out49[49])
{
    dmr_bytes_to_bits(in7, 6, out49);             /* bytes 0..5 -> bits 0..47 */
    out49[48] = (in7[6] & 0x80) ? 1 : 0;
}

void dmr_voice_build_embed(int pos, const uint8_t emb[4][4], uint8_t out6[6])
{
    dmr_bit embed[48];
    if (pos == 0) {
        memcpy(embed, DMR_BS_VOICE_SYNC, 48);
    } else {
        int eidx = pos - 1;                       /* pos 1..5 -> DMR_EMB[0..4] */
        memcpy(embed, DMR_EMB[eidx], 8);
        if (pos <= 4)
            dmr_bytes_to_bits(emb[pos - 1], 4, embed + 8);
        else
            memset(embed + 8, 0, 32);             /* pos 5: null embedded LC */
        memcpy(embed + 40, DMR_EMB[eidx] + 8, 8);
    }
    dmr_bits_to_bytes(embed, 48, out6);
}

void dmr_voice_build_burst(const uint8_t ambe21[21], int pos,
                           const uint8_t emb[4][4], uint8_t out33[33])
{
    dmr_bit a1_72[72], a2_72[72], a3_72[72];
    dmr_bit t49[49];

    dmr_voice_unpack_ambe49(ambe21 + 0,  t49); dmr_ambe_49_to_72(t49, a1_72);
    dmr_voice_unpack_ambe49(ambe21 + 7,  t49); dmr_ambe_49_to_72(t49, a2_72);
    dmr_voice_unpack_ambe49(ambe21 + 14, t49); dmr_ambe_49_to_72(t49, a3_72);

    uint8_t embed6[6];
    dmr_voice_build_embed(pos, emb, embed6);
    dmr_bit embed[48];
    dmr_bytes_to_bits(embed6, 6, embed);

    dmr_bit fb[264];
    int at = 0;
    memcpy(fb + at, a1_72, 72);        at += 72;
    memcpy(fb + at, a2_72, 36);        at += 36;
    memcpy(fb + at, embed, 48);        at += 48;
    memcpy(fb + at, a2_72 + 36, 36);   at += 36;
    memcpy(fb + at, a3_72, 72);        at += 72;

    dmr_bits_to_bytes(fb, 264, out33);
}

void dmr_voice_parse_burst(const uint8_t in33[33], uint8_t out21[21])
{
    dmr_bit burst[264];
    dmr_bytes_to_bits(in33, 33, burst);

    dmr_bit a1_72[72], a2_72[72], a3_72[72];
    memcpy(a1_72, burst + 0, 72);
    memcpy(a2_72, burst + 72, 36);
    memcpy(a2_72 + 36, burst + 156, 36);
    memcpy(a3_72, burst + 192, 72);

    dmr_bit t49[49];
    dmr_ambe_72_to_49(a1_72, t49); dmr_voice_pack_ambe49(t49, out21 + 0);
    dmr_ambe_72_to_49(a2_72, t49); dmr_voice_pack_ambe49(t49, out21 + 7);
    dmr_ambe_72_to_49(a3_72, t49); dmr_voice_pack_ambe49(t49, out21 + 14);
}
