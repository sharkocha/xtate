#include <string.h>
#include <stdio.h>
#include <assert.h>

#include <openssl/err.h>
#include <openssl/ssl.h>
#include <openssl/x509.h>
#include <openssl/x509v3.h>

#include "probe-modules.h"
#include "../pixie/pixie-timer.h"
#include "../util/safe-string.h"
#include "../util/fine-malloc.h"
#include "../output/output.h"
#include "../util/logger.h"
#include "../xconf.h"


#ifndef min
#define min(a,b) ((a)<(b)?(a):(b))
#endif

enum {
    TLS_STATE_HANDSHAKE,
    TLS_STATE_APP_HELLO,
    TLS_STATE_APP_RECEIVE_NEXT,
    TLS_STATE_APP_CLOSE,
    TLS_STATE_UNKNOWN
};

/*for internal x-ref*/
extern struct ProbeModule TlsStateProbe;
/*save Output*/
static struct Output *tls_out;
/*public SSL obj for all conn*/
static SSL_CTX *ssl_ctx;

struct TlsState {
    OSSL_HANDSHAKE_STATE handshake_state;
    struct ProbeState substate;
    SSL *ssl;
    BIO *rbio;
    BIO *wbio;
    unsigned char *data;
    size_t data_max_len;
    unsigned have_dump_version:1;
};

static void ssl_keylog_cb(const SSL *ssl, const char *line)
{
    struct ProbeTarget *tgt = SSL_get_ex_data(ssl, 0);
    if (!tgt)
        return;

    struct OutputItem item = {
        .ip_them   = tgt->ip_them,
        .port_them = tgt->port_them,
        .ip_me     = tgt->ip_me,
        .port_me   = tgt->port_me,
        .level     = Output_INFO,
    };

    safe_strcpy(item.classification, OUTPUT_CLS_LEN, "ssl key log");
    safe_strcpy(item.reason, OUTPUT_RSN_LEN, "recorded");
    safe_strcpy(item.report, OUTPUT_RPT_LEN, line);

    output_result(tls_out, &item);
}

struct TlsStateConf {
    struct ProbeModule *subprobe;
    char               *subprobe_args;
    unsigned            ssl_keylog:1;
    unsigned            dump_version:1;
};

static struct TlsStateConf tlsstate_conf = {0};

static int SET_subprobe(void *conf, const char *name, const char *value)
{
    UNUSEDPARM(conf);
    UNUSEDPARM(name);

    tlsstate_conf.subprobe = get_probe_module_by_name(value);
    if (!tlsstate_conf.subprobe) {
        fprintf(stderr, "[-] Invalid name of subprobe: %s.\n", value);
        return CONF_ERR;
    }

    return CONF_OK;
}

static int SET_subprobe_args(void *conf, const char *name, const char *value)
{
    UNUSEDPARM(conf);
    UNUSEDPARM(name);

    size_t len = strlen(value) + 1;
    if (tlsstate_conf.subprobe_args)
        free(tlsstate_conf.subprobe_args);
    tlsstate_conf.subprobe_args = CALLOC(1, len);
    memcpy(tlsstate_conf.subprobe_args, value, len);

    return CONF_OK;
}

static int SET_ssl_keylog(void *conf, const char *name, const char *value)
{
    UNUSEDPARM(conf);
    UNUSEDPARM(name);

    tlsstate_conf.ssl_keylog = parseBoolean(value);

    return CONF_OK;
}

static int SET_dump_version(void *conf, const char *name, const char *value)
{
    UNUSEDPARM(conf);
    UNUSEDPARM(name);

    tlsstate_conf.dump_version = parseBoolean(value);

    return CONF_OK;
}

