/* xcheck: interop harness.
 *   ./xcheck sign <passphrase>  -> prints a signed 73-byte DMRD packet as hex
 *   ./xcheck verify <passphrase> <hex73> -> prints VERIFY-OK / VERIFY-<err>
 */
#include <stdio.h>
#include <string.h>
#include "../obp.h"
#include "../dmr_voice.h"

int main(int argc, char **argv)
{
    (void)argc;
    const char *cmd  = argv[1];
    const char *pass = argv[2];
    uint8_t body[OBP_DMRD_BODY_LEN], pkt[OBP_DMRD_PKT_LEN];

    if (strcmp(cmd, "sign") == 0) {
        uint8_t lc9[9], head33[33], payload[33];
        uint8_t stream[4] = {0xc0, 0xff, 0xee, 0x00};
        /* group call head: TGID 0x2C0001, src 0x001234 */
        lc9[0]=lc9[1]=lc9[2]=0;
        lc9[3]=0x2c; lc9[4]=0x00; lc9[5]=0x01;
        lc9[6]=0x00; lc9[7]=0x12; lc9[8]=0x34;
        dmr_voice_build_head_term(lc9, 0, head33);
        memcpy(payload, head33, 33);
        obp_build_body(body, 0x42, 0x001234, 0x2c0001, 0x0000abcd,
                       OBPF_FRAMETYPE_DATASYNC | OBPF_SLT_VHEAD, stream, payload);
        obp_sign(body, pass, pkt);
        for (int i = 0; i < OBP_DMRD_PKT_LEN; i++) printf("%02x", pkt[i]);
        printf("\n");
        return 0;
    } else if (strcmp(cmd, "verify") == 0) {
        char *hex = argv[3];
        int n = (int)strlen(hex);
        if (n != 2 * OBP_DMRD_PKT_LEN) { printf("LEN-%d\n", n); return 1; }
        for (int i = 0; i < OBP_DMRD_PKT_LEN; i++)
            sscanf(hex + 2 * i, "%02hhx", &pkt[i]);
        int r = obp_verify(pkt, pass);
        printf(r == 0 ? "VERIFY-OK\n" : "VERIFY-%d\n", r);
        return r == 0 ? 0 : 1;
    }
    return 2;
}
