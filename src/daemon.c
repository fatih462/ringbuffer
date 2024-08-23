#include <stdio.h>
#include <pthread.h>
#include <unistd.h>
#include <string.h>
#include <stdlib.h>

#include "../include/daemon.h"
#include "../include/ringbuf.h"

/* IN THE FOLLOWING IS THE CODE PROVIDED FOR YOU 
 * changing the code will result in points deduction */

/********************************************************************
* NETWORK TRAFFIC SIMULATION: 
* This section simulates incoming messages from various ports using 
* files. Think of these input files as data sent by clients over theports
* network to our computer. The data isn't transmitted in a single 
* large file but arrives in multiple small packets. This concept
* is discussed in more detail in the advanced module: 
* Rechnernetze und Verteilte Systeme
*
* To simulate this parallel packet-based data transmission, we use multiple 
* threads. Each thread reads small segments of the files and writes these 
* smaller packets into the ring buffer. Between each packet, the
* thread sleeps for a random time between 1 and 100 us. This sleep
* simulates that data packets take varying amounts of time to arrive.
*********************************************************************/
typedef struct {
    rbctx_t* ctx;
    connection_t* connection;
} w_thread_args_t;

void* write_packets(void* arg) {
    /* extract arguments */
    rbctx_t* ctx = ((w_thread_args_t*) arg)->ctx;
    size_t from = (size_t) ((w_thread_args_t*) arg)->connection->from;
    size_t to = (size_t) ((w_thread_args_t*) arg)->connection->to;
    char* filename = ((w_thread_args_t*) arg)->connection->filename;

    /* open file */
    FILE *fp = fopen(filename, "r");
    if (fp == NULL) {
        fprintf(stderr, "Cannot open file with name %s\n", filename);
        exit(1);
    }

    /* read file in chunks and write to ringbuffer with random delay */
    unsigned char buf[MESSAGE_SIZE];
    size_t packet_id = 0;
    size_t read = 1;
    while (read > 0) {
        size_t msg_size = MESSAGE_SIZE - 3 * sizeof(size_t);
        read = fread(buf + 3 * sizeof(size_t), 1, msg_size, fp);
        if (read > 0) {
            memcpy(buf, &from, sizeof(size_t));
            memcpy(buf + sizeof(size_t), &to, sizeof(size_t));
            memcpy(buf + 2 * sizeof(size_t), &packet_id, sizeof(size_t));
            while(ringbuffer_write(ctx, buf, read + 3 * sizeof(size_t)) != SUCCESS){
                usleep(((rand() % 50) + 25)); // sleep for a random time between 25 and 75 us
            }
        }
        packet_id++;
        usleep(((rand() % (100 -1)) + 1)); // sleep for a random time between 1 and 100 us
    }
    fclose(fp);
    return NULL;
}

/* END OF PROVIDED CODE */


/********************************************************************/

/* YOUR CODE STARTS HERE */

// 1. read functionality
// 2. filtering functionality
// 3. (thread-safe) write to file functionality

typedef struct {
    size_t packet_id;
    pthread_mutex_t mtx;
    pthread_cond_t sig;
} packet_port;

typedef struct {
    rbctx_t* context;
    packet_port* packet_ports2;
} r_thread_args_t;

//packet_port packet_ports2[MAXIMUM_PORT]; // lieber nicht global definieren

int malicious(char* message, int16_t length) {
    char* malicious = "malicious";
    int16_t len = strlen(malicious);
    int16_t j = 0;

    for (int16_t i = 0; i < length; i++) {
        if (message[i] == malicious[j]) {
            j++;
            if (j == len) {
                return 1;
            }
        }
    }
    return 0;
}

int filter(size_t from_port, size_t to_port, char* message, size_t length) {
    if (from_port == to_port || from_port == 42 || to_port == 42
    || (from_port + to_port) == 42 || malicious(message, length) == 1) return 1;
    return 0;
}

void* read_packets(void* arg) {
    r_thread_args_t* args = (r_thread_args_t*) arg;
    rbctx_t* context = args->context;
    packet_port* packet_ports = args->packet_ports2;

    size_t from = 0, to = 0, packet_id = 0;
    char buf[MESSAGE_SIZE];

    pthread_setcancelstate(PTHREAD_CANCEL_DISABLE, NULL);
    pthread_setcanceltype(PTHREAD_CANCEL_DEFERRED, NULL);

    while (1) {
        pthread_setcancelstate(PTHREAD_CANCEL_ENABLE, NULL);
        pthread_testcancel();
        pthread_setcancelstate(PTHREAD_CANCEL_DISABLE, NULL);

        size_t buffer_len = MESSAGE_SIZE;
        int read = ringbuffer_read(context, buf, &buffer_len);
        if (read == SUCCESS) {
            memcpy(&from, buf, sizeof(size_t));
            memcpy(&to, buf + sizeof(size_t), sizeof(size_t));
            memcpy(&packet_id, buf + 2 * sizeof(size_t), sizeof(size_t));
            
            //char* message = buf + 3 * sizeof(size_t);
            size_t message_len = buffer_len - 3 * sizeof(size_t);
            char message[message_len];
            memcpy(message, buf + 3 * sizeof(size_t), message_len);

            packet_port *this_port = &packet_ports[to];

            if (filter(from, to, message, message_len) == 0) {
                //printf("from: %zu, to: %zu, packet_id: %zu\n", from, to, packet_id);
                pthread_mutex_lock(&this_port->mtx);
                
                while (packet_id != this_port->packet_id) {
                    pthread_cond_wait(&this_port->sig, &this_port->mtx);
                }
                
                char filename[9];
                sprintf(filename, "%zu.txt", to);

                FILE *fp = fopen(filename, "a");
                if (fp == NULL) {
                    pthread_mutex_unlock(&this_port->mtx);
                    exit(1);
                }

                fwrite(message, 1, message_len, fp);
                fclose(fp);

                this_port->packet_id += 1;
                pthread_cond_broadcast(&this_port->sig);
                pthread_mutex_unlock(&this_port->mtx);
            } else {
                pthread_mutex_lock(&this_port->mtx);
                while (packet_id != this_port->packet_id) {
                    pthread_cond_wait(&this_port->sig, &this_port->mtx);
                }

                this_port->packet_id += 1;
                pthread_cond_broadcast(&this_port->sig);
                pthread_mutex_unlock(&this_port->mtx);
            }
        }
    }
    return NULL;
}



