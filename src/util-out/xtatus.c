#include "xtatus.h"
#include "../pixie/pixie-timer.h"
#include "../util-misc/cross.h"
#include "../globals.h"
#include "../util-data/safe-string.h"
#include "../util-out/logger.h"
#include "../util-out/xprint.h"

#include <stdio.h>

void xtatus_print(Xtatus *xtatus, XtatusItem *item) {
    const char *fmt;
    double      elapsed_time;
    double      rate;
    double      now;
    double      percent_done      = 0.0;
    double      time_remaining    = 0.0;
    uint64_t    current_successed = 0;
    uint64_t    current_sent      = 0;
    double      successed_rate    = 0.0;
    double      sent_rate         = 0.0;
    double      hit_rate          = 0.0;
    double      kpps              = item->cur_pps / 1000;

    const char *json_fmt_infinite = "{"
                                    "\"state\":\"infinite\","
                                    "\"rate\":"
                                    "{"
                                    "\"kpps\":%.2f,"
                                    "\"pps\":%.2f,"
                                    "\"sent ps\":%.0f,"
                                    "\"successed ps\":%.0f,"
                                    "},"
                                    "\"sent\":%" PRIu64 ","
                                    "\"repeat\":%" PRIu64 ","
                                    "\"tm_event\":%" PRIu64 ","
                                    "\"txq\":%.2f%%,"
                                    "\"rxq\":%.2f%%,"
                                    "\"hit\":%.2f%%,"
                                    "\"add status\":\"%s\""
                                    "}\n";

    /*waiting state for infinite*/
    const char *json_fmt_exiting = "{"
                                   "\"state\":\"exiting\","
                                   "\"seconds\":%d,"
                                   "\"rate\":"
                                   "{"
                                   "\"kpps\":%.2f,"
                                   "\"pps\":%.2f,"
                                   "\"sent ps\":%.0f,"
                                   "\"successed ps\":%.0f,"
                                   "},"
                                   "\"sent\":%" PRIu64 ","
                                   "\"tm_event\":%" PRIu64 ","
                                   "\"txq\":%.2f%%,"
                                   "\"rxq\":%.2f%%,"
                                   "\"hit\":%.2f%%,"
                                   "\"add status\":\"%s\""
                                   "}\n";

    const char *json_fmt_waiting = "{"
                                   "\"state\":\"waiting\","
                                   "\"rate\":"
                                   "{"
                                   "\"kpps\":%.2f,"
                                   "\"pps\":%.2f"
                                   "},"
                                   "\"progress\":"
                                   "{"
                                   "\"percent\":%.2f,"
                                   "\"seconds\":%d,"
                                   "\"successed\":%" PRIu64 ","
                                   "\"failed\":%" PRIu64 ","
                                   "\"info\":%" PRIu64 ","
                                   "\"tm_event\":%" PRIu64 ","
                                   "\"txq\":%.2f%%,"
                                   "\"rxq\":%.2f%%,"
                                   "\"hit\":%.2f%%,"
                                   "\"transmit\":"
                                   "{"
                                   "\"sent\":%" PRIu64 ","
                                   "\"total\":%" PRIu64 ","
                                   "\"remaining\":%" PRIu64 ""
                                   "}"
                                   "},"
                                   "\"add status\":\"%s\""
                                   "}\n";

    const char *json_fmt_running = "{"
                                   "\"state\":\"running\","
                                   "\"rate\":"
                                   "{"
                                   "\"kpps\":%.2f,"
                                   "\"pps\":%.2f"
                                   "},"
                                   "\"progress\":"
                                   "{"
                                   "\"percent\":%.2f,"
                                   "\"eta\":"
                                   "{"
                                   "\"hours\":%u,"
                                   "\"mins\":%u,"
                                   "\"seconds\":%u"
                                   "},"
                                   "\"transmit\":"
                                   "{"
                                   "\"sent\":%" PRIu64 ","
                                   "\"total\":%" PRIu64 ","
                                   "\"remaining\":%" PRIu64 ""
                                   "},"
                                   "\"successed\":%" PRIu64 ","
                                   "\"failed\":%" PRIu64 ","
                                   "\"info\":%" PRIu64 ","
                                   "\"tm_event\":%" PRIu64 ","
                                   "\"txq\":%.2f%%,"
                                   "\"rxq\":%.2f%%,"
                                   "\"hit\":%.2f%%"
                                   "},"
                                   "\"add status\":\"%s\""
                                   "}\n";

    /*
     * ####  FUGGLY TIME HACK  ####
     *
     * PF_RING doesn't timestamp packets well, so we can't base time from
     * incoming packets. Checking the time ourself is too ugly on per-packet
     * basis. Therefore, we are going to create a global variable that keeps
     * the time, and update that variable whenever it's convenient. This
     * is one of those convenient places.
     */
    global_now = time(0);

    /* Get the time. NOTE: this is CLOCK_MONOTONIC_RAW on Linux, not
     * wall-clock time. */
    now = (double)pixie_gettime();

    /* Figure how many SECONDS have elapsed, in a floating point value.
     * Since the above timestamp is in microseconds, we need to
     * shift it by 1-million
     */
    elapsed_time = (now - xtatus->last.clock) / 1000000.0;
    if (elapsed_time <= 0)
        return;

    /* Figure out the "packets-per-second" number, which is just:
     *
     *  rate = packets_sent / elapsed_time;
     */
    rate = (item->cur_count - xtatus->last.count) * 1.0 / elapsed_time;

    /*
     * Smooth the number by averaging over the last several seconds
     */
    xtatus->last_rates[xtatus->last_count++ & (XTS_RATE_CACHE - 1)] = rate;
    rate                                                            = 0;
    for (unsigned i = 0; i < XTS_RATE_CACHE; i++) {
        rate += xtatus->last_rates[i];
    }
    rate /= XTS_RATE_CACHE;

    /*
     * Calculate "percent-done", which is just the total number of
     * packets sent divided by the number we need to send.
     */
    if (item->max_count)
        percent_done = (double)(item->cur_count * 100.0 / item->max_count);

    /*
     * Calculate the time remaining in the scan
     */
    if (item->max_count)
        time_remaining =
            (1.0 - percent_done / 100.0) * (item->max_count / rate);

    /*
     * some other stats
     */
    if (item->total_successed) {
        current_successed = item->total_successed - xtatus->total_successed;
        xtatus->total_successed = item->total_successed;
        successed_rate          = (1.0 * current_successed) / elapsed_time;
    }
    if (item->total_sent) {
        current_sent       = item->total_sent - xtatus->total_sent;
        xtatus->total_sent = item->total_sent;
        sent_rate          = (1.0 * current_sent) / elapsed_time;
        hit_rate = (100.0 * item->total_successed) / ((double)item->total_sent);
    }

    /*
     * Print the message to <stderr> so that <stdout> can be redirected
     * to a file (<stdout> reports what systems were found).
     */
    if (xtatus->is_infinite) {
        if (time_to_finish_tx) {
            if (item->print_in_json) {
                fmt = json_fmt_exiting;

                LOG(LEVEL_OUT, fmt, (int)item->exiting_secs, kpps,
                    item->cur_pps, sent_rate, successed_rate, item->cur_count,
                    item->total_tm_event, item->tx_queue_ratio,
                    item->rx_queue_ratio, hit_rate, item->add_status);
            } else {
                fmt = "rate:%6.2f-kpps, waiting %d-secs, sent/s=%.0f, "
                      "[+]/s=%.0f";

                LOG(LEVEL_OUT, fmt, kpps, (int)item->exiting_secs, sent_rate,
                    successed_rate);

                if (xtatus->print_ft_event) {
                    fmt = ", tm_event=%6$" PRIu64;
                    LOG(LEVEL_OUT, fmt, item->total_tm_event);
                }

                if (xtatus->print_queue) {
                    fmt = ", %5.2f%%-txq, %5.2f%%-rxq";
                    LOG(LEVEL_OUT, fmt, item->tx_queue_ratio,
                        item->rx_queue_ratio);
                }

                if (xtatus->print_hit_rate) {
                    fmt = ", %5.2f%%-hit";
                    LOG(LEVEL_OUT, fmt, hit_rate);
                }

                if (item->add_status[0]) {
                    fmt = ", %s";
                    LOG(LEVEL_OUT, fmt, item->add_status);
                }
            }
        } else {
            if (item->print_in_json) {
                fmt = json_fmt_infinite;

                LOG(LEVEL_OUT, fmt, kpps, item->cur_pps, sent_rate,
                    successed_rate, item->cur_count, item->repeat_count,
                    item->total_tm_event, item->tx_queue_ratio,
                    item->rx_queue_ratio, hit_rate, item->add_status);
            } else {
                fmt = "rate:%6.2f-kpps, round=%" PRIu64
                      ", sent/s=%.0f, [+]/s=%.0f";

                LOG(LEVEL_OUT, fmt, kpps, item->repeat_count + 1, sent_rate,
                    successed_rate);

                if (xtatus->print_ft_event) {
                    fmt = ", tm_event=%6$" PRIu64;
                    LOG(LEVEL_OUT, fmt, item->total_tm_event);
                }

                if (xtatus->print_queue) {
                    fmt = ", %5.2f%%-txq, %5.2f%%-rxq";
                    LOG(LEVEL_OUT, fmt, item->tx_queue_ratio,
                        item->rx_queue_ratio);
                }

                if (xtatus->print_hit_rate) {
                    fmt = ", %5.2f%%-hit";
                    LOG(LEVEL_OUT, fmt, hit_rate);
                }

                if (item->add_status[0]) {
                    fmt = ", %s";
                    LOG(LEVEL_OUT, fmt, item->add_status);
                }
            }
        }
    } else {
        if (time_to_finish_tx) {
            if (item->print_in_json) {
                fmt = json_fmt_waiting;

                LOG(LEVEL_OUT, fmt, item->cur_pps / 1000.0, item->cur_pps,
                    percent_done, (int)item->exiting_secs,
                    item->total_successed, item->total_failed, item->total_info,
                    item->total_tm_event, item->tx_queue_ratio,
                    item->rx_queue_ratio, hit_rate, item->cur_count,
                    item->max_count, item->max_count - item->cur_count,
                    item->add_status);
            } else {
                fmt = "rate:%6.2f-kpps, %5.2f%% done, waiting %d-secs, "
                      "[+]=%" PRIu64 ", [x]=%" PRIu64;

                LOG(LEVEL_OUT, fmt, item->cur_pps / 1000.0, percent_done,
                    (int)item->exiting_secs, item->total_successed,
                    item->total_failed);

                if (xtatus->print_info_num) {
                    fmt = ", [*]=%" PRIu64;
                    LOG(LEVEL_OUT, fmt, item->total_info);
                }

                if (xtatus->print_ft_event) {
                    fmt = ", tm_event=%" PRIu64;
                    LOG(LEVEL_OUT, fmt, item->total_tm_event);
                }

                if (xtatus->print_queue) {
                    fmt = ", %5.2f%%-txq, %5.2f%%-rxq";
                    LOG(LEVEL_OUT, fmt, item->tx_queue_ratio,
                        item->rx_queue_ratio);
                }

                if (xtatus->print_hit_rate) {
                    fmt = ", %5.2f%%-hit";
                    LOG(LEVEL_OUT, fmt, hit_rate);
                }

                if (item->add_status[0]) {
                    fmt = ", %s";
                    LOG(LEVEL_OUT, fmt, item->add_status);
                }
            }
        } else {
            if (item->print_in_json) {
                fmt = json_fmt_running;

                LOG(LEVEL_OUT, fmt, item->cur_pps / 1000.0, item->cur_pps,
                    percent_done, (unsigned)(time_remaining / 60 / 60),
                    (unsigned)(time_remaining / 60) % 60,
                    (unsigned)(time_remaining) % 60, item->cur_count,
                    item->max_count, item->max_count - item->cur_count,
                    item->total_successed, item->total_failed, item->total_info,
                    item->total_tm_event, item->tx_queue_ratio,
                    item->rx_queue_ratio, hit_rate, item->add_status);
            } else {
                fmt = "rate:%6.2f-kpps, %5.2f%% done,%4u:%02u:%02u remaining, "
                      "[+]=%" PRIu64 ", [x]=%" PRIu64;

                LOG(LEVEL_OUT, fmt, item->cur_pps / 1000.0, percent_done,
                    (unsigned)(time_remaining / 60 / 60),
                    (unsigned)(time_remaining / 60) % 60,
                    (unsigned)(time_remaining) % 60, item->total_successed,
                    item->total_failed);

                if (xtatus->print_info_num) {
                    fmt = ", [*]=%" PRIu64;
                    LOG(LEVEL_OUT, fmt, item->total_info);
                }

                if (xtatus->print_ft_event) {
                    fmt = ", tm_event=%" PRIu64;
                    LOG(LEVEL_OUT, fmt, item->total_tm_event);
                }

                if (xtatus->print_queue) {
                    fmt = ", %5.2f%%-txq, %5.2f%%-rxq";
                    LOG(LEVEL_OUT, fmt, item->tx_queue_ratio,
                        item->rx_queue_ratio);
                }

                if (xtatus->print_hit_rate) {
                    fmt = ", %5.2f%%-hit";
                    LOG(LEVEL_OUT, fmt, hit_rate);
                }

                if (item->add_status[0]) {
                    fmt = ", %s";
                    LOG(LEVEL_OUT, fmt, item->add_status);
                }
            }
        }
    }
    LOG(LEVEL_OUT, "\r");
    fflush(stderr);

    /*
     * Remember the values to be diffed against the next time around
     */
    xtatus->last.clock = now;
    xtatus->last.count = item->cur_count;
}

/***************************************************************************
 ***************************************************************************/
void xtatus_finish(Xtatus *xtatus) {
    UNUSEDPARM(xtatus);
    LOG(LEVEL_OUT, "\n");
}

/***************************************************************************
 ***************************************************************************/
void xtatus_start(Xtatus *xtatus) {
    memset(xtatus, 0, sizeof(*xtatus));
    xtatus->last.clock = clock();
    xtatus->last.time  = time(0);
    xtatus->last.count = 0;
}
