#include <stdlib.h>

#include "scan-modules.h"
#include "../xconf.h"
#include "../massip/massip-cookie.h"
#include "../templ/templ-tcp.h"
#include "../util-data/safe-string.h"
#include "../util-data/fine-malloc.h"
#include "../util-out/logger.h"

extern struct ScanModule ZBannerScan; /*for internal x-ref*/

struct ZBannerConf {
    unsigned no_banner_timeout:1;     /*--no-banner-tm*/
    unsigned is_port_timeout:1;       /*--port-tm*/
    unsigned is_port_success:1;       /*--port-success*/
    unsigned is_port_failure:1;       /*--port-fail*/
    unsigned record_ttl:1;
    unsigned record_ipid:1;
    unsigned record_win:1;
    unsigned record_mss:1;
};

static struct ZBannerConf zbanner_conf = {0};

static enum ConfigRes SET_record_mss(void *conf, const char *name, const char *value)
{
    UNUSEDPARM(conf);
    UNUSEDPARM(name);

    zbanner_conf.record_mss = parseBoolean(value);

    return Conf_OK;
}

static enum ConfigRes SET_record_ttl(void *conf, const char *name, const char *value)
{
    UNUSEDPARM(conf);
    UNUSEDPARM(name);

    zbanner_conf.record_ttl = parseBoolean(value);

    return Conf_OK;
}

static enum ConfigRes SET_record_ipid(void *conf, const char *name, const char *value)
{
    UNUSEDPARM(conf);
    UNUSEDPARM(name);

    zbanner_conf.record_ipid = parseBoolean(value);

    return Conf_OK;
}

static enum ConfigRes SET_record_win(void *conf, const char *name, const char *value)
{
    UNUSEDPARM(conf);
    UNUSEDPARM(name);

    zbanner_conf.record_win = parseBoolean(value);

    return Conf_OK;
}

static enum ConfigRes SET_banner_timeout(void *conf, const char *name, const char *value)
{
    UNUSEDPARM(conf);
    UNUSEDPARM(name);

    zbanner_conf.no_banner_timeout = parseBoolean(value);

    return Conf_OK;
}

static enum ConfigRes SET_port_timeout(void *conf, const char *name, const char *value)
{
    UNUSEDPARM(conf);
    UNUSEDPARM(name);

    zbanner_conf.is_port_timeout = parseBoolean(value);

    return Conf_OK;
}

static enum ConfigRes SET_port_success(void *conf, const char *name, const char *value)
{
    UNUSEDPARM(conf);
    UNUSEDPARM(name);

    zbanner_conf.is_port_success = parseBoolean(value);

    return Conf_OK;
}

static enum ConfigRes SET_port_failure(void *conf, const char *name, const char *value)
{
    UNUSEDPARM(conf);
    UNUSEDPARM(name);

    zbanner_conf.is_port_failure = parseBoolean(value);

    return Conf_OK;
}

static struct ConfigParam zbanner_parameters[] = {
    {
        "no-banner-timeout",
        SET_banner_timeout,
        Type_BOOL,
        {"no-banner-tm", "no-timeout-banner","no-tm-banner", 0},
        "Do not use timeout for banner grabbing while in timeout mode."
    },
    {
        "port-timeout",
        SET_port_timeout,
        Type_BOOL,
        {"timeout-port", "port-tm", "tm-port", 0},
        "Use timeout for port scanning(openness detection) while in timeout mode."
    },
    {
        "port-success",
        SET_port_success,
        Type_BOOL,
        {"success-port", 0},
        "Let port opening(contains zero syn-ack) results as success level."
        "(Default is info level)"
    },
    {
        "port-failure",
        SET_port_failure,
        Type_BOOL,
        {"failure-port", "port-fail", "fail-port", 0},
        "Let port closed results as failure level.(Default is info level)"
    },
    {
        "record-ttl",
        SET_record_ttl,
        Type_BOOL,
        {"ttl", 0},
        "Records TTL for IPv4 or Hop Limit for IPv6 in SYN-ACK or RST."
    },
    {
        "record-ipid",
        SET_record_ipid,
        Type_BOOL,
        {"ipid", 0},
        "Records IPID of SYN-ACK or RST just for IPv4."
    },
    {
        "record-win",
        SET_record_win,
        Type_BOOL,
        {"win", "window", 0},
        "Records TCP window size of SYN-ACK or RST."
    },
    {
        "record-mss",
        SET_record_mss,
        Type_BOOL,
        {"mss", 0},
        "Records TCP MSS option value of SYN-ACK if the option exists."
    },

