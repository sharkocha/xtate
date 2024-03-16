#include <string.h>
#include <time.h>

#include "../probe-modules.h"
#include "../../version.h"
#include "../../util/safe-string.h"

/*for internal x-ref*/
extern struct ProbeModule LzrTlsProbe;

static char lzr_tls_payload[] =
"\x16"                                                     /*handshake*/
"\x03\x01"                                                 /*TLSv1.0*/
"\x00\x75"                                                 /*length 117*/
"\x01"                                                     /*client hello*/
"\x00\x00\x71"                                             /*length 113*/
"\x03\x03"                                                 /*TLSv1.2*/
"\x00\x00\x00\x00\x00\x00\x00\x00"                         /*random*/
"\x00\x00\x00\x00\x00\x00\x00\x00"                         /*random*/
"\x00\x00\x00\x00\x00\x00\x00\x00"                         /*random*/
"\x00\x00\x00\x00\x00\x00\x00\x00"                         /*random*/
"\x00"                                                     /*session ID length 0*/
"\x00\x1a"                                                 /*cipher suites lenght 26*/
"\xc0\x2f"                                                 /*cipher suite*/
"\xc0\x2b"                                                 /*cipher suite*/
"\xc0\x11"                                                 /*cipher suite*/
"\xc0\x07"                                                 /*cipher suite*/
"\xc0\x13"                                                 /*cipher suite*/
"\xc0\x09"                                                 /*cipher suite*/
"\xc0\x14"                                                 /*cipher suite*/
"\xc0\x0a"                                                 /*cipher suite*/
"\x00\x05"                                                 /*cipher suite*/
"\x00\x2f"                                                 /*cipher suite*/
"\x00\x35"                                                 /*cipher suite*/
"\xc0\x12"                                                 /*cipher suite*/
"\x00\x0a"                                                 /*cipher suite*/
"\x01"                                                     /*compression methods length*/
"\x00"                                                     /*compression methods*/
"\x00\x2e"                                                 /*extension length 46*/
"\x00\x05\x00\x05\x01\x00\x00\x00\x00"                     /*ext status request*/
"\x00\x0a\x00\x08\x00\x06\x00\x17\x00\x18\x00\x19"         /*ext supported groups*/
"\x00\x0b\x00\x02\x01\x00"                                 /*ext ec point formats*/
"\x00\x0d\x00\x0a\x00\x08\x04\x01\x04\x03\x02\x01\x02\x03" /*ext signature algorithms*/
"\xff\x01\x00\x01\x00"                                     /*ext renegotiation info*/
;

static int lzr_tls_global_init(const void *xconf)
{
    /*fill the random bytes in payload*/
    unsigned r;
    srand((unsigned)time(NULL));
    char *p = lzr_tls_payload + 11; /*Now it's Random in Handshake Protocol*/
    for (unsigned i=0; i<32/4; i++) {
        r    = rand();
        p[0] = (r >> 24) & 0xFF;
        p[1] = (r >> 16) & 0xFF;
        p[2] = (r >>  8) & 0xFF;
        p[3] = (r >>  0) & 0xFF;
        p   += 4;
    }

    return 1;
}

static size_t
lzr_tls_make_payload(
    struct ProbeTarget *target,
    unsigned char *payload_buf)
{
    memcpy(payload_buf, lzr_tls_payload, sizeof(lzr_tls_payload)-1);
    return sizeof(lzr_tls_payload)-1;
}

static size_t
lzr_tls_get_payload_length(struct ProbeTarget *target)
{
    return sizeof(lzr_tls_payload)-1;
}

static int
lzr_tls_handle_reponse(
    struct ProbeTarget *target,
    const unsigned char *px, unsigned sizeof_px,
    struct OutputItem *item)
{
    if (sizeof_px==0) {
        item->level = Output_FAILURE;
        safe_strcpy(item->classification, OUTPUT_CLS_LEN, "not tls");
        safe_strcpy(item->reason, OUTPUT_RSN_LEN, "no response");
        return 0;
    }

    if (sizeof_px < 3) {
        item->level = Output_FAILURE;
        safe_strcpy(item->classification, OUTPUT_CLS_LEN, "not tls");
        safe_strcpy(item->reason, OUTPUT_RSN_LEN, "not matched");
        return 0;
    }

    if (safe_memmem(px, sizeof_px, "HTTPS", strlen("HTTPS"))) {
        item->level = Output_SUCCESS;
        safe_strcpy(item->classification, OUTPUT_CLS_LEN, "tls");
        safe_strcpy(item->reason, OUTPUT_RSN_LEN, "matched");
        return 0;
    }

    //http://blog.fourthbit.com/2014/12/23/traffic-analysis-of-an-ssl-slash-tls-session/
    // Record Type Values       dec      hex
    // -------------------------------------
    // CHANGE_CIPHER_SPEC        20     0x14
    // ALERT                     21     0x15
    // HANDSHAKE                 22     0x16
    // APPLICATION_DATA          23     0x17
    //Version Values            dec     hex
    // -------------------------------------
    // SSL 3.0                   3,0  0x0300
    // TLS 1.0                   3,1  0x0301
    // TLS 1.1                   3,2  0x0302
    // TLS 1.2                   3,3  0x0303
    // TLS 1.3                   3,4  0x0304
    if ((px[0]>=0x14 && px[0]<=0x17) && px[1]==0x03) {
        if (px[2]>=0x01 && px[2]<=0x04) {
            item->level = Output_SUCCESS;
            safe_strcpy(item->classification, OUTPUT_CLS_LEN, "tls");
            safe_strcpy(item->reason, OUTPUT_RSN_LEN, "matched");
            return 0;
        } else if (px[2]==0x00) {
            item->level = Output_SUCCESS;
            safe_strcpy(item->classification, OUTPUT_CLS_LEN, "ssl");
            safe_strcpy(item->reason, OUTPUT_RSN_LEN, "matched");
            return 0;
        }
    }
    
    item->level = Output_FAILURE;
    safe_strcpy(item->classification, OUTPUT_CLS_LEN, "not tls");
    safe_strcpy(item->reason, OUTPUT_RSN_LEN, "not matched");

    return 0;
}

struct ProbeModule LzrTlsProbe = {
    .name       = "lzr-tls",
    .type       = ProbeType_TCP,
    .multi_mode = Multi_Null,
    .multi_num  = 1,
    .params     = NULL,
    .desc =
        "LzrTls Probe sends an simple TLS ClientHello and identifies TLS/SSL service.",
    .global_init_cb                          = &lzr_tls_global_init,
    .make_payload_cb                         = &lzr_tls_make_payload,
    .get_payload_length_cb                   = &lzr_tls_get_payload_length,
    .validate_response_cb                    = NULL,
    .handle_response_cb                      = &lzr_tls_handle_reponse,
    .close_cb                                = &probe_close_nothing,
};