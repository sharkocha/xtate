#include <assert.h>
#include <signal.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "globals.h"
#include "receive.h"
#include "transmit.h"
#include "version.h"
#include "xconf.h"

#include "stub/stub-pcap.h"
#include "templ/templ-init.h"

#include "massip/massip-cookie.h"
#include "massip/massip-parse.h"

#include "rawsock/rawsock-adapter.h"
#include "rawsock/rawsock.h"

#include "pixie/pixie-backtrace.h"
#include "pixie/pixie-threads.h"
#include "pixie/pixie-timer.h"

#include "util-scan/initadapter.h"
#include "util-scan/listtargets.h"

#include "util-out/logger.h"
#include "util-out/xtatus.h"

#include "util-data/fine-malloc.h"
#include "util-scan/listrange.h"

#if defined(WIN32)
#include <WinSock.h>
#if defined(_MSC_VER)
#pragma comment(lib, "Ws2_32.lib")
#endif
#else
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>
#endif

/*
 * Use to hint Tx & Rx threads.
 * Should not be modified by Tx or Rx thread themselves but by
 * `mainscan` or `control_c_handler`
*/
unsigned volatile time_to_finish_tx = 0;
unsigned volatile time_to_finish_rx = 0;

/*
 * We update a global time in xtatus.c for less syscall.
 * Use this if you need rough and not accurate current time.
 */
time_t global_now;

static uint64_t usec_start;

/**
 * This is for some wrappered functions that use TemplateSet to create packets.
 * !Do not modify it unless u know what u are doing.
 */
TmplSet *global_tmplset;

static void _control_c_handler(int x) {

    static unsigned control_c_pressed = 0;

    if (control_c_pressed == 0) {
        LOG(LEVEL_OUT,
                "waiting several seconds to exit..."
                "                                                                           \n");
        /*First time of <ctrl-c>, tell Tx to stop*/
        control_c_pressed = 1;
        time_to_finish_tx = 1;
    } else {
        if (time_to_finish_rx) {
            /*Not first time of <ctrl-c> */
            /*and Rx is exiting, we just warn*/
            LOG(LEVEL_OUT, "\nERROR: Rx Thread is still running\n");
            /*Exit many <ctrl-c>*/
            if (time_to_finish_rx++ > 1)
                exit(1);
        } else {
            /*Not first time of <ctrl-c> */
            /*and we are waiting now*/
            /*tell Rx to exit*/
            time_to_finish_rx = 1;
        }
    }
}

