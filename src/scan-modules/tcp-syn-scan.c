#include <stdlib.h>

#include "scan-modules.h"
#include "../target/target-cookie.h"
#include "../templ/templ-tcp.h"
#include "../util-data/safe-string.h"
#include "../util-data/fine-malloc.h"

extern Scanner TcpSynScan; /*for internal x-ref*/

struct TcpSynConf {
    unsigned send_rst    : 1;
    unsigned zero_fail   : 1;
    unsigned record_ttl  : 1;
    unsigned record_ipid : 1;
    unsigned record_win  : 1;
    unsigned record_mss  : 1;
};

static struct TcpSynConf tcpsyn_conf = {0};

static ConfRes SET_record_mss(void *conf, const char *name, const char *value) {
    UNUSEDPARM(conf);
    UNUSEDPARM(name);

    tcpsyn_conf.record_mss = parse_str_bool(value);

    return Conf_OK;
}

static ConfRes SET_record_ttl(void *conf, const char *name, const char *value) {
    UNUSEDPARM(conf);
    UNUSEDPARM(name);

    tcpsyn_conf.record_ttl = parse_str_bool(value);

    return Conf_OK;
}

static ConfRes SET_record_ipid(void *conf, const char *name,
                               const char *value) {
    UNUSEDPARM(conf);
    UNUSEDPARM(name);

    tcpsyn_conf.record_ipid = parse_str_bool(value);

    return Conf_OK;
}

static ConfRes SET_record_win(void *conf, const char *name, const char *value) {
    UNUSEDPARM(conf);
    UNUSEDPARM(name);

    tcpsyn_conf.record_win = parse_str_bool(value);

    return Conf_OK;
}

static ConfRes SET_zero_fail(void *conf, const char *name, const char *value) {
    UNUSEDPARM(conf);
    UNUSEDPARM(name);

    tcpsyn_conf.zero_fail = parse_str_bool(value);

    return Conf_OK;
}

static ConfRes SET_send_rst(void *conf, const char *name, const char *value) {
    UNUSEDPARM(conf);
    UNUSEDPARM(name);

    tcpsyn_conf.send_rst = parse_str_bool(value);

    return Conf_OK;
}

static ConfParam tcpsyn_parameters[] = {
    {"send-rst",
     SET_send_rst,
     Type_FLAG,
     {"rst", 0},
     "Actively send an RST if got a SYN-ACK. This is useful when we are in "
     "bypassing mode or working on Windows and don't want to waste connection"
     " resources of targets."},
    {"fail-zerowin",
     SET_zero_fail,
     Type_FLAG,
     {"fail-zero", "zero-fail", 0},
     "Let SYN-ACK responds with zero window setting as failed. Default is "
     "success."},
    {"record-ttl",
     SET_record_ttl,
     Type_FLAG,
     {"ttl", 0},
     "Records TTL for IPv4 or Hop Limit for IPv6 in SYN-ACK or RST."},
    {"record-ipid",
     SET_record_ipid,
     Type_FLAG,
     {"ipid", 0},
     "Records IPID of SYN-ACK or RST just for IPv4."},
    {"record-win",
     SET_record_win,
     Type_FLAG,
     {"win", "window", 0},
     "Records TCP window size of SYN-ACK or RST."},
    {"record-mss",
     SET_record_mss,
     Type_FLAG,
     {"mss", 0},
     "Records TCP MSS option value of SYN-ACK if the option exists."},

    {0}};

static bool tcpsyn_transmit(uint64_t entropy, ScanTarget *target,
                            ScanTmEvent *event, unsigned char *px,
                            size_t *len) {
    /*we just handle tcp target*/
    if (target->target.ip_proto != IP_PROTO_TCP)
        return false;

    unsigned cookie =
        get_cookie(target->target.ip_them, target->target.port_them,
                   target->target.ip_me, target->target.port_me, entropy);

    *len =
        tcp_create_packet(target->target.ip_them, target->target.port_them,
                          target->target.ip_me, target->target.port_me, cookie,
                          0, TCP_FLAG_SYN, 0, 0, NULL, 0, px, PKT_BUF_SIZE);

    /*add timeout*/
    event->need_timeout = 1;

    return false;
}

static void tcpsyn_validate(uint64_t entropy, Recved *recved, PreHandle *pre) {
    /*record tcp packet to our source port*/
    if (recved->parsed.found == FOUND_TCP && recved->is_myip &&
        recved->is_myport)
        pre->go_record = 1;
    else
        return;

    ipaddress ip_them   = recved->parsed.src_ip;
    ipaddress ip_me     = recved->parsed.dst_ip;
    unsigned  port_them = recved->parsed.port_src;
    unsigned  port_me   = recved->parsed.port_dst;
    unsigned  seqno_me =
        TCP_ACKNO(recved->packet, recved->parsed.transport_offset);
    unsigned cookie = get_cookie(ip_them, port_them, ip_me, port_me, entropy);

    /*SYNACK*/
    if (TCP_HAS_FLAG(recved->packet, recved->parsed.transport_offset,
                     TCP_FLAG_SYN | TCP_FLAG_ACK)) {
        if (cookie == seqno_me - 1) {
            pre->go_dedup = 1;
        }
    }
    /*RST*/
    else if (TCP_HAS_FLAG(recved->packet, recved->parsed.transport_offset,
                          TCP_FLAG_RST)) {
        /*NOTE: diff from SYNACK*/
        if (cookie == seqno_me - 1 || cookie == seqno_me) {
            pre->go_dedup = 1;
        }
    }
}