    {0}
};

/**
 *For calc the conn index.
 * NOTE: We use a trick of src-port to differenciate multi-probes to avoid
 * mutual interference of connections.
 * Be careful to the source port range and probe num. Source port range is 256 in
 * default and can be set with flag `--source-port`.
*/
static unsigned src_port_start;

static bool
zbanner_global_init(const struct Xconf *xconf)
{
    src_port_start = xconf->nic.src.port.first;

    return true;
}

static bool
zbanner_transmit(
    uint64_t entropy,
    struct ScanTarget *target,
    struct ScanTmEvent *event,
    unsigned char *px, size_t *len)
{
    /*we just handle tcp target*/
    if (target->ip_proto != IP_PROTO_TCP)
        return false;

    unsigned seqno = get_cookie(target->ip_them, target->port_them, target->ip_me,
        src_port_start+target->index, entropy);

    *len = tcp_create_packet(
        target->ip_them, target->port_them, target->ip_me, src_port_start+target->index,
        seqno, 0, TCP_FLAG_SYN, NULL, 0, px, PKT_BUF_LEN);

    if (zbanner_conf.is_port_timeout) {
        event->need_timeout = 1;
        event->dedup_type   = 0;
        event->port_me      = src_port_start+target->index;
    }

    /*multi-probe Multi_Direct*/
    if (ZBannerScan.probe->multi_mode==Multi_Direct
        && target->index+1<ZBannerScan.probe->multi_num)
        return true;

    return false;
}

static void
zbanner_validate(
    uint64_t entropy,
    struct Received *recved,
    struct PreHandle *pre)
{
    if (recved->parsed.found == FOUND_TCP
        && recved->is_myip
        && recved->is_myport)
        pre->go_record = 1;
    else return;

    unsigned seqno_me  = TCP_ACKNO(recved->packet, recved->parsed.transport_offset);
    unsigned cookie    = get_cookie(recved->parsed.src_ip, recved->parsed.port_src,
            recved->parsed.dst_ip, recved->parsed.port_dst, entropy);


    /*syn-ack*/
    if (TCP_HAS_FLAG(recved->packet, recved->parsed.transport_offset,
        TCP_FLAG_SYN|TCP_FLAG_ACK)) {
        if (cookie == seqno_me - 1) {
            pre->go_dedup   = 1;
            pre->dedup_type = 0;
        }
    }
    /*
    * First packet with reponsed banner data.

    * We could recv Response DATA with some combinations of TCP flags
    * 1.[ACK]: maybe more data
    * 2.[PSH, ACK]: no more data
    * 3.[FIN, PSH, ACK]: no more data and disconnecting
    */
    else if (TCP_HAS_FLAG(recved->packet, recved->parsed.transport_offset, TCP_FLAG_ACK)
        && recved->parsed.app_length) {

        struct ProbeTarget ptarget = {
            .ip_proto  = recved->parsed.ip_protocol,
            .ip_them   = recved->parsed.src_ip,
            .ip_me     = recved->parsed.dst_ip,
            .port_them = recved->parsed.port_src,
            .port_me   = recved->parsed.port_dst,
            .cookie    = 0, /*zbanner can recognize reponse by itself*/
            .index     = recved->parsed.port_dst-src_port_start,
        };

        size_t payload_len;
        payload_len = ZBannerScan.probe->get_payload_length_cb(&ptarget);

        if (seqno_me == cookie + payload_len + 1) {
            pre->go_dedup   = 1;
            pre->dedup_type = 1;
        }
    }
    /*rst for syn (a little different)*/
    else if (TCP_HAS_FLAG(recved->packet, recved->parsed.transport_offset,
        TCP_FLAG_RST)) {
        if (seqno_me == cookie + 1 || seqno_me == cookie) {
            pre->go_dedup   = 1;
            pre->dedup_type = 0;
        }
    }
}

