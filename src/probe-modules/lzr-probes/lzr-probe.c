#include <stdio.h>

#include "lzr-probe.h"
#include "../../util/mas-safefunc.h"

#define LZR_SUBPROBE_NAME_LEN 20

/*
 * LZR Probe will use `handle_response_cb` of all subprobes listed here
 * to match the banner and identify its service automaticly.
 * 
 * Subprobes' names always start with 'lzr-', and could be used as a normal
 * ProbeModule. It reports what service it identified out and will report
 * nothong if no service identified.
 * 
 * When they specified as subprobes in LZR probe with `--probe-args`, we should
 * omit the 'lzr-' prefix like 'lzr-http' -> 'http'.
 * 
 * LZR probe uses specified subprobe to send payload, and matches all subprobes
 * for result reporting. It could reports more than one identified service type
 * or 'unknown' if nothing identified.
 * 
 * NOTE: While ProbeModule is as Subprobe of LZR, its init and close callback
 * funcs will never be called.
 */

extern struct ProbeModule LzrWaitProbe;
extern struct ProbeModule LzrHttpProbe;
extern struct ProbeModule LzrFtpProbe;
//! ADD NEW LZR SUBPROBES HERE
//! ALSO ADD TO stateless-probes.c IF NEEDED



static struct ProbeModule *lzr_subprobes[] = {
    &LzrWaitProbe,
    &LzrHttpProbe,
    &LzrFtpProbe,
    //! ADD NEW LZR SUBPROBES HERE
    //! ALSO ADD TO probe-modules.c IF NEEDED
};

/******************************************************************/

/*for x-refer*/
extern struct ProbeModule LzrProbe;

static struct ProbeModule *specified_subprobe;

static int
lzr_global_init()
{
    /*Use LzrWait if no subprobe specified*/
    if (!LzrProbe.args) {
        specified_subprobe = &LzrWaitProbe;
        fprintf(stderr, "[-] Use default LzrWait as subprobe of LzrProbe "
            "because no subprobe was specified by --probe-module-args.\n");
    } else {
        char subprobe_name[LZR_SUBPROBE_NAME_LEN] = "lzr-";
        memcpy(subprobe_name+strlen(subprobe_name), LzrProbe.args,
            LZR_SUBPROBE_NAME_LEN-strlen(subprobe_name)-1);

        specified_subprobe = get_probe_module_by_name(subprobe_name);
        if (specified_subprobe == NULL) {
            fprintf(stderr, "[-] Invalid name of subprobe for lzr.\n");
            return 0;
        }
    }

    LzrProbe.make_payload_cb = specified_subprobe->make_payload_cb;
    LzrProbe.get_payload_length_cb = specified_subprobe->get_payload_length_cb;

    return 1;
}

static void
lzr_handle_response(
    ipaddress ip_them, unsigned port_them,
    ipaddress ip_me, unsigned port_me,
    const unsigned char *px, unsigned sizeof_px,
    char *report, unsigned rpt_length)
{
    /**
     * I think STATELESS_BANNER_MAX_LEN is long enough.
     * However I am tired while coding there.
     * But its safe while I use safe copy funcs in every time.
    */
    char *buf_idx = report;
    size_t remain_len = rpt_length;
    
    /**
     * strcat every lzr subprobes match result
     * print results just like lzr:
     *     pop3-smtp-http
    */
    for (size_t i=0; i<sizeof(lzr_subprobes)/sizeof(struct ProbeModule*); i++) {
        if (lzr_subprobes[i]->handle_response_cb) {
            lzr_subprobes[i]->handle_response_cb(
                ip_them, port_them, ip_me, port_me,
                px, sizeof_px,
                buf_idx, remain_len
            );

            for (;buf_idx[0]!='\0';buf_idx++) {}
            buf_idx[0] = '-';
            buf_idx++;
            remain_len = rpt_length - (buf_idx - report);
        }
    }

    /*got nothing*/
    if (buf_idx==report) {
        safe_strcpy(report, rpt_length, "unknown");
        buf_idx += strlen("unknown");
    } else {
    /* truncat the last '-' */
        buf_idx[0] = '\0';
    }
}

struct ProbeModule LzrProbe = {
    .name = "lzr",
    .type = ProbeType_TCP,
    .desc =
        "LZR Probe is an implement of service identification of LZR. It sends a "
        "specified LZR subprobe(handshake) and try to match with all LZR subprobes "
        "with `handle_reponse_cb`.\n"
        "Specify LZR subprobe by probe arguments:\n"
        "    `--probe-module-args http`\n",
    .global_init_cb = &lzr_global_init,
    .rx_thread_init_cb = NULL,
    .tx_thread_init_cb = NULL,
    // `make_payload_cb` will be set dynamicly in lzr_global_init.
    // `get_payload_length_cb` will be set dynamicly in lzr_global_init.
    .validate_response_cb = NULL,
    .handle_response_cb = &lzr_handle_response,
    .close_cb = NULL,
};