#include <string.h>
#include <time.h>

#include "../probe-modules.h"
#include "../../version.h"
#include "../../util/safe-string.h"

/*for internal x-ref*/
extern struct ProbeModule LzrFoxProbe;

static unsigned char lzr_fox_payload[] = {
0x66,0x6F,0x78,0x20,0x61,0x20,0x31,0x20,0x2D,0x31,0x20,0x66,0x6F,0x78,0x20,0x68,
0x65,0x6C,0x6C,0x6F,0x0A,0x7B,0x0A,0x66,0x6F,0x78,0x2E,0x76,0x65,0x72,0x73,0x69,
0x6F,0x6E,0x3D,0x73,0x3A,0x31,0x2E,0x30,0x0A,0x69,0x64,0x3D,0x69,0x3A,0x31,0x0A,
0x68,0x6F,0x73,0x74,0x4E,0x61,0x6D,0x65,0x3D,0x73,0x3A,0x78,0x70,0x76,0x6D,0x2D,
0x30,0x6F,0x6D,0x64,0x63,0x30,0x31,0x78,0x6D,0x79,0x0A,0x68,0x6F,0x73,0x74,0x41,
0x64,0x64,0x72,0x65,0x73,0x73,0x3D,0x73,0x3A,0x31,0x39,0x32,0x2E,0x31,0x36,0x38,
0x2E,0x31,0x2E,0x31,0x32,0x35,0x0A,0x61,0x70,0x70,0x2E,0x6E,0x61,0x6D,0x65,0x3D,
0x73,0x3A,0x57,0x6F,0x72,0x6B,0x62,0x65,0x6E,0x63,0x68,0x0A,0x61,0x70,0x70,0x2E,
0x76,0x65,0x72,0x73,0x69,0x6F,0x6E,0x3D,0x73,0x3A,0x33,0x2E,0x37,0x2E,0x34,0x34,
0x0A,0x76,0x6D,0x2E,0x6E,0x61,0x6D,0x65,0x3D,0x73,0x3A,0x4A,0x61,0x76,0x61,0x20,
0x48,0x6F,0x74,0x53,0x70,0x6F,0x74,0x28,0x54,0x4D,0x29,0x20,0x53,0x65,0x72,0x76,
0x65,0x72,0x20,0x56,0x4D,0x0A,0x76,0x6D,0x2E,0x76,0x65,0x72,0x73,0x69,0x6F,0x6E,
0x3D,0x73,0x3A,0x32,0x30,0x2E,0x34,0x2D,0x62,0x30,0x32,0x0A,0x6F,0x73,0x2E,0x6E,
0x61,0x6D,0x65,0x3D,0x73,0x3A,0x57,0x69,0x6E,0x64,0x6F,0x77,0x73,0x20,0x58,0x50,
0x0A,0x6F,0x73,0x2E,0x76,0x65,0x72,0x73,0x69,0x6F,0x6E,0x3D,0x73,0x3A,0x35,0x2E,
0x31,0x0A,0x6C,0x61,0x6E,0x67,0x3D,0x73,0x3A,0x65,0x6E,0x0A,0x74,0x69,0x6D,0x65,
0x5A,0x6F,0x6E,0x65,0x3D,0x73,0x3A,0x41,0x6D,0x65,0x72,0x69,0x63,0x61,0x2F,0x4C,
0x6F,0x73,0x5F,0x41,0x6E,0x67,0x65,0x6C,0x65,0x73,0x3B,0x2D,0x32,0x38,0x38,0x30,
0x30,0x30,0x30,0x30,0x3B,0x33,0x36,0x30,0x30,0x30,0x30,0x30,0x3B,0x30,0x32,0x3A,
0x30,0x30,0x3A,0x30,0x30,0x2E,0x30,0x30,0x30,0x2C,0x77,0x61,0x6C,0x6C,0x2C,0x6D,
0x61,0x72,0x63,0x68,0x2C,0x38,0x2C,0x6F,0x6E,0x20,0x6F,0x72,0x20,0x61,0x66,0x74,
0x65,0x72,0x2C,0x73,0x75,0x6E,0x64,0x61,0x79,0x2C,0x75,0x6E,0x64,0x65,0x66,0x69,
0x6E,0x65,0x64,0x3B,0x30,0x32,0x3A,0x30,0x30,0x3A,0x30,0x30,0x2E,0x30,0x30,0x30,
0x2C,0x77,0x61,0x6C,0x6C,0x2C,0x6E,0x6F,0x76,0x65,0x6D,0x62,0x65,0x72,0x2C,0x31,
0x2C,0x6F,0x6E,0x20,0x6F,0x72,0x20,0x61,0x66,0x74,0x65,0x72,0x2C,0x73,0x75,0x6E,
0x64,0x61,0x79,0x2C,0x75,0x6E,0x64,0x65,0x66,0x69,0x6E,0x65,0x64,0x0A,0x68,0x6F,
0x73,0x74,0x49,0x64,0x3D,0x73,0x3A,0x57,0x69,0x6E,0x2D,0x39,0x39,0x43,0x42,0x2D,
0x44,0x34,0x39,0x44,0x2D,0x35,0x34,0x34,0x32,0x2D,0x30,0x37,0x42,0x42,0x0A,0x76,
0x6D,0x55,0x75,0x69,0x64,0x3D,0x73,0x3A,0x38,0x62,0x35,0x33,0x30,0x62,0x63,0x38,
0x2D,0x37,0x36,0x63,0x35,0x2D,0x34,0x31,0x33,0x39,0x2D,0x61,0x32,0x65,0x61,0x2D,
0x30,0x66,0x61,0x62,0x64,0x33,0x39,0x34,0x64,0x33,0x30,0x35,0x0A,0x62,0x72,0x61,
0x6E,0x64,0x49,0x64,0x3D,0x73,0x3A,0x76,0x79,0x6B,0x6F,0x6E,0x0A,0x7D,0x3B,0x3B,
0x0A,
};

