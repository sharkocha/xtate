#include <string.h>

#define PCRE2_CODE_UNIT_WIDTH 8
#include <pcre2.h>

#include "probe-modules.h"
#include "../proto/proto-http-maker.h"
#include "../proto/proto-http-parser.h"
#include "../util-data/fine-malloc.h"
#include "../util-data/safe-string.h"
#include "../crypto/crypto-base64.h"
#include "../crypto/crypto-nmapprobe.h"

struct HelloUdpConf {
    unsigned char          *hello;
    size_t                  hello_len;
    char                   *regex;
    size_t                  regex_len;
    pcre2_code             *compiled_re;
    pcre2_match_context    *match_ctx;
    unsigned                re_case_insensitive:1;
    unsigned                re_include_newlines:1;
    unsigned                report_while_regex:1;
};

static struct HelloUdpConf helloudp_conf = {0};

static enum Config_Res SET_report(void *conf, const char *name, const char *value)
{
    UNUSEDPARM(conf);
    UNUSEDPARM(name);

    helloudp_conf.report_while_regex = parseBoolean(value);

    return CONF_OK;
}

static enum Config_Res SET_newlines(void *conf, const char *name, const char *value)
{
    UNUSEDPARM(conf);
    UNUSEDPARM(name);

    helloudp_conf.re_include_newlines = parseBoolean(value);

    return CONF_OK;
}

static enum Config_Res SET_insensitive(void *conf, const char *name, const char *value)
{
    UNUSEDPARM(conf);
    UNUSEDPARM(name);

    helloudp_conf.re_case_insensitive = parseBoolean(value);

    return CONF_OK;
}

static enum Config_Res SET_regex(void *conf, const char *name, const char *value)
{
    UNUSEDPARM(conf);
    UNUSEDPARM(name);

    if (helloudp_conf.compiled_re)
        pcre2_code_free(helloudp_conf.compiled_re);
    if (helloudp_conf.match_ctx)
        pcre2_match_context_free(helloudp_conf.match_ctx);

    helloudp_conf.regex_len = strlen(value);
    if (helloudp_conf.regex_len==0) {
        LOG(LEVEL_ERROR, "FAIL: Invalid regex.\n");
        return CONF_ERR;
    }

    int pcre2_errcode;
    PCRE2_SIZE pcre2_erroffset;
    helloudp_conf.regex = STRDUP(value);
    helloudp_conf.compiled_re = pcre2_compile(
        (PCRE2_SPTR)helloudp_conf.regex,
        PCRE2_ZERO_TERMINATED,
        helloudp_conf.re_case_insensitive?PCRE2_CASELESS:0 | helloudp_conf.re_include_newlines?PCRE2_DOTALL:0,
        &pcre2_errcode,
        &pcre2_erroffset,
        NULL);
    
    if (!helloudp_conf.compiled_re) {
        LOG(LEVEL_ERROR, "[-]Regex compiled failed.\n");
        return CONF_ERR;
    }

    helloudp_conf.match_ctx = pcre2_match_context_create(NULL);
    if (!helloudp_conf.match_ctx) {
        LOG(LEVEL_ERROR, "[-]Regex allocates match_ctx failed.\n");
        return CONF_ERR;
    }

    pcre2_set_match_limit(helloudp_conf.match_ctx, 100000);

#ifdef pcre2_set_depth_limit
            // Changed name in PCRE2 10.30. PCRE2 uses macro definitions for function
            // names, so we don't have to add this to configure.ac.
            pcre2_set_depth_limit(helloudp_conf.match_ctx, 10000);
#else
            pcre2_set_recursion_limit(helloudp_conf.match_ctx, 10000);
#endif


    return CONF_OK;
}

static enum Config_Res SET_hello_string(void *conf, const char *name, const char *value)
{
    UNUSEDPARM(conf);
    UNUSEDPARM(name);

    if (helloudp_conf.hello)
        free(helloudp_conf.hello);

    helloudp_conf.hello_len = strlen(value);
    if (helloudp_conf.hello_len==0) {
        LOG(LEVEL_ERROR, "FAIL: Invalid hello string.\n");
        return CONF_ERR;
    }
    helloudp_conf.hello = MALLOC(helloudp_conf.hello_len);
    memcpy(helloudp_conf.hello, value, helloudp_conf.hello_len);

