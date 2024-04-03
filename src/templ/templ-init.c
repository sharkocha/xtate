#include <assert.h>
#include <string.h>
#include <stdlib.h>

#include "templ-init.h"
#include "templ-tcp.h"
#include "templ-opts.h"
#include "../massip/massip-port.h"
#include "../proto/proto-preprocess.h"
#include "../util-data/safe-string.h"
#include "../pixie/pixie-timer.h"
#include "../util-out/logger.h"
#include "../util-data/data-convert.h"
#include "../massip/cookie.h"
#include "../util-misc/unusedparm.h"
#include "../util-misc/checksum.h"
#include "../util-data/fine-malloc.h"
#include "../stub/stub-pcap-dlt.h"

/* For adding some fixed tcp options*/
unsigned char default_tcp_syn_template[] =
    "\0\1\2\3\4\5"      /* Ethernet: destination */
    "\6\7\x8\x9\xa\xb"  /* Ethernet: source */
    "\x08\x00"          /* Ethernet type: IPv4 */

    "\x45"              /* IP type */
    "\x00"
    "\x00\x2c"          /* total length = 44 bytes */
    "\x00\x00"          /* identification */
    "\x00\x00"          /* fragmentation flags */
    "\xFF\x06"          /* TTL=255, proto=TCP */
    "\xFF\xFF"          /* checksum */
    "\0\0\0\0"          /* source address */
    "\0\0\0\0"          /* destination address */

    "\0\0"              /* source port */
    "\0\0"              /* destination port */
    "\0\0\0\0"          /* sequence number */
    "\0\0\0\0"          /* ACK number */
    "\x60"              /* header length: the first 4bits 0110=6 -> 6*4=24bytes */
    "\x02"              /* SYN */
    "\xfa\xf0"          /* window 64240 (default ipv4 tcp win of my win11, and tcp win of win11 ipv6 is \xfd\x20->64800) */
    "\xFF\xFF"          /* checksum */
    "\x00\x00"          /* urgent pointer */
    "\x02\x04\x05\xb4"  /* opt [mss 1460] */
;

/* No options */
unsigned char default_tcp_template[] =
    "\0\1\2\3\4\5"      /* Ethernet: destination */
    "\6\7\x8\x9\xa\xb"  /* Ethernet: source */
    "\x08\x00"          /* Ethernet type: IPv4 */

    "\x45"              /* IP type */
    "\x00"
    "\x00\x28"          /* total length = 40 bytes */
    "\x00\x00"          /* identification */
    "\x00\x00"          /* fragmentation flags */
    "\xFF\x06"          /* TTL=255, proto=TCP */
    "\xFF\xFF"          /* checksum */
    "\0\0\0\0"          /* source address */
    "\0\0\0\0"          /* destination address */

    "\0\0"              /* source port */
    "\0\0"              /* destination port */
    "\0\0\0\0"          /* sequence number */
    "\0\0\0\0"          /* ACK number */
    "\x50"              /* header length: the first 4bits 0101=5 -> 5*4=20bytes */
    "\x02"              /* SYN */
    "\x04\x01"          /* window fixed to 1024, too large could make troubles for zbanner*/
    "\xFF\xFF"          /* checksum */
    "\x00\x00"          /* urgent pointer */
;

static unsigned char default_udp_template[] =
    "\0\1\2\3\4\5"      /* Ethernet: destination */
    "\6\7\x8\x9\xa\xb"  /* Ethernet: source */
    "\x08\x00"          /* Ethernet type: IPv4 */
    "\x45"              /* IP type */
    "\x00"
    "\x00\x1c"          /* total length = 28 bytes */
    "\x00\x00"          /* identification */
    "\x00\x00"          /* fragmentation flags */
    "\xFF\x11"          /* TTL=255, proto=UDP */
    "\xFF\xFF"          /* checksum */
    "\0\0\0\0"          /* source address */
    "\0\0\0\0"          /* destination address */

    "\xfe\xdc"          /* source port */
    "\x00\x00"          /* destination port */
    "\x00\x08"          /* length */
    "\x00\x00"          /* checksum */
