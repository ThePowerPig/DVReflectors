/* test_openbridge.c — OBP wire + voice payload unit tests.
 *
 * Coverage:
 *   - HMAC-SHA1 against RFC 2202 test vectors (incl. >64-byte key path)
 *   - DMRD build/parse round-trip + full flag classification matrix
 *   - sign/verify round-trip + corruption cases (bad magic / bad hmac)
 *   - 49-bit AMBE 7-byte packing vs the dmr_utils3 golden vectors
 *   - head/term payload build -> decode round-trip + layout spot checks
 *   - voice burst build -> parse round-trip at all 6 embed positions,
 *     with explicit EMBED field content checks
 *   - end-to-end DMRD: build body -> sign -> verify -> parse -> classify
 */
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include "../obp.h"
#include "../crypto.h"
#include "../dmr_voice.h"
#include "../dmr/dmr.h"

static int fails = 0, checks = 0;

#define CHECK(cond, msg) do { \
    checks++; \
    if (!(cond)) { fails++; fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, msg); } \
} while (0)

#define CHECK_MEM(cond, msg) do { \
    checks++; \
    if (!(cond)) { fails++; fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, msg); } \
} while (0)

/* ------------------------------------------------------------------ */
/* Golden values from tests/dsp_vectors.txt (dmr_utils3 reference)    */
/* ------------------------------------------------------------------ */
static const uint8_t LC0[9] = {0x00,0x10,0x20,0x00,0x0c,0x30,0x2f,0x9b,0xe5};
static const uint8_t LC1[9] = {0x00,0x00,0x00,0x00,0x00,0x5b,0x2f,0x9b,0x80};
static const uint8_t LC2[9] = {0x00,0x00,0x00,0x00,0x01,0x36,0x2f,0x7c,0x40};
static const uint8_t LC3[9] = {0x00,0x00,0x00,0x12,0x34,0x56,0x78,0x9a,0xbc};

/* lc3 embedded LC fragments (emblc1..4) */
static const uint8_t EMB3[4][4] = {
    {0x06,0x09,0x12,0x18},
    {0x0f,0x33,0x03,0x0f},
    {0x2d,0x18,0x3a,0x14},
    {0x3a,0x1b,0x24,0x2b},
};

/* 49-bit AMBE units (ambe72to49.N.out vectors) */
static const uint8_t AMBE21[21] = {
    0xf0,0x00,0x00,0x00,0x00,0x00,0x00,
    0x00,0x01,0x7c,0xa0,0x88,0x88,0x80,
    0xff,0xff,0x4c,0xff,0xff,0xff,0x80,
};


/* Expected 6-byte EMBED field per position (EMB header + fragment). */
static const uint8_t EMBED_EXPECT[6][6] = {
    {0x75,0x5f,0xd7,0xdf,0x75,0xf7},           /* pos 0: BS_VOICE_SYNC */
    {0x13,0x06,0x09,0x12,0x18,0x91},           /* pos 1: BURST_B + emblc1 */
    {0x17,0x0f,0x33,0x03,0x0f,0x74},           /* pos 2: BURST_C + emblc2 */
    {0x17,0x2d,0x18,0x3a,0x14,0x74},           /* pos 3: BURST_D + emblc3 */
    {0x15,0x3a,0x1b,0x24,0x2b,0x07},           /* pos 4: BURST_E + emblc4 */
    {0x11,0x00,0x00,0x00,0x00,0xe2},           /* pos 5: BURST_F + zeros */
};

/* ------------------------------------------------------------------ */
/* HMAC-SHA1: RFC 2202                                                 */
/* ------------------------------------------------------------------ */
static void hex(const uint8_t *p, int n, char *out)
{
    for (int i = 0; i < n; i++)
        sprintf(out + 2 * i, "%02x", p[i]);
}