    return CONF_OK;
}

static enum Config_Res SET_hello_nmap(void *conf, const char *name, const char *value)
{
    UNUSEDPARM(conf);
    UNUSEDPARM(name);

    if (helloudp_conf.hello)
        free(helloudp_conf.hello);

    helloudp_conf.hello_len = strlen(value);
    if (helloudp_conf.hello_len==0) {
        LOG(LEVEL_ERROR, "FAIL: Invalid hello string in nmap probe format.\n");
        return CONF_ERR;
    }

    helloudp_conf.hello     = CALLOC(1, helloudp_conf.hello_len);
    helloudp_conf.hello_len = nmapprobe_decode(value,
        helloudp_conf.hello_len, helloudp_conf.hello, helloudp_conf.hello_len);

    return CONF_OK;
}

static enum Config_Res SET_hello_base64(void *conf, const char *name, const char *value)
{
    UNUSEDPARM(conf);
    UNUSEDPARM(name);

    if (helloudp_conf.hello)
        free(helloudp_conf.hello);

    helloudp_conf.hello_len = strlen(value);
    if (helloudp_conf.hello_len==0) {
        LOG(LEVEL_ERROR, "FAIL: Invalid hello string in base64 format.\n");
        return CONF_ERR;
    }

    helloudp_conf.hello     = CALLOC(1, helloudp_conf.hello_len);
    helloudp_conf.hello_len = base64_decode((char *)helloudp_conf.hello,
        helloudp_conf.hello_len, value, helloudp_conf.hello_len);

    return CONF_OK;
}

static enum Config_Res SET_hello_file(void *conf, const char *name, const char *value)
{
    UNUSEDPARM(conf);
    UNUSEDPARM(name);

    if (helloudp_conf.hello)
        free(helloudp_conf.hello);
    
    FILE *fp = fopen(value, "rb");
    if (fp==NULL) {
        LOG(LEVEL_ERROR, "[-]Failed to open file %s.\n", value);
        return CONF_ERR;
    }

    /**
     * We may specify a large size file accidently, so limit the size by a buf.
    */
    unsigned char buf[PROBE_PAYLOAD_MAX_LEN];
    size_t bytes_read = fread(buf, 1, PROBE_PAYLOAD_MAX_LEN, fp);
    if (bytes_read==0) {
        LOG(LEVEL_ERROR, "[-]Failed to read valid hello in file %s.\n", value);
        perror(value);
        fclose(fp);
        return CONF_ERR;
    }
    fclose(fp);


    helloudp_conf.hello_len = bytes_read;
    helloudp_conf.hello     = MALLOC(bytes_read);
    memcpy(helloudp_conf.hello, buf, bytes_read);

    return CONF_OK;
}

static struct ConfigParam helloudp_parameters[] = {
    {
        "string",
        SET_hello_string,
        F_NONE,
        {0},
        "Specifies a string and set it as hello data after decoded."
        " This will overwrite hello data set by other parameters."
    },
    {
        "base64-string",
        SET_hello_base64,
        F_NONE,
        {"base64", 0},
        "Specifies a string in base64 format and set it as hello data after decoded."
        " This will overwrite hello data set by other parameters."
    },
    {
        "nmap-string",
        SET_hello_nmap,
        F_NONE,
        {"nmap", 0},
        "Specifies a string in nmap probe format and set it as hello data after decoded."
        " This will overwrite hello data set by other parameters."
    },
    {
        "file",
        SET_hello_file,
        F_NONE,
        {0},
        "Specifies a file and set the content of file as hello data."
        " This will overwrite hello data set by other parameters."
    },
    {
        "regex",
        SET_regex,
        F_NONE,
        {0},
        "Specifies a regex and sets matched response data as successed instead of"
        " reporting all results."
    },
    {
        "case-insensitive",
        SET_insensitive,
        F_BOOL,
        {"insensitive", 0},
        "Whether the specified regex is case-insensitive or not."
    },
    {
        "include-newlines",
        SET_newlines,
        F_BOOL,
        {"include-newline", "newline", "newlines", 0},
        "Whether the specified regex contains newlines."
    },
    {
        "report",
        SET_report,
        F_BOOL,
        {0},
        "Report response data after regex matching."
    },
    