;

static unsigned char default_sctp_template[] =
    "\0\1\2\3\4\5"      /* Ethernet: destination */
    "\6\7\x8\x9\xa\xb"  /* Ethernet: source */
    "\x08\x00"          /* Ethernet type: IPv4 */
    "\x45"              /* IP type */
    "\x00"
    "\x00\x34"          /* total length = 52 bytes */
    "\x00\x00"          /* identification */
    "\x00\x00"          /* fragmentation flags */
    "\xFF\x84"          /* TTL=255, proto = SCTP */
    "\x00\x00"          /* checksum */
    "\0\0\0\0"          /* source address */
    "\0\0\0\0"          /* destination address */

    "\x00\x00"          /* source port */
    "\x00\x00"          /* destination port */
    "\x00\x00\x00\x00"  /* verification tag */
    "\x58\xe4\x5d\x36"  /* checksum */
    "\x01"              /* type = init */
    "\x00"              /* flags = none */
    "\x00\x14"          /* length = 20 */
    "\x9e\x8d\x52\x25"  /* initiate tag */
    "\x00\x00\x80\x00"  /* receiver window credit */
    "\x00\x0a"          /* outbound streams = 10 */
    "\x08\x00"          /* inbound streams = 2048 */
    "\x46\x1a\xdf\x3d"  /* initial TSN */
;


static unsigned char default_icmp_ping_template[] =
    "\0\1\2\3\4\5"      /* Ethernet: destination */
    "\6\7\x8\x9\xa\xb"  /* Ethernet: source */
    "\x08\x00"          /* Ethernet type: IPv4 */
    "\x45"              /* IP type */
    "\x00"
    "\x00\x4c"          /* total length = 76 bytes */
    "\x00\x00"          /* identification */
    "\x00\x00"          /* fragmentation flags */
    "\xFF\x01"          /* TTL=255, proto=ICMP */
    "\xFF\xFF"          /* checksum */
    "\0\0\0\0"          /* source address */
    "\0\0\0\0"          /* destination address */

    "\x08\x00"          /* Ping Request */
    "\x00\x00"          /* checksum */

    "\x00\x00\x00\x00"  /* ID, seqno */

    "\x08\x09\x0a\x0b"  /* payload */
    "\x0c\x0d\x0e\x0f"
    "\x10\x11\x12\x13"
    "\x14\x15\x16\x17"
    "\x18\x19\x1a\x1b"
    "\x1c\x1d\x1e\x1f"
    "\x20\x21\x22\x23"
    "\x24\x25\x26\x27"
    "\x28\x29\x2a\x2b"
    "\x2c\x2d\x2e\x2f"
    "\x30\x31\x32\x33"
    "\x34\x35\x36\x37"
;

static unsigned char default_icmp_timestamp_template[] =
"\0\1\2\3\4\5"          /* Ethernet: destination */
    "\6\7\x8\x9\xa\xb"  /* Ethernet: source */
"\x08\x00"              /* Ethernet type: IPv4 */
"\x45"                  /* IP type */
"\x00"
"\x00\x28"              /* total length = 40 bytes */
"\x00\x00"              /* identification */
"\x00\x00"              /* fragmentation flags */
"\xFF\x01"              /* TTL=255, proto=ICMP */
"\xFF\xFF"              /* checksum */
"\0\0\0\0"              /* source address */
"\0\0\0\0"              /* destination address */

"\x0d\x00"              /* timestamp request */
"\x00\x00"              /* checksum */
"\x00\x00"              /* identifier */
"\x00\x00"              /* sequence number */
"\x00\x00\x00\x00"
"\x00\x00\x00\x00"
"\x00\x00\x00\x00"
;