static void test_hmac(void)
{
    uint8_t mac[20], key20[20];
    char out[41];

    /* RFC 2202 test case 2: key = 0x0b x 20, data = "Hi There" */
    memset(key20, 0x0b, 20);
    hmac_sha1(key20, 20, (const uint8_t *)"Hi There", 8, mac);
    hex(mac, 20, out);
    CHECK(strcmp(out, "b617318655057264e28bc0b6fb378c8ef146be00") == 0, "HMAC RFC2202 case 2");

    /* RFC 2202 test case 3: key = "Jefe" */
    hmac_sha1((const uint8_t *)"Jefe", 4,
              (const uint8_t *)"what do ya want for nothing?", 28, mac);
    hex(mac, 20, out);
    CHECK(strcmp(out, "effcdf6ae5eb2fa2d27416d5f184df9c259a7c79") == 0, "HMAC RFC2202 case 3");

    /* RFC 2202 test case 6: key = 0xaa x 80 (> 64-byte key path) */
    uint8_t bigkey[80];
    for (int i = 0; i < 80; i++) bigkey[i] = 0xaa;
    hmac_sha1(bigkey, 80,
              (const uint8_t *)"Test Using Larger Than Block-Size Key - Hash Key First", 54, mac);
    hex(mac, 20, out);
    CHECK(strcmp(out, "aa4ae5e15272d00e95705637ce8a3b55ed402112") == 0, "HMAC RFC2202 case 6 (80-byte key)");

    /* short-key sanity: value cross-verified with openssl and python hmac */
    hmac_sha1((const uint8_t *)"key", 3, (const uint8_t *)"Hi There", 8, mac);
    hex(mac, 20, out);
    CHECK(strcmp(out, "37dc8976df0a29180c80070b234998f5be3712d7") == 0, "HMAC key='key' cross-check");
}

/* ------------------------------------------------------------------ */
/* DMRD build/parse + classification                                   */
/* ------------------------------------------------------------------ */
static void test_dmr_fields(void)
{
    uint8_t body[OBP_DMRD_BODY_LEN];
    uint8_t stream[4] = {0xde,0xad,0xbe,0xef};
    uint8_t payload[33];
    memset(payload, 0x5a, sizeof payload);

    obp_build_body(body, 0x42, 0x001234, 0x2c0001, 0x12345678,
                   OBPF_FRAMETYPE_VOICE | 3, stream, payload);

    obp_frame f;
    obp_parse_body(body, &f);
    CHECK(f.seq == 0x42, "seq");
    CHECK(f.src == 0x001234, "src");
    CHECK(f.dst == 0x2c0001, "dst");
    CHECK(f.netid == 0x12345678, "netid");
    CHECK(f.flags == (OBPF_FRAMETYPE_VOICE | 3), "flags");
    CHECK(memcmp(f.stream_id, stream, 4) == 0, "stream_id");
    CHECK(memcmp(f.payload, payload, 33) == 0, "payload");
    CHECK(f.is_voice && f.voice_pos == 3, "classify voice pos 3");
    CHECK(!f.is_head && !f.is_term && !f.is_voicesync, "not head/term/voicesync");

    /* classification matrix */
    struct { uint8_t flags; int head, term, vsync, voice, pos; } cases[] = {
        { OBPF_FRAMETYPE_DATASYNC | OBPF_SLT_VHEAD, 1, 0, 0, 0, 0 },
        { OBPF_FRAMETYPE_DATASYNC | OBPF_SLT_VTERM, 0, 1, 0, 0, 0 },
        { OBPF_FRAMETYPE_VOICESYNC,                 0, 0, 1, 0, 0 },
        { OBPF_FRAMETYPE_VOICE | 1,                 0, 0, 0, 1, 1 },
        { OBPF_FRAMETYPE_VOICE | 2,                 0, 0, 0, 1, 2 },
        { OBPF_FRAMETYPE_VOICE | 5,                 0, 0, 0, 1, 5 },
        { OBPF_FRAMETYPE_VOICE | 0,                 0, 0, 0, 0, 0 }, /* dtype 0: unclassified voice */
    };
    for (size_t i = 0; i < sizeof cases / sizeof cases[0]; i++) {
        obp_build_body(body, 0, 1, 2, 3, cases[i].flags, stream, payload);
        obp_parse_body(body, &f);
        CHECK(f.is_head      == cases[i].head,  "matrix: is_head");
        CHECK(f.is_term      == cases[i].term,  "matrix: is_term");
        CHECK(f.is_voicesync == cases[i].vsync, "matrix: is_voicesync");
        CHECK(f.is_voice     == cases[i].voice, "matrix: is_voice");
        CHECK(f.voice_pos    == cases[i].pos,   "matrix: voice_pos");
    }

    /* unit call + vcsbk bits */
    obp_build_body(body, 0, 1, 2, 3, OBPF_FRAMETYPE_VOICE | 1 | OBPF_CALL_UNIT, stream, payload);
    obp_parse_body(body, &f);
    CHECK(f.is_unit_call, "unit call flag");
    CHECK(!f.is_vcsbk, "not vcsbk");

    obp_build_body(body, 0, 1, 2, 3, 0x23 | 1, stream, payload); /* flags & 0x23 == 0x23 */
    obp_parse_body(body, &f);
    CHECK(f.is_vcsbk, "vcsbk detection");
}

