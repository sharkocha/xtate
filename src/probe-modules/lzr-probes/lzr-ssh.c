#include <string.h>
#include <time.h>

#include "../probe-modules.h"
#include "../../version.h"
#include "../../util-data/safe-string.h"

/*for internal x-ref*/
extern struct ProbeModule LzrSshProbe;

static char lzr_ssh_payload[] =
"SSH-2.0-Go\r\n";


static size_t
lzr_ssh_make_payload(
    struct ProbeTarget *target,
    unsigned char *payload_buf)
{
    memcpy(payload_buf, lzr_ssh_payload, strlen(lzr_ssh_payload));
    return strlen(lzr_ssh_payload);
}

static size_t
lzr_ssh_get_payload_length(struct ProbeTarget *target)
{
    return strlen(lzr_ssh_payload);
}

static unsigned
lzr_ssh_handle_reponse(
    unsigned th_idx,
    struct ProbeTarget *target,
    const unsigned char *px, unsigned sizeof_px,
    struct OutputItem *item)
{

    if (safe_memismem(px, sizeof_px, "ssh", strlen("ssh"))
        && !safe_memismem(px, sizeof_px, "not implemented", strlen( "not implemented"))
        && !safe_memismem(px, sizeof_px, "bad", strlen("bad"))) {
        item->level = Output_SUCCESS;
        safe_strcpy(item->classification, OUTPUT_CLS_SIZE, "ssh");
        safe_strcpy(item->reason, OUTPUT_RSN_SIZE, "matched");
        return 0;
    }

    item->level = Output_FAILURE;
    safe_strcpy(item->classification, OUTPUT_CLS_SIZE, "not ssh");
    safe_strcpy(item->reason, OUTPUT_RSN_SIZE, "not matched");

    return 0;
}

static unsigned
lzr_ssh_handle_timeout(struct ProbeTarget *target, struct OutputItem *item)
{
    item->level = Output_FAILURE;
    safe_strcpy(item->classification, OUTPUT_CLS_SIZE, "not ssh");
    safe_strcpy(item->reason, OUTPUT_RSN_SIZE, "no response");
    return 0;
}

struct ProbeModule LzrSshProbe = {
    .name       = "lzr-ssh",
    .type       = ProbeType_TCP,
    .multi_mode = Multi_Null,
    .multi_num  = 1,
    .params     = NULL,
    .desc =
        "LzrSsh Probe sends an SSH probe and identifies SSH service.",
    .global_init_cb                          = &probe_global_init_nothing,
    .make_payload_cb                         = &lzr_ssh_make_payload,
    .get_payload_length_cb                   = &lzr_ssh_get_payload_length,
    .handle_response_cb                      = &lzr_ssh_handle_reponse,
    .handle_timeout_cb                       = &lzr_ssh_handle_timeout,
    .close_cb                                = &probe_close_nothing,
};