static struct ConfigParameter tlsstate_parameters[] = {
    {
        "subprobe",
        SET_subprobe,
        0,
        {"sub-probe-module", 0},
        "Specifies a ProbeModule as subprobe of TlsState Probe."
    },
    {
        "subprobe-arg",
        SET_subprobe_args,
        0,
        {"subprobe-args", 0},
        "Specifies arguments for subprobe."
    },
    {
        "ssl-keylog",
        SET_ssl_keylog,
        F_BOOL,
        {"key-log", 0},
        "Record the SSL key log to result."
    },
    {
        "dump-version",
        SET_dump_version,
        F_BOOL,
        {"version", 0},
        "Record SSL/TLS version to results."
    },

    {0}
};

/*init public SSL_CTX*/
static int
tlsstate_global_init(const void *xconf)
{
    /*save `out`*/
    tls_out = &((struct Xconf *)xconf)->output;

    const SSL_METHOD *meth;
    SSL_CTX *ctx;
    int res;

    LOG(LEVEL_INFO, "[ssl_init] >>>\n");

    /*support cryptographic algorithms from SSLv3.0 to TLSv1.3*/
    meth = TLS_method();
    if (meth == NULL) {
        LOG(LEVEL_WARNING, "TLS_method error\n");
        goto error0;
    }

    ctx = SSL_CTX_new(meth);
    if (ctx == NULL) {
        LOG(LEVEL_WARNING, "SSL_CTX_new error\n");
        goto error0;
    }

    /*no verification for server*/
    SSL_CTX_set_verify(ctx, SSL_VERIFY_NONE, NULL);

    /*support all versions*/
    SSL_CTX_set_min_proto_version(ctx, 0);
    SSL_CTX_set_max_proto_version(ctx, 0);

    /*security level 0 means: everything is permitted*/
    SSL_CTX_set_security_level(ctx, 0);

    /*ciphersuites allowed in TLSv1.2 or older*/
    res = SSL_CTX_set_cipher_list(ctx, "ALL:eNULL");
    if (res != 1) {
        LOG(LEVEL_WARNING, "SSL_CTX_set_cipher_list error %d\n", res);
    }
    /*ciphersuites allowed in TLSv1.3. (ALL & in order)*/
    res = SSL_CTX_set_ciphersuites(
        ctx, "TLS_AES_128_GCM_SHA256:TLS_AES_256_GCM_SHA384:"
             "TLS_CHACHA20_POLY1305_SHA256:TLS_AES_128_CCM_SHA256:"
             "TLS_AES_128_CCM_8_SHA256");
    if (res != 1) {
        LOG(LEVEL_WARNING, "SSL_CTX_set_ciphersuites error %d\n", res);
    }

    /**
     * set TLS key logging callback
     * typedef void (*SSL_CTX_keylog_cb_func)(const SSL *ssl, const char *line);
     * */
    if (tlsstate_conf.ssl_keylog) {
        SSL_CTX_set_keylog_callback(ctx, ssl_keylog_cb);
    }

    ssl_ctx = ctx;
    LOG(LEVEL_INFO, "SUCCESS init dynamic ssl\n");

    if (tlsstate_conf.subprobe_args
        && tlsstate_conf.subprobe->params) {
        if (set_parameters_from_substring(NULL,
            tlsstate_conf.subprobe->params, tlsstate_conf.subprobe_args)) {
            LOG(0, "FAIL: errors happened in param parsing of subprobe of TlsState.\n");
            exit(1);
        }
    }
    /*init for subprobe*/
    return tlsstate_conf.subprobe->global_init_cb(xconf);

error0:
    return 0;
}

static void tlsstate_close()
{
    tlsstate_conf.subprobe->close_cb();

    if (ssl_ctx) {
        SSL_CTX_free(ssl_ctx);
        ssl_ctx = NULL;
    }

    return;
}

