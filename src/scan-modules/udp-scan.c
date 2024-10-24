#include <stdlib.h>

#include "scan-modules.h"
#include "../xconf.h"
#include "../stub/stub-pcap-dlt.h"
#include "../target/target-cookie.h"
#include "../templ/templ-udp.h"
#include "../templ/templ-icmp.h"
#include "../util-data/safe-string.h"
#include "../util-data/fine-malloc.h"
#include "../util-out/logger.h"

extern Scanner UdpScan; /*for internal x-ref*/

struct UdpConf {
    unsigned record_ttl  : 1;
    unsigned record_ipid : 1;
};

static struct UdpConf udp_conf = {0};

static ConfRes SET_record_ttl(void *conf, const char *name, const char *value) {
    UNUSEDPARM(conf);
    UNUSEDPARM(name);

    udp_conf.record_ttl = parse_str_bool(value);

    return Conf_OK;
}

static ConfRes SET_record_ipid(void *conf, const char *name,
                               const char *value) {
    UNUSEDPARM(conf);
    UNUSEDPARM(name);

    udp_conf.record_ipid = parse_str_bool(value);

    return Conf_OK;
}

static ConfParam udp_parameters[] = {
    {"record-ttl",
     SET_record_ttl,
     Type_FLAG,
     {"ttl", 0},
     "Records TTL for IPv4 or Hop Limit for IPv6."},
    {"record-ipid",
     SET_record_ipid,
     Type_FLAG,
     {"ipid", 0},
     "Records IPID just for IPv4."},

    {0}};

/**
 *For calc the conn index.
 * NOTE: We use a trick of src-port to differenciate multi-probes to avoid
 * mutual interference of connections.
 * Be careful to the source port range and probe num. Source port range is 256
 *in default and can be set with flag `--source-port`.
 */
static unsigned src_port_start;

static bool udp_init(const XConf *xconf) {
    src_port_start = xconf->nic.src.port.first;

    return true;
}

static bool udp_transmit(uint64_t entropy, ScanTarget *target,
                         ScanTmEvent *event, unsigned char *px, size_t *len) {
    /*we just handle udp target*/
    if (target->target.ip_proto != IP_PROTO_UDP)
        return false;

    unsigned cookie = get_cookie(target->target.ip_them,
                                 target->target.port_them, target->target.ip_me,
                                 src_port_start + target->index, entropy);

    ProbeTarget ptarget = {
        .target.ip_proto  = target->target.ip_proto,
        .target.ip_them   = target->target.ip_them,
        .target.ip_me     = target->target.ip_me,
        .target.port_them = target->target.port_them,
        .target.port_me   = src_port_start + target->index,
        .cookie           = cookie,
        .index            = target->index,
    };

    unsigned char payload[PM_PAYLOAD_SIZE];
    size_t        payload_len = 0;

    payload_len = UdpScan.probe->make_payload_cb(&ptarget, payload);

    *len =
        udp_create_packet(target->target.ip_them, target->target.port_them,
                          target->target.ip_me, src_port_start + target->index,
                          0, payload, payload_len, px, PKT_BUF_SIZE);

    /*add timeout*/
    event->need_timeout   = 1;
    event->target.port_me = src_port_start + target->index;

    /*for multi-probe*/
    if (UdpScan.probe->multi_mode == Multi_Direct &&
        target->index + 1 < UdpScan.probe->multi_num)
        return true;
    else
        return false;
}

static void udp_validate(uint64_t entropy, Recved *recved, PreHandle *pre) {
    /*record packet to our source port*/
    if (recved->parsed.found == FOUND_UDP && recved->is_myip &&
        recved->is_myport) {
        pre->go_record = 1;

        ProbeTarget ptarget = {
            .target.ip_proto  = recved->parsed.ip_protocol,
            .target.ip_them   = recved->parsed.src_ip,
            .target.ip_me     = recved->parsed.dst_ip,
            .target.port_them = recved->parsed.port_src,
            .target.port_me   = recved->parsed.port_dst,
            .cookie = get_cookie(recved->parsed.src_ip, recved->parsed.port_src,
                                 recved->parsed.dst_ip, recved->parsed.port_dst,
                                 entropy),
            .index  = recved->parsed.port_dst - src_port_start,
        };

        if (UdpScan.probe->validate_response_cb(
                &ptarget, recved->packet + recved->parsed.app_offset,
                recved->parsed.app_length)) {
            pre->go_dedup = 1;
        }
    }
}

