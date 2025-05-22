#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <pthread.h>
#include "maze.h"
#include "debug.h"

static OBJECT **maze = NULL;
static int rows = 0;
static int cols = 0;
static pthread_mutex_t maze_mutex = PTHREAD_MUTEX_INITIALIZER;

void maze_init(char **template) {
    printf("[DEBUG] Entering maze_init\n");
    rows = 0;
    while (template[rows] != NULL) rows++;
    cols = strlen(template[0]);

    maze = malloc(rows * sizeof(OBJECT *));
    for (int r = 0; r < rows; r++) {
        maze[r] = malloc(cols * sizeof(OBJECT));
        memcpy(maze[r], template[r], cols);
    }
    printf("[DEBUG] Maze initialized with %d rows and %d cols\n", rows, cols);
}

void maze_fini() {
    printf("[DEBUG] Entering maze_fini\n");
    if (!maze) return;
    for (int r = 0; r < rows; r++)
        free(maze[r]);
    free(maze);
    maze = NULL;
    printf("[DEBUG] Maze finalized\n");
}

int maze_get_rows() {
    return rows;
}

int maze_get_cols() {
    return cols;
}

int maze_set_player(OBJECT avatar, int row, int col) {
    printf("[DEBUG] Entering maze_set_player: avatar=%c, row=%d, col=%d\n", avatar, row, col);
    pthread_mutex_lock(&maze_mutex);
    if (row < 0 || row >= rows || col < 0 || col >= cols || !IS_EMPTY(maze[row][col])) {
        pthread_mutex_unlock(&maze_mutex);
        return -1;
    }
    maze[row][col] = avatar;
    pthread_mutex_unlock(&maze_mutex);
    printf("[DEBUG] Player %c placed at (%d, %d)\n", avatar, row, col);
    return 0;
}

int maze_set_player_random(OBJECT avatar, int *rowp, int *colp) {
    printf("[DEBUG] Entering maze_set_player_random for avatar %c\n", avatar);
    const int MAX_ATTEMPTS = 1000;
    for (int attempts = 0; attempts < MAX_ATTEMPTS; attempts++) {
        int r = rand() % rows;
        int c = rand() % cols;
        if (maze_set_player(avatar, r, c) == 0) {
            if (rowp) *rowp = r;
            if (colp) *colp = c;
            printf("[DEBUG] Successfully placed avatar %c at random (%d, %d)\n", avatar, r, c);
            return 0;
        }
    }
    printf("[DEBUG] Failed to place avatar %c randomly after %d attempts\n", avatar, MAX_ATTEMPTS);
    return -1;
}

void maze_remove_player(OBJECT avatar, int row, int col) {
    printf("[DEBUG] Entering maze_remove_player: avatar=%c, row=%d, col=%d\n", avatar, row, col);
    pthread_mutex_lock(&maze_mutex);
    if (row >= 0 && row < rows && col >= 0 && col < cols && maze[row][col] == avatar) {
        maze[row][col] = EMPTY;
        printf("[DEBUG] Removed avatar %c from (%d, %d)\n", avatar, row, col);
    }
    pthread_mutex_unlock(&maze_mutex);
}

int maze_move(int row, int col, int dir) {
    printf("[DEBUG] Entering maze_move from (%d, %d) in dir=%d\n", row, col, dir);
    int dr[] = { -1, 0, 1, 0 };
    int dc[] = { 0, -1, 0, 1 };

    pthread_mutex_lock(&maze_mutex);
    if (row < 0 || row >= rows || col < 0 || col >= cols || IS_EMPTY(maze[row][col]) || !IS_AVATAR(maze[row][col])) {
        pthread_mutex_unlock(&maze_mutex);
        return -1;
    }

    int new_row = row + dr[dir];
    int new_col = col + dc[dir];

    if (new_row < 0 || new_row >= rows || new_col < 0 || new_col >= cols || !IS_EMPTY(maze[new_row][new_col])) {
        pthread_mutex_unlock(&maze_mutex);
        return -1;
    }

    maze[new_row][new_col] = maze[row][col];
    maze[row][col] = EMPTY;
    pthread_mutex_unlock(&maze_mutex);
    printf("[DEBUG] Moved player to (%d, %d)\n", new_row, new_col);
    return 0;
}

OBJECT maze_find_target(int row, int col, DIRECTION dir) {
    printf("[DEBUG] Entering maze_find_target from (%d, %d) dir=%d\n", row, col, dir);
    int dr[] = { -1, 0, 1, 0 };
    int dc[] = { 0, -1, 0, 1 };

    pthread_mutex_lock(&maze_mutex);
    while (1) {
        row += dr[dir];
        col += dc[dir];

        if (row < 0 || row >= rows || col < 0 || col >= cols) {
            pthread_mutex_unlock(&maze_mutex);
            return EMPTY;
        }
        if (!IS_EMPTY(maze[row][col])) {
            OBJECT found = maze[row][col];
            pthread_mutex_unlock(&maze_mutex);
            printf("[DEBUG] Found object '%c' at (%d, %d)\n", found, row, col);
            return IS_AVATAR(found) ? found : EMPTY;
        }
    }
}

int maze_get_view(VIEW *view, int row, int col, DIRECTION gaze, int depth) {
    printf("[DEBUG] Entering maze_get_view at (%d, %d) gaze=%d depth=%d\n", row, col, gaze, depth);
    int dr[] = { -1, 0, 1, 0 };
    int dc[] = { 0, -1, 0, 1 };

    pthread_mutex_lock(&maze_mutex);

    int actual_depth = 0;
    for (int d = 0; d < depth; d++) {
        int pos_row = row + dr[gaze] * d;
        int pos_col = col + dc[gaze] * d;

        if (pos_row < 0 || pos_row >= rows || pos_col < 0 || pos_col >= cols) {
            break;
        }

        (*view)[d][CORRIDOR] = maze[pos_row][pos_col];

        DIRECTION left = TURN_LEFT(gaze);
        int lw_row = pos_row + dr[left];
        int lw_col = pos_col + dc[left];
        (*view)[d][LEFT_WALL] = (lw_row >= 0 && lw_row < rows && lw_col >= 0 && lw_col < cols) ? maze[lw_row][lw_col] : '*';

        DIRECTION right = TURN_RIGHT(gaze);
        int rw_row = pos_row + dr[right];
        int rw_col = pos_col + dc[right];
        (*view)[d][RIGHT_WALL] = (rw_row >= 0 && rw_row < rows && rw_col >= 0 && rw_col < cols) ? maze[rw_row][rw_col] : '*';

        actual_depth++;
    }

    pthread_mutex_unlock(&maze_mutex);
    printf("[DEBUG] Completed maze_get_view with depth=%d\n", actual_depth);
    return actual_depth;
}

void show_view(VIEW *view, int depth) {
    printf("[DEBUG] Showing view with depth=%d\n", depth);
    for (int d = 0; d < depth; d++) {
        fprintf(stderr, "%c %c %c\n", (*view)[d][LEFT_WALL], (*view)[d][CORRIDOR], (*view)[d][RIGHT_WALL]);
    }
}

void show_maze() {
    printf("[DEBUG] Showing entire maze\n");
    pthread_mutex_lock(&maze_mutex);
    for (int r = 0; r < rows; r++) {
        for (int c = 0; c < cols; c++) {
            fputc(maze[r][c], stderr);
        }
        fputc('\n', stderr);
    }
    pthread_mutex_unlock(&maze_mutex);
}