/* ------------------------------------------------------------------ */
/* sign / verify                                                       */
/* ------------------------------------------------------------------ */
static void test_sign_verify(void)
{
    uint8_t body[OBP_DMRD_BODY_LEN], pkt[OBP_DMRD_PKT_LEN];
    uint8_t stream[4] = {1, 2, 3, 4};
    uint8_t payload[33];
    memset(payload, 0, sizeof payload);
    const char *pass = "correct horse battery staple";

    obp_build_body(body, 7, 0x00aabb, 0x2c0001, 0x01020304,
                   OBPF_FRAMETYPE_VOICE | 2, stream, payload);
    obp_sign(body, pass, pkt);
    CHECK(memcmp(pkt, body, OBP_DMRD_BODY_LEN) == 0, "signed pkt carries body");
    CHECK(obp_verify(pkt, pass) == 0, "verify good");

    CHECK(obp_verify(pkt, "wrong passphrase") == -2, "verify wrong key -> -2");
    CHECK(obp_verify(pkt, pass) == 0, "still good after wrong key");

    uint8_t p2[OBP_DMRD_PKT_LEN];
    memcpy(p2, pkt, sizeof p2);
    p2[20] ^= 0xff;  /* corrupt payload */
    CHECK(obp_verify(p2, pass) == -2, "verify corrupted body -> -2");

    memcpy(p2, pkt, sizeof p2);
    p2[0] = 'X';     /* corrupt magic */
    CHECK(obp_verify(p2, pass) == -1, "verify bad magic -> -1");

    memcpy(p2, pkt, sizeof p2);
    p2[OBP_DMRD_PKT_LEN - 1] ^= 0x01;  /* corrupt hmac */
    CHECK(obp_verify(p2, pass) == -2, "verify corrupted hmac -> -2");
}