static void udp_handle(unsigned th_idx, uint64_t entropy, Recved *recved,
                       OutItem *item, STACK *stack, FHandler *handler) {
    if (recved->parsed.found == FOUND_UDP) {
        ProbeTarget ptarget = {
            .target.ip_proto  = recved->parsed.ip_protocol,
            .target.ip_them   = recved->parsed.src_ip,
            .target.ip_me     = recved->parsed.dst_ip,
            .target.port_them = recved->parsed.port_src,
            .target.port_me   = recved->parsed.port_dst,
            .cookie = get_cookie(recved->parsed.src_ip, recved->parsed.port_src,
                                 recved->parsed.dst_ip, recved->parsed.port_dst,
                                 entropy),
            .index  = recved->parsed.port_dst - src_port_start,
        };

        unsigned is_multi = UdpScan.probe->handle_response_cb(
            th_idx, &ptarget, &recved->packet[recved->parsed.app_offset],
            recved->parsed.app_length, item);

        if (udp_conf.record_ttl)
            dach_set_int(&item->report, "ttl", recved->parsed.ip_ttl);
        if (udp_conf.record_ipid && recved->parsed.src_ip.version == 4)
            dach_set_int(&item->report, "ipid", recved->parsed.ip_v4_id);

        /*for multi-probe Multi_AfterHandle*/
        if (UdpScan.probe->multi_mode == Multi_AfterHandle && is_multi &&
            recved->parsed.port_dst == src_port_start &&
            UdpScan.probe->multi_num) {
            for (unsigned idx = 1; idx < UdpScan.probe->multi_num; idx++) {
                PktBuf *pkt_buffer = stack_get_pktbuf(stack);

                ProbeTarget ptarget = {
                    .target.ip_proto  = recved->parsed.ip_protocol,
                    .target.ip_them   = recved->parsed.src_ip,
                    .target.ip_me     = recved->parsed.dst_ip,
                    .target.port_them = recved->parsed.port_src,
                    .target.port_me   = src_port_start + idx,
                    .cookie           = get_cookie(
                        recved->parsed.src_ip, recved->parsed.port_src,
                        recved->parsed.dst_ip, src_port_start + idx, entropy),
                    .index = idx,
                };

                unsigned char payload[PM_PAYLOAD_SIZE];
                size_t        payload_len = 0;

                payload_len = UdpScan.probe->make_payload_cb(&ptarget, payload);

                pkt_buffer->length = udp_create_packet(
                    recved->parsed.src_ip, recved->parsed.port_src,
                    recved->parsed.dst_ip, src_port_start + idx, 0, payload,
                    payload_len, pkt_buffer->px, PKT_BUF_SIZE);

                stack_transmit_pktbuf(stack, pkt_buffer);

                /*add timeout*/
                if (handler) {
                    ScanTmEvent *tm_event = CALLOC(1, sizeof(ScanTmEvent));

                    tm_event->target.ip_proto  = IP_PROTO_UDP;
                    tm_event->target.ip_them   = recved->parsed.src_ip;
                    tm_event->target.ip_me     = recved->parsed.dst_ip;
                    tm_event->target.port_them = recved->parsed.port_src;
                    tm_event->target.port_me   = src_port_start + idx;

                    tm_event->need_timeout = 1;
                    tm_event->dedup_type   = 0;

                    ft_add_event(handler, tm_event, global_now);
                    tm_event = NULL;
                }
            }

            return;
        }

        /*for multi-probe Multi_DynamicNext*/
        if (UdpScan.probe->multi_mode == Multi_DynamicNext && is_multi) {
            PktBuf *pkt_buffer = stack_get_pktbuf(stack);

            ProbeTarget ptarget = {
                .target.ip_proto  = recved->parsed.ip_protocol,
                .target.ip_them   = recved->parsed.src_ip,
                .target.ip_me     = recved->parsed.dst_ip,
                .target.port_them = recved->parsed.port_src,
                .target.port_me   = src_port_start + is_multi - 1,
                .cookie =
                    get_cookie(recved->parsed.src_ip, recved->parsed.port_src,
                               recved->parsed.dst_ip,
                               src_port_start + is_multi - 1, entropy),
                .index = is_multi - 1,
            };

            unsigned char payload[PM_PAYLOAD_SIZE];
            size_t        payload_len = 0;

            payload_len = UdpScan.probe->make_payload_cb(&ptarget, payload);

            pkt_buffer->length = udp_create_packet(
                recved->parsed.src_ip, recved->parsed.port_src,
                recved->parsed.dst_ip, src_port_start + is_multi - 1, 0,
                payload, payload_len, pkt_buffer->px, PKT_BUF_SIZE);

            stack_transmit_pktbuf(stack, pkt_buffer);

            /*add timeout*/
            if (handler) {
                ScanTmEvent *tm_event = CALLOC(1, sizeof(ScanTmEvent));

                tm_event->target.ip_proto  = IP_PROTO_UDP;
                tm_event->target.ip_them   = recved->parsed.src_ip;
                tm_event->target.ip_me     = recved->parsed.dst_ip;
                tm_event->target.port_them = recved->parsed.port_src;
                tm_event->target.port_me   = src_port_start + is_multi - 1;

                tm_event->need_timeout = 1;
                tm_event->dedup_type   = 0;

                ft_add_event(handler, tm_event, global_now);
                tm_event = NULL;
            }

            return;
        }
    } else {
        PreInfo info = {0};
        preprocess_frame(recved->packet + recved->parsed.app_offset,
                         recved->length - recved->parsed.app_offset,
                         PCAP_DLT_RAW, &info);

        item->level            = OUT_FAILURE;
        item->target.ip_them   = info.dst_ip;
        item->target.port_them = info.port_dst;
        item->target.ip_me     = info.src_ip;
        item->target.port_me   = info.port_src;

        safe_strcpy(item->classification, OUT_CLS_SIZE, "closed");
        safe_strcpy(item->reason, OUT_RSN_SIZE, "port unreachable");
    }
}

