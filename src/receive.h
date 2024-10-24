#ifndef RECEIVE_H
#define RECEIVE_H

#include "xconf.h"

/***************************************************************************
 * Correspond to a receive thread.
 ***************************************************************************/
typedef struct RxThreadConfig {
    /** This points to the central configuration. Note that it's 'const',
     * meaning that the thread cannot change the contents. That'd be
     * unsafe */
    const XConf   *xconf;
    /*start time info for packet trace*/
    double         pt_start;
    /*unhandled fast-timeout event*/
    uint64_t       total_tm_event;
    /*all queue from dispatch thread to handle threads*/
    PACKET_QUEUE **handle_q;
    /*queue from rx thread to dispatch thread*/
    PACKET_QUEUE  *dispatch_q;
    /*thread handler(id for process)*/
    size_t         thread_handle_recv;
    /*is finished*/
    bool           done_receiving;
} RxThread;

/***************************************************************************
 *
 * Asynchronous receive thread
 *
 ***************************************************************************/
void receive_thread(void *v);

#endif