/*init SSL struct*/
static void
tlsstate_conn_init(struct ProbeState *state, struct ProbeTarget *target)
{
    int res;
    SSL *ssl;
    BIO *rbio, *wbio;
    unsigned char *data;
    struct TlsState *tls_state;
    unsigned int data_max_len = 4096;

    LOG(LEVEL_INFO, "[ssl_transmit_hello] >>>\n");

    if (ssl_ctx == NULL) {
        goto error0;
    }

    /*buffer for BIO*/
    data = (unsigned char *)malloc(data_max_len);
    if (data == NULL) {
        LOG(LEVEL_WARNING, "SSL alloc memory error 0x%X\n", data_max_len);
        goto error1;
    }

    tls_state = CALLOC(1, sizeof(struct TlsState));
    if (tls_state == NULL) {
        LOG(LEVEL_WARNING, "SSL alloc memory error 0x%" PRIx64 "\n",
            sizeof(struct TlsState));
        goto error2;
    }

    rbio = BIO_new(BIO_s_mem());
    if (rbio == NULL) {
        LOG(LEVEL_WARNING, "BIO_new(read) error\n");
        goto error3;
    }

    wbio = BIO_new(BIO_s_mem());
    if (wbio == NULL) {
        LOG(LEVEL_WARNING, "BIO_new(write) error\n");
        goto error4;
    }

    ssl = SSL_new(ssl_ctx);
    if (ssl == NULL) {
        LOG(LEVEL_WARNING, "SSL_new error\n");
        goto error5;
    }

    /*client mode*/
    SSL_set_connect_state(ssl);
 
    SSL_set_bio(ssl, rbio, wbio);


    /*save `target` to SSL object*/
    struct ProbeTarget *tgt = MALLOC(sizeof(struct ProbeTarget));
    tgt->ip_them   = target->ip_them;
    tgt->port_them = target->port_them;
    tgt->ip_me     = target->ip_me;
    tgt->port_me   = target->port_me;
    tgt->cookie    = target->cookie;
    tgt->index     = target->index;
    res = SSL_set_ex_data(ssl, 0, tgt);
    if (res != 1) {
        LOG(LEVEL_WARNING, "SSL_set_ex_data banout error\n");
        goto error6;
    }

    /*keep important struct in probe state*/
    tls_state->ssl  = ssl;
    tls_state->rbio = rbio;
    tls_state->wbio = wbio;
    tls_state->data = data;
    tls_state->data_max_len    = data_max_len;
    tls_state->handshake_state = TLS_ST_BEFORE; /*state for openssl*/
 
    state->data = tls_state;

    /*do conn init for subprobe*/
    tlsstate_conf.subprobe->conn_init_cb(&tls_state->substate, target);

    return;

    // SSL_set_ex_data(ssl, 1, NULL);
// error7:
    // SSL_set_ex_data(ssl, 0, NULL);
error6:
    free(tgt);
    SSL_free(ssl);
    wbio = NULL;
    rbio = NULL;
error5:
    if (wbio != NULL) {
        BIO_free(wbio);
        wbio = NULL;
    }
error4:
    if (rbio != NULL) {
        BIO_free(rbio);
        rbio = NULL;
    }
error3:
    free(state->data);
error2:
    free(data);
error1:
error0:

    return;
}

static void
tlsstate_conn_close(struct ProbeState *state, struct ProbeTarget *target)
{
    LOG(LEVEL_INFO, "[ssl_cleanup] >>>\n");

    if (!state->data) return;

    struct TlsState *tls_state = state->data;

    /*do conn close for subprobe*/
    tlsstate_conf.subprobe->conn_close_cb(&tls_state->substate, target);

    /*cleanup ex data in SSL obj*/
    free(SSL_get_ex_data(tls_state->ssl, 0));

    if (tls_state->ssl) {
        SSL_free(tls_state->ssl);
        tls_state->ssl = NULL;
        tls_state->rbio = NULL;
        tls_state->wbio = NULL;
    }

    if (tls_state->data) {
        free(tls_state->data);
        tls_state->data = NULL;
        tls_state->data_max_len = 0;
    }

    free(tls_state);
    state->data = NULL;
}

