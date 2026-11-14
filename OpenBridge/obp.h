/*
 * obp.h — OpenBridge Protocol (OBP) DMRD wire layer for DVReflectors.
 *
 * A packet is a 53-byte DMRD body followed by a 20-byte HMAC-SHA1 digest
 * computed over the 53-byte body with the shared passphrase (ASCII bytes) as
 * the HMAC key. 73 bytes total. No handshake, no session setup — one UDP
 * datagram per DMRD frame.
 *
 * Wire layout confirmed against the reference implementation
 * (HBlink4 hblink.py, class OPENBRIDGE send_system/datagram_received) and the
 * C reference (cc2obp openbridge/obp_link.c, GPLv3).
 *
 * DMRD body layout (bytes 0..52):
 *   [0..4)    "DMRD" magic
 *   [4]       seq (1 byte, 0..255, wraps)
 *   [5..8)    rf_src / source ID (3 bytes, big-endian, 24-bit)
 *   [8..11)   dst / TGID (3 bytes, big-endian, 24-bit)
 *   [11..15)  netid / network-repeater ID (4 bytes, big-endian). Overwritten
 *             with the SENDER's own network ID on send by default.
 *   [15]      flags (1 byte)
 *   [16..20)  stream_id (4 bytes, random per call)
 *   [20..53)  payload (33 bytes, one DMR burst)
 *
 * flags byte (offset 15):
 *   0x80  SLOT2 (timeslot 2; reflectors are slot 1)
 *   0x40  unit call (private — dropped by reflectors)
 *   0x30  frame_type: VOICE=0x00, VOICESYNC=0x10, DATASYNC=0x20
 *   0x0F  dtype: VHEAD=0x01, VTERM=0x02; for voice bursts 1..5 = burst
 *         position within the 6-frame embed cycle
 *
 * Classification (mirrors the reference RX flow):
 *   is_head  = frame_type==DATASYNC && dtype==VHEAD
 *   is_term  = frame_type==DATASYNC && dtype==VTERM
 *   voice    = frame_type==VOICE (dtype 1..5 carries the burst position)
 *   voicesync= frame_type==VOICESYNC (burst position 0 of the cycle)
 *
 * Copyright (C) 2026 DVReflectors contributors.
 * SPDX-License-Identifier: GPL-2.0-or-later
 */
#ifndef OBP_OBP_H
#define OBP_OBP_H

#include <stdint.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

#define OBP_DMRD_BODY_LEN   53
#define OBP_HMAC_LEN        20
#define OBP_DMRD_PKT_LEN    (OBP_DMRD_BODY_LEN + OBP_HMAC_LEN)   /* 73 */

/* DMRD body field offsets */
#define OBP_MAGIC_OFF       0
#define OBP_SEQ_OFF         4
#define OBP_SRC_OFF         5
#define OBP_DST_OFF         8
#define OBP_NETID_OFF       11
#define OBP_FLAGS_OFF       15
#define OBP_STREAM_OFF      16
#define OBP_PAYLOAD_OFF     20
#define OBP_PAYLOAD_LEN     33

/* Flags byte (offset 15) */
#define OBPF_SLOT2          0x80
#define OBPF_CALL_UNIT      0x40
#define OBPF_FRAMETYPE_MASK 0x30
#define OBPF_FRAMETYPE_VOICE     0x00
#define OBPF_FRAMETYPE_VOICESYNC 0x10
#define OBPF_FRAMETYPE_DATASYNC  0x20
#define OBPF_DTYPE_MASK     0x0F
#define OBPF_SLT_VHEAD      0x01
#define OBPF_SLT_VTERM      0x02
#define OBPF_VCSBK_MASK     0x23   /* vcsbk detection: (flags & 0x23) == 0x23 */

/* A parsed + classified DMRD body. */
typedef struct {
    uint8_t  seq;
    uint32_t src;        /* 24-bit source ID */
    uint32_t dst;        /* 24-bit destination / TGID */
    uint32_t netid;      /* 32-bit network/repeater ID */
    uint8_t  flags;
    uint8_t  stream_id[4];
    uint8_t  payload[33];
    /* classification (at most one of is_head/is_term/is_voicesync/is_voice set) */
    int      is_head;
    int      is_term;
    int      is_voicesync;   /* burst position 0 of the embed cycle */
    int      is_voice;       /* burst position 1..5 */
    int      voice_pos;      /* 1..5 when is_voice; 0 otherwise */
    int      is_unit_call;   /* OBPF_CALL_UNIT set */
    int      is_vcsbk;       /* (flags & OBPF_VCSBK_MASK) == OBPF_VCSBK_MASK */
} obp_frame;

/* Fill a 53-byte DMRD body. src/dst are 24-bit values. payload must be 33
 * bytes (see dmr_voice.h). */
void obp_build_body(uint8_t body[OBP_DMRD_BODY_LEN],
                    uint8_t seq, uint32_t src, uint32_t dst, uint32_t netid,
                    uint8_t flags, const uint8_t stream_id[4],
                    const uint8_t payload[OBP_PAYLOAD_LEN]);

/* body (53) + passphrase -> full 73-byte signed packet. */
void obp_sign(const uint8_t body[OBP_DMRD_BODY_LEN],
              const char *passphrase, uint8_t pkt[OBP_DMRD_PKT_LEN]);

/* Verify a 73-byte packet. Returns 0 if the magic and HMAC are both valid,
 * -1 on bad magic, -2 on HMAC mismatch. */
int obp_verify(const uint8_t pkt[OBP_DMRD_PKT_LEN], const char *passphrase);

/* Parse + classify a 53-byte DMRD body (magic already checked). */
void obp_parse_body(const uint8_t body[OBP_DMRD_BODY_LEN], obp_frame *f);

#ifdef __cplusplus
}
#endif

#endif /* OBP_OBP_H */