static void udp_timeout(uint64_t entropy, ScanTmEvent *event, OutItem *item,
                        STACK *stack, FHandler *handler) {
    /*all events is for banner*/

    ProbeTarget ptarget = {
        .target.ip_proto  = event->target.ip_proto,
        .target.ip_them   = event->target.ip_them,
        .target.ip_me     = event->target.ip_me,
        .target.port_them = event->target.port_them,
        .target.port_me   = event->target.port_me,
        .cookie =
            get_cookie(event->target.ip_them, event->target.port_them,
                       event->target.ip_me, event->target.port_me, entropy),
        .index = event->target.port_me - src_port_start,
    };

    unsigned is_multi = UdpScan.probe->handle_timeout_cb(&ptarget, item);

    /*for multi-probe Multi_AfterHandle*/
    if (UdpScan.probe->multi_mode == Multi_AfterHandle && is_multi &&
        event->target.port_me == src_port_start && UdpScan.probe->multi_num) {
        for (unsigned idx = 1; idx < UdpScan.probe->multi_num; idx++) {
            PktBuf *pkt_buffer = stack_get_pktbuf(stack);

            ProbeTarget ptarget = {
                .target.ip_proto  = event->target.ip_proto,
                .target.ip_them   = event->target.ip_them,
                .target.ip_me     = event->target.ip_me,
                .target.port_them = event->target.port_them,
                .target.port_me   = src_port_start + idx,
                .cookie           = get_cookie(
                    event->target.ip_them, event->target.port_them,
                    event->target.ip_me, src_port_start + idx, entropy),
                .index = idx,
            };

            unsigned char payload[PM_PAYLOAD_SIZE];
            size_t        payload_len = 0;

            payload_len = UdpScan.probe->make_payload_cb(&ptarget, payload);

            pkt_buffer->length = udp_create_packet(
                event->target.ip_them, event->target.port_them,
                event->target.ip_me, src_port_start + idx, 0, payload,
                payload_len, pkt_buffer->px, PKT_BUF_SIZE);

            stack_transmit_pktbuf(stack, pkt_buffer);

            /*add timeout*/
            if (handler) {
                ScanTmEvent *tm_event = CALLOC(1, sizeof(ScanTmEvent));

                tm_event->target.ip_proto  = IP_PROTO_UDP;
                tm_event->target.ip_them   = event->target.ip_them;
                tm_event->target.ip_me     = event->target.ip_me;
                tm_event->target.port_them = event->target.port_them;
                tm_event->target.port_me   = src_port_start + idx;

                tm_event->need_timeout = 1;
                tm_event->dedup_type   = 0;

                ft_add_event(handler, tm_event, global_now);
                tm_event = NULL;
            }
        }

        return;
    }

    /*for multi-probe Multi_DynamicNext*/
    if (UdpScan.probe->multi_mode == Multi_DynamicNext && is_multi) {
        PktBuf *pkt_buffer = stack_get_pktbuf(stack);

        ProbeTarget ptarget = {
            .target.ip_proto  = event->target.ip_proto,
            .target.ip_them   = event->target.ip_them,
            .target.ip_me     = event->target.ip_me,
            .target.port_them = event->target.port_them,
            .target.port_me   = src_port_start + is_multi - 1,
            .cookie = get_cookie(event->target.ip_them, event->target.port_them,
                                 event->target.ip_me,
                                 src_port_start + is_multi - 1, entropy),
            .index  = is_multi - 1,
        };

        unsigned char payload[PM_PAYLOAD_SIZE];
        size_t        payload_len = 0;

        payload_len = UdpScan.probe->make_payload_cb(&ptarget, payload);

        pkt_buffer->length = udp_create_packet(
            event->target.ip_them, event->target.port_them, event->target.ip_me,
            src_port_start + is_multi - 1, 0, payload, payload_len,
            pkt_buffer->px, PKT_BUF_SIZE);

        stack_transmit_pktbuf(stack, pkt_buffer);

        /*add timeout*/
        if (handler) {
            ScanTmEvent *tm_event = CALLOC(1, sizeof(ScanTmEvent));

            tm_event->target.ip_proto  = IP_PROTO_UDP;
            tm_event->target.ip_them   = event->target.ip_them;
            tm_event->target.ip_me     = event->target.ip_me;
            tm_event->target.port_them = event->target.port_them;
            tm_event->target.port_me   = src_port_start + is_multi - 1;

            tm_event->need_timeout = 1;
            tm_event->dedup_type   = 0;

            ft_add_event(handler, tm_event, global_now);
            tm_event = NULL;
        }

        return;
    }
}

