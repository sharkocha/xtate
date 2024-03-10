/*
    RING

    DERIVED FROM INTEL DPDK
    DERIVED FROM FREEBSD BUFRING
*/
/*-
 *   BSD LICENSE
 *
 *   Copyright(c) 2010-2013 Intel Corporation. All rights reserved.
 *   All rights reserved.
 *
 *   Redistribution and use in source and binary forms, with or without
 *   modification, are permitted provided that the following conditions
 *   are met:
 *
 *     * Redistributions of source code must retain the above copyright
 *       notice, this list of conditions and the following disclaimer.
 *     * Redistributions in binary form must reproduce the above copyright
 *       notice, this list of conditions and the following disclaimer in
 *       the documentation and/or other materials provided with the
 *       distribution.
 *     * Neither the name of Intel Corporation nor the names of its
 *       contributors may be used to endorse or promote products derived
 *       from this software without specific prior written permission.
 *
 *   THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 *   "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 *   LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
 *   A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
 *   OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
 *   SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
 *   LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 *   DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 *   THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 *   (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 *   OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 *
 */

/*
 * Derived from FreeBSD's bufring.c
 *
 **************************************************************************
 *
 * Copyright (c) 2007,2008 Kip Macy kmacy@freebsd.org
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
 *
 * 1. Redistributions of source code must retain the above copyright notice,
 *    this list of conditions and the following disclaimer.
 *
 * 2. The name of Kip Macy nor the names of other
 *    contributors may be used to endorse or promote products derived from
 *    this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
 * AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT OWNER OR CONTRIBUTORS BE
 * LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 * CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 *
 ***************************************************************************/
#include "mas-safefunc.h"
#include <errno.h>
#include <signal.h>
#include <stdarg.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

#include "../pixie/pixie-threads.h"
#include "../pixie/pixie-timer.h"

#if 0
#include <rte_atomic.h>
#include <rte_branch_prediction.h>
#include <rte_common.h>
#include <rte_eal.h>
#include <rte_eal_memconfig.h>
#include <rte_errno.h>
#include <rte_launch.h>
#include <rte_lcore.h>
#include <rte_log.h>
#include <rte_memory.h>
#include <rte_memzone.h>
#include <rte_per_lcore.h>
#include <rte_spinlock.h>
#include <rte_string_fns.h>
#include <rte_tailq.h>
#endif

#include "rte-ring.h"

/* true if x is a power of 2 */
#define POWEROF2(x) ((((x)-1) & (x)) == 0)

/* create the ring */
struct rte_ring *rte_ring_create(unsigned count, unsigned flags) {
  struct rte_ring *r;
  size_t ring_size;

#if 0
    /* compilation-time checks */
    RTE_BUILD_BUG_ON((sizeof(struct rte_ring) &
              CACHE_LINE_MASK) != 0);
    RTE_BUILD_BUG_ON((offsetof(struct rte_ring, cons) &
              CACHE_LINE_MASK) != 0);
    RTE_BUILD_BUG_ON((offsetof(struct rte_ring, prod) &
              CACHE_LINE_MASK) != 0);
#ifdef RTE_LIBRTE_RING_DEBUG
    RTE_BUILD_BUG_ON((sizeof(struct rte_ring_debug_stats) &
              CACHE_LINE_MASK) != 0);
    RTE_BUILD_BUG_ON((offsetof(struct rte_ring, stats) &
              CACHE_LINE_MASK) != 0);
#endif
#endif

  /* count must be a power of 2 */
  if ((!POWEROF2(count)) || (count > RTE_RING_SZ_MASK)) {
    rte_errno = EINVAL;
    fprintf(stderr,
            "Requested size is invalid, must be power of 2, and "
            "do not exceed the size limit %u\n",
            RTE_RING_SZ_MASK);
    return NULL;
  }

  ring_size = count * sizeof(void *) + sizeof(struct rte_ring);

  r = (struct rte_ring *)malloc(ring_size);
  if (r == NULL)
    abort();

  /* init the ring structure */
  memset(r, 0, sizeof(*r));

  r->flags = flags;
  r->prod.watermark = count;
  r->prod.sp_enqueue = !!(flags & RING_F_SP_ENQ);
  r->cons.sc_dequeue = !!(flags & RING_F_SC_DEQ);
  r->prod.size = r->cons.size = count;
  r->prod.mask = r->cons.mask = count - 1;
  r->prod.head = r->cons.head = 0;
  r->prod.tail = r->cons.tail = 0;

