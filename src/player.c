#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <pthread.h>
#include <signal.h>
#include "player.h"
#include "protocol.h"
#include "maze.h"
#include "debug.h"
#include <unistd.h>  // for sleep()

const char *player_get_name(PLAYER *player);
#define MAX_PLAYERS 26  // one avatar per letter A-Z

struct player {
    OBJECT avatar;
    char *name;
    int fd;
    int score;
    int row, col;
    DIRECTION dir;
    char (*view)[VIEW_WIDTH];  // pointer to 2D array
    pthread_mutex_t mutex;  // must be recursive
    int ref_count;
    volatile sig_atomic_t hit_flag;  // set by SIGUSR1
    pthread_t thread_id;  // NEW: store the thread handling this player

};


static PLAYER *players[MAX_PLAYERS];
static pthread_mutex_t players_mutex = PTHREAD_MUTEX_INITIALIZER;

static void player_broadcast_name(PLAYER *player) {
    // if (!player || !player->name) return;

    // char name_prefix[256];
    // int len = snprintf(name_prefix, sizeof(name_prefix), "%s[%c] ", player->name, player->avatar);
    // if (len <= 0 || len >= (int)sizeof(name_prefix)) return;

    // MZW_PACKET pkt = {
    //     .type = MZW_CHAT_PKT,
    //     .size = len
    // };

    // for (int i = 0; i < MAX_PLAYERS; i++) {
    //     if (players[i] && players[i]->fd >= 0) {
    //         player_send_packet(players[i], &pkt, name_prefix);
    //     }
    // }
}


void player_init(void) {
    printf("[DEBUG] Entering player_init\n");

    for (int i = 0; i < MAX_PLAYERS; i++) {
        players[i] = NULL;
    }

    printf("[DEBUG] Exiting player_init\n");
}

void player_fini(void) {
    printf("[DEBUG] Entering player_fini\n");

    for (int i = 0; i < MAX_PLAYERS; i++) {
        if (players[i]) {
            player_unref(players[i], "player_fini cleanup");
        }
    }

    printf("[DEBUG] Exiting player_fini\n");
}

PLAYER *get_player_by_index(int idx) {
    if (idx < 0 || idx >= 26) return NULL;
    return players[idx];  // players[] must be declared static or global in this file
}


PLAYER *player_login(int clientfd, OBJECT avatar, char *name) {
    printf("[DEBUG] Entering player_login\n");

    pthread_mutex_lock(&players_mutex);
    int idx = avatar - 'A';
    printf("[DEBUG] Attempting login: fd=%d, avatar=%c, idx=%d\n", clientfd, avatar, idx);
    if (idx < 0 || idx >= MAX_PLAYERS || players[idx]) {
        pthread_mutex_unlock(&players_mutex);
        printf("[DEBUG] Login failed: avatar already in use or invalid index\n");
        printf("[DEBUG] Exiting player_login with failure\n");
        return NULL;
    }

    PLAYER *p = calloc(1, sizeof(PLAYER));
    if (!p) {
        pthread_mutex_unlock(&players_mutex);
        printf("[DEBUG] Login failed: calloc for PLAYER failed\n");
        printf("[DEBUG] Exiting player_login with failure\n");
        return NULL;
    }

    p->view = calloc(VIEW_DEPTH, sizeof(*p->view));
    if (!p->view) {
        free(p);
        pthread_mutex_unlock(&players_mutex);
        printf("[DEBUG] Login failed: calloc for view failed\n");
        printf("[DEBUG] Exiting player_login with failure\n");
        return NULL;
    }

    p->avatar = avatar;
    p->fd = clientfd;
    p->score = 0;
    p->dir = NORTH;
    p->ref_count = 1;
    p->hit_flag = 0;
    p->name = (name && strlen(name) > 0) ? strdup(name) : strdup("anonymous");
    p->thread_id = pthread_self();  // ⬅️ store the current thread ID


    pthread_mutexattr_t attr;
    pthread_mutexattr_init(&attr);
    pthread_mutexattr_settype(&attr, PTHREAD_MUTEX_RECURSIVE);
    pthread_mutex_init(&p->mutex, &attr);
    pthread_mutexattr_destroy(&attr);

    players[idx] = p;
    printf("[DEBUG] Player %c logged in successfully\n", avatar);
    pthread_mutex_unlock(&players_mutex);

    printf("[DEBUG] Exiting player_login with success\n");
    return p;
}