/* ------------------------------------------------------------------ */
/* 49-bit AMBE 7-byte packing                                          */
/* ------------------------------------------------------------------ */
static void test_ambe49_packing(void)
{
    /* explicit layout: bit pattern 0b0101...0101 + bit48=1 */
    dmr_bit in49[49];
    for (int i = 0; i < 49; i++) in49[i] = (i % 2 == 0);   /* 1,0,1,0,... */
    in49[48] = 1;
    uint8_t out7[7];
    dmr_voice_pack_ambe49(in49, out7);
    CHECK(out7[0] == 0xaa && out7[1] == 0xaa && out7[2] == 0xaa &&
          out7[3] == 0xaa && out7[4] == 0xaa && out7[5] == 0xaa &&
          out7[6] == 0x80, "pack layout MSB-first, bit48 in MSB of byte 6");

    dmr_bit back49[49];
    dmr_voice_unpack_ambe49(out7, back49);
    CHECK(memcmp(back49, in49, 49) == 0, "unpack == original");

    /* golden vectors: 72-bit -> 49 -> 7-byte must match ambe72to49.N.out */
    struct { const char *name; const uint8_t in9[9]; const uint8_t out7[7]; } v[3] = {
        { "0", {0xac,0xaa,0x40,0x20,0x00,0x44,0x40,0x80,0x80},
          {0xf0,0x00,0x00,0x00,0x00,0x00,0x00} },
        { "1", {0x01,0x02,0x03,0x04,0x05,0x06,0x07,0x08,0x09},
          {0x00,0x01,0x7c,0xa0,0x88,0x88,0x80} },
        { "2", {0xff,0xff,0xff,0xff,0xff,0xff,0xff,0xff,0xff},
          {0xff,0xff,0x4c,0xff,0xff,0xff,0x80} },
    };
    for (int i = 0; i < 3; i++) {
        dmr_bit in72[72]; dmr_bytes_to_bits(v[i].in9, 9, in72);
        dmr_bit a49[49]; dmr_ambe_72_to_49(in72, a49);
        uint8_t o7[7];
        dmr_voice_pack_ambe49(a49, o7);
        CHECK(memcmp(o7, v[i].out7, 7) == 0, "pack matches golden vector");
        dmr_bit u49[49]; dmr_voice_unpack_ambe49(v[i].out7, u49);
        CHECK(memcmp(u49, a49, 49) == 0, "unpack matches golden 49-bit");
    }
}

/* ------------------------------------------------------------------ */
/* head/term payloads                                                  */
/* ------------------------------------------------------------------ */
static void test_head_term(void)
{
    const uint8_t *lcs[4] = { LC0, LC1, LC2, LC3 };

    for (int i = 0; i < 4; i++) {
        uint8_t out33[33], dec9[9];
        for (int is_term = 0; is_term <= 1; is_term++) {
            dmr_voice_build_head_term(lcs[i], is_term, out33);
            CHECK(dmr_voice_parse_head_term(out33, dec9) == 0, "parse returns 0");
            CHECK(memcmp(dec9, lcs[i], 9) == 0,
                  is_term ? "term LC round-trip" : "head LC round-trip");

            /* layout: data sync at bits 108..156, slot type at 98..108/156..166 */
            dmr_bit fb[264];
            dmr_bytes_to_bits(out33, 33, fb);
            const dmr_bit *sync = is_term ? DMR_BS_DATA_SYNC : DMR_BS_DATA_SYNC;
            CHECK(memcmp(fb + 108, sync, 48) == 0, "data sync at 108..156");
            const dmr_bit *st = is_term ? DMR_SLOT_TYPE_VTERM : DMR_SLOT_TYPE_VHEAD;
            CHECK(memcmp(fb + 98, st, 10) == 0, "slot type high 10 at 98..108");
            CHECK(memcmp(fb + 156, st + 10, 10) == 0, "slot type low 10 at 156..166");
        }
    }
}

/* ------------------------------------------------------------------ */
/* voice bursts                                                        */
/* ------------------------------------------------------------------ */
static void test_bursts(void)
{
    for (int pos = 0; pos < 6; pos++) {
        uint8_t out33[33];
        dmr_voice_build_burst(AMBE21, pos, EMB3, out33);

        /* EMBED field content at bits 108..156 */
        dmr_bit fb[264];
        dmr_bytes_to_bits(out33, 33, fb);
        uint8_t embed6[6];
        dmr_bits_to_bytes(fb + 108, 48, embed6);
        CHECK(memcmp(embed6, EMBED_EXPECT[pos], 6) == 0, "embed content at position");

        /* round-trip: the three AMBE units come back intact */
        uint8_t back21[21];
        dmr_voice_parse_burst(out33, back21);
        CHECK(memcmp(back21, AMBE21, 21) == 0, "burst ambe21 round-trip");
    }

    /* direct build_embed check for every position */
    for (int pos = 0; pos < 6; pos++) {
        uint8_t e6[6];
        dmr_voice_build_embed(pos, EMB3, e6);
        CHECK(memcmp(e6, EMBED_EXPECT[pos], 6) == 0, "build_embed output");
    }
}