static void tcpsyn_handle(unsigned th_idx, uint64_t entropy, Recved *recved,
                          OutItem *item, STACK *stack, FHandler *handler) {
    unsigned mss_them;
    bool     mss_found;
    uint16_t win_them;

    win_them = TCP_WIN(recved->packet, recved->parsed.transport_offset);

    /*SYNACK*/
    if (TCP_HAS_FLAG(recved->packet, recved->parsed.transport_offset,
                     TCP_FLAG_SYN | TCP_FLAG_ACK)) {
        item->level = OUT_SUCCESS;

        if (win_them == 0) {
            safe_strcpy(item->classification, OUT_CLS_SIZE, "fake-open");
            safe_strcpy(item->reason, OUT_RSN_SIZE, "zerowin");

            if (tcpsyn_conf.zero_fail)
                item->level = OUT_FAILURE;
        } else {
            safe_strcpy(item->classification, OUT_CLS_SIZE, "open");
            safe_strcpy(item->reason, OUT_RSN_SIZE, "syn-ack");
        }

        if (tcpsyn_conf.send_rst) {
            unsigned seqno_me =
                TCP_ACKNO(recved->packet, recved->parsed.transport_offset);
            unsigned seqno_them =
                TCP_SEQNO(recved->packet, recved->parsed.transport_offset);

            PktBuf *pkt_buffer = stack_get_pktbuf(stack);

            pkt_buffer->length = tcp_create_packet(
                recved->parsed.src_ip, recved->parsed.port_src,
                recved->parsed.dst_ip, recved->parsed.port_dst, seqno_me,
                seqno_them + 1, TCP_FLAG_RST, 0, 0, NULL, 0, pkt_buffer->px,
                PKT_BUF_SIZE);

            stack_transmit_pktbuf(stack, pkt_buffer);
        }

        if (tcpsyn_conf.record_mss) {
            /*comput of mss is not easy*/
            mss_them = tcp_get_mss(recved->packet, recved->length, &mss_found);
            if (mss_found)
                dach_set_int(&item->report, "mss", mss_them);
        }
    }
    /*RST*/
    else {
        item->level = OUT_FAILURE;
        safe_strcpy(item->reason, OUT_RSN_SIZE, "rst");
        safe_strcpy(item->classification, OUT_CLS_SIZE, "closed");
    }

    if (tcpsyn_conf.record_ttl)
        dach_set_int(&item->report, "ttl", recved->parsed.ip_ttl);
    if (tcpsyn_conf.record_ipid && recved->parsed.src_ip.version == 4)
        dach_set_int(&item->report, "ipid", recved->parsed.ip_v4_id);
    if (tcpsyn_conf.record_win)
        dach_set_int(&item->report, "win", win_them);
}

static void tcpsyn_timeout(uint64_t entropy, ScanTmEvent *event, OutItem *item,
                           STACK *stack, FHandler *handler) {
    item->level = OUT_FAILURE;
    safe_strcpy(item->classification, OUT_CLS_SIZE, "closed");
    safe_strcpy(item->reason, OUT_RSN_SIZE, "timeout");
}

Scanner TcpSynScan = {
    .name                = "tcp-syn",
    .required_probe_type = 0,
    .support_timeout     = 1,
    .params              = tcpsyn_parameters,
    /* is syn-ack or rst in ipv4 & ipv6*/
    .bpf_filter          = "(ip && tcp && (tcp[tcpflags]==(tcp-syn|tcp-ack) || "
                           "tcp[tcpflags]==tcp-rst)) "
                           "|| (ip6 && tcp && (ip6[40+13]==(tcp-syn|tcp-ack) || "
                           "ip6[40+13]==tcp-rst))",
    .short_desc          = "Default ScanModule for TCP SYN scan.",
    .desc = "TcpSynScan sends a TCP SYN packet to target port. Expect a SYNACK "
            "response to believe the port is open or an RST for closed in TCP "
            "protocol.\n"
            "TcpSynScan is the default ScanModule.",

    .init_cb     = &scan_init_nothing,
    .transmit_cb = &tcpsyn_transmit,
    .validate_cb = &tcpsyn_validate,
    .handle_cb   = &tcpsyn_handle,
    .timeout_cb  = &tcpsyn_timeout,
    .poll_cb     = &scan_poll_nothing,
    .close_cb    = &scan_close_nothing,
    .status_cb   = &scan_no_status,
};