#include <string.h>

#include "../probe-modules.h"
#include "../../util-data/safe-string.h"

/*for internal x-ref*/
extern struct ProbeModule LzrVncProbe;

static unsigned
lzr_vnc_handle_response(
    unsigned th_idx,
    struct ProbeTarget *target,
    const unsigned char *px, unsigned sizeof_px,
    struct OutputItem *item)
{

    if (safe_memmem(px, sizeof_px, "RFB", strlen("RFB"))) {
        item->level = OP_SUCCESS;
        safe_strcpy(item->classification, OP_CLS_SIZE, "vnc");
        safe_strcpy(item->reason, OP_RSN_SIZE, "matched");
        return 0;
    }

    item->level = OP_FAILURE;
    safe_strcpy(item->classification, OP_CLS_SIZE, "not vnc");
    safe_strcpy(item->reason, OP_RSN_SIZE, "not matched");

    return 0;
}

static unsigned
lzr_vnc_handle_timeout(struct ProbeTarget *target, struct OutputItem *item)
{
    item->level = OP_FAILURE;
    safe_strcpy(item->classification, OP_CLS_SIZE, "not vnc");
    safe_strcpy(item->reason, OP_RSN_SIZE, "no response");
    return 0;
}

struct ProbeModule LzrVncProbe = {
    .name       = "lzr-vnc",
    .type       = ProbeType_TCP,
    .multi_mode = Multi_Null,
    .multi_num  = 1,
    .params     = NULL,
    .desc =
        "LzrVnc Probe sends no payload and identifies VNC service.",
    .global_init_cb                          = &probe_global_init_nothing,
    .make_payload_cb                         = &probe_make_no_payload,
    .get_payload_length_cb                   = &probe_no_payload_length,
    .handle_response_cb                      = &lzr_vnc_handle_response,
    .handle_timeout_cb                       = &lzr_vnc_handle_timeout,
    .close_cb                                = &probe_close_nothing,
};