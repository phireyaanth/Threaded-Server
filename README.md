# Threaded-Server
CSE 320
Homework 5 - Spring 2025
Professor Eugene Stark
Due Date: Friday 5/9/2025 @ 11:59pm

INTRODUCTION:
This assignment involves implementing a multi-threaded network game server for a MazeWar game.
You will progressively replace precompiled modules with your own implementations.
Focus areas include: POSIX threads, concurrency safety, network socket programming, and event-driven game logic.

KEY OBJECTIVES:
- Implement main() with port/socket setup and thread spawning (Task I)
- Implement protocol.c: proto_send_packet and proto_recv_packet (Task II)
- Implement client_registry.c for managing client connections (Task III)
- Implement mzw_client_service() in server.c to handle client-server packet logic (Task IV)
- Implement maze.c to manage maze data structures, views, and laser logic (Task V)
- Implement player.c to track avatars, synchronization, chat, scoring, and signals (Task VI)

BUILD AND TESTING:
- Compile with `make` or `make debug` (uses mazewar_debug.a)
- Run server: `./bin/mazewar -p 3333`
- Run graphical client: `util/gclient -p 3333`
- Run test client: `util/tclient -p 3333 [-q]`
- Use Criterion for unit testing (test/mazewar_tests.c)
- Use Valgrind with `--leak-check=full --track-fds=yes` to find memory and FD leaks

NOTES:
- Header files in include/ must not be modified.
- Avoid adding global variables or non-specified functions in modules.
- Use recursive mutexes for PLAYER object locking.
- Use reference counting in PLAYER objects; thread-safe and must free when count is 0.
- SIGNALS: SIGUSR1 is used to interrupt threads hit by lasers. SIGHUP cleanly shuts down server.
- View updates: use full or incremental updates with CLEAR/SHOW packets.
- All player-to-client communication must go through player_send_packet()
- Use shutdown(fd, SHUT_RD) to trigger clean disconnects.

SUBMISSION:
- Only include source files for completed modules.
- Do not include object files or unmodified base code headers.
- Use `git submit hw5` to submit.
