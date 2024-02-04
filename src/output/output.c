#include <stdio.h>

#include "output.h"

void
output_result(
    const struct Output *output,
    const struct PreprocessedInfo *parsed,
    time_t timestamp, unsigned successed,
    const char *classification, const char *report)
{
    if (!successed && !output->is_show_failed)
        return;

    ipaddress ip_me = parsed->dst_ip;
    ipaddress ip_them = parsed->src_ip;
    unsigned port_me = parsed->port_dst;
    unsigned port_them = parsed->port_src;

    ipaddress_formatted_t ip_me_fmt = ipaddress_fmt(ip_me);
    ipaddress_formatted_t ip_them_fmt = ipaddress_fmt(ip_them);

    unsigned count = 0;

    if (parsed->found==FOUND_ICMP || parsed->found==FOUND_ARP) {
        char fmt[] = "%s on host: %-15s because of \"%s\"";
        count = fprintf(stdout, fmt,
            successed?"Success":"Failure",
            ip_them_fmt.string,
            classification);
    } else {
        char fmt[] = "%s on host: %-15s port: %-5u because of \"%s\"";
        count = fprintf(stdout, fmt,
            successed?"Success":"Failure",
            ip_them_fmt.string, port_them,
            classification);
    }
    
    if (output->is_show_report) {
        count += fprintf(stdout, ". Report: [%s]", report);
    }
    
    
    if (count < 90)
            fprintf(stdout, "%.*s", (int)(89-count),
                    "                                          "
                    "                                          ");

    fprintf(stdout, "\n");
    fflush(stdout);
}