    {0}
};

/*for internal x-ref*/
extern struct ProbeModule HelloUdpProbe;

static bool
helloudp_global_init(const struct Xconf *xconf)
{
    if (helloudp_conf.hello==NULL || helloudp_conf.hello_len==0) {
        LOG(LEVEL_ERROR, "[-]HelloUdpProbe: No hello data specified.\n");
        return false;
    }

    if (helloudp_conf.regex==NULL || helloudp_conf.regex_len==0) {
        LOG(LEVEL_ERROR, "[-]HelloUdpProbe: No regex set.\n");
        return false;
    }

    return true;
}

static size_t
helloudp_make_payload(
    struct ProbeTarget *target,
    unsigned char *payload_buf)
{
    if (helloudp_conf.hello==NULL || helloudp_conf.hello_len==0) {
        return 0;
    }

    memcpy(payload_buf, helloudp_conf.hello, helloudp_conf.hello_len);
    return helloudp_conf.hello_len;
}

static bool
helloudp_validate_response(
    struct ProbeTarget *target,
    const unsigned char *px, unsigned sizeof_px)
{
    if (sizeof_px==0) {
        return false;
    }

    pcre2_match_data *match_data;
    int rc;

    match_data = pcre2_match_data_create_from_pattern(helloudp_conf.compiled_re, NULL);
    if (!match_data) {
        LOG(LEVEL_ERROR, "FAIL: cannot allocate match_data when matching.\n");
        return false;
    }

    rc = pcre2_match(helloudp_conf.compiled_re,
        (PCRE2_SPTR8)px, (int)sizeof_px,
        0, 0, match_data, helloudp_conf.match_ctx);

    pcre2_match_data_free(match_data);

    /*matched one. ps: "offset is too small" means successful, too*/
    if (rc >= 0) {
        return true;
    } else {
        return false;
    }
}

static unsigned
helloudp_handle_response(
    struct ProbeTarget *target,
    const unsigned char *px, unsigned sizeof_px,
    struct OutputItem *item)
{
    if (sizeof_px==0) {
        safe_strcpy(item->classification, OUTPUT_CLS_LEN, "no service");
        safe_strcpy(item->reason, OUTPUT_RSN_LEN, "timeout");
        item->level = Output_FAILURE;
        return 0;
    }

    item->level = Output_SUCCESS;
    safe_strcpy(item->classification, OUTPUT_CLS_LEN, "success");
    safe_strcpy(item->reason, OUTPUT_RSN_LEN, "matched");

    if (helloudp_conf.report_while_regex)
        normalize_string(px, sizeof_px, item->report, OUTPUT_RPT_LEN);

    return 0;
}

static void
helloudp_close()
{
    if (helloudp_conf.hello) {
        free(helloudp_conf.hello);
        helloudp_conf.hello = NULL;
    }
    helloudp_conf.hello_len = 0;

    if (helloudp_conf.regex) {
        free(helloudp_conf.regex);
        helloudp_conf.regex = NULL;
    }
    helloudp_conf.regex_len = 0;

    if (helloudp_conf.compiled_re) {
        pcre2_code_free(helloudp_conf.compiled_re);
        helloudp_conf.compiled_re = NULL;
    }

    if (helloudp_conf.match_ctx) {
        pcre2_match_context_free(helloudp_conf.match_ctx);
        helloudp_conf.match_ctx = NULL;
    }

}

struct ProbeModule HelloUdpProbe = {
    .name       = "hello-udp",
    .type       = ProbeType_UDP,
    .multi_mode = Multi_Null,
    .multi_num  = 1,
    .params     = helloudp_parameters,
    .desc =
        "HelloUdpProbe is the udp version of HelloPorbe, it uses static"
        " content set by user as hello data and reports banner if it matches regex"
        " that user set. It is used to test POC immediatly under udp.\n"
        "NOTE: We must specify hello data and regex for HelloUdpProbe.",
    .global_init_cb                    = &helloudp_global_init,
    .make_payload_cb                   = &helloudp_make_payload,
    .get_payload_length_cb             = NULL,
    .validate_response_cb              = &helloudp_validate_response,
    .handle_response_cb                = &helloudp_handle_response,
    .close_cb                          = &helloudp_close,
};