static void
zbanner_handle(
    unsigned th_idx,
    uint64_t entropy,
    struct Received *recved,
    struct OutputItem *item,
    struct stack_t *stack,
    struct FHandler *handler)
{
    unsigned mss_them;
    bool     mss_found;
    uint16_t win_them;

    unsigned seqno_me   = TCP_ACKNO(recved->packet, recved->parsed.transport_offset);
    unsigned seqno_them = TCP_SEQNO(recved->packet, recved->parsed.transport_offset);

    /*SYNACK*/
    if (TCP_HAS_FLAG(recved->packet, recved->parsed.transport_offset,
        TCP_FLAG_SYN|TCP_FLAG_ACK)) {

        /*zerowin could be a kind of port open*/
        if (zbanner_conf.is_port_success) {
            item->level = Output_SUCCESS;
        }

        win_them = TCP_WIN(recved->packet, recved->parsed.transport_offset);

        if (zbanner_conf.record_ttl)
            dach_printf(&item->report, "ttl", true, "%d", recved->parsed.ip_ttl);
        if (zbanner_conf.record_ipid && recved->parsed.src_ip.version==4)
            dach_printf(&item->report, "ipid", true, "%d", recved->parsed.ip_v4_id);
        if (zbanner_conf.record_win)
            dach_printf(&item->report, "win", true, "%d", win_them);
        if (zbanner_conf.record_mss) {
            /*comput of mss is not easy*/
            mss_them = tcp_get_mss(recved->packet, recved->length, &mss_found);
            if (!mss_found) mss_them = 0;
            dach_printf(&item->report, "mss", true, "%d", mss_them);
        }

        if (win_them == 0) {
            safe_strcpy(item->classification, OUTPUT_CLS_SIZE, "fake-open");
            safe_strcpy(item->reason, OUTPUT_RSN_SIZE, "zerowin");
        } else {
            safe_strcpy(item->classification, OUTPUT_CLS_SIZE, "open");
            safe_strcpy(item->reason, OUTPUT_RSN_SIZE, "syn-ack");

            /*stack(send) ack with probe*/
            struct ProbeTarget ptarget = {
                .ip_proto  = recved->parsed.ip_protocol,
                .ip_them   = recved->parsed.src_ip,
                .ip_me     = recved->parsed.dst_ip,
                .port_them = recved->parsed.port_src,
                .port_me   = recved->parsed.port_dst,
                .cookie    = 0, /*zbanner can recognize reponse by itself*/
                .index     = recved->parsed.port_dst-src_port_start,
            };

            unsigned char payload[PROBE_PAYLOAD_MAX_LEN];
            size_t payload_len = 0; 

            payload_len = ZBannerScan.probe->make_payload_cb(&ptarget, payload);

            struct PacketBuffer *pkt_buffer = stack_get_packetbuffer(stack);

            pkt_buffer->length = tcp_create_packet(
                recved->parsed.src_ip, recved->parsed.port_src,
                recved->parsed.dst_ip, recved->parsed.port_dst,
                seqno_me, seqno_them+1, TCP_FLAG_ACK,
                payload, payload_len, pkt_buffer->px, PKT_BUF_LEN);

            stack_transmit_packetbuffer(stack, pkt_buffer);

            /*add timeout for banner*/
            if (handler && !zbanner_conf.no_banner_timeout) {
                struct ScanTmEvent *tm_event =
                    CALLOC(1, sizeof(struct ScanTmEvent));

                tm_event->ip_proto  = IP_PROTO_TCP;
                tm_event->ip_them   = recved->parsed.src_ip;
                tm_event->ip_me     = recved->parsed.dst_ip;
                tm_event->port_them = recved->parsed.port_src;
                tm_event->port_me   = recved->parsed.port_dst;

                tm_event->need_timeout = 1;
                tm_event->dedup_type   = 1; /*1 for banner*/

                ft_add_event(handler, tm_event, global_now);
                tm_event = NULL;
            }

            /*multi-probe Multi_IfOpen*/
            if (ZBannerScan.probe->multi_mode==Multi_IfOpen
                && recved->parsed.port_dst==src_port_start) {

                for (unsigned idx=1; idx<ZBannerScan.probe->multi_num; idx++) {

                    unsigned cookie = get_cookie(recved->parsed.src_ip, recved->parsed.port_src,
                        recved->parsed.dst_ip, src_port_start+idx, entropy);

                    struct PacketBuffer *pkt_buffer = stack_get_packetbuffer(stack);

                    pkt_buffer->length = tcp_create_packet(
                        recved->parsed.src_ip, recved->parsed.port_src,
                        recved->parsed.dst_ip, src_port_start+idx,
                        cookie, 0, TCP_FLAG_SYN,
                        NULL, 0, pkt_buffer->px, PKT_BUF_LEN);

                    stack_transmit_packetbuffer(stack, pkt_buffer);

                    /*add timeout for port*/
                    if (handler && zbanner_conf.is_port_timeout) {
                        struct ScanTmEvent *tm_event =
                            CALLOC(1, sizeof(struct ScanTmEvent));

                        tm_event->ip_proto  = IP_PROTO_TCP;
                        tm_event->ip_them   = recved->parsed.src_ip;
                        tm_event->ip_me     = recved->parsed.dst_ip;
                        tm_event->port_them = recved->parsed.port_src;
                        tm_event->port_me   = src_port_start+idx;

                        tm_event->need_timeout = 1;
                        tm_event->dedup_type   = 0; /*0 for port*/

                        ft_add_event(handler, tm_event, global_now);
                        tm_event = NULL;
                    }
                }
            }
        }
    }
    /*RST*/
    else if (TCP_HAS_FLAG(recved->packet, recved->parsed.transport_offset,
        TCP_FLAG_RST)) {

        win_them = TCP_WIN(recved->packet, recved->parsed.transport_offset);

        if (zbanner_conf.record_ttl)
            dach_printf(&item->report, "ttl", true, "%d", recved->parsed.ip_ttl);
        if (zbanner_conf.record_ipid && recved->parsed.src_ip.version==4)
            dach_printf(&item->report, "ipid", true, "%d", recved->parsed.ip_v4_id);
        if (zbanner_conf.record_win)
            dach_printf(&item->report, "win", true, "%d", win_them);

        safe_strcpy(item->reason, OUTPUT_RSN_SIZE, "rst");
        safe_strcpy(item->classification, OUTPUT_CLS_SIZE, "closed");

        if (zbanner_conf.is_port_failure) {
            item->level = Output_FAILURE;
        }
    }
    /*Banner*/
    else {

        /*send rst first to disconn*/
        struct PacketBuffer *pkt_buffer = stack_get_packetbuffer(stack);

        pkt_buffer->length = tcp_create_packet(
            recved->parsed.src_ip, recved->parsed.port_src,
            recved->parsed.dst_ip, recved->parsed.port_dst,
            seqno_me, seqno_them+1, TCP_FLAG_RST,
            NULL, 0, pkt_buffer->px, PKT_BUF_LEN);

        stack_transmit_packetbuffer(stack, pkt_buffer);

        struct ProbeTarget ptarget = {
            .ip_proto  = recved->parsed.ip_protocol,
            .ip_them   = recved->parsed.src_ip,
            .ip_me     = recved->parsed.dst_ip,
            .port_them = recved->parsed.port_src,
            .port_me   = recved->parsed.port_dst,
            .cookie    = 0, /*zbanner can recognize reponse by itself*/
            .index     = recved->parsed.port_dst-src_port_start,
        };

        unsigned is_multi = ZBannerScan.probe->handle_response_cb(
            th_idx,
            &ptarget,
            &recved->packet[recved->parsed.app_offset],
            recved->parsed.app_length, item);

        /*multi-probe Multi_AfterHandle*/
        if (ZBannerScan.probe->multi_mode==Multi_AfterHandle
            && is_multi && recved->parsed.port_dst==src_port_start) {
            for (unsigned idx=1; idx<ZBannerScan.probe->multi_num; idx++) {

                unsigned cookie = get_cookie(recved->parsed.src_ip, recved->parsed.port_src,
                    recved->parsed.dst_ip, src_port_start+idx, entropy);

                struct PacketBuffer *pkt_buffer = stack_get_packetbuffer(stack);

                pkt_buffer->length = tcp_create_packet(
                    recved->parsed.src_ip, recved->parsed.port_src,
                    recved->parsed.dst_ip, src_port_start+idx,
                    cookie, 0, TCP_FLAG_SYN,
                    NULL, 0, pkt_buffer->px, PKT_BUF_LEN);

                stack_transmit_packetbuffer(stack, pkt_buffer);

                /*add timeout for port*/
                if (handler && zbanner_conf.is_port_timeout) {
                    struct ScanTmEvent *tm_event =
                        CALLOC(1, sizeof(struct ScanTmEvent));

                    tm_event->ip_proto  = IP_PROTO_TCP;
                    tm_event->ip_them   = recved->parsed.src_ip;
                    tm_event->ip_me     = recved->parsed.dst_ip;
                    tm_event->port_them = recved->parsed.port_src;
                    tm_event->port_me   = src_port_start+idx;

                    tm_event->need_timeout = 1;
                    tm_event->dedup_type   = 0; /*0 for port*/

                    ft_add_event(handler, tm_event, global_now);
                    tm_event = NULL;
                }
            }

            return;
        }

        /*multi-probe Multi_DynamicNext*/
        if (ZBannerScan.probe->multi_mode==Multi_DynamicNext && is_multi) {
            unsigned cookie = get_cookie(recved->parsed.src_ip, recved->parsed.port_src,
                recved->parsed.dst_ip, src_port_start+is_multi-1, entropy);

            struct PacketBuffer *pkt_buffer = stack_get_packetbuffer(stack);

            pkt_buffer->length = tcp_create_packet(
                recved->parsed.src_ip, recved->parsed.port_src,
                recved->parsed.dst_ip, src_port_start+is_multi-1,
                cookie, 0, TCP_FLAG_SYN,
                NULL, 0, pkt_buffer->px, PKT_BUF_LEN);

            stack_transmit_packetbuffer(stack, pkt_buffer);

            /*add timeout for port*/
            if (handler && zbanner_conf.is_port_timeout) {
                struct ScanTmEvent *tm_event =
                    CALLOC(1, sizeof(struct ScanTmEvent));

                tm_event->ip_proto  = IP_PROTO_TCP;
                tm_event->ip_them   = recved->parsed.src_ip;
                tm_event->ip_me     = recved->parsed.dst_ip;
                tm_event->port_them = recved->parsed.port_src;
                tm_event->port_me   = src_port_start+is_multi-1;

                tm_event->need_timeout = 1;
                tm_event->dedup_type   = 0; /*0 for port*/

                ft_add_event(handler, tm_event, global_now);
                tm_event = NULL;
            }
        }
    }
}