static unsigned char default_arp_template[] =
    "\xff\xff\xff\xff\xff\xff"    /* Ethernet: destination */
    "\x00\x00\x00\x00\x00\x00"    /* Ethernet: source */
    "\x08\x06"                    /* Ethernet type: ARP */
    "\x00\x01"                    /* hardware = Ethernet */
    "\x08\x00"                    /* protocol = IPv4 */
    "\x06\x04"                    /* MAC length = 6, IPv4 length = 4 */
    "\x00\x01"                    /* opcode = request */

    "\x00\x00\x00\x00\x00\x00"
    "\x00\x00\x00\x00"

    "\x00\x00\x00\x00\x00\x00"
    "\x00\x00\x00\x00"
;


static unsigned char default_ndp_ns_template[] =
"\0\1\2\3\4\5"                /* Ethernet: destination */
    "\6\7\x8\x9\xa\xb"        /* Ethernet: source */
"\x08\x00"                    /* Ethernet type: IPv4 */
"\x45"                        /* IP type */
"\x00"
"\x00\x34"                    /* total length = 54 bytes */
"\x00\x00"                    /* identification */
"\x00\x00"                    /* fragmentation flags */
"\xFF\x01"                    /* TTL=255, proto=UDP */
"\xFF\xFF"                    /* checksum */
"\0\0\0\0"                    /* source address */
"\0\0\0\0"                    /* destination address */

"\x87\x00"                    /* neighbor solicitation */
"\x00\x00"                    /* checksum */
"\x00\x00\x00\x00"            /*reserved*/
"\x00\x00\x00\x00"            /*Target address*/
"\x00\x00\x00\x00"
"\x00\x00\x00\x00"
"\x00\x00\x00\x00"
"\x01"                        /*ICMPv6 Option Type: Source link-layer address*/
"\x01"                        /*Length for 8 bytes*/
"\x00\x00\x00\x00\x00\x00"    /*Link-layer address*/
;

#if defined(WIN32) || defined(_WIN32)
#define AF_INET6 23
#else
#include <sys/socket.h>
#endif

/***************************************************************************
 * Creates an IPv6 packet from an IPv4 template, by simply replacing
 * the IPv4 header with the IPv6 header.
 ***************************************************************************/
static void
_template_init_ipv6(struct TemplatePacket *tmpl, macaddress_t router_mac_ipv6,
    unsigned data_link_type)
{
    struct PreprocessedInfo parsed;
    unsigned payload_length;
    unsigned offset_ip;
    unsigned offset_tcp;
    unsigned offset_tcp6;
    unsigned char *buf;
    unsigned x;

    /* Zero out everything and start from scratch */
    if (tmpl->ipv6.packet) {
        free(tmpl->ipv6.packet);
        memset(&tmpl->ipv6, 0, sizeof(tmpl->ipv6));
    }

    /* Parse the existing IPv4 packet */
    x = preprocess_frame(tmpl->ipv4.packet, tmpl->ipv4.length, data_link_type, &parsed);
    if (!x || parsed.found == FOUND_NOTHING) {
        LOG(LEVEL_ERROR, "ERROR: bad packet template\n");
        exit(1);
    }

    /* The "payload" in this case is everything past the IP header,
     * so TCP or UDP headers are inside the IP payload */
    payload_length = tmpl->ipv4.length - tmpl->ipv4.offset_tcp;
    offset_ip      = tmpl->ipv4.offset_ip;
    offset_tcp     = tmpl->ipv4.offset_tcp;

    /* Create a copy of the IPv4 packet */
    buf = MALLOC(tmpl->ipv4.length + 40);
    memcpy(buf, tmpl->ipv4.packet, tmpl->ipv4.length);
    tmpl->ipv6.packet = buf;


    /* destination = end of IPv6 header
     * source = end of IPv4 header
     * contents = everything after IPv4/IPv6 header */
    offset_tcp6 = offset_ip + 40;
    memmove(buf + offset_tcp6,
            buf + offset_tcp,       
            payload_length
            );

    /* fill the IPv6 header with zeroes */
    memset(buf + offset_ip, 0, 40);
    tmpl->ipv6.length = offset_ip + 40 + payload_length;
    
    switch (data_link_type) {
        case PCAP_DLT_NULL: /* Null VPN tunnel */
            /* FIXME: insert platform dependent value here */
            *(int*)buf = AF_INET6;
            break;
    case PCAP_DLT_RAW: /* Raw (nothing before IP header) */
        break;
        case PCAP_DLT_ETHERNET: /* Ethernet */
            /* Reset the destination MAC address to be the IPv6 router
             * instead of the IPv4 router, which sometimes are different */
            memcpy(buf + 0, router_mac_ipv6.addr, 6);
            
            /* Reset the Ethertype field to 0x86dd (meaning IPv6) */
            buf[12] = 0x86;
            buf[13] = 0xdd;
            break;
    }
    

    /* IP.version = 6 */
    buf[offset_ip + 0] = 0x60; 

    /* Set payload length field. In IPv4, this field included the header,
     * but in IPv6, it's everything after the header. In other words,
     * the size of an IPv6 packet is 40+payload_length, whereas in IPv4
     * it was total_length. */
    U16_TO_BE(buf+offset_ip+4, payload_length);

    /* Set the "next header" field.
     * TODO: need to fix ICMP */
    buf[offset_ip + 6] = (unsigned char)parsed.ip_protocol;
    if (parsed.ip_protocol == 1) {
        buf[offset_ip + 6] = 58; /* ICMPv6 */
        if (payload_length > 0 && buf[offset_tcp6 + 0] == 8) {
            /* PING -> PINGv6 */
            buf[offset_tcp6 + 0] = 128;
        }
    }

    /* Hop limit starts out as 255 */
    buf[offset_ip + 7] = 0xFF;

    /* Parse our newly construct IPv6 packet */
    x = preprocess_frame(buf, tmpl->ipv6.length, data_link_type, &parsed);
    if (!x || parsed.found == FOUND_NOTHING) {
        LOG(LEVEL_ERROR, "[-] FAILED: bad packet template\n");
        exit(1);
    }
    tmpl->ipv6.offset_ip  = parsed.ip_offset;
    tmpl->ipv6.offset_tcp = parsed.transport_offset;
    tmpl->ipv6.offset_app = parsed.app_offset;


}