void player_logout(PLAYER *player) {
    printf("[DEBUG] Entering player_logout for %c\n", player->avatar);

    pthread_mutex_lock(&players_mutex);
    int idx = player->avatar - 'A';
    players[idx] = NULL;
    pthread_mutex_unlock(&players_mutex);

    maze_remove_player(player->avatar, player->row, player->col);

    MZW_PACKET pkt = {
        .type = MZW_SCORE_PKT,
        .param1 = player->avatar,
        .param2 = -1,
        .size = 0
    };
    player_send_packet(player, &pkt, NULL);

    printf("[DEBUG] Player %c logged out\n", player->avatar);
    player_unref(player, "logout");

    printf("[DEBUG] Exiting player_logout for %c\n", player->avatar);
}


void player_reset(PLAYER *player) {
    printf("[DEBUG] Entering player_reset for %c\n", player->avatar);

    pthread_mutex_lock(&player->mutex);
    printf("[DEBUG] Acquired player mutex for %c\n", player->avatar);

    maze_remove_player(player->avatar, player->row, player->col);
    printf("[DEBUG] Called maze_remove_player for %c\n", player->avatar);

    if (maze_set_player_random(player->avatar, &player->row, &player->col) != 0) {
        printf("[DEBUG] Failed to set player %c randomly\n", player->avatar);
        warn("Could not place player %c in maze. Skipping reset.", player->avatar);
        pthread_mutex_unlock(&player->mutex);
        printf("[DEBUG] Exiting player_reset early due to placement failure for %c\n", player->avatar);
        return;
    }

    printf("[DEBUG] Calling player_update_view for %c\n", player->avatar);
    player_update_view(player);
    printf("[DEBUG] player_update_view done for %c\n", player->avatar);

    // Notify other players to update their views
    for (int i = 0; i < MAX_PLAYERS; i++) {
        if (players[i] && players[i] != player) {
            player_invalidate_view(players[i]);
            player_update_view(players[i]);
        }
    }

    // Re-add the player's score to the scoreboard
    MZW_PACKET pkt = {
        .type = MZW_SCORE_PKT,
        .param1 = player->avatar,
        .param2 = player->score,
        .size = 0
    };

    for (int i = 0; i < MAX_PLAYERS; i++) {
        if (players[i]) {
            player_send_packet(players[i], &pkt, NULL);
        }
    }

    // 🔁 Re-broadcast name to ensure gclient links avatar to name
    player_broadcast_name(player);

    pthread_mutex_unlock(&player->mutex);
    printf("[DEBUG] Exiting player_reset for %c\n", player->avatar);
}


PLAYER *player_get(unsigned char avatar) {
    printf("[DEBUG] Entering player_get for avatar %c\n", avatar);

    pthread_mutex_lock(&players_mutex);
    int idx = avatar - 'A';

    if (idx < 0 || idx >= MAX_PLAYERS || !players[idx]) {
        pthread_mutex_unlock(&players_mutex);
        printf("[DEBUG] Exiting player_get: avatar %c not found\n", avatar);
        return NULL;
    }

    PLAYER *p = player_ref(players[idx], "player_get");
    pthread_mutex_unlock(&players_mutex);

    printf("[DEBUG] Exiting player_get with player %c\n", avatar);
    return p;
}

PLAYER *player_ref(PLAYER *player, char *why) {
    printf("[DEBUG] Entering player_ref for %c (%s)\n", player->avatar, why);

    pthread_mutex_lock(&player->mutex);
    player->ref_count++;
    printf("[DEBUG] player_ref: %c ref_count=%d (%s)\n", player->avatar, player->ref_count, why);
    pthread_mutex_unlock(&player->mutex);

    printf("[DEBUG] Exiting player_ref for %c\n", player->avatar);
    return player;
}


