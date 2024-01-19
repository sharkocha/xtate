#ifndef TRANSMIT_H
#define TRANSMIT_H

#include "xconf.h"
#include "util/throttle.h"

/***************************************************************************
 * Correspond to a transmit thread.
 ***************************************************************************/
struct TxThread {
    /** This points to the central configuration. Note that it's 'const',
     * meaning that the thread cannot change the contents. That'd be
     * unsafe */
    const struct Xconf *xconf;

    /**
     * The index of the tx/rx thread
     */
    unsigned tx_index;

    /**
     * A copy of the master 'index' variable. This is just advisory for
     * other threads, to tell them how far we've gotten.
     */
    volatile uint64_t my_index;

    unsigned done_transmitting;

    struct Throttler throttler[1];

    uint64_t *total_syns;

    /**
     * Set to zero if no corresponded thread.
     */
    size_t thread_handle_xmit;
};


/***************************************************************************
 * This thread spews packets as fast as it can
 *
 *      THIS IS WHERE ALL THE EXCITEMENT HAPPENS!!!!
 *      90% of CPU cycles are in the function.
 *
 ***************************************************************************/
void
transmit_thread(void *v); /*aka. scanning_thread() */

#endif