static void
tlsstate_make_hello(
    struct DataPass *pass,
    struct ProbeState *state,
    struct ProbeTarget *target)
{
    int res, res_ex;
    size_t offset = 0;
    struct TlsState *tls_state = state->data;

    res = SSL_do_handshake(tls_state->ssl);
    res_ex = SSL_ERROR_NONE;
    if (res < 0) {
        res_ex = SSL_get_error(tls_state->ssl, res);
    }

    if (res == 1) {
        // if success, but its impossible
    } else if (res < 0 && res_ex == SSL_ERROR_WANT_READ) {
        offset = 0;
        while (true) {
            /*extend if buffer is not enough*/
            if (tls_state->data_max_len - offset <= 0) {
                unsigned char *tmp_data = NULL;
                tmp_data = REALLOC(tls_state->data, tls_state->data_max_len * 2);
                if (tmp_data == NULL) {
                    LOG(LEVEL_WARNING, "SSL realoc memory error 0x%" PRIxPTR "\n",
                        tls_state->data_max_len * 2);
                    goto error1;
                }
                tls_state->data = tmp_data;
                tls_state->data_max_len = tls_state->data_max_len * 2;
            }

            /*get ClientHello here*/
            res = BIO_read(tls_state->wbio,
                           tls_state->data + offset,
                           (int)(tls_state->data_max_len - offset));
            if (res > 0) {
                LOG(LEVEL_INFO, "[ssl_transmit_hello]BIO_read: %d\n", res);
                offset += (size_t)res;
            } else if (res == 0 || res == -1) {
                LOG(LEVEL_INFO, "[ssl_transmit_hello]BIO_read: %d\n", res);
                break;
            } else {
                LOG(LEVEL_WARNING,
                    "[ssl_transmit_hello]BIO_read failed with error: %d\n", res);
                goto error1;
            }
        }
    } else {
        LOG(LEVEL_WARNING, "SSL_do_handshake failed with error: %d, ex_error: %d\n",
            res, res_ex);
        goto error1;
    }

    /*save state for openssl*/
    tls_state->handshake_state = SSL_get_state(tls_state->ssl);

    /*telling to send ClientHello*/
    pass->payload = tls_state->data;
    pass->len     = offset;
    pass->flag    = PASS__copy;
    return;
error1:
    pass->payload = NULL;
    pass->len     = 0;
    pass->close   = 1;
    return;
}

static void output_version(struct Output *out,
    struct ProbeTarget *target, SSL *ssl)
{
    int version = SSL_version(ssl);

    struct OutputItem item = {
        .ip_them   = target->ip_them,
        .port_them = target->port_them,
        .ip_me     = target->ip_me,
        .port_me   = target->port_me,
        .level     = Output_INFO,
    };

    safe_strcpy(item.classification, OUTPUT_CLS_LEN, "tls version");
    safe_strcpy(item.reason, OUTPUT_RSN_LEN, "recorded");

    switch (version) {
    case SSL3_VERSION:
        safe_strcpy(item.report, OUTPUT_RPT_LEN, "SSLv3.0");
        break;
    case TLS1_VERSION:
        safe_strcpy(item.report, OUTPUT_RPT_LEN, "TLSv1.0");
        break;
    case TLS1_1_VERSION:
        safe_strcpy(item.report, OUTPUT_RPT_LEN, "TLSv1.1");
        break;
    case TLS1_2_VERSION:
        safe_strcpy(item.report, OUTPUT_RPT_LEN, "TLSv1.2");
        break;
    case TLS1_3_VERSION:
        safe_strcpy(item.report, OUTPUT_RPT_LEN, "TLSv1.3");
        break;
    default:
        safe_strcpy(item.report, OUTPUT_RPT_LEN, "Other");
    }

    output_result(tls_out, &item);
}