static void
zbanner_timeout(
    uint64_t entropy,
    struct ScanTmEvent *event,
    struct OutputItem *item,
    struct stack_t *stack,
    struct FHandler *handler)
{
    /*event for port*/
    if (event->dedup_type==0) {
        safe_strcpy(item->reason, OUTPUT_RSN_SIZE, "timeout");
        safe_strcpy(item->classification, OUTPUT_CLS_SIZE, "closed");
        if (zbanner_conf.is_port_failure) {
            item->level = Output_FAILURE;
        }
        return;
    }

    /*event for banner*/

    struct ProbeTarget ptarget = {
        .ip_proto  = event->ip_proto,
        .ip_them   = event->ip_them,
        .ip_me     = event->ip_me,
        .port_them = event->port_them,
        .port_me   = event->port_me,
        .cookie    = 0, /*zbanner can recognize reponse by itself*/
        .index     = event->port_me-src_port_start,
    };

    unsigned is_multi = ZBannerScan.probe->handle_timeout_cb(&ptarget, item);

    /*multi-probe Multi_AfterHandle*/
    if (ZBannerScan.probe->multi_mode==Multi_AfterHandle
        && is_multi && event->port_me==src_port_start) {
        for (unsigned idx=1; idx<ZBannerScan.probe->multi_num; idx++) {

            unsigned cookie = get_cookie(event->ip_them, event->port_them,
                event->ip_me, src_port_start+idx, entropy);

            struct PacketBuffer *pkt_buffer = stack_get_packetbuffer(stack);

            pkt_buffer->length = tcp_create_packet(
                event->ip_them, event->port_them,
                event->ip_me,   src_port_start+idx,
                cookie, 0, TCP_FLAG_SYN,
                NULL, 0, pkt_buffer->px, PKT_BUF_LEN);

            stack_transmit_packetbuffer(stack, pkt_buffer);

            /*add timeout for port*/
            if (handler && zbanner_conf.is_port_timeout) {
                struct ScanTmEvent *tm_event =
                    CALLOC(1, sizeof(struct ScanTmEvent));

                tm_event->ip_proto  = IP_PROTO_TCP;
                tm_event->ip_them   = event->ip_them;
                tm_event->ip_me     = event->ip_me;
                tm_event->port_them = event->port_them;
                tm_event->port_me   = src_port_start+idx;

                tm_event->need_timeout = 1;
                tm_event->dedup_type   = 0; /*0 for port*/

                ft_add_event(handler, tm_event, global_now);
                tm_event = NULL;
            }
        }
    }