static char lzr_fox_prefix[] = "fox a 0 -1 fox hello";


static size_t
lzr_fox_make_payload(
    struct ProbeTarget *target,
    unsigned char *payload_buf)
{
    memcpy(payload_buf, lzr_fox_payload, sizeof(lzr_fox_payload));
    return sizeof(lzr_fox_payload);
}

static size_t
lzr_fox_get_payload_length(struct ProbeTarget *target)
{
    return sizeof(lzr_fox_payload);
}

static int
lzr_fox_handle_reponse(
    struct ProbeTarget *target,
    const unsigned char *px, unsigned sizeof_px,
    struct OutputItem *item)
{
    if (sizeof_px==0) {
        item->level = Output_FAILURE;
        safe_strcpy(item->classification, OUTPUT_CLS_LEN, "not fox");
        safe_strcpy(item->reason, OUTPUT_RSN_LEN, "no response");
        return 0;
    }

    for (unsigned i=0; i<sizeof_px && i<strlen(lzr_fox_prefix); i++) {
        if (px[i]!=lzr_fox_prefix[i]) {
            item->level = Output_FAILURE;
            safe_strcpy(item->classification, OUTPUT_CLS_LEN, "not fox");
            safe_strcpy(item->reason, OUTPUT_RSN_LEN, "not matched");
            return 0;
        }
    }

    item->level = Output_SUCCESS;
    safe_strcpy(item->classification, OUTPUT_CLS_LEN, "fox");
    safe_strcpy(item->reason, OUTPUT_RSN_LEN, "matched");

    return 0;
}

struct ProbeModule LzrFoxProbe = {
    .name       = "lzr-fox",
    .type       = ProbeType_TCP,
    .multi_mode = Multi_Null,
    .multi_num  = 1,
    .params     = NULL,
    .desc =
        "LzrFox Probe sends an FOX probe and identifies FOX service.",
    .global_init_cb                          = &probe_init_nothing,
    .make_payload_cb                         = &lzr_fox_make_payload,
    .get_payload_length_cb                   = &lzr_fox_get_payload_length,
    .validate_response_cb                    = NULL,
    .handle_response_cb                      = &lzr_fox_handle_reponse,
    .close_cb                                = &probe_close_nothing,
};