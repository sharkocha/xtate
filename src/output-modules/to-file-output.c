#include "output-modules.h"

#include "../util/logger.h"
#include "../pixie/pixie-file.h"

static char fmt_host[]   = "%s host: %-15s";
static char fmt_port[]   = " port: %-5u";
static char fmt_cls []   = " \"%s\"";
static char fmt_reason[] = " because of \"%s\"";
static char fmt_report[] = "  Report: %s";

extern struct OutputModule ToFileOutput; /*for internal x-ref*/

static FILE *file;

static int
tofile_init(const struct Output *out)
{

    int err = pixie_fopen_shareable(
        &file, out->output_filename, out->is_append);

    if (err != 0 || file == NULL) {
        LOG(LEVEL_ERROR, "[-] ToFileOutput: could not open file %s for %s\n",
            out->output_filename, out->is_append?"appending":"writing");
        perror(out->output_filename);
        return -1;
    }

    return 1;
}

static void
tofile_result(const struct Output *out, const struct OutputItem *item)
{
    if (item->level==Output_INFO && !out->is_show_info)
        return;
    if (item->level==Output_FAILURE && !out->is_show_failed)
        return;
    
    ipaddress_formatted_t ip_them_fmt = ipaddress_fmt(item->ip_them);

    unsigned count = 0;

    switch (item->level)
    {
    case Output_SUCCESS:
        count = fprintf(file, fmt_host, "[+]", ip_them_fmt.string);
        break;
    case Output_FAILURE:
        count = fprintf(file, fmt_host, "[x]", ip_them_fmt.string);
        break;
    case Output_INFO:
        count = fprintf(file, fmt_host, "[*]", ip_them_fmt.string);
        break;
    default:
        return;
    }
    
    if (item->port_them) {
        count += fprintf(file, fmt_port, item->port_them);
    }

    if (item->classification[0]) {
        count += fprintf(file, fmt_cls, item->classification);
    }

    if (item->reason[0]) {
        count += fprintf(file, fmt_reason, item->reason);
    }

    if (item->report[0]) {
        count += fprintf(file, fmt_report, item->report);
    }

    fprintf(file, "\n");
}

static void
tofile_close(const struct Output *out)
{
    fflush(file);
    fclose(file);
}

struct OutputModule ToFileOutput = {
    .name               = "to-file",
    .need_file          = 1,
    .params             = NULL,
    .init_cb            = &tofile_init,
    .result_cb          = &tofile_result,
    .close_cb           = &tofile_close,
    .desc               =
        "ToFileOutput save results same as stdout to specified file without color.",
};