Scanner UdpScan = {
    .name                = "udp",
    .required_probe_type = ProbeType_UDP,
    .support_timeout     = 1,
    .params              = udp_parameters,
    /*udp and icmp port unreachable in ipv4 & ipv6*/
    .bpf_filter          = "udp",
    .short_desc          = "Single-packet UDP scan with specified ProbeModule.",
    .desc =
        "UdpScan sends a udp packet with ProbeModule data to target port "
        "and expects a udp response to believe the port is open or an icmp "
        "port "
        "unreachable message if closed. Responsed data will be processed and "
        "formed a report by ProbeModule.\n"
        "UdpScan prefer the first reponse udp packet. But all packets to us "
        "could be record to pcap file.\n"
        "NOTE: Our host may send an ICMP Port Unreachable message to target "
        "after"
        " received udp response because we send udp packets bypassing the "
        "protocol"
        " stack of OS. Sometimes it can cause problems or needless "
        "retransmission"
        " from server side. We could add iptables rules displayed in "
        "`firewall` "
        "directory to ban this. Or we could observe some strange things.",

    .init_cb     = &udp_init,
    .transmit_cb = &udp_transmit,
    .validate_cb = &udp_validate,
    .handle_cb   = &udp_handle,
    .timeout_cb  = &udp_timeout,
    .poll_cb     = &scan_poll_nothing,
    .close_cb    = &scan_close_nothing,
    .status_cb   = &scan_no_status,
};