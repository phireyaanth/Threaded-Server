#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <signal.h>
#include <errno.h>
#include <pthread.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <sys/socket.h>

#include "client_registry.h"
#include "maze.h"
#include "player.h"
#include "debug.h"
#include "server.h"

//int debug_show_maze = 0;


static void terminate(int status);  // Forward declaration

static char *default_maze[] = {
    "******************************",
    "***** %%%%%%%%% &&&&&&&&&&& **",
    "***** %%%%%%%%%        $$$$  *",
    "*           $$$$$$ $$$$$$$$$ *",
    "*##########                  *",
    "*########## @@@@@@@@@@@@@@@@@*",
    "*           @@@@@@@@@@@@@@@@@*",
    "******************************",
    NULL
};

// SIGHUP handler
void handle_sighup(int sig) {
    printf("[DEBUG] Entering handle_sighup with signal %d\n", sig);
    (void)sig;
    terminate(EXIT_SUCCESS);
    // No exit needed: terminate will call it
}

int main(int argc, char *argv[]) {
    printf("[DEBUG] Entering main\n");

    int opt;
    int port = 0;

    while ((opt = getopt(argc, argv, "p:")) != -1) {
        switch (opt) {
            case 'p':
                port = atoi(optarg);
                break;
            default:
                fprintf(stderr, "Usage: %s -p <port>\n", argv[0]);
                exit(EXIT_FAILURE);
        }
    }

    if (port <= 0) {
        fprintf(stderr, "Error: Port number required via -p <port>\n");
        exit(EXIT_FAILURE);
    }

    client_registry = creg_init();
    maze_init(default_maze);
    player_init();
    debug_show_maze = 1;

    struct sigaction sa;
    sa.sa_handler = handle_sighup;
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = SA_RESTART;
    if (sigaction(SIGHUP, &sa, NULL) < 0) {
        perror("sigaction");
        terminate(EXIT_FAILURE);
    }

    int server_fd;
    struct sockaddr_in server_addr;

    if ((server_fd = socket(AF_INET, SOCK_STREAM, 0)) < 0) {
        perror("socket");
        terminate(EXIT_FAILURE);
    }

    int optval = 1;
    setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR, &optval, sizeof(optval));

    memset(&server_addr, 0, sizeof(server_addr));
    server_addr.sin_family = AF_INET;
    server_addr.sin_addr.s_addr = htonl(INADDR_ANY);
    server_addr.sin_port = htons(port);

    if (bind(server_fd, (struct sockaddr *)&server_addr, sizeof(server_addr)) < 0) {
        perror("bind");
        close(server_fd);
        terminate(EXIT_FAILURE);
    }

    if (listen(server_fd, 5) < 0) {
        perror("listen");
        close(server_fd);
        terminate(EXIT_FAILURE);
    }

    info("MazeWar server listening on port %d", port);

    while (1) {
        int *client_fd = malloc(sizeof(int));
        if (!client_fd) {
            error("malloc failed");
            continue;
        }

        *client_fd = accept(server_fd, NULL, NULL);
        if (*client_fd < 0) {
            free(client_fd);
            if (errno == EINTR) continue;
            perror("accept");
            continue;
        }

        pthread_t tid;
        if (pthread_create(&tid, NULL, mzw_client_service, client_fd) != 0) {
            error("pthread_create failed");
            close(*client_fd);
            free(client_fd);
            continue;
        }
        pthread_detach(tid);
    }

    terminate(EXIT_SUCCESS);
    return 0; // not reached, but good practice
}


/*
 * Function called to cleanly shut down the server.
 */
void terminate(int status) {
    printf("[DEBUG] Entering terminate with status=%d\n", status);

    creg_shutdown_all(client_registry);
    debug("Waiting for service threads to terminate...");
    creg_wait_for_empty(client_registry);
    debug("All service threads terminated.");

    creg_fini(client_registry);
    player_fini();
    maze_fini();

    debug("MazeWar server terminating");

    printf("[DEBUG] Exiting terminate with status=%d\n", status);
    exit(status);
}

