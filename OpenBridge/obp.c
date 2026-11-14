/*
 * obp.c — see obp.h.
 *
 * Wire format confirmed against the reference implementation (HBlink4
 * hblink.py, class OPENBRIDGE) and the C reference (cc2obp
 * openbridge/obp_link.c, GPLv3): 53-byte DMRD body + 20-byte HMAC-SHA1,
 * key = full passphrase string as raw ASCII bytes.
 *
 * Copyright (C) 2026 DVReflectors contributors.
 * SPDX-License-Identifier: GPL-2.0-or-later
 */
#include "obp.h"
#include "crypto.h"
#include <string.h>

static uint32_t rd24(const uint8_t *p)
{
    return ((uint32_t)p[0] << 16) | ((uint32_t)p[1] << 8) | p[2];
}

static uint32_t rd32(const uint8_t *p)
{
    return ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16) |
           ((uint32_t)p[2] << 8) | p[3];
}

static void wr24(uint8_t *p, uint32_t v)
{
    p[0] = (uint8_t)(v >> 16);
    p[1] = (uint8_t)(v >> 8);
    p[2] = (uint8_t)v;
}

static void wr32(uint8_t *p, uint32_t v)
{
    p[0] = (uint8_t)(v >> 24);
    p[1] = (uint8_t)(v >> 16);
    p[2] = (uint8_t)(v >> 8);
    p[3] = (uint8_t)v;
}

void obp_build_body(uint8_t body[OBP_DMRD_BODY_LEN],
                    uint8_t seq, uint32_t src, uint32_t dst, uint32_t netid,
                    uint8_t flags, const uint8_t stream_id[4],
                    const uint8_t payload[OBP_PAYLOAD_LEN])
{
    memcpy(body, "DMRD", 4);
    body[OBP_SEQ_OFF] = seq;
    wr24(body + OBP_SRC_OFF, src & 0xFFFFFF);
    wr24(body + OBP_DST_OFF, dst & 0xFFFFFF);
    wr32(body + OBP_NETID_OFF, netid);
    body[OBP_FLAGS_OFF] = flags;
    memcpy(body + OBP_STREAM_OFF, stream_id, 4);
    memcpy(body + OBP_PAYLOAD_OFF, payload, OBP_PAYLOAD_LEN);
}

void obp_sign(const uint8_t body[OBP_DMRD_BODY_LEN],
              const char *passphrase, uint8_t pkt[OBP_DMRD_PKT_LEN])
{
    memcpy(pkt, body, OBP_DMRD_BODY_LEN);
    hmac_sha1((const uint8_t *)passphrase, strlen(passphrase),
              pkt, OBP_DMRD_BODY_LEN, pkt + OBP_DMRD_BODY_LEN);
}

/* Constant-time digest compare (defense in depth). */
static int ct_eq(const uint8_t *a, const uint8_t *b, size_t n)
{
    uint8_t diff = 0;
    for (size_t i = 0; i < n; i++)
        diff |= (uint8_t)(a[i] ^ b[i]);
    return diff == 0;
}

int obp_verify(const uint8_t pkt[OBP_DMRD_PKT_LEN], const char *passphrase)
{
    if (memcmp(pkt, "DMRD", 4) != 0)
        return -1;
    uint8_t expect[OBP_HMAC_LEN];
    hmac_sha1((const uint8_t *)passphrase, strlen(passphrase),
              pkt, OBP_DMRD_BODY_LEN, expect);
    if (!ct_eq(expect, pkt + OBP_DMRD_BODY_LEN, OBP_HMAC_LEN))
        return -2;
    return 0;
}

void obp_parse_body(const uint8_t body[OBP_DMRD_BODY_LEN], obp_frame *f)
{
    f->seq     = body[OBP_SEQ_OFF];
    f->src     = rd24(body + OBP_SRC_OFF);
    f->dst     = rd24(body + OBP_DST_OFF);
    f->netid   = rd32(body + OBP_NETID_OFF);
    f->flags   = body[OBP_FLAGS_OFF];
    memcpy(f->stream_id, body + OBP_STREAM_OFF, 4);
    memcpy(f->payload,   body + OBP_PAYLOAD_OFF, OBP_PAYLOAD_LEN);

    int frame_type = f->flags & OBPF_FRAMETYPE_MASK;
    int dtype      = f->flags & OBPF_DTYPE_MASK;

    f->is_head      = (frame_type == OBPF_FRAMETYPE_DATASYNC && dtype == OBPF_SLT_VHEAD);
    f->is_term      = (frame_type == OBPF_FRAMETYPE_DATASYNC && dtype == OBPF_SLT_VTERM);
    f->is_voicesync = (frame_type == OBPF_FRAMETYPE_VOICESYNC);
    f->is_voice     = (frame_type == OBPF_FRAMETYPE_VOICE && dtype != 0);
    f->voice_pos    = f->is_voice ? dtype : 0;
    f->is_unit_call = (f->flags & OBPF_CALL_UNIT) != 0;
    f->is_vcsbk     = (f->flags & OBPF_VCSBK_MASK) == OBPF_VCSBK_MASK;
}