/***************************************************************************
 * Here we take a packet template, parse it, then make it easier to work
 * with.
 ***************************************************************************/
static void
_template_init(
    struct TemplatePacket *tmpl,
    macaddress_t source_mac,
    macaddress_t router_mac_ipv4,
    macaddress_t router_mac_ipv6,
    const void *packet_bytes,
    size_t packet_size,
    unsigned data_link_type
    )
{
    unsigned char *px;
    struct PreprocessedInfo parsed;
    unsigned x;
    
    /*
     * Create the new template structure:
     * - zero it out
     * - make copy of the old packet to serve as new template
     */
    memset(tmpl, 0, sizeof(*tmpl));
    tmpl->ipv4.length = (unsigned)packet_size;

    tmpl->ipv4.packet = MALLOC(2048 + packet_size);
    memcpy(tmpl->ipv4.packet, packet_bytes, tmpl->ipv4.length);
    px = tmpl->ipv4.packet;

    x = preprocess_frame(px, tmpl->ipv4.length, 1 /*enet*/, &parsed);
    if (!x || parsed.found == FOUND_NOTHING) {
        LOG(LEVEL_ERROR, "ERROR: bad packet template\n");
        exit(1);
    }
    tmpl->ipv4.offset_ip  = parsed.ip_offset;
    tmpl->ipv4.offset_tcp = parsed.transport_offset;
    tmpl->ipv4.offset_app = parsed.app_offset;
    if (parsed.found == FOUND_ARP) {
        tmpl->ipv4.length = parsed.ip_offset + 28;
    } else
        tmpl->ipv4.length = parsed.ip_offset + parsed.ip_length;

    /*
     * Overwrite the MAC and IP addresses
     */
    memcpy(px+0, router_mac_ipv4.addr, 6);
    memcpy(px+6, source_mac.addr, 6);
    memset((void*)parsed._ip_src, 0, 4);
    memset((void*)parsed._ip_dst, 0, 4);


    /*
     * ARP
     *
     * If this is an ARP template (for doing arpscans), then just set our
     * configured source IP and MAC addresses.
     */
    if (parsed.found == FOUND_ARP) {
        memcpy((char*)parsed._ip_src - 6, source_mac.addr, 6);
        tmpl->proto = Proto_ARP;
        return;
    }

    /*
     * IPv4
     *
     * Calculate the partial checksum. We zero out the fields that will be
     * added later the packet, then calculate the checksum as if they were
     * zero. This makes recalculation of the checksum easier when we transmit
     */
    memset(px + tmpl->ipv4.offset_ip +  4, 0, 2);  /* IP ID field */
    memset(px + tmpl->ipv4.offset_ip + 10, 0, 2); /* checksum */
    memset(px + tmpl->ipv4.offset_ip + 12, 0, 8); /* addresses */
    tmpl->ipv4.checksum_ip = checksum_ip_header( tmpl->ipv4.packet,
                                            tmpl->ipv4.offset_ip,
                                            tmpl->ipv4.length);

    /*
     * Higher layer protocols: zero out dest/checksum fields, then calculate
     * a partial checksum
     */
    switch (parsed.ip_protocol) {
    case 1: /* ICMP */
            tmpl->ipv4.offset_app   = tmpl->ipv4.length;
            tmpl->ipv4.checksum_tcp = checksum_icmp(tmpl->ipv4.packet,
                tmpl->ipv4.offset_tcp, tmpl->ipv4.length-tmpl->ipv4.offset_tcp);
            switch (px[tmpl->ipv4.offset_tcp]) {
                case 8:
                    tmpl->proto = Proto_ICMP_ping;
                    break;
                case 13:
                    tmpl->proto = Proto_ICMP_timestamp;
                    break;
                case 135:
                    tmpl->proto = Proto_NDP_ns;
                    break;
            }
            break;
        break;
    case 6: /* TCP */
        /* zero out fields that'll be overwritten */
        memset(px + tmpl->ipv4.offset_tcp +  0, 0, 8); /* destination port and seqno */
        memset(px + tmpl->ipv4.offset_tcp + 16, 0, 2); /* checksum */
        tmpl->ipv4.checksum_tcp = checksum_tcp(tmpl->ipv4.packet, tmpl->ipv4.offset_ip,
            tmpl->ipv4.offset_tcp, tmpl->ipv4.length-tmpl->ipv4.offset_tcp);
        tmpl->proto = Proto_TCP;
        break;
    case 17: /* UDP */
        memset(px + tmpl->ipv4.offset_tcp + 6, 0, 2); /* checksum */
        tmpl->ipv4.checksum_tcp = checksum_udp(tmpl->ipv4.packet,
            tmpl->ipv4.offset_ip, tmpl->ipv4.offset_tcp,
            tmpl->ipv4.length-tmpl->ipv4.offset_tcp);
        tmpl->proto = Proto_UDP;
        break;
    case 132: /* SCTP */
        tmpl->ipv4.checksum_tcp = checksum_sctp(
                                    tmpl->ipv4.packet + tmpl->ipv4.offset_tcp,
                                    tmpl->ipv4.length - tmpl->ipv4.offset_tcp);
        tmpl->proto = Proto_SCTP;
        break;
    }

    /*
     * DATALINK KLUDGE
     *
     * Adjust the data link header in case of Raw IP packets. This isn't
     * the correct way to do this, but I'm too lazy to refactor code
     * for the right way, so we'll do it this way now.
     */
    if (data_link_type == PCAP_DLT_NULL /* Null VPN tunnel */) {
        int linkproto = 2; /* AF_INET */
        tmpl->ipv4.length     -= tmpl->ipv4.offset_ip - sizeof(int);
        tmpl->ipv4.offset_tcp -= tmpl->ipv4.offset_ip - sizeof(int);
        tmpl->ipv4.offset_app -= tmpl->ipv4.offset_ip - sizeof(int);
        memmove(tmpl->ipv4.packet + sizeof(int),
                tmpl->ipv4.packet + tmpl->ipv4.offset_ip,
                tmpl->ipv4.length);
        tmpl->ipv4.offset_ip = 4;
        memcpy(tmpl->ipv4.packet, &linkproto, sizeof(int));
    } else if (data_link_type == PCAP_DLT_RAW /* Raw IP */) {
        tmpl->ipv4.length     -= tmpl->ipv4.offset_ip;
        tmpl->ipv4.offset_tcp -= tmpl->ipv4.offset_ip;
        tmpl->ipv4.offset_app -= tmpl->ipv4.offset_ip;
        memmove(tmpl->ipv4.packet,
                tmpl->ipv4.packet + tmpl->ipv4.offset_ip,
                tmpl->ipv4.length);
        tmpl->ipv4.offset_ip = 0;
    } else if (data_link_type == PCAP_DLT_ETHERNET) {
    /* the default, do nothing */
    } else {
    LOG(LEVEL_ERROR, "[-] FAILED: bad packet template, unknown data link type\n");
        LOG(LEVEL_ERROR, "    [hint] xtate doesn't know how to format packets for this interface\n");
    exit(1);
    }

    /* Now create an IPv6 template based upon the IPv4 template */
    _template_init_ipv6(tmpl, router_mac_ipv6, data_link_type);
}

