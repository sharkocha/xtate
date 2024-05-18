#include <string.h>
#include <time.h>

#include "../probe-modules.h"
#include "../../version.h"
#include "../../util-data/safe-string.h"

/*for internal x-ref*/
extern struct ProbeModule LzrMemcachedBinaryProbe;

static char lzr_memb_payload[] = {
    0x80, 0x10, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0,
    0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0
};

static size_t
lzr_memb_make_payload(
    struct ProbeTarget *target,
    unsigned char *payload_buf)
{
    memcpy(payload_buf, lzr_memb_payload, sizeof(lzr_memb_payload));
    return sizeof(lzr_memb_payload);
}

static size_t
lzr_memb_get_payload_length(struct ProbeTarget *target)
{
    return sizeof(lzr_memb_payload);
}

static unsigned
lzr_memb_handle_reponse(
    unsigned th_idx,
    struct ProbeTarget *target,
    const unsigned char *px, unsigned sizeof_px,
    struct OutputItem *item)
{

    if (px[0]==0x81
        || safe_memmem(px, sizeof_px, "ERROR\r\n", strlen("ERROR\r\n"))) {
        item->level = Output_SUCCESS;
        safe_strcpy(item->classification, OUTPUT_CLS_SIZE, "memcached_binary");
        safe_strcpy(item->reason, OUTPUT_RSN_SIZE, "matched");
        return 0;
    }

    item->level = Output_FAILURE;
    safe_strcpy(item->classification, OUTPUT_CLS_SIZE, "not memcached_binary");
    safe_strcpy(item->reason, OUTPUT_RSN_SIZE, "not matched");

    return 0;
}

static unsigned
lzr_memb_handle_timeout(struct ProbeTarget *target, struct OutputItem *item)
{
    item->level = Output_FAILURE;
    safe_strcpy(item->classification, OUTPUT_CLS_SIZE, "not memcached_binary");
    safe_strcpy(item->reason, OUTPUT_RSN_SIZE, "no response");
    return 0;
}

struct ProbeModule LzrMemcachedBinaryProbe = {
    .name       = "lzr-memcached_binary",
    .type       = ProbeType_TCP,
    .multi_mode = Multi_Null,
    .multi_num  = 1,
    .params     = NULL,
    .desc =
        "LzrMemcachedBinary Probe sends a Memcached Binary request and identifies"
        " Memcached Binary service.",
    .global_init_cb                          = &probe_global_init_nothing,
    .make_payload_cb                         = &lzr_memb_make_payload,
    .get_payload_length_cb                   = &lzr_memb_get_payload_length,
    .handle_response_cb                      = &lzr_memb_handle_reponse,
    .handle_timeout_cb                       = &lzr_memb_handle_timeout,
    .close_cb                                = &probe_close_nothing,
};