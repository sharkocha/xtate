#include <stdlib.h>
#include <string.h>

#include "scan-modules.h"
#include "../cookie.h"
#include "../templ/templ-arp.h"
#include "../util/mas-safefunc.h"
#include "../util/mas-malloc.h"

extern struct ScanModule ArpReqScan; /*for internal x-ref*/

static int
arpreq_make_packet(
    unsigned cur_proto,
    ipaddress ip_them, unsigned port_them,
    ipaddress ip_me, unsigned port_me,
    uint64_t entropy, unsigned index,
    unsigned char *px, unsigned sizeof_px, size_t *r_length)
{
    /*we do not need a cookie and actually cannot set it*/

    *r_length = arp_create_request_packet(ip_them, ip_me, px, sizeof_px);
    
    /*no need do send again in this moment*/
    return 0;
}

static int
arpreq_filter_packet(
    struct PreprocessedInfo *parsed, uint64_t entropy,
    const unsigned char *px, unsigned sizeof_px,
    unsigned is_myip, unsigned is_myport)
{
    /*I do not think we should care about any other types of arp packet.*/
    if (parsed->found==FOUND_ARP && is_myip
        && parsed->opcode==ARP_OPCODE_REPLY) {
        return 1;
    }
    
    return 0;
}

/**
 * Unfortunately, we cannot validate arp replies with a good way, but getting all
 * replies does not seem to be a bad thing.
*/

static int
arpreq_dedup_packet(
    struct PreprocessedInfo *parsed, uint64_t entropy,
    const unsigned char *px, unsigned sizeof_px,
    ipaddress *ip_them, unsigned *port_them,
    ipaddress *ip_me, unsigned *port_me, unsigned *type)
{
    return 1;
}

static int
arpreq_handle_packet(
    struct PreprocessedInfo *parsed, uint64_t entropy,
    const unsigned char *px, unsigned sizeof_px,
    struct OutputItem *item)
{
    item->ip_them   = parsed->src_ip;
    item->port_them = 0;
    item->ip_me     = parsed->dst_ip;
    item->port_me   = 0;

    item->is_success = 1;
    safe_strcpy(item->reason, OUTPUT_RSN_LEN, "arp reply");
    safe_strcpy(item->classification, OUTPUT_CLS_LEN, "alive");
    snprintf(item->report, OUTPUT_RPT_LEN, "%02X:%02X:%02X:%02X:%02X:%02X",
        parsed->mac_src[0], parsed->mac_src[1], parsed->mac_src[2],
        parsed->mac_src[3], parsed->mac_src[4], parsed->mac_src[5]);

    /*no need to response*/
    return 0;
}

struct ScanModule ArpReqScan = {
    .name = "arpreq",
    .required_probe_type = 0,
    .desc =
        "ArpReqScan sends an ARP Request packet to broadcast mac addr"
        "(all one) with target ipv4 addr we request. Expect an ARP Reply packet "
        "with actual mac addr of requested target and print mac addr as report. "
        "ArpReqScan does not support ipv6 target because ipv6 use neighbor "
        "discovery messages of Neighbor Dicovery Protocol(NDP) implemented by ICMPv6 "
        " to dicovery neighbors and their mac addr. ArpReqScan will ignore ipv6 "
        "targets.\n"
        "NOTE: ArpReqScan works in local area network only, so remember to use\n"
        "    `--lan-mode`\n"
        "or to set router mac like:\n"
        "    `--router-mac ff-ff-ff-ff-ff-ff`.\n",

    .global_init_cb = &scan_init_nothing,
    .rx_thread_init_cb = &scan_init_nothing,
    .tx_thread_init_cb = &scan_init_nothing,

    .make_packet_cb = &arpreq_make_packet,

    .filter_packet_cb = &arpreq_filter_packet,
    .validate_packet_cb = &scan_valid_all,
    .dedup_packet_cb = &arpreq_dedup_packet,
    .handle_packet_cb = &arpreq_handle_packet,
    .response_packet_cb = &scan_response_nothing,

    .close_cb = &scan_close_nothing,
};