static void
tlsstate_parse_response(
    struct DataPass *pass,
    struct ProbeState *state,
    struct Output *out,
    struct ProbeTarget *target,
    const unsigned char *px,
    unsigned sizeof_px)
{
    #if 1
    int res, res_ex;
    int is_continue;
    struct TlsState *tls_state = state->data;

    if (state->state == TLS_STATE_UNKNOWN) {
        is_continue = 0;
    } else {
        is_continue = 1;
    }

    if (is_continue && px != NULL && sizeof_px != 0) {

        size_t offset = 0;
        uint64_t now_time = pixie_gettime();
        res = 0;

        while (offset < sizeof_px) {
            res = BIO_write(tls_state->rbio, px + offset,
                            (unsigned int)min(16384, sizeof_px - offset));
            LOG(LEVEL_INFO, "[ssl_parse_record]BIO_write: %d \n", res);
            if (res > 0) {
                offset += (size_t)res;
                continue;
            } else {
                LOG(LEVEL_WARNING,
                    "[ssl_parse_record]BIO_write failed with error: %d\n", res);
                /*close connection*/
                pass->payload = NULL;
                pass->len     = 0;
                pass->close   = 1;
                return;
            }
        }

        now_time = pixie_gettime() - now_time;
        if (sizeof_px > 16384 || now_time > 1000000) {
            LOGip(LEVEL_WARNING, target->ip_them, target->port_them,
                  "[ssl_parse_record]len px: 0x%" PRIxPTR ", time: " PRIu64
                  " millis\n",
                  sizeof_px, now_time * 1000);
            LOG(LEVEL_WARNING, "[ssl_parse_record]offset: 0x%" PRIxPTR ", res = %d\n",
                offset, res);
            if (sizeof_px > 3) {
                LOG(LEVEL_WARNING, "[ssl_parse_record]dump: %02X %02X %02X %02X\n",
                    px[0], px[1], px[2], px[3]);
            }
        }
    }

    while (is_continue) {

        switch (state->state) {

        /*still in handshake*/
        case TLS_STATE_HANDSHAKE:

            res = SSL_do_handshake(tls_state->ssl);
            res_ex = SSL_ERROR_NONE;

            if (res < 0) {
                res_ex = SSL_get_error(tls_state->ssl, res);
            }

            tls_state->handshake_state = SSL_get_state(tls_state->ssl);

            /*output version*/
            if (tlsstate_conf.dump_version && !tls_state->have_dump_version) {
                output_version(out, target, tls_state->ssl);
                tls_state->have_dump_version = 1;
            }

            //!output version and cypher suites info
            // if (pstate->sub.ssl_dynamic.have_dump_version == false &&
            //     tls_state->handshake_state != TLS_ST_BEFORE &&
            //     tls_state->handshake_state != TLS_ST_CW_CLNT_HELLO &&
            //     (SSL_get_current_cipher(pstate->sub.ssl_dynamic.ssl) ||  /*crypto algo using by cur conn*/
            //      SSL_get_pending_cipher(pstate->sub.ssl_dynamic.ssl))) { /*crypto algo would be used by cur conn*/

            //     BANNER_VERSION(banout, pstate->sub.ssl_dynamic.ssl);
            //     BANNER_CIPHER(banout, pstate->sub.ssl_dynamic.ssl);

            //     pstate->sub.ssl_dynamic.have_dump_version = true;
            // }

            //!output X.509 info
            // if (pstate->sub.ssl_dynamic.have_dump_cert == false &&
            //     SSL_get_peer_cert_chain(tls_state->ssl) != NULL) {

            //     if (banner1->is_capture_cert) {
            //       BANNER_CERTS(banout, pstate->sub.ssl_dynamic.ssl);
            //     }

            //     BANNER_NAMES(banout, pstate->sub.ssl_dynamic.ssl);
            //     pstate->sub.ssl_dynamic.have_dump_cert = true;
            // }

            //finished handshake
            if (res == 1) {

                //handshake successfully
                if (tls_state->handshake_state == TLS_ST_OK) {
                    /*We also can do conn init for subprobe here,
                    but I must know exactly whether subprobe has been inited.*/
                    state->state = TLS_STATE_APP_HELLO;

                } else {
                    LOG(LEVEL_WARNING, "Unknown handshake state %d\n",
                        tls_state->handshake_state);
                    state->state = TLS_STATE_UNKNOWN;
                }

            } else if (res < 0 && res_ex == SSL_ERROR_WANT_READ) { //go on handshake

                size_t offset = 0;

                while (true) {
                    if (tls_state->data_max_len - offset <= 0) {
                        unsigned char *tmp_data = NULL;
                        tmp_data = (unsigned char *)REALLOC(
                            tls_state->data,
                            tls_state->data_max_len * 2);
                        if (tmp_data == NULL) {
                          LOG(LEVEL_WARNING,
                              "[ssl_parse_record]SSL realoc memory error 0x%" PRIxPTR "\n",
                              tls_state->data_max_len * 2);
                          state->state = TLS_STATE_UNKNOWN;
                          break;
                        } else {
                          tls_state->data = tmp_data;
                          tls_state->data_max_len = tls_state->data_max_len * 2;
                        }
                    }

                    res = BIO_read(
                        tls_state->wbio,
                        tls_state->data + offset,
                        (unsigned int)(tls_state->data_max_len - offset));

                    if (res > 0) {
                        LOG(LEVEL_INFO, "[ssl_parse_record]BIO_read: %d\n", res);
                        offset += (size_t)res;
                    } else if (res == 0 || res == -1) {
                        LOG(LEVEL_INFO, "[ssl_parse_record]BIO_read: %d\n", res);
                        break;
                    } else {
                        LOG(LEVEL_WARNING,
                            "[ssl_parse_record]BIO_read failed with error: %d\n", res);
                        state->state = TLS_STATE_UNKNOWN;
                        break;
                    }
                }

                if (state->state != TLS_STATE_UNKNOWN) {
                    pass->payload = tls_state->data;
                    pass->len     = offset;
                    pass->flag    = PASS__copy;
                    is_continue = 0;
                    return;
                }

            } else {  //cannot go on handshake
                LOG(LEVEL_DEBUG,
                    "[ssl_parse_record]SSL_do_handshake failed with error: %d, "
                    "ex_error: %d\n",
                    res, res_ex);
                state->state = TLS_STATE_UNKNOWN;
            }
            break;

        //!It's time for subprobe to say hello
        case TLS_STATE_APP_HELLO: {

            struct DataPass subpass = {0};
            tlsstate_conf.subprobe->make_hello_cb(&subpass, &tls_state->substate, target);

            /*Just support this now*/
            assert(subpass.payload != NULL && subpass.len != 0);

            res = 1;

            if (subpass.payload != NULL && subpass.len != 0) {
                res = SSL_write(tls_state->ssl, subpass.payload, subpass.len);
            }

            if (res <= 0) {
                res_ex = SSL_get_error(tls_state->ssl, res);
                LOG(LEVEL_WARNING, "[ssl_parse_record]SSL_write error: %d %d\n", res,
                    res_ex);
                state->state = TLS_STATE_UNKNOWN;
            } else {
                LOG(LEVEL_INFO, "[ssl_parse_record]SSL_write: %d\n", res);
                size_t offset = 0;
                while (true) {
                    if (tls_state->data_max_len - offset <= 0) {
                        unsigned char *tmp_data = NULL;
                        tmp_data = (unsigned char *)REALLOC(
                            tls_state->data,
                            tls_state->data_max_len * 2);
                        if (tmp_data == NULL) {
                            LOG(LEVEL_WARNING,
                                "[ssl_parse_record]SSL realoc memory error 0x%" PRIxPTR "\n",
                                tls_state->data_max_len * 2);
                            state->state = TLS_STATE_UNKNOWN;
                            break;
                        } else {
                            tls_state->data = tmp_data;
                            tls_state->data_max_len = tls_state->data_max_len * 2;
                        }
                    }

                    res = BIO_read(
                        tls_state->wbio,
                        tls_state->data + offset,
                        (unsigned int)(tls_state->data_max_len - offset));
                    if (res > 0) {
                        LOG(LEVEL_INFO, "[ssl_parse_record]BIO_read: %d\n", res);
                        offset += (size_t)res;
                    } else if (res == 0 || res == -1) {
                        LOG(LEVEL_DEBUG, "[ssl_parse_record]BIO_read: %d\n", res);
                        break;
                    } else {
                        LOG(LEVEL_WARNING,
                            "[ssl_parse_record]BIO_read failed with error: %d\n", res);
                        state->state = TLS_STATE_UNKNOWN;
                        break;
                    }
                }
                if (state->state != TLS_STATE_UNKNOWN) {
                    state->state  = TLS_STATE_APP_RECEIVE_NEXT;
                    pass->payload = tls_state->data;
                    pass->len     = offset;
                    pass->flag    = PASS__copy;
                    is_continue = 0;
                    return;
                }
            }
        } break;

        //!Pass data to subprobe to handle. Cannot send data again.
        case TLS_STATE_APP_RECEIVE_NEXT:
            while (true) {
                res = SSL_read(tls_state->ssl, tls_state->data,
                    (unsigned int)tls_state->data_max_len);
                if (res > 0) {
                    LOG(LEVEL_INFO, "[ssl_parse_record]SSL_read: %d\n", res);

                    struct DataPass subpass = {0};

                    tlsstate_conf.subprobe->parse_response_cb(&subpass,
                        &tls_state->substate,
                        out, target,
                        tls_state->data, res);

                    assert(subpass.payload == NULL && subpass.len == 0);

                    continue;
                } else {
                    res_ex = SSL_get_error(tls_state->ssl, res);
                    if (res_ex == SSL_ERROR_WANT_READ) {
                        is_continue = 0;
                    } else if (res_ex == SSL_ERROR_ZERO_RETURN) {
                        state->state = TLS_STATE_APP_CLOSE;
                    } else {
                        if (res_ex != SSL_ERROR_SSL) {
                            LOG(LEVEL_WARNING, "[ssl_parse_record]SSL_read error: %d %d\n",
                                res, res_ex);
                        }
                        state->state = TLS_STATE_UNKNOWN;
                    }
                    break;
                }
            }
            break;

        case TLS_STATE_APP_CLOSE:
            pass->close = 1;
            is_continue = 0;
            state->state = TLS_STATE_UNKNOWN;
            break;

        case TLS_STATE_UNKNOWN:
            pass->close = 1;
            return;
        }
    }

    return;

    #endif
}

struct ProbeModule TlsStateProbe = {
    .name       = "tls-state",
    .type       = ProbeType_STATE,
    .multi_mode = Multi_Null,
    .multi_num  = 1,
    .hello_wait = 0,
    .params     = tlsstate_parameters,
    .desc =
        "TlsState Probe emulates SSL/TLS layer by OpenSSL BIO machanism. "
        "It is used with TcpState ScanModule to perform TLS probing based on our"
        " user-spase TCP stack. TlsState is just a middle layer(probe), so we "
        "should specify a subprobe for it.",
    .global_init_cb                    = &tlsstate_global_init,
    .make_payload_cb                   = NULL,
    .get_payload_length_cb             = NULL,
    .validate_response_cb              = NULL,
    .handle_response_cb                = NULL,
    .conn_init_cb                      = &tlsstate_conn_init,
    .make_hello_cb                     = &tlsstate_make_hello,
    .parse_response_cb                 = &tlsstate_parse_response,
    .conn_close_cb                     = &tlsstate_conn_close,
    .close_cb                          = &tlsstate_close,
};