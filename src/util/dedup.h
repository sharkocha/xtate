#ifndef DEDUP_H
#define DEDUP_H
#include "../massip/massip-addr.h"

struct DedupTable *
dedup_create(unsigned dedup_win);

void
dedup_destroy(struct DedupTable *table);

unsigned
dedup_is_duplicate(struct DedupTable *dedup,
    ipaddress ip_them, unsigned port_them,
    ipaddress ip_me, unsigned port_me, unsigned type);

/**
 * Simple unit test
 * @return 0 on success, 1 on failure.
 */
int dedup_selftest(void);


#endif
