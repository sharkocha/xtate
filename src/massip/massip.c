#include "massip.h"
#include "massip-parse.h"
#include "massip-rangesv4.h"
#include "massip-rangesv6.h"
#include "massip-rangesport.h"
#include <string.h>
#include <ctype.h>

#include "../util-out/logger.h"

const char *ip_proto_to_string(unsigned ip_proto)
{
    switch (ip_proto) {
        case IP_PROTO_HOPOPT:        return "HOPOPT";
        case IP_PROTO_ICMP:          return "ICMP";
        case IP_PROTO_IGMP:          return "IGMP";
        case IP_PROTO_GGP:           return "GGP";
        case IP_PROTO_IP:            return "IP";
        case IP_PROTO_TCP:           return "TCP";
        case IP_PROTO_EGP:           return "EGP";
        case IP_PROTO_IGP:           return "IGP";
        case IP_PROTO_UDP:           return "UDP";
        case IP_PROTO_IPv6:          return "IPv6";
        case IP_PROTO_IPv6_Route:    return "IPv6_Route";
        case IP_PROTO_IPv6_Frag:     return "IPv6_Frag";
        case IP_PROTO_IDRP:          return "IDRP";
        case IP_PROTO_GRE:           return "GRE";
        case IP_PROTO_MHRP:          return "MHRP";
        case IP_PROTO_ESP:           return "ESP";
        case IP_PROTO_AH:            return "AH";
        case IP_PROTO_MOBILE:        return "MOBILE";
        case IP_PROTO_IPv6_ICMP:     return "IPv6_ICMP";
        case IP_PROTO_IPv6_NoNxt:    return "IPv6_NoNxt";
        case IP_PROTO_IPv6_Opts:     return "IPv6_Opts";
        case IP_PROTO_IPIP:          return "IPIP";
        case IP_PROTO_ETHERIP:       return "ETHERIP";
        case IP_PROTO_ENCAP:         return "ENCAP";
        case IP_PROTO_L2TP:          return "L2TP";
        case IP_PROTO_SCTP:          return "SCTP";

        default:
            return "Other";
    }
}

void massip_apply_excludes(struct MassIP *targets, struct MassIP *exclude)
{
    rangelist_exclude(&targets->ipv4, &exclude->ipv4);
    range6list_exclude(&targets->ipv6, &exclude->ipv6);
    rangelist_exclude(&targets->ports, &exclude->ports);
}

void massip_optimize(struct MassIP *targets)
{
    rangelist_optimize(&targets->ipv4);
    range6list_optimize(&targets->ipv6);
    rangelist_optimize(&targets->ports);

    targets->count_ports = rangelist_count(&targets->ports);
    targets->count_ipv4s = rangelist_count(&targets->ipv4);
    targets->count_ipv6s = range6list_count(&targets->ipv6).lo;
    targets->ipv4_index_threshold = targets->count_ipv4s * rangelist_count(&targets->ports);
}

void massip_pick(const struct MassIP *massip, uint64_t index, ipaddress *addr, unsigned *port)
{
    /*
     * We can return either IPv4 or IPv6 addresses
     */
    if (index < massip->ipv4_index_threshold) {
        addr->version = 4;
        addr->ipv4 = rangelist_pick(&massip->ipv4, index % massip->count_ipv4s);
        *port = rangelist_pick(&massip->ports, index / massip->count_ipv4s);
    } else {
        addr->version = 6;
        index -= massip->ipv4_index_threshold;
        addr->ipv6 = range6list_pick(&massip->ipv6, index % massip->count_ipv6s);
        *port = rangelist_pick(&massip->ports, index / massip->count_ipv6s);
    }
}

bool massip_has_ip(const struct MassIP *massip, ipaddress ip)
{
    if (ip.version == 6)
        return range6list_is_contains(&massip->ipv6, ip.ipv6);
    else
        return rangelist_is_contains(&massip->ipv4, ip.ipv4);
}

bool massip_has_port(const struct MassIP *massip, unsigned port)
{
    return rangelist_is_contains(&massip->ports, port);
}

bool massip_has_ipv4_targets(const struct MassIP *massip)
{
    return massip->ipv4.count != 0;
}
bool massip_has_target_ports(const struct MassIP *massip)
{
    return massip->ports.count != 0;
}
bool massip_has_ipv6_targets(const struct MassIP *massip)
{
    return massip->ipv6.count != 0;
}


int massip_add_target_string(struct MassIP *massip, const char *string)
{
    const char *ranges = string;
    size_t offset = 0;
    size_t max_offset = strlen(ranges);

    while (offset < max_offset) {
        struct Range range;
        struct Range6 range6;
        int err;

        /* Grab the next IPv4 or IPv6 range */
        err = massip_parse_range(ranges, &offset, max_offset, &range, &range6);
        switch (err) {
        case Ipv4_Address:
            rangelist_add_range(&massip->ipv4, range.begin, range.end);
            break;
        case Ipv6_Address:
            range6list_add_range(&massip->ipv6, range6.begin, range6.end);
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

int massip_add_port_string(struct MassIP *targets, const char *string, unsigned defaultrange)
{
    unsigned is_error = 0;
    rangelist_parse_ports(&targets->ports, string, &is_error, defaultrange);
    if (is_error)
        return 1;
    else
        return 0;
}


int massip_selftest()
{
    struct MassIP targets;
    struct MassIP excludes;
    int err;
    int line;
    massint128_t count;

    memset(&targets, 0, sizeof(targets));
    memset(&excludes, 0, sizeof(targets));

    rangelist_parse_ports(&targets.ports, "80", 0, 0);

    /* First, create a list of targets */
    line = __LINE__;
    err = massip_add_target_string(&targets, "2607:f8b0:4002:801::2004/124,1111::1");
    if (err)
        goto fail;

    /* Second, create an exclude list */
    line = __LINE__;
    err = massip_add_target_string(&excludes, "2607:f8b0:4002:801::2004/126,1111::/16");
    if (err)
        goto fail;

    /* Third, apply the excludes, causing ranges to be removed
     * from the target list */
    massip_apply_excludes(&targets, &excludes);

    /* Now make sure the count equals the expected count */
    line = __LINE__;
    count = massip_range(&targets);
    if (count.hi != 0 || count.lo != 12)
        goto fail;

    return 0;
fail:
    LOG(LEVEL_ERROR, "[-] massip: test fail, line=%d\n", line);
    return 1;
}