/***************************************************************************
 ***************************************************************************/
void
template_packet_init(
    struct TemplateSet *templset,
    macaddress_t source_mac,
    macaddress_t router_mac_ipv4,
    macaddress_t router_mac_ipv6,
    int data_link,
    uint64_t entropy,
    const struct TemplateOptions *templ_opts)
{
    unsigned char *buf;
    size_t length;
    templset->count = 0;
    templset->entropy = entropy;


    /* [SCTP] */
    _template_init(&templset->pkts[Proto_SCTP],
                   source_mac, router_mac_ipv4, router_mac_ipv6,
                   default_sctp_template,
                   sizeof(default_sctp_template)-1,
                   data_link);
    templset->count++;

    /* [TCP] */
    length = sizeof(default_tcp_template) - 1;
    buf = MALLOC(length);
    memcpy(buf, default_tcp_template, length);
    _template_init(&templset->pkts[Proto_TCP],
                   source_mac, router_mac_ipv4, router_mac_ipv6,
                   buf,
                   length,
                   data_link);
    templset->count++;
    free(buf);

    /* [TCP SYN] */
    length = sizeof(default_tcp_syn_template) - 1;
    buf = MALLOC(length);
    memcpy(buf, default_tcp_syn_template, length);
    templ_tcp_apply_options(&buf, &length, templ_opts); /*set options for syn*/
    _template_init(&templset->pkts[Proto_TCP_SYN],
                   source_mac, router_mac_ipv4, router_mac_ipv6,
                   buf,
                   length,
                   data_link);
    templset->count++;
    free(buf);

    /* [UDP] */
    _template_init(&templset->pkts[Proto_UDP],
                   source_mac, router_mac_ipv4, router_mac_ipv6,
                   default_udp_template,
                   sizeof(default_udp_template)-1,
                   data_link);
    templset->count++;

    /* [ICMP ping] */
    _template_init(&templset->pkts[Proto_ICMP_ping],
                   source_mac, router_mac_ipv4, router_mac_ipv6,
                   default_icmp_ping_template,
                   sizeof(default_icmp_ping_template)-1,
                   data_link);
    templset->count++;

    /* [ICMP timestamp] */
    _template_init(&templset->pkts[Proto_ICMP_timestamp],
                   source_mac, router_mac_ipv4, router_mac_ipv6,
                   default_icmp_timestamp_template,
                   sizeof(default_icmp_timestamp_template)-1,
                   data_link);
    templset->count++;

    /* [ARP] */
    _template_init( &templset->pkts[Proto_ARP],
                    source_mac, router_mac_ipv4, router_mac_ipv6,
                    default_arp_template,
                    sizeof(default_arp_template)-1,
                    data_link);
    templset->count++;


    /* [NDP NS] */
    _template_init( &templset->pkts[Proto_NDP_ns],
                    source_mac, router_mac_ipv4, router_mac_ipv6,
                    default_ndp_ns_template,
                    sizeof(default_ndp_ns_template)-1,
                    data_link);
    templset->count++;
}

