#include "probe-modules.h"
#include "../proto/proto-dns.h"
#include "../massip/massip-parse.h"
#include "../util-data/safe-string.h"
#include "../util-data/fine-malloc.h"
#include "../util-data/data-convert.h"

/*for internal x-ref*/
extern Probe DnsProbe;

struct DnsConf {
    char *req_name;
    dns_record_type req_type;
    unsigned print_all_ans:1;
    unsigned print_all_auth:1;
    unsigned print_all_add:1;
};

static struct DnsConf dns_conf = {0};

static ConfRes SET_ptr(void *conf, const char *name, const char *value)
{
    UNUSEDPARM(conf);
    UNUSEDPARM(name);

    ipaddress ip = massip_parse_ip(value);
    if (ip.version==0) {
        LOG(LEVEL_ERROR, "PTR request ip is invalid: %s.\n", value);
        return Conf_ERR;
    }

    ipaddress_ptr_t ip_ptr = ipaddress_ptr_fmt(ip);

    dns_conf.req_name = STRDUP(ip_ptr.string);
    dns_conf.req_type = dns_str_to_record_type("ptr");

    if (dns_conf.req_type==DNS_REC_INVALID) {
        LOG(LEVEL_ERROR, "internal: invalide request type of dns.\n");
        return Conf_ERR;
    }

    return Conf_OK;
}

static ConfRes SET_print_all_add(void *conf, const char *name, const char *value)
{
    UNUSEDPARM(conf);
    UNUSEDPARM(name);

    dns_conf.print_all_add = parseBoolean(value);

    return Conf_OK;
}

static ConfRes SET_print_all_auth(void *conf, const char *name, const char *value)
{
    UNUSEDPARM(conf);
    UNUSEDPARM(name);

    dns_conf.print_all_auth = parseBoolean(value);

    return Conf_OK;
}

static ConfRes SET_print_all_answer(void *conf, const char *name, const char *value)
{
    UNUSEDPARM(conf);
    UNUSEDPARM(name);

    dns_conf.print_all_ans = parseBoolean(value);

    return Conf_OK;
}

static ConfRes SET_req_name(void *conf, const char *name, const char *value)
{
    UNUSEDPARM(conf);
    UNUSEDPARM(name);

    char  *str     = STRDUP(value);
    size_t str_len = strlen(str);
    if (str_len == 0) {
        LOG(LEVEL_ERROR, "request name of dns is error.\n");
        return Conf_ERR;
    }

    dns_conf.req_name = str;

    return Conf_OK;
}

static ConfRes SET_req_type(void *conf, const char *name, const char *value)
{
    UNUSEDPARM(conf);
    UNUSEDPARM(name);

    size_t str_len = strlen(value);
    if (str_len == 0) {
        LOG(LEVEL_ERROR, "request type of dns is error.\n");
        return Conf_ERR;
    }

    dns_conf.req_type = dns_str_to_record_type(value);

    if (dns_conf.req_type==DNS_REC_INVALID) {
        LOG(LEVEL_ERROR, "invalide request type of dns.\n");
        return Conf_ERR;
    }

    return Conf_OK;
}

static ConfParam dns_parameters[] = {
    {
        "req-name",
        SET_req_name,
        Type_NONE,
        {"name", 0},
        "Specifies dns request name like 'www.google.com'."
    },
    {
        "req-type",
        SET_req_type,
        Type_NONE,
        {"type", 0},
        "Specifies dns request type like 'A', 'AAAA'."
    },
    {
        "ptr-request",
        SET_ptr,
        Type_NONE,
        {"ptr", "ptr-ip", 0},
        "This is a wrapper of setting request type to PTR and request name by "
        "the ip we specified."
    },
    {
        "all-answer",
        SET_print_all_answer,
        Type_BOOL,
        {"all-ans", 0},
        "Print all answer records instead of only the first in default."
    },
    {
        "all-authority",
        SET_print_all_auth,
        Type_BOOL,
        {"all-auth", 0},
        "Print all authority records instead of only the first in default."
    },
    {
        "all-additional",
        SET_print_all_add,
        Type_BOOL,
        {"all-addition", "all-add", 0},
        "Print all additional records instead of only the first in default."
    },

    {0}
};

