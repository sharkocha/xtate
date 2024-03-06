#include "xtatus.h"
#include "pixie/pixie-timer.h"
#include "util/unusedparm.h"
#include "globals.h"
#include "util/mas-safefunc.h"
#include "util/bool.h"
#include <stdio.h>


/***************************************************************************
 * Print a xtatus message about once-per-second to the command-line. This
 * algorithm is a little funky because checking the timestamp on EVERY
 * packet is slow.
 ***************************************************************************/
void
xtatus_print(
    struct Xtatus *xtatus,
    uint64_t count,
    uint64_t max_count,
    double pps,
    uint64_t total_successed,
    uint64_t total_failed,
    uint64_t total_sent,
    uint64_t total_tm_event,
    uint64_t exiting,
    bool json_status)
{
    double elapsed_time;
    double rate;
    double now;
    double percent_done;
    double time_remaining;
    uint64_t current_successed = 0;
    uint64_t current_sent = 0;
    double successed_rate = 0.0;
    double sent_rate = 0.0;
    double kpps = pps / 1000;
    const char *fmt;

    /* Support for --json-status; does not impact legacy/default output */
    
    /**
     * {"state":"*","rate":{"kpps":24.99,"pps":24985.49,"synps": 27763,"ackps":4,"tcbps":4},"tcb": 33,"syn":246648}
     */
    const char* json_fmt_infinite =
    "{"
        "\"state\":\"*\","
        "\"rate\":"
        "{"
            "\"kpps\":%.2f,"
            "\"pps\":%.2f,"
            "\"sent ps\":%.0f,"
            "\"successed ps\":%.0f,"
        "},"
        "\"sent\":%," PRIu64
        "\"tm_event\":%" PRIu64
    "}\n";
    
    /**
     * {"state":"waiting","rate":{"kpps":0.00,"pps":0.00},"progress":{"percent":21.87,"seconds":4,"found":56,"syn":{"sent": 341436,"total":1561528,"remaining":1220092}}}
     */
    const char *json_fmt_waiting = 
    "{"
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
            "\"tm_event\":%" PRIu64 ","
            "\"transmit\":"
            "{"
                "\"sent\":%" PRIu64 ","
                "\"total\":%" PRIu64 ","
                "\"remaining\":%" PRIu64
            "}" 
        "}"
    "}\n";

    /**
     * {"state":"running","rate":{"kpps":24.92,"pps":24923.07},"progress":{"percent":9.77,"eta":{
     *      "hours":0,"mins":0,"seconds":55},"syn":{"sent": 152510,"total": 1561528,"remaining": 1409018},"found": 27}}
     */
    const char *json_fmt_running = 
    "{"
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
                "\"remaining\":%" PRIu64
            "}," 
            "\"successed\":%" PRIu64 ","
            "\"failed\":%" PRIu64 ","
            "\"tm_event\":%" PRIu64
        "}"
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
    elapsed_time = (now - xtatus->last.clock)/1000000.0;
    if (elapsed_time <= 0)
        return;

    /* Figure out the "packets-per-second" number, which is just:
     *
     *  rate = packets_sent / elapsed_time;
     */
    rate = (count - xtatus->last.count)*1.0/elapsed_time;

    /*
     * Smooth the number by averaging over the last 8 seconds
     */
     xtatus->last_rates[xtatus->last_count++ & 0x7] = rate;
     rate =     xtatus->last_rates[0]
                + xtatus->last_rates[1]
                + xtatus->last_rates[2]
                + xtatus->last_rates[3]
                + xtatus->last_rates[4]
                + xtatus->last_rates[5]
                + xtatus->last_rates[6]
                + xtatus->last_rates[7]
                ;
    rate /= 8;
    /*if (rate == 0)
        return;*/

    /*
     * Calculate "percent-done", which is just the total number of
     * packets sent divided by the number we need to send.
     */
    percent_done = (double)(count*100.0/max_count);


    /*
     * Calculate the time remaining in the scan
     */
    time_remaining  = (1.0 - percent_done/100.0) * (max_count / rate);

    /*
     * some other stats
     */
    if (total_successed) {
        current_successed = total_successed - xtatus->total_successed;
        xtatus->total_successed = total_successed;
        successed_rate = (1.0*current_successed)/elapsed_time;
    }
    if (total_sent) {
        current_sent = total_sent - xtatus->total_sent;
        xtatus->total_sent = total_sent;
        sent_rate = (1.0*current_sent)/elapsed_time;
    }

    /*
     * Print the message to <stderr> so that <stdout> can be redirected
     * to a file (<stdout> reports what systems were found).
     */

    if (xtatus->is_infinite) {
        if (json_status == 1) {
            fmt = json_fmt_infinite;

            fprintf(stderr,
                    fmt,
                    kpps,
                    pps,
                    sent_rate,
                    successed_rate,
                    count,
                    total_tm_event);
        } else {
            fmt = "rate:%6.2f-kpps, sent/s=%.0f, successed/s=%.0f , tm_event=%6$" PRIu64 "                \r";

            fprintf(stderr,
                    fmt,
                    kpps,
                    sent_rate,
                    successed_rate,
                    total_tm_event);
        }
    } else {
        if (is_tx_done) {
            if (json_status == 1) {
                fmt = json_fmt_waiting;

                fprintf(stderr,
                        fmt,
                        pps/1000.0,
                        pps,
                        percent_done,
                        (int)exiting,
                        total_successed,
                        total_failed,
                        total_tm_event,
                        count,
                        max_count,
                        max_count-count);
            } else {
                fmt = "rate:%6.2f-kpps, %5.2f%% done, waiting %d-secs, successed=%" PRIu64 ", failed=%" PRIu64 ", tm_event=%" PRIu64 "       \r";

                fprintf(stderr,
                        fmt,
                        pps/1000.0,
                        percent_done,
                        (int)exiting,
                        total_successed,
                        total_failed,
                        total_tm_event);
            }
            
        } else {
            if (json_status == 1) {
                fmt = json_fmt_running;

                fprintf(stderr,
                    fmt,
                    pps/1000.0,
                    pps,
                    percent_done,
                    (unsigned)(time_remaining/60/60),
                    (unsigned)(time_remaining/60)%60,
                    (unsigned)(time_remaining)%60,
                    count,
                    max_count,
                    max_count-count,
                    total_successed,
                    total_failed,
                    total_tm_event);
            } else {
                fmt = "rate:%6.2f-kpps, %5.2f%% done,%4u:%02u:%02u remaining, successed=%" PRIu64 ", failed=%" PRIu64 ", tm_event=%" PRIu64 "     \r";

                fprintf(stderr,
                    fmt,
                    pps/1000.0,
                    percent_done,
                    (unsigned)(time_remaining/60/60),
                    (unsigned)(time_remaining/60)%60,
                    (unsigned)(time_remaining)%60,
                    total_successed,
                    total_failed,
                    total_tm_event);
            }
        }
    }
    fflush(stderr);

    /*
     * Remember the values to be diffed against the next time around
     */
    xtatus->last.clock = now;
    xtatus->last.count = count;
}

/***************************************************************************
 ***************************************************************************/
void
xtatus_finish(struct Xtatus *xtatus)
{
    UNUSEDPARM(xtatus);
    fprintf(stderr,"\n");
}

/***************************************************************************
 ***************************************************************************/
void
xtatus_start(struct Xtatus *xtatus)
{
    memset(xtatus, 0, sizeof(*xtatus));
    xtatus->last.clock = clock();
    xtatus->last.time = time(0);
    xtatus->last.count = 0;
    xtatus->timer = 0x1;
}
