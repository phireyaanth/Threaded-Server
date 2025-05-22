#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <errno.h>
#include <arpa/inet.h>

#include "protocol.h"
#include "debug.h"

#define HEADER_SIZE sizeof(MZW_PACKET)

/*
 * Helper to write `count` bytes to fd.
 * Handles partial writes and EINTR.
 */
static ssize_t write_all(int fd, const void *buf, size_t count) {
    printf("[DEBUG] Entering write_all\n");

    size_t written = 0;
    const char *ptr = buf;
    while (written < count) {
        ssize_t w = write(fd, ptr + written, count - written);
        if (w < 0) {
            if (errno == EINTR) continue;
            printf("[DEBUG] Exiting write_all with error\n");
            return -1;
        }
        if (w == 0) break;
        written += w;
    }

    printf("[DEBUG] Exiting write_all\n");
    return written == count ? 0 : -1;
}


/*
 * Helper to read `count` bytes from fd.
 * Handles partial reads and EINTR.
 */
static ssize_t read_all(int fd, void *buf, size_t count) {
    printf("[DEBUG] Entering read_all\n");

    size_t read_bytes = 0;
    char *ptr = buf;
    while (read_bytes < count) {
        ssize_t r = read(fd, ptr + read_bytes, count - read_bytes);
        if (r < 0) {
            if (errno == EINTR) continue;
            printf("[DEBUG] Exiting read_all with error\n");
            return -1;
        }
        if (r == 0) break;  // EOF
        read_bytes += r;
    }

    printf("[DEBUG] Exiting read_all\n");
    return read_bytes == count ? 0 : -1;
}


/*
 * Send a packet with optional payload.
 */
int proto_send_packet(int fd, MZW_PACKET *pkt, void *data) {
    printf("[DEBUG] Entering proto_send_packet\n");

    if (!pkt || fd < 0) {
        errno = EINVAL;
        printf("[DEBUG] Exiting proto_send_packet with error: invalid arguments\n");
        return -1;
    }

    // Convert fields to network byte order (copy so we don't modify original)
    MZW_PACKET net_pkt = *pkt;
    net_pkt.size = htons(pkt->size);
    net_pkt.timestamp_sec = htonl(pkt->timestamp_sec);
    net_pkt.timestamp_nsec = htonl(pkt->timestamp_nsec);

    // Send packet header
    if (write_all(fd, &net_pkt, HEADER_SIZE) < 0) {
        printf("[DEBUG] Exiting proto_send_packet with error: failed to write header\n");
        return -1;
    }

    // Send optional payload
    if (pkt->size > 0 && data != NULL) {
        if (write_all(fd, data, pkt->size) < 0) {
            printf("[DEBUG] Exiting proto_send_packet with error: failed to write payload\n");
            return -1;
        }
    }

    printf("[DEBUG] Exiting proto_send_packet successfully\n");
    return 0;
}


/*
 * Receive a packet, and optional payload (malloc'd if present).
 */
int proto_recv_packet(int fd, MZW_PACKET *pkt, void **datap) {
    printf("[DEBUG] Entering proto_recv_packet\n");

    if (!pkt || fd < 0 || !datap) {
        errno = EINVAL;
        printf("[DEBUG] Exiting proto_recv_packet with error: invalid arguments\n");
        return -1;
    }

    *datap = NULL;

    // Read packet header
    MZW_PACKET net_pkt;
    if (read_all(fd, &net_pkt, HEADER_SIZE) < 0) {
        printf("[DEBUG] Exiting proto_recv_packet with error: failed to read header\n");
        return -1;
    }

    // Convert fields back to host byte order
    pkt->type = net_pkt.type;
    pkt->param1 = net_pkt.param1;
    pkt->param2 = net_pkt.param2;
    pkt->param3 = net_pkt.param3;
    pkt->size = ntohs(net_pkt.size);
    pkt->timestamp_sec = ntohl(net_pkt.timestamp_sec);
    pkt->timestamp_nsec = ntohl(net_pkt.timestamp_nsec);

    // If there is a payload, read it
    if (pkt->size > 0) {
        void *payload = malloc(pkt->size);
        if (!payload) {
            printf("[DEBUG] Exiting proto_recv_packet with error: malloc failed\n");
            return -1;
        }

        if (read_all(fd, payload, pkt->size) < 0) {
            free(payload);
            printf("[DEBUG] Exiting proto_recv_packet with error: failed to read payload\n");
            return -1;
        }

        *datap = payload;
    }

    printf("[DEBUG] Exiting proto_recv_packet successfully\n");
    return 0;
}