static bool
dns_global_init(const Xconf *xconf)
{
    if (!dns_conf.req_name) {
        LOG(LEVEL_ERROR, "Please specify a dns request name by --req-name.\n");
        return false;
    }

    if (dns_conf.req_type == 0) {
        LOG(LEVEL_HINT, "Use default dns A record type because no request type was specified by --req-type.\n");
        dns_conf.req_type = DNS_REC_A;
    }

    return true;
}

static size_t
dns_make_payload(
    ProbeTarget *target,
    unsigned char *payload_buf)
{
    memset(payload_buf, 0, PM_PAYLOAD_SIZE);
    int res_len = dns_question_create(payload_buf,
        dns_conf.req_name, dns_conf.req_type, target->cookie & 0xFFFF);

    return (size_t)res_len;
}

static bool
dns_validate_response(
    ProbeTarget *target,
    const unsigned char *px, unsigned sizeof_px)
{
    if (sizeof_px<2) {
        return false;
    }

    /*maybe we can do more validation to ensure this is a valid dns packet*/
    if (U16_EQUAL_TO_BE(px, target->cookie & 0xFFFF)) {
        return true;
    }

    return false;
}

static unsigned
dns_handle_response(
    unsigned th_idx,
    ProbeTarget *target,
    const unsigned char *px, unsigned sizeof_px,
    OutItem *item)
{

    dns_pkt_t dns_pkt;

    if (!dns_parse_reply((uint8_t *)px, sizeof_px, &dns_pkt)) {
        item->level = OUT_FAILURE;
        safe_strcpy(item->classification, OUT_CLS_SIZE, "not dns");
        safe_strcpy(item->reason, OUT_RSN_SIZE, "parse failed");
        return 0;
    }

    item->level = OUT_SUCCESS;
    safe_strcpy(item->classification, OUT_CLS_SIZE, "dns reply");

    dns_record_t *rec;
    char tmp_data[50];
    DataLink *link;

    if (dns_pkt.head.header.ans_count > 0) {
        rec = &dns_pkt.body.ans[0];
        dns_raw_record_data2str(rec, (uint8_t *)px, (uint8_t *)px+sizeof_px,
            false, tmp_data, sizeof(tmp_data));
        const char *type_str  = dns_record_type2str(rec->type);
        const char *class_str = dns_class2str(rec->class);

        link = dach_append_char(&item->report, "answer RR", '[');
        link = dach_append_by_link(link, type_str, strlen(type_str));
        link = dach_append_char_by_link(link, ' ');
        link = dach_append_by_link(link, class_str, strlen(class_str));
        link = dach_append_char_by_link(link, ' ');
        link = dach_append_by_link(link, tmp_data, strlen(tmp_data));
        link = dach_append_char_by_link(link, ']');
    }

    if (dns_pkt.head.header.ans_count>1 && dns_conf.print_all_ans) {
        for (uint16_t i=1; i<dns_pkt.head.header.ans_count; i++) {
            rec = &dns_pkt.body.ans[i];
            dns_raw_record_data2str(rec, (uint8_t *)px, (uint8_t *)px+sizeof_px,
                false, tmp_data, sizeof(tmp_data));
            const char *type_str  = dns_record_type2str(rec->type);
            const char *class_str = dns_class2str(rec->class);

            link = dach_append_by_link(link, ", [", 3);
            link = dach_append_by_link(link, type_str, strlen(type_str));
            link = dach_append_char_by_link(link, ' ');
            link = dach_append_by_link(link, class_str, strlen(class_str));
            link = dach_append_char_by_link(link, ' ');
            link = dach_append_by_link(link, tmp_data, strlen(tmp_data));
            link = dach_append_char_by_link(link, ']');
        }
    }

    if (dns_pkt.head.header.auth_count > 0) {
        rec = &dns_pkt.body.auth[0];
        dns_raw_record_data2str(rec, (uint8_t *)px, (uint8_t *)px+sizeof_px,
            false, tmp_data, sizeof(tmp_data));
        const char *type_str  = dns_record_type2str(rec->type);
        const char *class_str = dns_class2str(rec->class);

        link = dach_append_char(&item->report, "authority RR", '[');
        link = dach_append_by_link(link, type_str, strlen(type_str));
        link = dach_append_char_by_link(link, ' ');
        link = dach_append_by_link(link, class_str, strlen(class_str));
        link = dach_append_char_by_link(link, ' ');
        link = dach_append_by_link(link, tmp_data, strlen(tmp_data));
        link = dach_append_char_by_link(link, ']');
    }

    if (dns_pkt.head.header.auth_count>1 && dns_conf.print_all_auth) {
        for (uint16_t i=1; i<dns_pkt.head.header.auth_count; i++) {
            rec = &dns_pkt.body.auth[i];
            dns_raw_record_data2str(rec, (uint8_t *)px, (uint8_t *)px+sizeof_px,
                false, tmp_data, sizeof(tmp_data));
            const char *type_str  = dns_record_type2str(rec->type);
            const char *class_str = dns_class2str(rec->class);

            link = dach_append_by_link(link, ", [", 3);
            link = dach_append_by_link(link, type_str, strlen(type_str));
            link = dach_append_char_by_link(link, ' ');
            link = dach_append_by_link(link, class_str, strlen(class_str));
            link = dach_append_char_by_link(link, ' ');
            link = dach_append_by_link(link, tmp_data, strlen(tmp_data));
            link = dach_append_char_by_link(link, ']');
        }
    }

    if (dns_pkt.head.header.add_count > 0) {
        rec = &dns_pkt.body.add[0];
        dns_raw_record_data2str(rec, (uint8_t *)px, (uint8_t *)px+sizeof_px,
            false, tmp_data, sizeof(tmp_data));
        const char *type_str  = dns_record_type2str(rec->type);
        const char *class_str = dns_class2str(rec->class);

        link = dach_append_char(&item->report, "additional RR", '[');
        link = dach_append_by_link(link, type_str, strlen(type_str));
        link = dach_append_char_by_link(link, ' ');
        link = dach_append_by_link(link, class_str, strlen(class_str));
        link = dach_append_char_by_link(link, ' ');
        link = dach_append_by_link(link, tmp_data, strlen(tmp_data));
        link = dach_append_char_by_link(link, ']');
    }

    if (dns_pkt.head.header.add_count>1 && dns_conf.print_all_add) {
        for (uint16_t i=1; i<dns_pkt.head.header.add_count; i++) {
            rec = &dns_pkt.body.add[i];
            dns_raw_record_data2str(rec, (uint8_t *)px, (uint8_t *)px+sizeof_px,
                false, tmp_data, sizeof(tmp_data));
            const char *type_str  = dns_record_type2str(rec->type);
            const char *class_str = dns_class2str(rec->class);

            link = dach_append_by_link(link, ", [", 3);
            link = dach_append_by_link(link, type_str, strlen(type_str));
            link = dach_append_char_by_link(link, ' ');
            link = dach_append_by_link(link, class_str, strlen(class_str));
            link = dach_append_char_by_link(link, ' ');
            link = dach_append_by_link(link, tmp_data, strlen(tmp_data));
            link = dach_append_char_by_link(link, ']');
        }
    }

    return 0;
}

