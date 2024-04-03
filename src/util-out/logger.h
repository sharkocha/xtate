#ifndef LOGGER_H
#define LOGGER_H
#include "../massip/massip-addr.h"

#define LEVEL_ERROR   0
#define LEVEL_WARNING 1
#define LEVEL_INFO    2
#define LEVEL_DEBUG   3
#define LEVEL_DETAIL  4

void LOG(int level, const char *fmt, ...);
void LOGip(int level, ipaddress ip, unsigned port, const char *fmt, ...);
void LOGnet(unsigned port_me, ipaddress ip_them, const char *fmt, ...);
int LOGopenssl(int level);


void LOG_add_level(int level);
int LOG_get_level();

#endif