static int _main_scan(struct Xconf *xconf) {
    /**
     * According to C99 standards while using designated initializer:
     * 
     *     "Omitted fields are implicitly initialized the same as for objects
     * that have static storage duration."
     * 
     * ref: https://gcc.gnu.org/onlinedocs/gcc/Designated-Inits.html
     * 
     * This is more efficient to got an all-zero var than memset and could got
     * a partial-zero var conveniently.
     */
    time_t                now                   = time(0);
    TmplSet    tmplset               = {0};
    struct Xtatus         status                = {.last={0}};
    struct XtatusItem     status_item           = {0};
    struct RxThread       rx_thread[1]          = {{0}};
    struct TxThread      *tx_thread;
    uint64_t              count_ports; 
    uint64_t              count_ips;
    uint64_t              range;
    double                tx_free_entries;
    double                rx_free_entries;
    double                rx_queue_ratio_tmp;

    tx_thread = CALLOC(xconf->tx_thread_count, sizeof(struct TxThread));

    /*
     * Initialize the task size
     */
    count_ips = rangelist_count(&xconf->targets.ipv4) +
                range6list_count(&xconf->targets.ipv6).lo;
    if (count_ips == 0) {
        LOG(LEVEL_ERROR, "target IP address list empty\n");
        LOG(LEVEL_HINT, "try something like \"--range 10.0.0.0/8\"\n");
        LOG(LEVEL_HINT, "try something like \"--range 192.168.0.100-192.168.0.200\"\n");
        return 1;
    }
    count_ports = rangelist_count(&xconf->targets.ports);
    if (count_ports == 0) {
        LOG(LEVEL_ERROR, "no ports were specified\n");
        LOG(LEVEL_HINT, "try something like \"-p80,8000-9000\"\n");
        LOG(LEVEL_HINT, "try something like \"--ports 0-65535\"\n");
        return 1;
    }
    range = count_ips * count_ports;

    /*
     * If the IP address range is very big, then require the
     * user apply an exclude range
     */
    if (count_ips > 1000000000ULL && rangelist_count(&xconf->exclude.ipv4) == 0) {
        LOG(LEVEL_ERROR, "range too big, need confirmation\n");
        LOG(LEVEL_OUT, "    to prevent accidents, at least one --exclude must be "
               "specified\n");
        LOG(LEVEL_HINT, "use \"--exclude 255.255.255.255\" as a simple confirmation\n");
        exit(1);
    }

    if (initialize_adapter(xconf) != 0)
        exit(1);
    if (!xconf->nic.is_usable) {
        LOG(LEVEL_ERROR, "failed to detect IP of interface\n");
        LOG(LEVEL_OUT, "    did you spell the name correctly?\n");
        LOG(LEVEL_HINT, "if it has no IP address, "
               "manually set with \"--adapter-ip 192.168.100.5\"\n");
        exit(1);
    }

    /*
     * Set the "source ports" of everything we transmit.
     */
    if (xconf->nic.src.port.range == 0) {
        unsigned port = 40000 + now % 20000;
        xconf->nic.src.port.first = port;
        xconf->nic.src.port.last  = port + XCONF_DFT_PORT_RANGE;
        xconf->nic.src.port.range = xconf->nic.src.port.last-xconf->nic.src.port.first;
    }

    /*
     * create callback queue
     */
    xconf->stack = stack_create(xconf->nic.source_mac, &xconf->nic.src,
                                xconf->stack_buf_count);

    /*
     * create fast-timeout table
     */
    if (xconf->is_fast_timeout) {
        xconf->ft_table = ft_init_table(xconf->ft_spec);
    }

    /*
     * Initialize the packet templates and attributes
     */
    xconf->tmplset = &tmplset;
    global_tmplset = &tmplset;

    /* it should be set before template init*/
    if (xconf->tcp_init_window)
        template_set_tcp_syn_window_of_default(xconf->tcp_init_window);
    if (xconf->tcp_window)
        template_set_tcp_window_of_default(xconf->tcp_window);

    template_packet_init(xconf->tmplset, xconf->nic.source_mac,
        xconf->nic.router_mac_ipv4, xconf->nic.router_mac_ipv6,
        stack_if_datalink(xconf->nic.adapter), xconf->seed, xconf->templ_opts);

    if (xconf->packet_ttl)
        template_set_ttl(xconf->tmplset, xconf->packet_ttl);

    if (xconf->nic.is_vlan)
        template_set_vlan(xconf->tmplset, xconf->nic.vlan_id);

    /*
     * Choose a default ScanModule if not specified.
     * Wrong specification will be handled in SET_scan_module in xconf.c
     */
    if (!xconf->scan_module) {
        xconf->scan_module = get_scan_module_by_name("tcp-syn");
        LOG(LEVEL_ERROR, "Default ScanModule `tcpsyn` is chosen because no ScanModule "
            "was specified.\n");
    }

    /*validate probe type*/
    if (xconf->scan_module->required_probe_type==ProbeType_NULL) {
        if (xconf->probe_module) {
            LOG(LEVEL_ERROR, "ScanModule %s does not support any probe.\n",
                xconf->scan_module->name);
            exit(1);
        }
    } else {
        if (!xconf->probe_module
            || xconf->probe_module->type != xconf->scan_module->required_probe_type) {
            LOG(LEVEL_ERROR, "ScanModule %s needs probe of %s type.\n",
                xconf->scan_module->name,
                get_probe_type_name(xconf->scan_module->required_probe_type));
            exit(1);
        }
    }

    /*
     * Config params & Do global init for ScanModule
     */
    xconf->scan_module->probe = xconf->probe_module;

    if (xconf->scan_module_args
        && xconf->scan_module->params) {
        if (set_parameters_from_substring(NULL,
            xconf->scan_module->params, xconf->scan_module_args)) {
            LOG(LEVEL_ERROR, "errors happened in sub param parsing of ScanModule.\n");
            exit(1);
        }
    }
    if (!xconf->scan_module->init_cb(xconf)) {
        LOG(LEVEL_ERROR, "errors happened in global init of ScanModule.\n");
        exit(1);
    }

    /*
     * Config params & Do global init for ProbeModule
     */
    if (xconf->probe_module) {

        if (xconf->probe_module_args
            && xconf->probe_module->params) {
            if (set_parameters_from_substring(NULL,
                xconf->probe_module->params, xconf->probe_module_args)) {
                LOG(LEVEL_ERROR, "errors happened in sub param parsing of ProbeModule.\n");
                exit(1);
            }
        }

        if (!xconf->probe_module->init_cb(xconf)) {
            LOG(LEVEL_ERROR, "errors in ProbeModule global initializing\n");
            exit(1);
        }
    }

    /*
     * Do init for OutputModule
     */
    if (!output_init(&xconf->out_conf)) {
        LOG(LEVEL_ERROR, "errors in OutputModule initializing\n");
        exit(1);
    }

    /*
     * BPF filter
     * We set BPF filter for pcap at last to avoid the filter affect router-mac
     * getting by ARP.
     * And the filter string is combined from ProbeModule and user setting.
     */
    if (!xconf->is_no_bpf) {
        rawsock_set_filter(
            xconf->nic.adapter,
            xconf->scan_module->bpf_filter,
            xconf->bpf_filter);
    }

    /*
     * trap <ctrl-c>
     */
    signal(SIGINT, _control_c_handler);

    /*
     * Prepare for tx threads
     */
    for (unsigned index = 0; index < xconf->tx_thread_count; index++) {
        struct TxThread *parms          = &tx_thread[index];
        parms->xconf                    = xconf;
        parms->tx_index                 = index;
        parms->my_index                 = xconf->resume.index;
        parms->done_transmitting        = false;
        parms->thread_handle_xmit       = 0;
    }
    /*
     * Prepare for rx thread
     */
    rx_thread->xconf                    = xconf;
    rx_thread->done_receiving           = false;
    rx_thread->thread_handle_recv       = 0;
    /** needed for --packet-trace option so that we know when we started
     * the scan
     */
    rx_thread->pt_start = 1.0 * pixie_gettime() / 1000000.0;

    /*
     * Print helpful text
     */
    char buffer[80];
    struct tm x;

    now = time(0);
    safe_gmtime(&x, &now);
    strftime(buffer, sizeof(buffer), "%Y-%m-%d %H:%M:%S GMT", &x);
    LOG(LEVEL_OUT,
        "\nStarting " XTATE_FIRST_UPPER_NAME " " XTATE_VERSION " at %s\n",
        buffer);
    LOG(LEVEL_OUT, "("XTATE_GITHUB")\n");

    LOG(LEVEL_OUT, "ScanModule  : %s\n", xconf->scan_module->name);
    if (xconf->probe_module)
        LOG(LEVEL_OUT, "ProbeModule : %s\n", xconf->probe_module->name);
    if (xconf->out_conf.output_module)
        LOG(LEVEL_OUT, "OutputModule: %s\n", xconf->out_conf.output_module->name);

    LOG(LEVEL_OUT, "Scanning %u hosts [%u port%s/host]\n\n", (unsigned)count_ips,
        (unsigned)count_ports, (count_ports == 1) ? "" : "s");

    /*
     * Start tx & rx threads
     */
    rx_thread->thread_handle_recv =
        pixie_begin_thread(receive_thread, 0, rx_thread);
    for (unsigned index = 0; index < xconf->tx_thread_count; index++) {
        struct TxThread *parms    = &tx_thread[index];
        parms->thread_handle_xmit = pixie_begin_thread(transmit_thread, 0, parms);
    }

    /**
     * set status outputing
    */
    xtatus_start(&status);
    status.print_ft_event    = xconf->is_fast_timeout;
    status.print_queue       = xconf->is_status_queue;
    status.print_info_num    = xconf->is_status_info_num;
    status.print_hit_rate    = xconf->is_status_hit_rate;
    status.is_infinite       = xconf->is_infinite;

    /*
     * Now wait for <ctrl-c> to be pressed OR for Tx Threads to exit.
     * Tx Threads can shutdown by themselves for finishing their tasks.
     * We also can use <ctrl-c> to make them exit early.
     * All controls are decided by global variable `time_to_finish_tx`.
     */
    pixie_usleep(1000 * 100);
    LOG(LEVEL_INFO, "waiting for threads to finish\n");
    while (!time_to_finish_tx) {

        /* Find the min-index, repeat and rate */
        status_item.total_sent   = 0;
        status_item.cur_pps      = 0.0;
        status_item.cur_count    = UINT64_MAX;
        status_item.repeat_count = UINT64_MAX;
        for (unsigned i = 0; i < xconf->tx_thread_count; i++) {
            struct TxThread *parms = &tx_thread[i];

            if (status_item.cur_count > parms->my_index)
                status_item.cur_count = parms->my_index;

            if (status_item.repeat_count > parms->my_repeat)
                status_item.repeat_count = parms->my_repeat;

            status_item.cur_pps    += parms->throttler->current_rate;
            status_item.total_sent += parms->total_sent;
        }

        /**
         * Rx handle queue is the bottle-neck, we got the most severe one.
         */
        status_item.rx_queue_ratio = 100.0;
        if (rx_thread->handle_q) {
            for (unsigned i=0; i<xconf->rx_handler_count; i++) {
                rx_free_entries = rte_ring_free_count(rx_thread->handle_q[i]);
                rx_queue_ratio_tmp = rx_free_entries*100.0 /
                    (double)(xconf->dispatch_buf_count);

                if (status_item.rx_queue_ratio>rx_queue_ratio_tmp)
                    status_item.rx_queue_ratio = rx_queue_ratio_tmp;
            }
        }

        /**
         * Tx handle queue maybe short if something wrong.
         */
        tx_free_entries            = rte_ring_free_count(xconf->stack->transmit_queue);
        status_item.tx_queue_ratio = tx_free_entries*100.0/(double)xconf->stack_buf_count;

        /* Note: This is how we tell the Tx has ended */
        if (xconf->is_infinite) {
            if (xconf->repeat && status_item.repeat_count>=xconf->repeat)
                time_to_finish_tx = 1;
        } else {
            if (status_item.cur_count >= range)
                time_to_finish_tx = 1;
        }

        /**
         * additional status from scan module
         */
        status_item.add_status[0] = '\0';
        xconf->scan_module->status_cb(status_item.add_status);

        /**
         * update other status item fields
         */
        status_item.total_successed = xconf->out_conf.total_successed;
        status_item.total_failed    = xconf->out_conf.total_failed;
        status_item.total_info      = xconf->out_conf.total_info;
        status_item.total_tm_event  = rx_thread->total_tm_event;
        status_item.max_count       = range;
        status_item.print_in_json   = xconf->is_status_ndjson;

        xtatus_print(&status, &status_item);

        /* Sleep for almost a second */
        pixie_mssleep(350);
    }

    /*
     * If we haven't completed the scan, then save the resume
     * information.
     */
    if (status_item.cur_count < range && !xconf->is_infinite && !xconf->is_noresume) {
        xconf->resume.index = status_item.cur_count;
        xconf_save_state(xconf);
    }

    /*
     * Now Tx Threads have breaked out the main loop of sending because of
     * `time_to_finish_tx` and go into loop of `stack_flush_packets` before `time_to_finish_rx`.
     * Rx Thread exits just by our setting of `time_to_finish_rx` according to time
     * waiting.
     * So `time_to_finish_rx` is the important signal both for Tx/Rx Thread to exit.
     */
    now = time(0);
    for (;;) {

        /* Find the min-index, repeat and rate */
        status_item.total_sent   = 0;
        status_item.cur_pps      = 0.0;
        status_item.cur_count    = UINT64_MAX;
        status_item.repeat_count = UINT64_MAX;
        for (unsigned i = 0; i < xconf->tx_thread_count; i++) {
            struct TxThread *parms = &tx_thread[i];

            if (status_item.cur_count > parms->my_index)
                status_item.cur_count = parms->my_index;

            if (status_item.repeat_count > parms->my_repeat)
                status_item.repeat_count = parms->my_repeat;

            status_item.cur_pps    += parms->throttler->current_rate;
            status_item.total_sent += parms->total_sent;
        }

        /**
         * Rx handle queue is the bottle-neck, we got the most severe one.
         */
        status_item.rx_queue_ratio = 100.0;
        if (rx_thread->handle_q) {
            for (unsigned i=0; i<xconf->rx_handler_count; i++) {
                rx_free_entries = rte_ring_free_count(rx_thread->handle_q[i]);
                rx_queue_ratio_tmp = rx_free_entries*100.0 /
                    (double)(xconf->dispatch_buf_count);

                if (status_item.rx_queue_ratio>rx_queue_ratio_tmp)
                    status_item.rx_queue_ratio = rx_queue_ratio_tmp;
            }
        }

        /**
         * Tx handle queue maybe short if something wrong.
         */
        tx_free_entries            = rte_ring_free_count(xconf->stack->transmit_queue);
        status_item.tx_queue_ratio = tx_free_entries*100.0/(double)xconf->stack_buf_count;

        /**
         * additional status from scan module
         */
        status_item.add_status[0] = '\0';
        xconf->scan_module->status_cb(status_item.add_status);

        /**
         * update other status item fields
         */
        status_item.total_successed = xconf->out_conf.total_successed;
        status_item.total_failed    = xconf->out_conf.total_failed;
        status_item.total_info      = xconf->out_conf.total_info;
        status_item.total_tm_event  = rx_thread->total_tm_event;
        status_item.max_count       = range;
        status_item.print_in_json   = xconf->is_status_ndjson;
        status_item.exiting_secs    = xconf->wait - (time(0) - now);

        xtatus_print(&status, &status_item);

        /*no more waiting or too many <ctrl-c>*/
        if (time(0) - now >= xconf->wait || time_to_finish_rx) {
            LOG(LEVEL_DEBUG, "telling threads to exit..."
                "                                           \n");
            time_to_finish_rx = 1;
            break;
        }

        pixie_mssleep(350);
    }

    for (unsigned i = 0; i < xconf->tx_thread_count; i++) {
        struct TxThread *parms = &tx_thread[i];
        pixie_thread_join(parms->thread_handle_xmit);
    }
    pixie_thread_join(rx_thread->thread_handle_recv);

    uint64_t usec_now = pixie_gettime();
    LOG(LEVEL_OUT, "\n%u milliseconds elapsed\n",
            (unsigned)((usec_now - usec_start) / 1000));

    /*
     * Now cleanup everything
     */
    xtatus_finish(&status);

    xconf->scan_module->close_cb();

    if (xconf->probe_module) {
        xconf->probe_module->close_cb();
    }

    output_close(&xconf->out_conf);

    free(tx_thread);

    if (xconf->is_fast_timeout) {
        ft_close_table(xconf->ft_table);
        xconf->ft_table = NULL;
    }

    rawsock_close_adapter(xconf->nic.adapter);

    LOG(LEVEL_INFO, "all threads have exited                    \n");

    return 0;
}

