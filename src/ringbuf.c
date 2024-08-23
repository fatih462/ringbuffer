#include "../include/ringbuf.h"
#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <stdlib.h>
#include <sys/types.h>

/*
typedef struct {
    uint8_t* read;
    uint8_t* write;
    uint8_t* begin;
    uint8_t* end; //1 step AFTER the last readable address
    pthread_mutex_t mtx;
    pthread_cond_t sig;
} rbctx_t;
*/

void ringbuffer_init(rbctx_t *context, void *buffer_location, size_t buffer_size)
{
    /* your solution here */
    context->begin = (uint8_t*)buffer_location;
    context->read = (uint8_t*)buffer_location;
    context->write = (uint8_t*)buffer_location;
    context->end = (uint8_t*)buffer_location + buffer_size;

    pthread_mutex_init(&context->mtx, NULL);
    pthread_cond_init(&context->sig, NULL);


    return;
}

int ringbuffer_write(rbctx_t *context, void *message, size_t message_len)
{
    /* your solution here */
    pthread_mutex_lock(&context->mtx);

    size_t free_s;
    if (context->write >= context->read) {
        free_s = (context->end - context->write) + (context->read - context->begin) - 1;
    } else {
        free_s = context->read - context->write - 1;
    }

    while ((sizeof(size_t) + message_len) > free_s) {
        struct timespec ts;
        clock_gettime(CLOCK_REALTIME, &ts);
        ts.tv_sec += 1;

        int time = pthread_cond_timedwait(&context->sig, &context->mtx, &ts);
        if (time != 0) {
            pthread_mutex_unlock(&context->mtx);
            return RINGBUFFER_FULL;
        }

        if (context->write >= context->read) {
            free_s = (context->end - context->write) + (context->read - context->begin) - 1;
        } else {
            free_s = context->read - context->write - 1;
        }
    }

    for (size_t i = 0; i < sizeof(size_t); i++) {
        *(context->write++) = ((uint8_t*)&message_len)[i];
        if (context->write == context->end) context->write = context->begin;
    }

    for (size_t i = 0; i < message_len; i++) {
        *(context->write++) = ((uint8_t*)message)[i];
        if (context->write == context->end) context->write = context->begin;
    }

    pthread_cond_signal(&context->sig);
    pthread_mutex_unlock(&context->mtx);
    return SUCCESS;
}

int ringbuffer_read(rbctx_t *context, void *buffer, size_t *buffer_len)
{
    /* your solution here */
    pthread_mutex_lock(&context->mtx);

    while (context->read == context->write) {
        struct timespec ts;
        clock_gettime(CLOCK_REALTIME, &ts);
        ts.tv_sec += 1;

        int time = pthread_cond_timedwait(&context->sig, &context->mtx, &ts);
        if (time != 0) {
            pthread_mutex_unlock(&context->mtx);
            return RINGBUFFER_EMPTY;
        }
    }

    size_t message_len = 0;
    for (size_t i = 0; i < sizeof(size_t); i++) {
        ((uint8_t*)&message_len)[i] = *(context->read++);
        if (context->read == context->end) context->read = context->begin;
    }

    if (message_len > *buffer_len) {
        pthread_mutex_unlock(&context->mtx);
        return OUTPUT_BUFFER_TOO_SMALL;
    }

    for (size_t i = 0; i < message_len; i++) {
        ((uint8_t*)buffer)[i] = *(context->read++);
        if (context->read == context->end) context->read = context->begin;
    }

    *buffer_len = message_len;
    pthread_cond_signal(&context->sig);
    pthread_mutex_unlock(&context->mtx);
    return SUCCESS;
}

void ringbuffer_destroy(rbctx_t *context)
{
    /* your solution here */
    pthread_mutex_destroy(&context->mtx);
    pthread_cond_destroy(&context->sig);
}
