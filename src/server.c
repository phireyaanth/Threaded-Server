#include <stdlib.h>
#include <unistd.h>
#include <pthread.h>
#include <signal.h>
#include <string.h>
#include <errno.h>

#include "server.h"
#include "client_registry.h"
#include "protocol.h"
#include "player.h"
#include "maze.h"
#include "debug.h"

#define MAX_PLAYERS 26             // if not already defined
typedef struct player PLAYER;     // forward declaration of opaque struct
PLAYER *get_player_by_index(int idx); // forward declare the function
// Accessor functions from player.c
const char *player_get_name(PLAYER *player);
int player_get_score(PLAYER *player);
char player_get_avatar(PLAYER *player);

volatile sig_atomic_t got_hit = 0;

void sigusr1_handler(int sig) {
    got_hit = 1;
}




int debug_show_maze = 0;

extern CLIENT_REGISTRY *client_registry;

void *mzw_client_service(void *arg) {
    printf("[DEBUG] Entered mzw_client_service\n");

    int fd = *((int *)arg);
    free(arg);  // Clean up the dynamically allocated fd wrapper

    struct sigaction sa;
    sa.sa_handler = sigusr1_handler;
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = 0;
    sigaction(SIGUSR1, &sa, NULL);



    printf("[DEBUG] Detached thread and registering client (fd=%d)\n", fd);
    pthread_detach(pthread_self());
    creg_register(client_registry, fd);

    PLAYER *player = NULL;
    int logged_in = 0;

    MZW_PACKET pkt;
    void *payload = NULL;

    while (1) {
        if (player != NULL) {
            printf("[DEBUG] Checking for laser hit\n");
            if (got_hit && player) {
                got_hit = 0;
                player_check_for_laser_hit(player);
            }

        }

        printf("[DEBUG] Waiting to receive packet on fd=%d\n", fd);
        if (proto_recv_packet(fd, &pkt, &payload) < 0) {
            printf("[DEBUG] proto_recv_packet failed or client disconnected on fd=%d\n", fd);
            break;
        }

        if (!logged_in && pkt.type != MZW_LOGIN_PKT) {
            printf("[DEBUG] No LOGIN received. Auto-logging in as A Anonymous\n");

            char *default_name = strdup("Anonymous");
            unsigned char avatar = 0;

            // Scan for first available avatar from 'A' to 'Z'
            for (int i = 0; i < 26; i++) {
                PLAYER *existing = get_player_by_index(i); // You must have this in player.c
                if (existing == NULL) {
                    avatar = 'A' + i;
                    break;
                }
            }

            if (avatar == 0) {
                printf("[DEBUG] Auto-login failed: no available avatars\n");
                free(default_name);
                break;
            }

            printf("[DEBUG] Auto-picked avatar '%c' for Anonymous\n", avatar);
            player = player_login(fd, avatar, default_name);

            free(default_name);

            if (!player) {
                printf("[DEBUG] Auto-login failed: avatar A already in use\n");
                MZW_PACKET reply = {
                    .type = MZW_INUSE_PKT,
                    .size = 0
                };
                proto_send_packet(fd, &reply, NULL);
                if (payload) {
                    free(payload);
                    payload = NULL;
                }
                break;
            }

            logged_in = 1;
            printf("[DEBUG] Auto-login successful\n");

            MZW_PACKET reply = {
                .type = MZW_READY_PKT,
                .size = 0
            };
            proto_send_packet(fd, &reply, NULL);

            printf("[DEBUG] Resetting player view after auto-login\n");
            player_reset(player);

            if (payload) {
                free(payload);
                payload = NULL;
            }

            continue;  // Go back to top of loop
        }


        switch (pkt.type) {
            case MZW_LOGIN_PKT: {
                printf("[DEBUG] Received LOGIN packet\n");

                if (logged_in) break;

                if (pkt.size > 256) {
                    warn("LOGIN payload too large");
                    free(payload);
                    break;
                }

                unsigned char avatar = pkt.param1;
                char *name = payload ? strndup(payload, pkt.size) : NULL;

                // Dump raw payload bytes
                if (payload && pkt.size > 0) {
                    printf("[DEBUG] Raw payload bytes (length=%d): ", pkt.size);
                    for (int i = 0; i < pkt.size; i++) {
                        printf("%02x ", ((unsigned char *)payload)[i]);
                    }
                    printf("\n");
                }

                // Debug print of parsed name
                if (name) {
                    printf("[DEBUG] Received login name: '%.*s' (length=%d)\n", pkt.size, name, pkt.size);
                } else {
                    printf("[DEBUG] No login name received (name is NULL)\n");
                }

                free(payload);
                payload = NULL;

                printf("[DEBUG] Attempting login with avatar '%c'\n", avatar);
                player = player_login(fd, avatar, name);

                if (!player) {
                    printf("[DEBUG] Login failed for avatar '%c'. Trying fallback...\n", avatar);

                    if (name && strcmp(name, "Anonymous") == 0) {
                        for (int i = 0; i < 26; i++) {
                            unsigned char try_avatar = 'A' + i;
                            if (get_player_by_index(i) == NULL) {
                                printf("[DEBUG] Trying fallback avatar '%c'\n", try_avatar);
                                player = player_login(fd, try_avatar, name);
                                if (player) {
                                    avatar = try_avatar;
                                    break;
                                }
                            }
                        }
                    }

                    if (!player) {
                        printf("[DEBUG] All avatars in use or fallback failed\n");
                        MZW_PACKET reply = {
                            .type = MZW_INUSE_PKT,
                            .size = 0
                        };
                        proto_send_packet(fd, &reply, NULL);
                        free(name);  // free only after done using it
                        break;
                    }
                }

                free(name);  // move here

                logged_in = 1;
                printf("[DEBUG] Login successful\n");

                MZW_PACKET reply = {
                    .type = MZW_READY_PKT,
                    .size = 0
                };
                proto_send_packet(fd, &reply, NULL);

                printf("[DEBUG] Resetting player view\n");
                player_reset(player);
                printf("[DEBUG] Broadcasting initial score for %c (%s)\n",
                    player_get_avatar(player),
                    player_get_name(player) ? player_get_name(player) : "null");

                MZW_PACKET score_pkt = {
                    .type = MZW_SCORE_PKT,
                    .param1 = player_get_avatar(player),
                    .param2 = player_get_score(player),
                    .size = player_get_name(player) ? strlen(player_get_name(player)) : 0
                };

                for (int i = 0; i < MAX_PLAYERS; i++) {
                    PLAYER *p = get_player_by_index(i);
                    if (p) {
                        player_send_packet(p, &score_pkt, (void *)player_get_name(player));
                    }
                }


                break;
            }


           case MZW_MOVE_PKT: {
                printf("[DEBUG] MOVE packet received\n");
                int sign = pkt.param1;

                if (player_move(player, sign) == 0) {
                    printf("[DEBUG] Movement succeeded for %c, updating view\n", player_get_avatar(player));
                    player_update_view(player);
                } else {
                    printf("[DEBUG] Movement failed for %c, no view update\n", player_get_avatar(player));
                }

                break;
            }

            case MZW_TURN_PKT: {
                printf("[DEBUG] TURN packet received\n");
                int dir = pkt.param1;
                player_rotate(player, dir);
                player_update_view(player);

                if (debug_show_maze)
                    show_maze();
                break;
            }


            case MZW_FIRE_PKT: {
                printf("[DEBUG] FIRE packet received\n");
                player_fire_laser(player);

                if (debug_show_maze)
                    show_maze();
                break;
            }


            case MZW_REFRESH_PKT: {
                printf("[DEBUG] REFRESH packet received\n");
                player_invalidate_view(player);
                player_update_view(player);

                if (debug_show_maze)
                    show_maze();
                break;
            }


            case MZW_SEND_PKT: {
                printf("[DEBUG] SEND (chat) packet received\n");
                if (payload && pkt.size > 0)
                    player_send_chat(player, payload, pkt.size);
                break;
            }


            default:
                warn("[DEBUG] Unhandled packet type %d", pkt.type);
                break;
        }

        if (payload) {
            free(payload);
            payload = NULL;
        }

        if (debug_show_maze)
            show_maze();
    }

    printf("[DEBUG] Cleaning up after client on fd=%d\n", fd);
    if (payload) free(payload);

    if (logged_in && player != NULL)
        player_logout(player);

    close(fd);
    creg_unregister(client_registry, fd);

    printf("[DEBUG] Exiting mzw_client_service\n");
    return NULL;
}
