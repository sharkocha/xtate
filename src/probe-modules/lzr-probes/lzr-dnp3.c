#include <string.h>
#include <time.h>

#include "../probe-modules.h"
#include "../../version.h"
#include "../../util-data/safe-string.h"

/*for internal x-ref*/
extern struct ProbeModule LzrDnp3Probe;

static unsigned char lzr_dnp3_payload[] = {
0x00,0x00,0x05,0x64,0x05,0xC9,0x00,0x00,0x00,0x00,0x36,0x4C,0x05,0x64,0x05,0xC9,
0x01,0x00,0x00,0x00,0xDE,0x8E,0x05,0x64,0x05,0xC9,0x02,0x00,0x00,0x00,0x9F,0x84,
0x05,0x64,0x05,0xC9,0x03,0x00,0x00,0x00,0x77,0x46,0x05,0x64,0x05,0xC9,0x04,0x00,
0x00,0x00,0x1D,0x90,0x05,0x64,0x05,0xC9,0x05,0x00,0x00,0x00,0xF5,0x52,0x05,0x64,
0x05,0xC9,0x06,0x00,0x00,0x00,0xB4,0x58,0x05,0x64,0x05,0xC9,0x07,0x00,0x00,0x00,
0x5C,0x9A,0x05,0x64,0x05,0xC9,0x08,0x00,0x00,0x00,0x19,0xB9,0x05,0x64,0x05,0xC9,
0x09,0x00,0x00,0x00,0xF1,0x7B,0x05,0x64,0x05,0xC9,0x0A,0x00,0x00,0x00,0xB0,0x71,
0x05,0x64,0x05,0xC9,0x0B,0x00,0x00,0x00,0x58,0xB3,0x05,0x64,0x05,0xC9,0x0C,0x00,
0x00,0x00,0x32,0x65,0x05,0x64,0x05,0xC9,0x0D,0x00,0x00,0x00,0xDA,0xA7,0x05,0x64,
0x05,0xC9,0x0E,0x00,0x00,0x00,0x9B,0xAD,0x05,0x64,0x05,0xC9,0x0F,0x00,0x00,0x00,
0x73,0x6F,0x05,0x64,0x05,0xC9,0x10,0x00,0x00,0x00,0x11,0xEB,0x05,0x64,0x05,0xC9,
0x11,0x00,0x00,0x00,0xF9,0x29,0x05,0x64,0x05,0xC9,0x12,0x00,0x00,0x00,0xB8,0x23,
0x05,0x64,0x05,0xC9,0x13,0x00,0x00,0x00,0x50,0xE1,0x05,0x64,0x05,0xC9,0x14,0x00,
0x00,0x00,0x3A,0x37,0x05,0x64,0x05,0xC9,0x15,0x00,0x00,0x00,0xD2,0xF5,0x05,0x64,
0x05,0xC9,0x16,0x00,0x00,0x00,0x93,0xFF,0x05,0x64,0x05,0xC9,0x17,0x00,0x00,0x00,
0x7B,0x3D,0x05,0x64,0x05,0xC9,0x18,0x00,0x00,0x00,0x3E,0x1E,0x05,0x64,0x05,0xC9,
0x19,0x00,0x00,0x00,0xD6,0xDC,0x05,0x64,0x05,0xC9,0x1A,0x00,0x00,0x00,0x97,0xD6,
0x05,0x64,0x05,0xC9,0x1B,0x00,0x00,0x00,0x7F,0x14,0x05,0x64,0x05,0xC9,0x1C,0x00,
0x00,0x00,0x15,0xC2,0x05,0x64,0x05,0xC9,0x1D,0x00,0x00,0x00,0xFD,0x00,0x05,0x64,
0x05,0xC9,0x1E,0x00,0x00,0x00,0xBC,0x0A,0x05,0x64,0x05,0xC9,0x1F,0x00,0x00,0x00,
0x54,0xC8,0x05,0x64,0x05,0xC9,0x20,0x00,0x00,0x00,0x01,0x4F,0x05,0x64,0x05,0xC9,
0x21,0x00,0x00,0x00,0xE9,0x8D,0x05,0x64,0x05,0xC9,0x22,0x00,0x00,0x00,0xA8,0x87,
0x05,0x64,0x05,0xC9,0x23,0x00,0x00,0x00,0x40,0x45,0x05,0x64,0x05,0xC9,0x24,0x00,
0x00,0x00,0x2A,0x93,0x05,0x64,0x05,0xC9,0x25,0x00,0x00,0x00,0xC2,0x51,0x05,0x64,
0x05,0xC9,0x26,0x00,0x00,0x00,0x83,0x5B,0x05,0x64,0x05,0xC9,0x27,0x00,0x00,0x00,
0x6B,0x99,0x05,0x64,0x05,0xC9,0x28,0x00,0x00,0x00,0x2E,0xBA,0x05,0x64,0x05,0xC9,
0x29,0x00,0x00,0x00,0xC6,0x78,0x05,0x64,0x05,0xC9,0x2A,0x00,0x00,0x00,0x87,0x72,
0x05,0x64,0x05,0xC9,0x2B,0x00,0x00,0x00,0x6F,0xB0,0x05,0x64,0x05,0xC9,0x2C,0x00,
0x00,0x00,0x05,0x66,0x05,0x64,0x05,0xC9,0x2D,0x00,0x00,0x00,0xED,0xA4,0x05,0x64,
0x05,0xC9,0x2E,0x00,0x00,0x00,0xAC,0xAE,0x05,0x64,0x05,0xC9,0x2F,0x00,0x00,0x00,
0x44,0x6C,0x05,0x64,0x05,0xC9,0x30,0x00,0x00,0x00,0x26,0xE8,0x05,0x64,0x05,0xC9,
0x31,0x00,0x00,0x00,0xCE,0x2A,0x05,0x64,0x05,0xC9,0x32,0x00,0x00,0x00,0x8F,0x20,
0x05,0x64,0x05,0xC9,0x33,0x00,0x00,0x00,0x67,0xE2,0x05,0x64,0x05,0xC9,0x34,0x00,
0x00,0x00,0x0D,0x34,0x05,0x64,0x05,0xC9,0x35,0x00,0x00,0x00,0xE5,0xF6,0x05,0x64,
0x05,0xC9,0x36,0x00,0x00,0x00,0xA4,0xFC,0x05,0x64,0x05,0xC9,0x37,0x00,0x00,0x00,
0x4C,0x3E,0x05,0x64,0x05,0xC9,0x38,0x00,0x00,0x00,0x09,0x1D,0x05,0x64,0x05,0xC9,
0x39,0x00,0x00,0x00,0xE1,0xDF,0x05,0x64,0x05,0xC9,0x3A,0x00,0x00,0x00,0xA0,0xD5,
0x05,0x64,0x05,0xC9,0x3B,0x00,0x00,0x00,0x48,0x17,0x05,0x64,0x05,0xC9,0x3C,0x00,
0x00,0x00,0x22,0xC1,0x05,0x64,0x05,0xC9,0x3D,0x00,0x00,0x00,0xCA,0x03,0x05,0x64,
0x05,0xC9,0x3E,0x00,0x00,0x00,0x8B,0x09,0x05,0x64,0x05,0xC9,0x3F,0x00,0x00,0x00,
0x63,0xCB,0x05,0x64,0x05,0xC9,0x40,0x00,0x00,0x00,0x58,0x4A,0x05,0x64,0x05,0xC9,
0x41,0x00,0x00,0x00,0xB0,0x88,0x05,0x64,0x05,0xC9,0x42,0x00,0x00,0x00,0xF1,0x82,
0x05,0x64,0x05,0xC9,0x43,0x00,0x00,0x00,0x19,0x40,0x05,0x64,0x05,0xC9,0x44,0x00,
0x00,0x00,0x73,0x96,0x05,0x64,0x05,0xC9,0x45,0x00,0x00,0x00,0x9B,0x54,0x05,0x64,
0x05,0xC9,0x46,0x00,0x00,0x00,0xDA,0x5E,0x05,0x64,0x05,0xC9,0x47,0x00,0x00,0x00,
0x32,0x9C,0x05,0x64,0x05,0xC9,0x48,0x00,0x00,0x00,0x77,0xBF,0x05,0x64,0x05,0xC9,
0x49,0x00,0x00,0x00,0x9F,0x7D,0x05,0x64,0x05,0xC9,0x4A,0x00,0x00,0x00,0xDE,0x77,
0x05,0x64,0x05,0xC9,0x4B,0x00,0x00,0x00,0x36,0xB5,0x05,0x64,0x05,0xC9,0x4C,0x00,
0x00,0x00,0x5C,0x63,0x05,0x64,0x05,0xC9,0x4D,0x00,0x00,0x00,0xB4,0xA1,0x05,0x64,
0x05,0xC9,0x4E,0x00,0x00,0x00,0xF5,0xAB,0x05,0x64,0x05,0xC9,0x4F,0x00,0x00,0x00,
0x1D,0x69,0x05,0x64,0x05,0xC9,0x50,0x00,0x00,0x00,0x7F,0xED,0x05,0x64,0x05,0xC9,
0x51,0x00,0x00,0x00,0x97,0x2F,0x05,0x64,0x05,0xC9,0x52,0x00,0x00,0x00,0xD6,0x25,
0x05,0x64,0x05,0xC9,0x53,0x00,0x00,0x00,0x3E,0xE7,0x05,0x64,0x05,0xC9,0x54,0x00,
0x00,0x00,0x54,0x31,0x05,0x64,0x05,0xC9,0x55,0x00,0x00,0x00,0xBC,0xF3,0x05,0x64,
0x05,0xC9,0x56,0x00,0x00,0x00,0xFD,0xF9,0x05,0x64,0x05,0xC9,0x57,0x00,0x00,0x00,
0x15,0x3B,0x05,0x64,0x05,0xC9,0x58,0x00,0x00,0x00,0x50,0x18,0x05,0x64,0x05,0xC9,
0x59,0x00,0x00,0x00,0xB8,0xDA,0x05,0x64,0x05,0xC9,0x5A,0x00,0x00,0x00,0xF9,0xD0,
0x05,0x64,0x05,0xC9,0x5B,0x00,0x00,0x00,0x11,0x12,0x05,0x64,0x05,0xC9,0x5C,0x00,
0x00,0x00,0x7B,0xC4,0x05,0x64,0x05,0xC9,0x5D,0x00,0x00,0x00,0x93,0x06,0x05,0x64,
0x05,0xC9,0x5E,0x00,0x00,0x00,0xD2,0x0C,0x05,0x64,0x05,0xC9,0x5F,0x00,0x00,0x00,
0x3A,0xCE,0x05,0x64,0x05,0xC9,0x60,0x00,0x00,0x00,0x6F,0x49,0x05,0x64,0x05,0xC9,
0x61,0x00,0x00,0x00,0x87,0x8B,0x05,0x64,0x05,0xC9,0x62,0x00,0x00,0x00,0xC6,0x81,
0x05,0x64,0x05,0xC9,0x63,0x00,0x00,0x00,0x2E,0x43,
};

