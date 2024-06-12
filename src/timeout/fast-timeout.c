/**
 * This is a fast version of event-timeout. All expire time spec of events are
 * the same. So we just use a linked list to save and just pop events from head
 * instead of going around all the nodes.
 * 
 * TODO: Reduce num of system calls to time()
*/
#include "fast-timeout.h"
#include "../util-data/fine-malloc.h"

struct FHandler {
    time_t            spec;
    lfqueue_t        *queue;
    struct FEntry    *oldest; /*oldest event poped from queue*/
};

struct FEntry {
    /**
     * time the Entry was create
    */
    time_t timestamp;
    /**
     * point to correspond event or struct
    */
    void *event;
};

struct FTable {
    /**
     * What time spec elapses before now should an event be timeout
    */
    time_t       spec;
    lfqueue_t    queue_t;
};


struct FTable * ft_init_table(time_t spec)
{
    struct FTable *table = MALLOC(sizeof(struct FTable));
    lfqueue_init(&table->queue_t);
    table->spec = spec;

    return table;
}

struct FHandler * ft_get_handler(struct FTable *table)
{
    struct FHandler *handler = MALLOC(sizeof(struct FHandler));
    handler->spec   = table->spec;
    handler->queue  = &table->queue_t;
    handler->oldest = NULL;

    return handler;
}

void ft_add_event(struct FHandler *handler, void *event, time_t now)
{
    struct FEntry *entry = MALLOC(sizeof(struct FEntry));
    entry->timestamp     = now;
    entry->event         = event;

    lfqueue_enq(handler->queue, entry);
}

void * ft_pop_event(struct FHandler *handler, time_t now)
{
    if (!handler->oldest) {
        handler->oldest = lfqueue_deq(handler->queue);
        /*no event*/
        if (!handler->oldest)
            return NULL;
    }

    if (now - handler->oldest->timestamp >= handler->spec) {
        void *ret = handler->oldest->event;
        free(handler->oldest);
        handler->oldest = NULL;
        return ret;
    }

    return NULL;
}

void ft_close_handler(struct FHandler *handler) {
    if (handler->oldest) {
        if (handler->oldest->event)
            free(handler->oldest->event);
        free(handler->oldest);
        handler->oldest = NULL;
    }

    free(handler);
}

void ft_close_table(struct FTable *table)
{
    struct FEntry *entry = lfqueue_deq(&table->queue_t);
    while (entry) {
        free(entry->event);
        free(entry);
        entry = lfqueue_deq(&table->queue_t);
    }
    lfqueue_destroy(&table->queue_t);
    free(table);
}

uint64_t ft_event_count(struct FHandler *handler)
{
    uint64_t ret;
    ret = lfqueue_size(handler->queue);
    if (handler->oldest)
        ret++;
    return ret;
}