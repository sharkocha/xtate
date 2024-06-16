#include "output-modules.h"

#include "../util-out/logger.h"
#include "../util-data/safe-string.h"
#include "../pixie/pixie-file.h"

extern struct OutputModule CsvOutput; /*for internal x-ref*/

static FILE *file;

static const char header_csv[] =
"time,"
"level,"
"ip_proto,"
"ip_them,"
"port_them,"
"ip_me,"
"port_me,"
"classification,"
"reason,"
"report"
"\n"
;

static const char fmt_csv_prefix[] =
"\"%s\","
"\"%s\","
"\"%s\","
"\"%s\","
"%u,"
"\"%s\","
"%u,"
"\"%s\","
"\"%s\","
"\""
;

static const char fmt_csv_str_inffix[] =
"\'%s\':\'%s\',"
;

static const char fmt_csv_num_inffix[] =
"\'%s\':%s,"
;

static const char fmt_csv_suffix[] =
"\""
"\n"
;

static char format_time[32];

static bool
csv_init(const struct Output *out)
{

    int err = pixie_fopen_shareable(
        &file, out->output_filename, out->is_append);

    if (err != 0 || file == NULL) {
        LOG(LEVEL_ERROR, "CsvOutput: could not open file %s for %s.\n",
            out->output_filename, out->is_append?"appending":"writing");
        perror(out->output_filename);
        return false;
    }

    err = fputs(header_csv, file);

    if (err<0) {
        LOG(LEVEL_ERROR, "CsvOutput: could not write header to file.\n");
    }

    return true;
}

static void
csv_result(struct OutputItem *item)
{
    bool output_port = (item->ip_proto==IP_PROTO_TCP
        || item->ip_proto==IP_PROTO_UDP || item->ip_proto==IP_PROTO_SCTP);

    ipaddress_formatted_t ip_them_fmt = ipaddress_fmt(item->ip_them);
    ipaddress_formatted_t ip_me_fmt   = ipaddress_fmt(item->ip_me);

    iso8601_time_str(format_time, sizeof(format_time), &item->timestamp);

    int err = fprintf(file, fmt_csv_prefix,
        format_time,
        output_level_to_string(item->level),
        ip_proto_to_string(item->ip_proto),
        ip_them_fmt.string,
        output_port?item->port_them:0,
        ip_me_fmt.string,
        output_port?item->port_me:0,
        item->classification,
        item->reason);

    if (err<0) goto error;

    struct DataLink *pre = item->report.link;
    while (pre->next) {
        err = fprintf(file,
            pre->next->is_number?fmt_csv_num_inffix:fmt_csv_str_inffix,
            pre->next->name, pre->next->data);
        if (err<0) goto error;
        pre = pre->next;
    }

    /*at least one report, overwrite the last comma*/
    if (item->report.link->next) {
        fseek(file, -1, SEEK_CUR);
    }
    err = fprintf(file, fmt_csv_suffix);
    if (err<0) goto error;

    return;

error:
    LOG(LEVEL_ERROR, "CsvOutput: could not write result to file.\n");
}

static void
csv_close(const struct Output *out)
{
    fflush(file);
    fclose(file);
}

struct OutputModule CsvOutput = {
    .name               = "csv",
    .need_file          = 1,
    .params             = NULL,
    .init_cb            = &csv_init,
    .result_cb          = &csv_result,
    .close_cb           = &csv_close,
    .desc =
        "CsvOutput save results in Comma-Seperated Values(csv) format to "
        "specified file.\n"
        "NOTE: CsvOutput doesn't convert any escaped chars in result.",
};