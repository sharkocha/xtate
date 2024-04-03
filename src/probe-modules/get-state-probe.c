#include <string.h>
#include <stdio.h>

#include "probe-modules.h"
#include "../util-data/safe-string.h"
#include "../output-modules/output-modules.h"
#include "../util-out/logger.h"

#define GET_STATE_PAYLOAD "GET / HTTP/1.0\r\n\r\n"

/*for internal x-ref*/
extern struct ProbeModule GetStateProbe;

struct GetStateConf {
    unsigned get_whole_page:1;
};

static struct GetStateConf getstate_conf = {0};


static int SET_whole_page(void *conf, const char *name, const char *value)
{
    UNUSEDPARM(conf);
    UNUSEDPARM(name);

    getstate_conf.get_whole_page = parseBoolean(value);

    return CONF_OK;
}

static struct ConfigParameter getstate_parameters[] = {
    {
        "whole-page",
        SET_whole_page,
        F_BOOL,
        {"whole", 0},
        "Get the whole page before connection timeout, not just the banner."
    },

    {0}
};

static unsigned getstate_global_init(const struct Xconf *xconf)
{
    LOG(LEVEL_WARNING, "[GetState Probe global initing] >>>\n");
    return 1;
}

static void getstate_close()
{
    LOG(LEVEL_WARNING, "[GetState Probe closing] >>>\n");
}

static void
getstate_conn_init(struct ProbeState *state, struct ProbeTarget *target)
{
    LOG(LEVEL_WARNING, "[GetState Probe conn initing] >>>\n");
}

static void
getstate_conn_close(struct ProbeState *state, struct ProbeTarget *target)
{
    LOG(LEVEL_WARNING, "[GetState Probe conn closing] >>>\n");
}

static void
getstate_make_hello(
    struct DataPass *pass,
    struct ProbeState *state,
    struct ProbeTarget *target)
{
    LOG(LEVEL_WARNING, "[GetState Probe making hello] >>>\n");
    /*static data and don't close the conn*/
    pass->payload = (unsigned char *)GET_STATE_PAYLOAD;
    pass->len     = strlen(GET_STATE_PAYLOAD);
    datapass_set_data(pass, (unsigned char *)GET_STATE_PAYLOAD,
        strlen(GET_STATE_PAYLOAD), 0);
}

static void
getstate_parse_response(
    struct DataPass *pass,
    struct ProbeState *state,
    struct Output *out,
    struct ProbeTarget *target,
    const unsigned char *px,
    unsigned sizeof_px)
{
    LOG(LEVEL_WARNING, "[GetState Probe parsing response] >>>\n");
    if (!getstate_conf.get_whole_page) {
        if (state->state) return;
        state->state = 1;
        pass->is_close = 1;
    }

    struct OutputItem item = {
        .level = Output_SUCCESS,
        .ip_them = target->ip_them,
        .ip_me = target->ip_me,
        .port_them = target->port_them,
        .port_me = target->port_me,
    };

    safe_strcpy(item.classification, OUTPUT_CLS_LEN, "banner");
    safe_strcpy(item.reason, OUTPUT_RSN_LEN, "responsed");
    normalize_string(px, sizeof_px, item.report, OUTPUT_RPT_LEN);

    output_result(out, &item);
}

struct ProbeModule GetStateProbe = {
    .name       = "get-state",
    .type       = ProbeType_STATE,
    .multi_mode = Multi_Null,
    .multi_num  = 1,
    .hello_wait = 0,
    .params     = getstate_parameters,
    .desc =
        "GetState Probe sends target port a simple HTTP HTTP Get request:\n"
        "    `GET / HTTP/1.0\\r\\n\\r\\n`\n"
        "And could get a simple result from http server fastly. GetState is the "
        "state version of GetRequest Probe for testing ScanModules that needs a"
        " probe of state type.",
    .global_init_cb                    = &getstate_global_init,
    .make_payload_cb                   = NULL,
    .get_payload_length_cb             = NULL,
    .validate_response_cb              = NULL,
    .handle_response_cb                = NULL,
    .conn_init_cb                      = &getstate_conn_init,
    .make_hello_cb                     = &getstate_make_hello,
    .parse_response_cb                 = &getstate_parse_response,
    .conn_close_cb                     = &getstate_conn_close,
    .close_cb                          = &getstate_close,
};