/***************************************************************************
 ***************************************************************************/
int main(int argc, char *argv[]) {

    /*init logger*/
    LOG_init();

    struct Xconf xconf[1];
    memset(xconf, 0, sizeof(xconf));

    int has_target_addresses = 0;
    int has_target_ports     = 0;

#if defined(WIN32)
  {
    WSADATA x;
    WSAStartup(0x101, &x);
  }
#endif

    usec_start = pixie_gettime();
    global_now = time(0);

    /* Set system to report debug information on crash */
    int is_backtrace = 1;
    for (unsigned i = 1; i < (unsigned)argc; i++) {
        if (strcmp(argv[i], "--nobacktrace") == 0)
            is_backtrace = 0;
    }
    if (is_backtrace)
        pixie_backtrace_init(argv[0]);

    //=================================================Define default params
    xconf->blackrock_rounds                 = XCONF_DFT_BLACKROCK_ROUND;
    xconf->tx_thread_count                  = XCONF_DFT_TX_THD_COUNT;
    xconf->rx_handler_count                 = XCONF_DFT_RX_HDL_COUNT;
    xconf->stack_buf_count                  = XCONF_DFT_STACK_BUF_COUNT;
    xconf->dispatch_buf_count               = XCONF_DFT_DISPATCH_BUF_COUNT;
    xconf->max_rate                         = XCONF_DFT_MAX_RATE;
    xconf->dedup_win                        = XCONF_DFT_DEDUP_WIN;
    xconf->shard.one                        = XCONF_DFT_SHARD_ONE;
    xconf->shard.of                         = XCONF_DFT_SHARD_OF;
    xconf->ft_spec                          = XCONF_DFT_FT_SPEC;
    xconf->wait                             = XCONF_DFT_WAIT;
    xconf->nic.snaplen                      = XCONF_DFT_SNAPLEN;
    xconf->max_packet_len                   = XCONF_DFT_MAX_PKT_LEN;

    xconf_command_line(xconf, argc, argv);
    if (xconf->seed == 0)
        xconf->seed = get_one_entropy(); /* entropy for randomness */

    /* a separate "raw socket" initialization step for Windows and PF_RING. */
    if (pcap_init() != 0)
        LOG(LEVEL_ERROR, "libpcap: failed to load\n");

    rawsock_init();

    has_target_addresses = massip_has_ipv4_targets(&xconf->targets) ||
                           massip_has_ipv6_targets(&xconf->targets);
    has_target_ports     = massip_has_target_ports(&xconf->targets);
    massip_apply_excludes(&xconf->targets, &xconf->exclude);
    if (!has_target_ports) {
        massip_add_port_string(&xconf->targets, "o:0", 0);

        // LOG(LEVEL_HINT, "NOTE: no ports were specified, use default other proto port 0.\n");
        // LOG(LEVEL_HINT, " ignored if the ScanModule does not need port. (eg. "
        //     "icmp, arp, ndp, etc.)\n");
        // LOG(LEVEL_HINT, " or try something like \"-p 80,8000-9000\"\n");
    }

    /* Optimize target selection so it's a quick binary search instead
     * of walking large memory tables. When we scan the entire Internet
     * our --excludefile will chop up our pristine 0.0.0.0/0 range into
     * hundreds of subranges. This allows us to grab addresses faster. */
    massip_optimize(&xconf->targets);

    /* FIXME: we only support 63-bit scans at the current time.
     */
    if (massint128_bitcount(massip_range(&xconf->targets)) > 63) {
        LOG(LEVEL_ERROR,
            "scan range too large, max is 63-bits, requested is %u "
            "bits\n",
            massint128_bitcount(massip_range(&xconf->targets)));
        LOG(LEVEL_HINT,
            "scan range is number of IP addresses times "
            "number of ports\n");
        LOG(LEVEL_HINT, "IPv6 subnet must be at least /66 \n");
        exit(1);
    }

    switch (xconf->op) {
    case Operation_Default:
        xconf_set_parameter(xconf, "usage", "true");
        break;

    case Operation_Scan:
        if (rangelist_count(&xconf->targets.ipv4) == 0 &&
            massint128_is_zero(range6list_count(&xconf->targets.ipv6))) {
            LOG(LEVEL_ERROR, "target IP address list empty\n");
            if (has_target_addresses) {
                LOG(LEVEL_ERROR, "all addresses were removed by exclusion ranges\n");
            } else {
                LOG(LEVEL_HINT, "try something like \"--range 10.0.0.0/8\"\n");
                LOG(LEVEL_OUT, "    or \"--range 192.168.0.100-192.168.0.200\"\n");
            }
            exit(1);
        }
        if (rangelist_count(&xconf->targets.ports) == 0 && has_target_ports) {
            LOG(LEVEL_ERROR, " all ports were removed by exclusion ranges\n");
            break;
        }

        _main_scan(xconf);

        break;

    case Operation_Echo:
        xconf_echo(xconf, stdout);
        break;

    case Operation_DebugIF:
        rawsock_selftest_if(xconf->nic.ifname);
        break;

    case Operation_ListCidr:
        xconf_echo_cidr(xconf, stdout);
        break;

    case Operation_ListRange:
        listrange(xconf);
        break;

    case Operation_ListTargets:
        listip(xconf);
        return 0;

    case Operation_ListAdapters:
        rawsock_list_adapters();
        break;

    case Operation_ListScanModules:
        list_all_scan_modules();
        break;

    case Operation_HelpScanModule:
        help_scan_module(xconf->scan_module);
        break;

    case Operation_ListProbeModules:
        list_all_probe_modules();
        break;

    case Operation_HelpProbeModule:
        help_probe_module(xconf->probe_module);
        break;

    case Operation_ListOutputModules:
        list_all_output_modules();
        break;

    case Operation_HelpOutputModule:
        help_output_module(xconf->out_conf.output_module);
        break;

    case Operation_PrintHelp:
        xconf_print_help();
        break;

    case Operation_PrintIntro:
        xconf_print_intro();
        break;

    case Operation_PrintVersion:
        xconf_print_version();
        break;

    case Operation_Selftest:
        xconf_selftest();
        break;

    case Operation_Benchmark:
        xconf_benchmark(xconf->blackrock_rounds);
        break;
    }

    /*close logger*/
    LOG_close();

    return 0;
}
