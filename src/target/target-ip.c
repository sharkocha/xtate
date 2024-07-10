#include "target-ip.h"
#include "target-parse.h"
#include "target-rangesv4.h"
#include "target-rangesv6.h"
#include "target-rangesport.h"
#include <string.h>
#include <ctype.h>

#include "../util-out/logger.h"

const char *ip_proto_to_string(unsigned ip_proto)
{
    switch (ip_proto) {
        case IP_PROTO_HOPOPT:                return "HOPOPT";
        case IP_PROTO_ICMP:                  return "ICMP";
        case IP_PROTO_IGMP:                  return "IGMP";
        case IP_PROTO_GGP:                   return "GGP";
        case IP_PROTO_IPv4:                  return "IPv4";
        case IP_PROTO_TCP:                   return "TCP";
        case IP_PROTO_EGP:                   return "EGP";
        case IP_PROTO_IGP:                   return "IGP";
        case IP_PROTO_UDP:                   return "UDP";
        case IP_PROTO_IPv6:                  return "IPv6";
        case IP_PROTO_IPv6_Route:            return "IPv6_Route";
        case IP_PROTO_IPv6_Frag:             return "IPv6_Frag";
        case IP_PROTO_IDRP:                  return "IDRP";
        case IP_PROTO_GRE:                   return "GRE";
        case IP_PROTO_Min_IPv4:              return "Min_IPv4";
        case IP_PROTO_IPv6_ICMP:             return "IPv6_ICMP";
        case IP_PROTO_IPv6_NoNxt:            return "IPv6_NoNxt";
        case IP_PROTO_IPv6_Opts:             return "IPv6_Opts";
        case IP_PROTO_OSPFIGP:               return "OSPFIGP";
        case IP_PROTO_ETHERIP:               return "ETHERIP";
        case IP_PROTO_L2TP:                  return "L2TP";
        case IP_PROTO_ISIS_over_IPv4:        return "ISIS_over_IPv4";
        case IP_PROTO_SCTP:                  return "SCTP";
        case IP_PROTO_MPLS_in_IP:            return "MPLS_in_IP";

        default:
            return "Other";
    }
}

void targetip_apply_excludes(TargetIP *targets, TargetIP *exclude)
{
    rangelist_exclude(&targets->ipv4, &exclude->ipv4);
    range6list_exclude(&targets->ipv6, &exclude->ipv6);
    rangelist_exclude(&targets->ports, &exclude->ports);
}

void targetip_optimize(TargetIP *targets)
{
    rangelist_optimize(&targets->ipv4);
    range6list_optimize(&targets->ipv6);
    rangelist_optimize(&targets->ports);

    targets->count_ports = rangelist_count(&targets->ports);
    targets->count_ipv4s = rangelist_count(&targets->ipv4);
    targets->count_ipv6s = range6list_count(&targets->ipv6).lo;
    targets->ipv4_index_threshold = targets->count_ipv4s * rangelist_count(&targets->ports);
}

void targetip_pick(const TargetIP *targetip, uint64_t index, ipaddress *addr, unsigned *port)
{
    /*
     * We can return either IPv4 or IPv6 addresses
     */
    if (index < targetip->ipv4_index_threshold) {
        addr->version = 4;
        addr->ipv4 = rangelist_pick(&targetip->ipv4, index % targetip->count_ipv4s);
        *port = rangelist_pick(&targetip->ports, index / targetip->count_ipv4s);
    } else {
        addr->version = 6;
        index -= targetip->ipv4_index_threshold;
        addr->ipv6 = range6list_pick(&targetip->ipv6, index % targetip->count_ipv6s);
        *port = rangelist_pick(&targetip->ports, index / targetip->count_ipv6s);
    }
}

bool targetip_has_ip(const TargetIP *targetip, ipaddress ip)
{
    if (ip.version == 6)
        return range6list_is_contains(&targetip->ipv6, ip.ipv6);
    else
        return rangelist_is_contains(&targetip->ipv4, ip.ipv4);
}

bool targetip_has_port(const TargetIP *targetip, unsigned port)
{
    return rangelist_is_contains(&targetip->ports, port);
}

bool targetip_has_ipv4_targets(const TargetIP *targetip)
{
    return targetip->ipv4.count != 0;
}
bool targetip_has_target_ports(const TargetIP *targetip)
{
    return targetip->ports.count != 0;
}
bool targetip_has_ipv6_targets(const TargetIP *targetip)
{
    return targetip->ipv6.count != 0;
}


int targetip_add_target_string(TargetIP *targetip, const char *string)
{
    const char *ranges     = string;
    size_t      offset     = 0;
    size_t      max_offset = strlen(ranges);

    while (offset < max_offset) {
        struct Range  range;
        struct Range6 range6;
        int err;

        /* Grab the next IPv4 or IPv6 range */
        err = targetip_parse_range(ranges, &offset, max_offset, &range, &range6);
        switch (err) {
        case Ipv4_Address:
            rangelist_add_range(&targetip->ipv4, range.begin, range.end);
            break;
        case Ipv6_Address:
            range6list_add_range(&targetip->ipv6, range6.begin, range6.end);
            break;
        default:
            offset = max_offset; /* An error means skipping the rest of the string */
            return 1;
        }
        while (offset < max_offset && (isspace(ranges[offset]&0xFF) || ranges[offset] == ','))
            offset++;
    }
    return 0;
}

int targetip_add_port_string(TargetIP *targets, const char *string, unsigned defaultrange)
{
    unsigned is_error = 0;
    rangelist_parse_ports(&targets->ports, string, &is_error, defaultrange);
    if (is_error)
        return 1;
    else
        return 0;
}


int targetip_selftest()
{
    TargetIP targets   = {.ipv4={0}, .ipv6={0}, .ports={0}};
    TargetIP excludes  = {.ipv4={0}, .ipv6={0}, .ports={0}};
    int128_t  count;
    int           line;
    int           err;

    rangelist_parse_ports(&targets.ports, "80", 0, 0);

    /* First, create a list of targets */
    line = __LINE__;
    err = targetip_add_target_string(&targets, "2607:f8b0:4002:801::2004/124,1111::1");
    if (err)
        goto fail;

    /* Second, create an exclude list */
    line = __LINE__;
    err = targetip_add_target_string(&excludes, "2607:f8b0:4002:801::2004/126,1111::/16");
    if (err)
        goto fail;

    /* Third, apply the excludes, causing ranges to be removed
     * from the target list */
    targetip_apply_excludes(&targets, &excludes);

    /* Now make sure the count equals the expected count */
    line = __LINE__;
    count = targetip_range(&targets);
    if (count.hi != 0 || count.lo != 12)
        goto fail;

    return 0;
fail:
    LOG(LEVEL_ERROR, "targetip: test fail, line=%d\n", line);
    return 1;
}