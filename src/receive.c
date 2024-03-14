#include "receive.h"

#include <assert.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "globals.h"
#include "xconf.h"

#include "output/output.h"

#include "rawsock/rawsock-adapter.h"
#include "rawsock/rawsock-pcapfile.h"
#include "rawsock/rawsock.h"

#include "stack/stack-arpv4.h"
#include "stack/stack-ndpv6.h"
#include "stack/stack-queue.h"

#include "pixie/pixie-threads.h"
#include "pixie/pixie-timer.h"

#include "util/dedup.h"
#include "util/logger.h"
#include "util/fine-malloc.h"
#include "util/ptrace.h"
#include "util/readrange.h"

#include "timeout/fast-timeout.h"


void receive_thread(void *v) {
    struct RxThread *parms            = (struct RxThread *)v;
    const struct Xconf *xconf         = parms->xconf;
    struct Output output              = xconf->output;
    struct Adapter *adapter           = xconf->nic.adapter;
    int data_link                     = stack_if_datalink(adapter);
    struct DedupTable *dedup          = NULL;
    struct PcapFile *pcapfile         = NULL;
    uint64_t entropy                  = xconf->seed;
    struct stack_t *stack             = xconf->stack;
    struct ScanTimeoutEvent *tm_event = NULL;
    struct FHandler ft_handler;

    /* some status variables */
    uint64_t *status_successed_count = MALLOC(sizeof(uint64_t));
    uint64_t *status_failed_count    = MALLOC(sizeof(uint64_t));
    uint64_t *status_timeout_count   = MALLOC(sizeof(uint64_t));
    *status_successed_count          = 0;
    *status_failed_count             = 0;
    *status_timeout_count            = 0;
    parms->total_successed           = status_successed_count;
    parms->total_failed              = status_failed_count;
    parms->total_tm_event            = status_timeout_count;

    LOG(1, "[+] starting receive thread\n");

    output_init(&output);

    /* Lock threads to the CPUs one by one.
     * Tx threads follow  the only one Rx thread.
     */
    if (pixie_cpu_get_count() > 1) {
        pixie_cpu_set_affinity(0);
    }

    if (xconf->pcap_filename[0]) {
        pcapfile = pcapfile_openwrite(xconf->pcap_filename, 1);
    }

    if (!xconf->is_nodedup)
        dedup = dedup_create(xconf->dedup_win);

    if (xconf->is_offline) {
        while (!is_rx_done)
            pixie_usleep(10000);
        parms->done_receiving = 1;
        goto end;
    }

    if (xconf->is_fast_timeout) {
        ft_init_handler(xconf->ft_table, &ft_handler);
    }

    LOG(2, "[+] THREAD: recv: starting main loop\n");
    while (!is_rx_done) {

        struct ScanModule *scan_module = xconf->scan_module;

        /*handle a fast-timeout event in each loop*/
        if (xconf->is_fast_timeout) {

            tm_event = ft_pop_event(&ft_handler, time(0));
            /*dedup timeout event and other packets together*/
            if (tm_event) {
                if ((!xconf->is_nodedup &&
                     !dedup_is_duplicate(dedup, tm_event->ip_them, tm_event->port_them,
                                         tm_event->ip_me, tm_event->port_me,
                                         tm_event->dedup_type))
                    || xconf->is_nodedup) {

                    struct OutputItem item = {
                        .ip_them   = tm_event->ip_them,
                        .ip_me     = tm_event->ip_me,
                        .port_them = tm_event->port_them,
                        .port_me   = tm_event->port_me,
                    };

                    scan_module->timeout_cb(entropy, tm_event, &item, stack, &ft_handler);

                    output_result(&output, &item);

                    if (!item.no_output) {
                        if (item.level==Output_SUCCESS)
                            (*status_successed_count)++;
                        else if (item.level==Output_FAILURE)
                            (*status_failed_count)++;
                    }
                }
                free(tm_event);
                tm_event = NULL;
            }

            *status_timeout_count = ft_event_count(&ft_handler);
        }

        struct Received recved = {0};

        int err = rawsock_recv_packet(adapter, &(recved.length), &(recved.secs),
                                      &(recved.usecs), &(recved.packet));
        if (err != 0) {
            continue;
        }
        if (recved.length > 1514)
            continue;

        unsigned x = preprocess_frame(recved.packet, recved.length, data_link,
                                      &recved.parsed);
        if (!x)
            continue; /* corrupt packet */

        ipaddress ip_them   = recved.parsed.src_ip;
        ipaddress ip_me     = recved.parsed.dst_ip;
        unsigned  port_them = recved.parsed.port_src;
        unsigned  port_me   = recved.parsed.port_dst;

        assert(ip_me.version   != 0);
        assert(ip_them.version != 0);

        recved.is_myip   = is_my_ip(stack->src, ip_me);
        recved.is_myport = is_my_port(stack->src, port_me);

        struct PreHandle pre = {
            .go_record       = 0,
            .go_dedup        = 0,
            .dedup_ip_them   = ip_them,
            .dedup_port_them = port_them,
            .dedup_ip_me     = ip_me,
            .dedup_port_me   = port_me,
            .dedup_type      = SCAN_MODULE_DEFAULT_DEDUP_TYPE,
        };

        scan_module->validate_cb(entropy, &recved, &pre);

        if (!pre.go_record)
            continue;

        if (parms->xconf->nmap.packet_trace)
            packet_trace(stdout, parms->pt_start, recved.packet, recved.length, 0);

        /* Save raw packet in --pcap file */
        if (pcapfile) {
            pcapfile_writeframe(pcapfile, recved.packet, recved.length, recved.length,
                                recved.secs, recved.usecs);
        }

        if (!pre.go_dedup)
            continue;

        if (!xconf->is_nodedup && !pre.no_dedup) {
            if (dedup_is_duplicate(dedup, pre.dedup_ip_them, pre.dedup_port_them,
                                   pre.dedup_ip_me, pre.dedup_port_me,
                                   pre.dedup_type)) {
                continue;
            }
        }

        struct OutputItem item = {
            .ip_them   = ip_them,
            .ip_me     = ip_me,
            .port_them = port_them,
            .port_me   = port_me,
        };

        if (xconf->is_fast_timeout)
            scan_module->handle_cb(entropy, &recved, &item, stack, &ft_handler);
        else
            scan_module->handle_cb(entropy, &recved, &item, stack, NULL);

        output_result(&output, &item);

        if (!item.no_output) {
            if (item.level==Output_SUCCESS)
                (*status_successed_count)++;
            else if (item.level==Output_FAILURE)
                (*status_failed_count)++;
        }
    }

    LOG(1, "[+] exiting receive thread                            \n");

    /*
     * cleanup
     */
end:
    output_close(&output);

    if (!xconf->is_nodedup)
        dedup_destroy(dedup);
    if (pcapfile)
        pcapfile_close(pcapfile);
    if (xconf->is_fast_timeout)
        ft_close_handler(&ft_handler);

    /*TODO: free stack packet buffers */

    /* Thread is about to exit */
    parms->done_receiving = 1;
}
