#ifndef OUTPUT_H
#define OUTPUT_H

#include <time.h>
#include <ctype.h>
#include <stdio.h>

#include "../massip/massip-addr.h"

#define OUTPUT_RSN_LEN   30
#define OUTPUT_CLS_LEN   30
#define OUTPUT_RPT_LEN 2048

struct Output{
    char                  output_filename[256];
    FILE                 *output_file;
    void                 *mutex; /*avoid messing the output*/
    uint64_t             *total_successed;
    uint64_t             *total_failed;
    unsigned              is_append:1;
    unsigned              is_interactive:1;
    unsigned              is_show_failed:1;
    unsigned              is_show_info:1;
};

enum OutputLevel{
    Output_INFO = 0,
    Output_FAILURE,
    Output_SUCCESS,
};

struct OutputItem{
    enum OutputLevel      level;
    ipaddress             ip_them;
    unsigned              port_them; /*no outputting if zero*/
    ipaddress             ip_me;
    unsigned              port_me; /*no outputting if zero*/
    /*no outputting if start with zero*/
    char                  reason[OUTPUT_RSN_LEN];
    char                  classification[OUTPUT_CLS_LEN];
    char                  report[OUTPUT_RPT_LEN];
    time_t                timestamp;
    unsigned              no_output:1;
};

/*prepare for outputing results*/
void
output_init(struct Output *output);

void
output_result(
    const struct Output *output,
    const struct OutputItem *item);

/*destroy resources of output*/
void
output_close(struct Output *output);

#endif