void player_unref(PLAYER *player, char *why) {
    printf("[DEBUG] Entering player_unref for %c (%s)\n", player->avatar, why);

    pthread_mutex_lock(&player->mutex);
    player->ref_count--;
    printf("[DEBUG] player_unref: %c ref_count=%d (%s)\n", player->avatar, player->ref_count, why);

    if (player->ref_count == 0) {
        pthread_mutex_unlock(&player->mutex);
        pthread_mutex_destroy(&player->mutex);
        free(player->name);
        free(player->view);
        printf("[DEBUG] Freed player %c\n", player->avatar);
        printf("[DEBUG] Exiting player_unref for %c — object destroyed\n", player->avatar);
        free(player);
    } else {
        pthread_mutex_unlock(&player->mutex);
        printf("[DEBUG] Exiting player_unref for %c — object retained\n", player->avatar);
    }
}


int player_send_packet(PLAYER *player, MZW_PACKET *pkt, void *data) {
    printf("[DEBUG] Entering player_send_packet: sending type %d to %c (fd=%d)\n",
           pkt->type, player->avatar, player->fd);

    pthread_mutex_lock(&player->mutex);
    int ret = proto_send_packet(player->fd, pkt, data);
    pthread_mutex_unlock(&player->mutex);

    if (ret < 0) {
        printf("[DEBUG] proto_send_packet failed for %c\n", player->avatar);
    }

    printf("[DEBUG] Exiting player_send_packet for %c\n", player->avatar);
    return ret;
}



int player_get_location(PLAYER *player, int *rowp, int *colp, int *dirp) {
    printf("[DEBUG] Entering player_get_location for %c\n", player->avatar);

    pthread_mutex_lock(&player->mutex);
    if (rowp) *rowp = player->row;
    if (colp) *colp = player->col;
    if (dirp) *dirp = player->dir;
    pthread_mutex_unlock(&player->mutex);

    printf("[DEBUG] Exiting player_get_location for %c with row=%d, col=%d, dir=%d\n",
           player->avatar, player->row, player->col, player->dir);
    return 0;
}


int player_move(PLAYER *player, int sign) {
    printf("[DEBUG] Entering player_move for %c with sign=%d\n", player->avatar, sign);

    pthread_mutex_lock(&player->mutex);
    int dir = (sign == 1) ? player->dir : REVERSE(player->dir);

    if (maze_move(player->row, player->col, dir) == 0) {
        player->row += (dir == NORTH) ? -1 : (dir == SOUTH) ? 1 : 0;
        player->col += (dir == WEST) ? -1 : (dir == EAST) ? 1 : 0;
        printf("[DEBUG] Player %c moved to (%d, %d)\n", player->avatar, player->row, player->col);
        player_update_view(player);
        pthread_mutex_unlock(&player->mutex);
        printf("[DEBUG] Exiting player_move for %c: move successful\n", player->avatar);
        return 0;
    }

    pthread_mutex_unlock(&player->mutex);
    printf("[DEBUG] Exiting player_move for %c: move failed\n", player->avatar);
    return -1;
}


void player_rotate(PLAYER *player, int dir) {
    printf("[DEBUG] Entering player_rotate for %c with dir=%d\n", player->avatar, dir);

    pthread_mutex_lock(&player->mutex);
    player->dir = (dir == 1) ? TURN_LEFT(player->dir) : TURN_RIGHT(player->dir);
    printf("[DEBUG] Player %c rotated to direction %d\n", player->avatar, player->dir);
    player_invalidate_view(player);
    pthread_mutex_unlock(&player->mutex);

    printf("[DEBUG] Exiting player_rotate for %c\n", player->avatar);
}