/* ------------------------------------------------------------------ */
/* group LC builder                                                    */
/* ------------------------------------------------------------------ */
static void test_group_lc(void)
{
    uint8_t lc[9];
    dmr_voice_build_group_lc(lc, 0x2c0001, 0x001234);
    CHECK(lc[0] == 0 && lc[1] == 0 && lc[2] == 0, "group LC opt bytes");
    CHECK(lc[3] == 0x2c && lc[4] == 0x00 && lc[5] == 0x01, "group LC tgid");
    CHECK(lc[6] == 0x00 && lc[7] == 0x12 && lc[8] == 0x34, "group LC src");
}

/* ------------------------------------------------------------------ */
/* end-to-end DMRD                                                     */
/* ------------------------------------------------------------------ */
static void test_end_to_end(void)
{
    uint8_t body[OBP_DMRD_BODY_LEN], pkt[OBP_DMRD_PKT_LEN];
    uint8_t stream[4] = {0xc0,0xff,0xee,0x00};
    const char *pass = "test passphrase";

    /* head */
    uint8_t lc9[9], head33[33];
    dmr_voice_build_group_lc(lc9, 0x2c0001, 0x001234);
    dmr_voice_build_head_term(lc9, 0, head33);
    obp_build_body(body, 1, 0x001234, 0x2c0001, 0x0000abcd,
                   OBPF_FRAMETYPE_DATASYNC | OBPF_SLT_VHEAD, stream, head33);
    obp_sign(body, pass, pkt);

    CHECK(obp_verify(pkt, pass) == 0, "e2e head verify");
    obp_frame f;
    obp_parse_body(pkt, &f);
    CHECK(f.is_head, "e2e head classify");
    CHECK(f.seq == 1 && f.src == 0x001234 && f.dst == 0x2c0001 && f.netid == 0x0000abcd,
          "e2e head fields");
    CHECK(memcmp(f.stream_id, stream, 4) == 0, "e2e head stream");
    uint8_t dec9[9];
    dmr_voice_parse_head_term(f.payload, dec9);
    CHECK(memcmp(dec9, lc9, 9) == 0, "e2e head LC decode");

    /* voice burst pos 3 */
    uint8_t burst33[33];
    dmr_voice_build_burst(AMBE21, 3, EMB3, burst33);
    obp_build_body(body, 2, 0x001234, 0x2c0001, 0x0000abcd,
                   OBPF_FRAMETYPE_VOICE | 3, stream, burst33);
    obp_sign(body, pass, pkt);
    CHECK(obp_verify(pkt, pass) == 0, "e2e burst verify");
    obp_parse_body(pkt, &f);
    CHECK(f.is_voice && f.voice_pos == 3, "e2e burst classify");
    uint8_t back21[21];
    dmr_voice_parse_burst(f.payload, back21);
    CHECK(memcmp(back21, AMBE21, 21) == 0, "e2e burst ambe decode");

    /* voice sync (pos 0) */
    dmr_voice_build_burst(AMBE21, 0, EMB3, burst33);
    obp_build_body(body, 3, 0x001234, 0x2c0001, 0x0000abcd,
                   OBPF_FRAMETYPE_VOICESYNC, stream, burst33);
    obp_sign(body, pass, pkt);
    CHECK(obp_verify(pkt, pass) == 0, "e2e voicesync verify");
    obp_parse_body(pkt, &f);
    CHECK(f.is_voicesync, "e2e voicesync classify");
}

int main(void)
{
    test_hmac();
    test_dmr_fields();
    test_sign_verify();
    test_ambe49_packing();
    test_head_term();
    test_bursts();
    test_group_lc();
    test_end_to_end();

    printf("OpenBridge self-test: %d checks, %d failures\n", checks, fails);
    return fails ? 1 : 0;
}
