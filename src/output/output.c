#include <stdio.h>

#include "output.h"
#include "../pixie/pixie-file.h"
#include "../pixie/pixie-threads.h"
#include "../util/logger.h"
#include "../util/xprint.h"

static char fmt_host[]   = "%s host: %-15s";
static char fmt_port[]   = " port: %-5u";
static char fmt_cls []   = " \"%s\"";
static char fmt_reason[] = " because of \"%s\"";
static char fmt_report[] = "  "XPRINT_CH_COLOR_YELLOW"Report: %s";

void
output_init(struct Output *output)
{
    if (output->output_filename[0]) {
        int err = pixie_fopen_shareable(
            &output->output_file, output->output_filename, output->is_append);

        if (err != 0 || output->output_file == NULL) {
            LOG(LEVEL_ERROR, "[-] output: could not open file %s for %s\n",
                output->output_filename, output->is_append?"appending":"writing");
            LOG(LEVEL_ERROR, "            output results to stdout now.\n");
            perror(output->output_filename);
            output->output_file = NULL;
        }
    }

    output->mutex = pixie_create_mutex();
}

/*Some special processes should be done when output to stdout for avoiding mess*/
static void
output_result_to_stdout(
    const struct Output *output,
    const struct OutputItem *item)
{
    if (item->level==Output_INFO && !output->is_show_info)
        return;
    if (item->level==Output_FAILURE && !output->is_show_failed)
        return;

    // ipaddress_formatted_t ip_me_fmt = ipaddress_fmt(item->ip_me);
    ipaddress_formatted_t ip_them_fmt = ipaddress_fmt(item->ip_them);

    unsigned count = 0;

    switch (item->level)
    {
    case Output_SUCCESS:
        count = fprintf(stdout, fmt_host,
            XPRINT_CH_COLOR_GREEN"[+]", ip_them_fmt.string);
        break;
    case Output_FAILURE:
        count = fprintf(stdout, fmt_host,
            XPRINT_CH_COLOR_RED"[x]", ip_them_fmt.string);
        break;
    case Output_INFO:
        count = fprintf(stdout, fmt_host,
            XPRINT_CH_COLOR_CYAN"[*]", ip_them_fmt.string);
        break;
    default:
        return;
    }

    
    if (item->port_them) {
        count += fprintf(stdout, fmt_port, item->port_them);
    }

    if (item->classification[0]) {
        count += fprintf(stdout, fmt_cls, item->classification);
    }

    if (item->reason[0]) {
        count += fprintf(stdout, fmt_reason, item->reason);
    }

    // fprintf(stdout, OUTPUT_COLOR_RESET);

    if (item->report[0]) {
        count += fprintf(stdout, fmt_report, item->report);
    }
    
    if (count < 120)
            fprintf(stdout, "%*s", (int)(120-count), "");

    fprintf(stdout, XPRINT_COLOR_RESET"\n");
    fflush(stdout);
}

static void
output_result_to_file(
    const struct Output *output,
    const struct OutputItem *item)
{
    if (item->level==Output_INFO && !output->is_show_info)
        return;
    if (item->level==Output_FAILURE && !output->is_show_failed)
        return;
    
    FILE *fp = output->output_file;

    // ipaddress_formatted_t ip_me_fmt = ipaddress_fmt(item->ip_me);
    ipaddress_formatted_t ip_them_fmt = ipaddress_fmt(item->ip_them);

    unsigned count = 0;

    switch (item->level)
    {
    case Output_SUCCESS:
        count = fprintf(stdout, fmt_host, "[+]", ip_them_fmt.string);
        break;
    case Output_FAILURE:
        count = fprintf(stdout, fmt_host, "[x]", ip_them_fmt.string);
        break;
    case Output_INFO:
        count = fprintf(stdout, fmt_host, "[*]", ip_them_fmt.string);
        break;
    default:
        return;
    }
    
    if (item->port_them) {
        count += fprintf(fp, fmt_port, item->port_them);
    }

    if (item->classification[0]) {
        count += fprintf(fp, fmt_cls, item->classification);
    }

    if (item->reason[0]) {
        count += fprintf(fp, fmt_reason, item->reason);
    }

    if (item->report[0]) {
        count += fprintf(fp, fmt_report, item->report);
    }

    fprintf(fp, "\n");
}

void
output_result(
    const struct Output *output,
    const struct OutputItem *item)
{
    if (item->no_output)
        return;

    pixie_acquire_mutex(output->mutex);

    if (item->level==Output_SUCCESS)
        (*(output->total_successed))++;
 
    if (item->level==Output_FAILURE)
        (*(output->total_failed))++;

    if (output->output_file) {
        output_result_to_file(output, item);
        if (output->is_interactive) {
            output_result_to_stdout(output, item);
        }
    } else {
        output_result_to_stdout(output, item);
    }

    pixie_release_mutex(output->mutex);
}

void
output_close(struct Output *output)
{
    if (output->output_file) {
        fflush(output->output_file);
        fclose(output->output_file);
        pixie_delete_mutex(output->mutex);
    }
}