    /*multi-probe Multi_DynamicNext*/
    if (ZBannerScan.probe->multi_mode==Multi_DynamicNext && is_multi) {
        unsigned cookie = get_cookie(event->ip_them, event->port_them,
            event->ip_me, src_port_start+is_multi-1, entropy);

        struct PacketBuffer *pkt_buffer = stack_get_packetbuffer(stack);

        pkt_buffer->length = tcp_create_packet(
            event->ip_them, event->port_them,
            event->ip_me,   src_port_start+is_multi-1,
            cookie, 0, TCP_FLAG_SYN,
            NULL, 0, pkt_buffer->px, PKT_BUF_LEN);

        stack_transmit_packetbuffer(stack, pkt_buffer);

        /*add timeout for port*/
        if (handler && zbanner_conf.is_port_timeout) {
            struct ScanTmEvent *tm_event =
                CALLOC(1, sizeof(struct ScanTmEvent));

            tm_event->ip_proto  = IP_PROTO_TCP;
            tm_event->ip_them   = event->ip_them;
            tm_event->ip_me     = event->ip_me;
            tm_event->port_them = event->port_them;
            tm_event->port_me   = src_port_start+is_multi-1;

            tm_event->need_timeout = 1;
            tm_event->dedup_type   = 0; /*0 for port*/

            ft_add_event(handler, tm_event, global_now);
            tm_event = NULL;
        }
    }
}