static unsigned
dns_handle_timeout(ProbeTarget *target, OutItem *item)
{
    item->level = OUT_FAILURE;
    safe_strcpy(item->classification, OUT_CLS_SIZE, "no response");
    safe_strcpy(item->reason, OUT_RSN_SIZE, "timeout");
    return 0;
}

static void dns_close()
{
    if (dns_conf.req_name) {
        free(dns_conf.req_name);
        dns_conf.req_name = NULL;
    }
}

Probe DnsProbe = {
    .name       = "dns",
    .type       = ProbeType_UDP,
    .multi_mode = Multi_Null,
    .multi_num  = 1,
    .params     = dns_parameters,
    .desc =
        "DnsProbe sends a dns request specified by user to target udp port and "
        "expects a dns reply. DnsProbe is used for scanning multiple dns servers"
        " by one type request, instead of doing multiple dns requests to one dns"
        " server.\n"
        "NOTE: If we have not added iptables rules to ban the outwards ICMP Port"
        " Unreachable sending, dns response retransmission will happen and waste"
        " resource both on scanner and targets. And an interesting thing will "
        "happen: every retransmited dns reply carries a different answer.",
    .init_cb                                 = &dns_global_init,
    .make_payload_cb                         = &dns_make_payload,
    .validate_response_cb                    = &dns_validate_response,
    .handle_response_cb                      = &dns_handle_response,
    .handle_timeout_cb                       = &dns_handle_timeout,
    .close_cb                                = &dns_close,
};