void player_fire_laser(PLAYER *player) {
    printf("[DEBUG] Entering player_fire_laser for %c\n", player->avatar);
    printf("[DEBUG] I detected the Escape key — shot fired by %c\n", player->avatar);

    pthread_mutex_lock(&player->mutex);
    OBJECT target = maze_find_target(player->row, player->col, player->dir);

    if (IS_AVATAR(target)) {
        PLAYER *victim = player_get(target);
        if (victim) {
            victim->hit_flag = 1;
            pthread_kill(victim->thread_id, SIGUSR1);  // <-- this triggers the immediate respawn
            printf("[DEBUG] Player %c hit player %c with laser (hit_flag set and signal sent)\n", player->avatar, target);
            player_unref(victim, "fired hit");
        }

        player->score++;
        printf("[DEBUG] Player %c score incremented to %d\n", player->avatar, player->score);

        // Broadcast updated score
        for (int i = 0; i < MAX_PLAYERS; i++) {
            if (players[i]) {
                MZW_PACKET pkt = {
                    .type = MZW_SCORE_PKT,
                    .param1 = player->avatar,
                    .param2 = player->score,
                    .size = 0
                };
                player_send_packet(players[i], &pkt, NULL);
            }
        }

        player_broadcast_name(player);
    } else {
        printf("[DEBUG] Player %c fired but hit nothing\n", player->avatar);
    }

    pthread_mutex_unlock(&player->mutex);
    printf("[DEBUG] Exiting player_fire_laser for %c\n", player->avatar);
}


void player_invalidate_view(PLAYER *player) {
    printf("[DEBUG] Entering player_invalidate_view for %c\n", player->avatar);

    pthread_mutex_lock(&player->mutex);
    memset(player->view, 0, VIEW_DEPTH * sizeof(*player->view));
    printf("[DEBUG] Player %c view invalidated\n", player->avatar);
    pthread_mutex_unlock(&player->mutex);

    printf("[DEBUG] Exiting player_invalidate_view for %c\n", player->avatar);
}


void player_update_view(PLAYER *player) {
    printf("[DEBUG] Entered player_update_view for %c\n", player->avatar);

    pthread_mutex_lock(&player->mutex);
    printf("[DEBUG] Acquired mutex in player_update_view for %c\n", player->avatar);

    if (!player->view) {
        printf("[DEBUG] player->view is NULL. Aborting update_view for %c\n", player->avatar);
        pthread_mutex_unlock(&player->mutex);
        printf("[DEBUG] Released mutex and exiting player_update_view early for %c\n", player->avatar);
        return;
    }

    int depth = maze_get_view((VIEW *)player->view, player->row, player->col, player->dir, VIEW_DEPTH);
    printf("[DEBUG] maze_get_view completed. Depth = %d for %c\n", depth, player->avatar);

    if (depth <= 0 || depth > VIEW_DEPTH) {
        printf("[DEBUG] Invalid view depth (%d). Exiting player_update_view for %c\n", depth, player->avatar);
        pthread_mutex_unlock(&player->mutex);
        printf("[DEBUG] Released mutex and exiting player_update_view early for %c\n", player->avatar);
        return;
    }

    printf("[DEBUG] Sending CLEAR packet to %c\n", player->avatar);
    MZW_PACKET clear_pkt = { .type = MZW_CLEAR_PKT, .size = 0 };

    if (player_send_packet(player, &clear_pkt, NULL) < 0) {
        printf("[DEBUG] Failed to send CLEAR packet for %c\n", player->avatar);
    } else {
        printf("[DEBUG] Sent CLEAR packet to %c\n", player->avatar);
    }

    for (int d = 0; d < depth; d++) {
        printf("[DEBUG] Entering depth loop d=%d for %c\n", d, player->avatar);
        for (int side = 0; side < VIEW_WIDTH; side++) {
            printf("[DEBUG] Accessing player->view[%d][%d] for %c\n", d, side, player->avatar);
            char cell = player->view[d][side];
            printf("[DEBUG] Got view cell = '%c' at [%d][%d] for %c\n", cell, d, side, player->avatar);

            MZW_PACKET show_pkt = {
                .type = MZW_SHOW_PKT,
                .param1 = cell,
                .param2 = side,
                .param3 = d,
                .size = 0
            };
            printf("[DEBUG] Prepared SHOW packet for cell '%c' at side=%d depth=%d\n", cell, side, d);

            if (player_send_packet(player, &show_pkt, NULL) < 0) {
                printf("[DEBUG] Failed to send SHOW packet for %c (depth=%d, side=%d)\n", player->avatar, d, side);
            } else {
                printf("[DEBUG] Sent SHOW packet for %c: cell=%c, depth=%d, side=%d\n", player->avatar, cell, d, side);
            }
        }
    }

    pthread_mutex_unlock(&player->mutex);
    printf("[DEBUG] Released mutex and exiting player_update_view for %c\n", player->avatar);
}