void template_set_tcp_syn_window_of_default(unsigned window)
{
    U16_TO_BE(default_tcp_syn_template+48, window);
}

void template_set_tcp_window_of_default(unsigned window)
{
    U16_TO_BE(default_tcp_template+48, window);
}

void
template_set_ttl(struct TemplateSet *tmplset, unsigned ttl)
{
    unsigned i;

    for (i=0; i<tmplset->count; i++) {
        struct TemplatePacket *tmpl = &tmplset->pkts[i];
        unsigned char *px           = tmpl->ipv4.packet;
        unsigned offset             = tmpl->ipv4.offset_ip;

        px[offset+8] = (unsigned char)(ttl);
        tmpl->ipv4.checksum_ip = checksum_ip_header(
            tmpl->ipv4.packet, tmpl->ipv4.offset_ip, tmpl->ipv4.length);
    }
}

void
template_packet_set_ttl(struct TemplatePacket *tmpl_pkt, unsigned ttl)
{
    unsigned char *px = tmpl_pkt->ipv4.packet;
    unsigned offset = tmpl_pkt->ipv4.offset_ip;

    px[offset+8] = (unsigned char)(ttl);
    tmpl_pkt->ipv4.checksum_ip = checksum_ip_header(tmpl_pkt->ipv4.packet,
        tmpl_pkt->ipv4.offset_ip, tmpl_pkt->ipv4.length);
}