/* YOUR CODE ENDS HERE */

/********************************************************************/

int simpledaemon(connection_t* connections, int nr_of_connections) {
    /* initialize ringbuffer */
    rbctx_t rb_ctx;
    size_t rbuf_size = 1024;
    void *rbuf = malloc(rbuf_size);
    if (rbuf == NULL) {
        fprintf(stderr, "Error allocation ringbuffer\n");
    }

    ringbuffer_init(&rb_ctx, rbuf, rbuf_size);

    /****************************************************************
    * WRITER THREADS 
    * ***************************************************************/

    /* prepare writer thread arguments */
    w_thread_args_t w_thread_args[nr_of_connections];
    for (int i = 0; i < nr_of_connections; i++) {
        w_thread_args[i].ctx = &rb_ctx;
        w_thread_args[i].connection = &connections[i];
        /* guarantee that port numbers range from MINIMUM_PORT (0) - MAXIMUMPORT */
        if (connections[i].from > MAXIMUM_PORT || connections[i].to > MAXIMUM_PORT ||
            connections[i].from < MINIMUM_PORT || connections[i].to < MINIMUM_PORT) {
            fprintf(stderr, "Port numbers %d and/or %d are too large\n", connections[i].from, connections[i].to);
            exit(1);
        }
    }

    /* start writer threads */
    pthread_t w_threads[nr_of_connections];
    for (int i = 0; i < nr_of_connections; i++) {
        pthread_create(&w_threads[i], NULL, write_packets, &w_thread_args[i]);
    }

    /****************************************************************
    * READER THREADS
    * ***************************************************************/

    pthread_t r_threads[NUMBER_OF_PROCESSING_THREADS];

    /* END OF PROVIDED CODE */

    /********************************************************************/

    /* YOUR CODE STARTS HERE */

    // 1. think about what arguments you need to pass to the processing threads
    // 2. start the processing threads
    packet_port packet_ports2[MAXIMUM_PORT];

    for (int i = 0; i < MAXIMUM_PORT; i++) {
        packet_ports2[i].packet_id = 0;
        pthread_mutex_init(&packet_ports2[i].mtx, NULL);
        pthread_cond_init(&packet_ports2[i].sig, NULL);
    }

    r_thread_args_t r_thread_args[NUMBER_OF_PROCESSING_THREADS];
    for (int i = 0; i < NUMBER_OF_PROCESSING_THREADS; i++) {
        r_thread_args[i].context = &rb_ctx;
        r_thread_args[i].packet_ports2 = packet_ports2;
        pthread_create(&r_threads[i], NULL, read_packets, &r_thread_args[i]);
    }

    /* YOUR CODE ENDS HERE */

    /********************************************************************/



    /* IN THE FOLLOWING IS THE CODE PROVIDED FOR YOU 
     * changing the code will result in points deduction */

    /****************************************************************
     * CLEANUP
     * ***************************************************************/

    /* after 5 seconds JOIN all threads (we should definitely have received all messages by then) */
    printf("daemon: waiting for 5 seconds before canceling reading threads\n");
    sleep(5);
    for (int i = 0; i < NUMBER_OF_PROCESSING_THREADS; i++) {
        pthread_cancel(r_threads[i]);
    }

    /* wait for all threads to finish */
    for (int i = 0; i < nr_of_connections; i++) {
        pthread_join(w_threads[i], NULL);
    }

    /* join all threads */
    for (int i = 0; i < NUMBER_OF_PROCESSING_THREADS; i++) {
        pthread_join(r_threads[i], NULL);
    }

    /* END OF PROVIDED CODE */



    /********************************************************************/
    
    /* YOUR CODE STARTS HERE */

    // use this section to free any memory, destory mutexe etc.
    for (int i = 0; i < MAXIMUM_PORT; i++){
        pthread_mutex_destroy(&packet_ports2[i].mtx);
        pthread_cond_destroy(&packet_ports2[i].sig);
    }

    /* YOUR CODE ENDS HERE */

    /********************************************************************/



    /* IN THE FOLLOWING IS THE CODE PROVIDED FOR YOU 
    * changing the code will result in points deduction */

    free(rbuf);
    ringbuffer_destroy(&rb_ctx);

    return 0;

    /* END OF PROVIDED CODE */
}
