#include <stdlib.h>

#include "scan-modules.h"
#include "../cookie.h"
#include "../templ/templ-icmp.h"
#include "../util/mas-safefunc.h"
#include "../util/mas-malloc.h"

extern struct ScanModule IcmpTimeScan; /*for internal x-ref*/

static int
icmptime_transmit(
    uint64_t entropy,
    struct ScanTarget *target,
    unsigned char *px, size_t *len)
{
    /*icmp timestamp is just for ipv4*/
    if (target->ip_them.version!=4)
        return 0; 

    /*we do not care target port*/
    unsigned cookie = get_cookie(
        target->ip_them, 0, target->ip_me, 0, entropy);

    *len = icmp_create_timestamp_packet(
        target->ip_them, target->ip_me,
        cookie, cookie, 255, px, PKT_BUF_LEN);

    return 0;
}

static void
icmptime_validate(
    uint64_t entropy,
    struct Received *recved,
    struct PreHandle *pre)
{
    /*record icmpv4 to my ip*/
    if (recved->parsed.found == FOUND_ICMP
        && recved->is_myip
        && recved->parsed.src_ip.version==4)
        pre->go_record = 1;
    else return;
    
    ipaddress ip_them = recved->parsed.src_ip;
    ipaddress ip_me = recved->parsed.dst_ip;
    unsigned cookie = get_cookie(ip_them, 0, ip_me, 0, entropy);

    if (get_icmp_type(&recved->parsed)==ICMPv4_TYPE_TIMESTAMP_REPLY
        &&get_icmp_code(&recved->parsed)==ICMPv4_CODE_TIMESTAMP_REPLY
        &&get_icmp_cookie(&recved->parsed, recved->packet)==cookie) {
        pre->go_dedup = 1;
    }
}

static void
icmptime_handle(
    uint64_t entropy,
    struct Received *recved,
    struct OutputItem *item,
    struct stack_t *stack)
{
    item->port_them  = 0;
    item->port_me    = 0;
    item->is_success = 1;

    safe_strcpy(item->reason, OUTPUT_RSN_LEN, "timestamp reply");
    safe_strcpy(item->classification, OUTPUT_CLS_LEN, "alive");
}

struct ScanModule IcmpTimeScan = {
    .name = "icmptime",
    .required_probe_type = 0,
    .desc =
        "IcmpTimeScan sends a ICMP Timestamp mesage to target host. Expect an "
        "ICMP Timestamp Reply to believe the host is alive.\n",

    .global_init_cb         = &scan_init_nothing,
    .transmit_cb            = &icmptime_transmit,
    .validate_cb            = &icmptime_validate,
    .handle_cb              = &icmptime_handle,
    .close_cb               = &scan_close_nothing,
};