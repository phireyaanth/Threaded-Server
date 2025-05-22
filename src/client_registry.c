#include <stdlib.h>
#include <stdio.h>
#include <unistd.h>
#include <pthread.h>
#include <string.h>
#include <errno.h>
#include <signal.h>
#include <sys/socket.h>


#include "client_registry.h"
#include "debug.h"

#define MAX_CLIENTS  FD_SETSIZE  // can safely track this many

struct client_registry {
    int clients[MAX_CLIENTS];     // file descriptors of connected clients
    int count;                    // number of connected clients
    pthread_mutex_t mutex;       // protects clients[] and count
    pthread_cond_t empty;        // signaled when count reaches 0
};

CLIENT_REGISTRY *client_registry = NULL;

/*
 * Initialize the client registry.
 */
CLIENT_REGISTRY *creg_init() {
    printf("[DEBUG] Entering creg_init\n");

    CLIENT_REGISTRY *cr = calloc(1, sizeof(CLIENT_REGISTRY));
    if (!cr) {
        printf("[DEBUG] Exiting creg_init with failure (calloc)\n");
        return NULL;
    }

    pthread_mutex_init(&cr->mutex, NULL);
    pthread_cond_init(&cr->empty, NULL);

    for (int i = 0; i < MAX_CLIENTS; i++) {
        cr->clients[i] = -1;
    }

    printf("[DEBUG] Exiting creg_init with success\n");
    return cr;
}


/*
 * Finalize the client registry.
 */
void creg_fini(CLIENT_REGISTRY *cr) {
    printf("[DEBUG] Entering creg_fini\n");

    if (!cr) {
        printf("[DEBUG] Exiting creg_fini: NULL registry\n");
        return;
    }

    pthread_mutex_destroy(&cr->mutex);
    pthread_cond_destroy(&cr->empty);
    free(cr);

    printf("[DEBUG] Exiting creg_fini: registry finalized and memory freed\n");
}


/*
 * Register a new client file descriptor.
 */
void creg_register(CLIENT_REGISTRY *cr, int fd) {
    printf("[DEBUG] Entering creg_register with fd=%d\n", fd);

    pthread_mutex_lock(&cr->mutex);

    for (int i = 0; i < MAX_CLIENTS; i++) {
        if (cr->clients[i] == -1) {
            cr->clients[i] = fd;
            cr->count++;
            debug("Registered client fd=%d (total=%d)", fd, cr->count);
            pthread_mutex_unlock(&cr->mutex);
            printf("[DEBUG] Exiting creg_register: fd=%d registered\n", fd);
            return;
        }
    }

    pthread_mutex_unlock(&cr->mutex);
    error("Too many clients to register.");
    printf("[DEBUG] Exiting creg_register with error: client limit reached\n");
}


/*
 * Unregister a client file descriptor.
 */
void creg_unregister(CLIENT_REGISTRY *cr, int fd) {
    printf("[DEBUG] Entering creg_unregister with fd=%d\n", fd);

    pthread_mutex_lock(&cr->mutex);

    for (int i = 0; i < MAX_CLIENTS; i++) {
        if (cr->clients[i] == fd) {
            cr->clients[i] = -1;
            cr->count--;
            debug("Unregistered client fd=%d (remaining=%d)", fd, cr->count);

            if (cr->count == 0)
                pthread_cond_signal(&cr->empty);

            break;
        }
    }

    pthread_mutex_unlock(&cr->mutex);
    printf("[DEBUG] Exiting creg_unregister for fd=%d\n", fd);
}


/*
 * Wait until all clients have disconnected.
 */
void creg_wait_for_empty(CLIENT_REGISTRY *cr) {
    printf("[DEBUG] Entering creg_wait_for_empty\n");

    pthread_mutex_lock(&cr->mutex);

    while (cr->count > 0) {
        printf("[DEBUG] Waiting: %d clients still connected\n", cr->count);
        pthread_cond_wait(&cr->empty, &cr->mutex);
    }

    pthread_mutex_unlock(&cr->mutex);
    printf("[DEBUG] Exiting creg_wait_for_empty: all clients disconnected\n");
}

/*
 * Shut down all registered client connections.
 */
void creg_shutdown_all(CLIENT_REGISTRY *cr) {
    printf("[DEBUG] Entering creg_shutdown_all\n");

    pthread_mutex_lock(&cr->mutex);

    for (int i = 0; i < MAX_CLIENTS; i++) {
        if (cr->clients[i] != -1) {
            shutdown(cr->clients[i], SHUT_RD);
            debug("Shutdown client fd=%d", cr->clients[i]);
        }
    }

    pthread_mutex_unlock(&cr->mutex);
    printf("[DEBUG] Exiting creg_shutdown_all\n");
}