void
template_set_vlan(struct TemplateSet *tmplset, unsigned vlan)
{
    unsigned i;
    
    for (i=0; i<tmplset->count; i++) {
        struct TemplatePacket *tmpl = &tmplset->pkts[i];
        unsigned char *px;

        if (tmpl->ipv4.length < 14)
            continue;
        
        px = MALLOC(tmpl->ipv4.length + 4);
        memcpy(px, tmpl->ipv4.packet, 12);
        memcpy(px+16, tmpl->ipv4.packet+12, tmpl->ipv4.length - 12);
        
        px[12] = 0x81;
        px[13] = 0x00;
        U16_TO_BE(px+14, vlan);
        
        tmpl->ipv4.packet = px;
        tmpl->ipv4.length += 4;
        
        tmpl->ipv4.offset_ip  += 4;
        tmpl->ipv4.offset_tcp += 4;
        tmpl->ipv4.offset_app += 4;
    }
}

void
template_packet_set_vlan(struct TemplatePacket *tmpl_pkt, unsigned vlan)
{
    unsigned char *px;

    if (tmpl_pkt->ipv4.length < 14)
        return;
    
    px = MALLOC(tmpl_pkt->ipv4.length + 4);
    memcpy(px, tmpl_pkt->ipv4.packet, 12);
    memcpy(px+16, tmpl_pkt->ipv4.packet+12, tmpl_pkt->ipv4.length - 12);
    
    px[12] = 0x81;
    px[13] = 0x00;
    U16_TO_BE(px+14, vlan);
    
    tmpl_pkt->ipv4.packet = px;
    tmpl_pkt->ipv4.length += 4;
    
    tmpl_pkt->ipv4.offset_ip  += 4;
    tmpl_pkt->ipv4.offset_tcp += 4;
    tmpl_pkt->ipv4.offset_app += 4;
}

unsigned
get_real_protocol_and_port(unsigned *port)
{
    if (*port < Templ_TCP + 65536)
        return Proto_TCP;
    else if (*port < Templ_UDP + 65536) {
        *port &= 0xFFFF;
        return Proto_UDP;
    } else if (*port < Templ_SCTP + 65536) {
        *port &= 0xFFFF;
        return Proto_SCTP;
    } else if (*port == Templ_ICMP_echo) {
        return Proto_ICMP_ping;
    } else if (*port == Templ_ICMP_timestamp) {
        return Proto_ICMP_timestamp;
    } else if (*port == Templ_ARP) {
        return Proto_ARP;
    } else {
        return 0;
    }

}