static size_t
lzr_dnp3_make_payload(
    struct ProbeTarget *target,
    unsigned char *payload_buf)
{
    memcpy(payload_buf, lzr_dnp3_payload, sizeof(lzr_dnp3_payload));
    return sizeof(lzr_dnp3_payload);
}

static size_t
lzr_dnp3_get_payload_length(struct ProbeTarget *target)
{
    return sizeof(lzr_dnp3_payload);
}

static unsigned
lzr_dnp3_handle_reponse(
    unsigned th_idx,
    struct ProbeTarget *target,
    const unsigned char *px, unsigned sizeof_px,
    struct OutputItem *item)
{
    if (sizeof_px>=10 && px[0]==0x05 && px[1]==0x64) {
        item->level = Output_SUCCESS;
        safe_strcpy(item->classification, OUTPUT_CLS_LEN, "dnp3");
        safe_strcpy(item->reason, OUTPUT_RSN_LEN, "matched");
        return 0;
    }

    item->level = Output_FAILURE;
    safe_strcpy(item->classification, OUTPUT_CLS_LEN, "not dnp3");
    safe_strcpy(item->reason, OUTPUT_RSN_LEN, "not matched");

    return 0;
}

static unsigned
lzr_dnp3_handle_timeout(struct ProbeTarget *target, struct OutputItem *item)
{
    item->level = Output_FAILURE;
    safe_strcpy(item->classification, OUTPUT_CLS_LEN, "not dnp3");
    safe_strcpy(item->reason, OUTPUT_RSN_LEN, "no response");
    return 0;
}

struct ProbeModule LzrDnp3Probe = {
    .name       = "lzr-dnp3",
    .type       = ProbeType_TCP,
    .multi_mode = Multi_Null,
    .multi_num  = 1,
    .params     = NULL,
    .desc =
        "LzrDnp3 Probe sends an DNP3 probe and identifies DNP3 service.",
    .global_init_cb                          = &probe_global_init_nothing,
    .make_payload_cb                         = &lzr_dnp3_make_payload,
    .get_payload_length_cb                   = &lzr_dnp3_get_payload_length,
    .handle_response_cb                      = &lzr_dnp3_handle_reponse,
    .handle_timeout_cb                       = &lzr_dnp3_handle_timeout,
    .close_cb                                = &probe_close_nothing,
};