  return r;
}

/*
 * change the high water mark. If *count* is 0, water marking is
 * disabled
 */
int rte_ring_set_water_mark(struct rte_ring *r, unsigned count) {
  if (count >= r->prod.size)
    return -EINVAL;

  /* if count is 0, disable the watermarking */
  if (count == 0)
    count = r->prod.size;

  r->prod.watermark = count;
  return 0;
}

/* dump the status of the ring on the console */
void rte_ring_dump(const struct rte_ring *r) {
#ifdef RTE_LIBRTE_RING_DEBUG
  struct rte_ring_debug_stats sum;
  unsigned lcore_id;
#endif

  printf("  flags=%x\n", r->flags);
  printf("  size=%u\n", r->prod.size);
  printf("  ct=%u\n", r->cons.tail);
  printf("  ch=%u\n", r->cons.head);
  printf("  pt=%u\n", r->prod.tail);
  printf("  ph=%u\n", r->prod.head);
  printf("  used=%u\n", rte_ring_count(r));
  printf("  avail=%u\n", rte_ring_free_count(r));
  if (r->prod.watermark == r->prod.size)
    printf("  watermark=0\n");
  else
    printf("  watermark=%u\n", r->prod.watermark);

    /* sum and dump statistics */
#ifdef RTE_LIBRTE_RING_DEBUG
  memset(&sum, 0, sizeof(sum));
  for (lcore_id = 0; lcore_id < RTE_MAX_LCORE; lcore_id++) {
    sum.enq_success_bulk += r->stats[lcore_id].enq_success_bulk;
    sum.enq_success_objs += r->stats[lcore_id].enq_success_objs;
    sum.enq_quota_bulk += r->stats[lcore_id].enq_quota_bulk;
    sum.enq_quota_objs += r->stats[lcore_id].enq_quota_objs;
    sum.enq_fail_bulk += r->stats[lcore_id].enq_fail_bulk;
    sum.enq_fail_objs += r->stats[lcore_id].enq_fail_objs;
    sum.deq_success_bulk += r->stats[lcore_id].deq_success_bulk;
    sum.deq_success_objs += r->stats[lcore_id].deq_success_objs;
    sum.deq_fail_bulk += r->stats[lcore_id].deq_fail_bulk;
    sum.deq_fail_objs += r->stats[lcore_id].deq_fail_objs;
  }
  printf("  size=%u\n", r->prod.size);
  printf("  enq_success_bulk=%" PRIu64 "\n", sum.enq_success_bulk);
  printf("  enq_success_objs=%" PRIu64 "\n", sum.enq_success_objs);
  printf("  enq_quota_bulk=%" PRIu64 "\n", sum.enq_quota_bulk);
  printf("  enq_quota_objs=%" PRIu64 "\n", sum.enq_quota_objs);
  printf("  enq_fail_bulk=%" PRIu64 "\n", sum.enq_fail_bulk);
  printf("  enq_fail_objs=%" PRIu64 "\n", sum.enq_fail_objs);
  printf("  deq_success_bulk=%" PRIu64 "\n", sum.deq_success_bulk);
  printf("  deq_success_objs=%" PRIu64 "\n", sum.deq_success_objs);
  printf("  deq_fail_bulk=%" PRIu64 "\n", sum.deq_fail_bulk);
  printf("  deq_fail_objs=%" PRIu64 "\n", sum.deq_fail_objs);
#else
  printf("  no statistics available\n");
#endif
}

/* dump the status of all rings on the console */
#if 0
void
rte_ring_list_dump(void)
{
    const struct rte_ring *mp;
    struct rte_ring_list *ring_list;

    /* check that we have an initialised tail queue */
    if ((ring_list =
         RTE_TAILQ_LOOKUP_BY_IDX(RTE_TAILQ_RING, rte_ring_list)) == NULL) {
        rte_errno = E_RTE_NO_TAILQ;
        return;
    }

    rte_rwlock_read_lock(RTE_EAL_TAILQ_RWLOCK);

    TAILQ_FOREACH(mp, ring_list, next) {
        rte_ring_dump(mp);
    }

    rte_rwlock_read_unlock(RTE_EAL_TAILQ_RWLOCK);
}
#endif

typedef size_t Element;