void player_check_for_laser_hit(PLAYER *player) {
    if (!player) return;

    printf("[DEBUG] Entering player_check_for_laser_hit for %c\n", player->avatar);

    pthread_mutex_lock(&player->mutex);

    if (player->hit_flag) {
        printf("[DEBUG] Player %c was hit by laser (hit_flag=1), processing respawn\n", player->avatar);
        player->hit_flag = 0;

        // Remove from maze
        maze_remove_player(player->avatar, player->row, player->col);

        // Remove score from scoreboard
        MZW_PACKET pkt = {
            .type = MZW_SCORE_PKT,
            .param1 = player->avatar,
            .param2 = -1,
            .size = 0
        };
        player_send_packet(player, &pkt, NULL);

        // 🔁 Re-announce name so gclient keeps it on scoreboard
        player_broadcast_name(player);

        // Send alert
        MZW_PACKET alert = {
            .type = MZW_ALERT_PKT,
            .size = 0
        };
        player_send_packet(player, &alert, NULL);

        // ⬇️ Invalidate views for all other players
        for (int i = 0; i < MAX_PLAYERS; i++) {
            if (players[i] && players[i] != player) {
                player_invalidate_view(players[i]);
                player_update_view(players[i]);
            }
        }

        pthread_mutex_unlock(&player->mutex);
        printf("[DEBUG] Player %c entering purgatory...\n", player->avatar);
        sleep(3);  // purgatory time
        printf("[DEBUG] Player %c exiting purgatory\n", player->avatar);

        // Respawn player
        player_reset(player);
    } else {
        printf("[DEBUG] No laser hit detected for %c (hit_flag=0)\n", player->avatar);
        pthread_mutex_unlock(&player->mutex);
    }

    printf("[DEBUG] Exiting player_check_for_laser_hit for %c\n", player->avatar);
}



void player_send_chat(PLAYER *player, char *msg, size_t len) {
    printf("[DEBUG] Entering player_send_chat for %c\n", player->avatar);

    if (msg == NULL || len == 0) {
        printf("[DEBUG] Exiting player_send_chat early: empty message\n");
        return;
    }

    char full_msg[512];
    const char *name = player_get_name(player);  // safely returns "anonymous" if NULL
    int prefix_len = snprintf(full_msg, sizeof(full_msg), "%s[%c] ", name, player->avatar);
    if (prefix_len < 0 || prefix_len >= (int)sizeof(full_msg)) {
        printf("[DEBUG] Exiting player_send_chat early: snprintf failed\n");
        return;
    }


    size_t max_msg_len = sizeof(full_msg) - prefix_len - 1;
    size_t to_copy = (len < max_msg_len) ? len : max_msg_len;
    memcpy(full_msg + prefix_len, msg, to_copy);
    full_msg[prefix_len + to_copy] = '\0';

    MZW_PACKET pkt = {
        .type = MZW_CHAT_PKT,
        .size = strlen(full_msg)
    };

    printf("[DEBUG] Player %c sending chat: %s\n", player->avatar, full_msg);

    for (int i = 0; i < MAX_PLAYERS; i++) {
        PLAYER *recipient = player_get('A' + i);
        if (recipient) {
            player_send_packet(recipient, &pkt, full_msg);
            player_unref(recipient, "chat");
        }
    }

    printf("[DEBUG] Exiting player_send_chat for %c\n", player->avatar);
}

const char *player_get_name(PLAYER *player) {
    if (player && player->name)
        return player->name;
    return "Anonymous";
}


int player_get_score(PLAYER *player) {
    return player->score;
}

char player_get_avatar(PLAYER *player) {
    return player->avatar;
}

