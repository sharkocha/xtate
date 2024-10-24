#include <stdlib.h>

#include "scan-modules.h"
#include "../xconf.h"
#include "../target/target-cookie.h"
#include "../templ/templ-icmp.h"
#include "../util-data/safe-string.h"
#include "../util-data/fine-malloc.h"

extern Scanner IcmpEchoScan; /*for internal x-ref*/

struct IcmpEchoConf {
    unsigned record_ttl  : 1;
    unsigned record_ipid : 1;
};

static struct IcmpEchoConf icmpecho_conf = {0};

static ConfRes SET_record_ttl(void *conf, const char *name, const char *value) {
    UNUSEDPARM(conf);
    UNUSEDPARM(name);

    icmpecho_conf.record_ttl = parse_str_bool(value);

    return Conf_OK;
}

static ConfRes SET_record_ipid(void *conf, const char *name,
                               const char *value) {
    UNUSEDPARM(conf);
    UNUSEDPARM(name);

    icmpecho_conf.record_ipid = parse_str_bool(value);

    return Conf_OK;
}

static ConfParam icmpecho_parameters[] = {
    {"record-ttl",
     SET_record_ttl,
     Type_FLAG,
     {"ttl", 0},
     "Records TTL for IPv4 or Hop Limit for IPv6 in ICMP Echo Reply."},
    {"record-ipid",
     SET_record_ipid,
     Type_FLAG,
     {"ipid", 0},
     "Records IPID of ICMP Echo Reply just for IPv4."},

    {0}};

static bool icmpecho_transmit(uint64_t entropy, ScanTarget *target,
                              ScanTmEvent *event, unsigned char *px,
                              size_t *len) {
    if (target->target.ip_proto != IP_PROTO_Other)
        return false;

    /*we do not care target port*/
    unsigned cookie =
        get_cookie(target->target.ip_them, 0, target->target.ip_me, 0, entropy);
    uint16_t id   = (cookie >> 16) & 0xFF;
    uint16_t seq  = (cookie >> 0) & 0xFF;
    uint16_t ipid = cookie ^ entropy;

    *len =
        icmp_echo_create_packet(target->target.ip_them, target->target.ip_me,
                                id, seq, ipid, 64, NULL, 0, px, PKT_BUF_SIZE);

    /*add timeout*/
    event->need_timeout     = 1;
    event->target.port_them = 0;
    event->target.port_me   = 0;

    return false;
}

static void icmpecho_validate(uint64_t entropy, Recved *recved,
                              PreHandle *pre) {
    /*record icmp to my ip*/
    if (recved->parsed.found == FOUND_ICMP && recved->is_myip)
        pre->go_record = 1;
    else
        return;

    ipaddress ip_them = recved->parsed.src_ip;
    ipaddress ip_me   = recved->parsed.dst_ip;
    unsigned  cookie  = get_cookie(ip_them, 0, ip_me, 0, entropy);

    if (recved->parsed.src_ip.version == 4 &&
        recved->parsed.icmp_type == ICMPv4_TYPE_ECHO_REPLY &&
        recved->parsed.icmp_code == ICMPv4_CODE_ECHO_REPLY &&
        recved->parsed.icmp_id == ((cookie >> 16) & 0xFF) &&
        recved->parsed.icmp_seq == ((cookie >> 0) & 0xFF)) {
        pre->go_dedup        = 1;
        pre->dedup_port_them = 0;
        pre->dedup_port_me   = 0;
    } else if (recved->parsed.src_ip.version == 6 &&
               recved->parsed.icmp_type == ICMPv6_TYPE_ECHO_REPLY &&
               recved->parsed.icmp_code == ICMPv6_CODE_ECHO_REPLY &&
               recved->parsed.icmp_id == ((cookie >> 16) & 0xFF) &&
               recved->parsed.icmp_seq == ((cookie >> 0) & 0xFF)) {
        pre->go_dedup        = 1;
        pre->dedup_port_them = 0;
        pre->dedup_port_me   = 0;
    }
}

static void icmpecho_handle(unsigned th_idx, uint64_t entropy, Recved *recved,
                            OutItem *item, STACK *stack, FHandler *handler) {
    item->target.port_them = 0;
    item->target.port_me   = 0;
    item->no_port          = 1;
    item->level            = OUT_SUCCESS;

    safe_strcpy(item->reason, OUT_RSN_SIZE, "echo reply");
    safe_strcpy(item->classification, OUT_CLS_SIZE, "alive");

    if (icmpecho_conf.record_ttl)
        dach_set_int(&item->report, "ttl", recved->parsed.ip_ttl);
    if (icmpecho_conf.record_ipid && recved->parsed.src_ip.version == 4)
        dach_set_int(&item->report, "ipid", recved->parsed.ip_v4_id);
}

static void icmpecho_timeout(uint64_t entropy, ScanTmEvent *event,
                             OutItem *item, STACK *stack, FHandler *handler) {
    item->no_port = 1;
    item->level   = OUT_FAILURE;
    safe_strcpy(item->classification, OUT_CLS_SIZE, "down");
    safe_strcpy(item->reason, OUT_RSN_SIZE, "timeout");
}

Scanner IcmpEchoScan = {
    .name                = "icmp-echo",
    .required_probe_type = 0,
    .support_timeout     = 1,
    .params              = icmpecho_parameters,
    /*icmp echo reply in ipv4 & ipv6*/
    .bpf_filter          = "(icmp && (icmp[0]==0 && icmp[1]==0)) "
                           "|| (icmp6 && (icmp6[0]==129 && icmp6[1]==0))",
    .short_desc          = "ICMP Ping scan to find alive hosts.",
    .desc = "IcmpEchoScan sends an ICMP ECHO Request packet to target host. "
            "Expect an "
            "ICMP ECHO Reply to believe the host is alive.\n"
            "NOTE: Don't specify any ports for this module.",

    .init_cb     = &scan_init_nothing,
    .transmit_cb = &icmpecho_transmit,
    .validate_cb = &icmpecho_validate,
    .handle_cb   = &icmpecho_handle,
    .timeout_cb  = &icmpecho_timeout,
    .poll_cb     = &scan_poll_nothing,
    .close_cb    = &scan_close_nothing,
    .status_cb   = &scan_no_status,
};