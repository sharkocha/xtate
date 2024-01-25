#ifndef TEMPL_TCP_H
#define TEMPL_TCP_H

#include <stdio.h>

#include "templ-pkt.h"
#include "../util/bool.h" /* <stdbool.h> */
#include "../massip/massip-addr.h"

struct TemplateOptions;

#define TCP_FLAG_CWR 0B10000000
#define TCP_FLAG_ECE 0B01000000
#define TCP_FLAG_URG 0B00100000
#define TCP_FLAG_ACK 0B00010000
#define TCP_FLAG_PSH 0B00001000
#define TCP_FLAG_RST 0B00000100
#define TCP_FLAG_SYN 0B00000010
#define TCP_FLAG_FIN 0B00000001

#define TCP_SEQNO(px,i) (px[i+4]<<24|px[i+5]<<16|px[i+6]<<8|px[i+7])
#define TCP_ACKNO(px,i) (px[i+8]<<24|px[i+9]<<16|px[i+10]<<8|px[i+11])
#define TCP_FLAGS(px,i) (px[(i)+13])
#define TCP_WIN(px,i) (px[i+14]<<8|px[i+15]) /*calc TCP window size*/

#define TCP_HAS_FLAG(px,i,flag) ((TCP_FLAGS((px),(i)) & (flag)) == (flag))


/**
 * Called during configuration, to apply all the various changes the
 * user asked for on the command-line, such as optioms like:
 * --tcp-mss 1460
 * --tcp-sackperm
 * --tcp-wscale 3
 */
void
templ_tcp_apply_options(unsigned char **inout_buf, size_t *inout_length,
                  const struct TemplateOptions *templ_opts);

/**
 * Set's the TCP "window" field. The purpose is to cause the recipient
 * to fragment data on the response, thus evading IDS that triggers on
 * out going packets
 */
void
tcp_set_window(unsigned char *px, size_t px_length, unsigned window);

/**
 * Conduct a selftest of all the functions that manipulate the TCP
 * header template.
 */
int
templ_tcp_selftest(void);

/**
 * Create a TCP packet containing a payload, based on the original
 * template used for the SYN
 */
size_t
tcp_create_by_template(
        const struct TemplatePacket *pkt,
        ipaddress ip_them, unsigned port_them,
        ipaddress ip_me, unsigned port_me,
        unsigned seqno, unsigned ackno,
        unsigned flags,
        const unsigned char *payload, size_t payload_length,
        unsigned char *px, size_t px_length);

/**
 * This is a wrapped func that uses global_tmplset to create tcp packet.
*/
size_t
tcp_create_packet(
        ipaddress ip_them, unsigned port_them,
        ipaddress ip_me, unsigned port_me,
        unsigned seqno, unsigned ackno,
        unsigned flags,
        const unsigned char *payload, size_t payload_length,
        unsigned char *px, size_t px_length);

#endif