struct ScanModule ZBannerScan = {
    .name                = "zbanner",
    .required_probe_type = ProbeType_TCP,
    .support_timeout     = 1,
    .params              = zbanner_parameters,
    .bpf_filter = /*is rst or with ack in ipv4 & ipv6*/
        "(ip && tcp && (tcp[tcpflags]|tcp-ack!=0 || tcp[tcpflags]==tcp-rst)) "
        "|| (ip6 && tcp && (ip6[40+13]|tcp-ack!=0 || ip6[40+13]==tcp-rst))",
    .desc =
        "ZBannerScan tries to contruct TCP conn with target port and send data "
        "from specified ProbeModule. Data in first reponse packet will be handled"
        " by specified ProbeModule.\n"
        "What important is the whole process was done in completely stateless. "
        "So ZBannerScan is very fast for large-scale probing like banner grabbing,"
        " service identification and etc.\n"
        "By the way, ZBanner support `timeout` just for banner response and port"
        " openness(syn-ack).\n"
        "NOTE1: Must specify a TcpType ProbeModule for ZBannerScan like:\n"
        "    `--probe-module xxx`\n"
        "NOTE2: ZBannerScan will construct complete TCP conns. So must avoid Linux"
        " system sending RST automatically by adding iptable rules displayed in "
        "`firewall` directory.\n"
        "NOTE3: Slow send rate may cause target host's retransmition.",

    .global_init_cb               = &zbanner_global_init,
    .transmit_cb                  = &zbanner_transmit,
    .validate_cb                  = &zbanner_validate,
    .handle_cb                    = &zbanner_handle,
    .timeout_cb                   = &zbanner_timeout,
    .poll_cb                      = &scan_poll_nothing,
    .close_cb                     = &scan_close_nothing,
};