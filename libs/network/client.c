/*
 * client.c - Secure Game Client.
 * Uses net.h for all network I/O.
 */

#include "net.h"

#define MASTER_IP   "127.0.0.1"
#define MASTER_TCP  5555
#define MAX_LINE    256

/* Helper: read a line from TCP socket (uses net_tcp_recv) */
static int read_line(net_socket sock, char *buf, int size) {
    static char recv_buf[4096];
    static int recv_len = 0, recv_pos = 0;
    int n;
    char *newline;

    while (1) {
        newline = memchr(recv_buf + recv_pos, '\n', recv_len - recv_pos);
        if (newline) {
            int line_len = (int)(newline - (recv_buf + recv_pos));
            if (line_len >= size - 1) line_len = size - 1;
            memcpy(buf, recv_buf + recv_pos, line_len);
            buf[line_len] = '\0';
            recv_pos += line_len + 1;
            if (recv_pos >= recv_len) { recv_pos = 0; recv_len = 0; }
            return line_len;
        }
        if (recv_len >= 4096 - 1) { recv_pos = 0; recv_len = 0; }
        n = net_tcp_recv(sock, recv_buf + recv_len, 4096 - recv_len - 1);
        if (n <= 0) return -1;
        recv_len += n;
        recv_buf[recv_len] = '\0';
    }
}

int main(int argc, char *argv[]) {
    net_socket tcp_sock;
    char line[MAX_LINE];
    char username[32], password[32];
    char session_token[64];
    uint32 match_id;
    char server_ip[16];
    uint16 server_port;
    net_socket udp_sock = NET_INVALID_SOCKET;
    struct sockaddr_in srv_addr;
    float stick_x = 0.0f, stick_y = 0.0f;
    uint8 buttons = 0;

    net_init();

    printf("=== Game Client (Secure) ===\n");
    printf("Enter username: "); fgets(username, sizeof(username), stdin);
    username[strlen(username)-1] = '\0';
    printf("Enter password: "); fgets(password, sizeof(password), stdin);
    password[strlen(password)-1] = '\0';

    /* Connect to master */
    tcp_sock = net_tcp_connect(MASTER_IP, MASTER_TCP);
    if (tcp_sock == NET_INVALID_SOCKET) {
        printf("Cannot connect to master\n");
        net_cleanup();
        return 1;
    }

    /* Register */
    sprintf(line, "REGISTER %s %s\n", username, password);
    net_tcp_send(tcp_sock, line, strlen(line));
    read_line(tcp_sock, line, sizeof(line));
    printf("Register: %s", line);
    if (strncmp(line, "ERR", 3) == 0) {
        net_close_socket(tcp_sock);
        net_cleanup();
        return 1;
    }

    /* Login */
    sprintf(line, "LOGIN %s %s\n", username, password);
    net_tcp_send(tcp_sock, line, strlen(line));
    read_line(tcp_sock, line, sizeof(line));
    if (strncmp(line, "ERR", 3) == 0) {
        printf("Login failed: %s", line);
        net_close_socket(tcp_sock);
        net_cleanup();
        return 1;
    }
    sscanf(line, "OK %s", session_token);
    printf("Logged in. Token: %s\n", session_token);

    /* ---------- SECURE STEP: Create UDP socket and tell master our port ---------- */
    udp_sock = net_create_udp_socket();
    if (udp_sock == NET_INVALID_SOCKET) {
        printf("Failed to create UDP socket\n");
        net_close_socket(tcp_sock);
        net_cleanup();
        return 1;
    }

    /* Bind to an ephemeral port (0 = let OS choose) */
    if (net_udp_bind(udp_sock, 0) != 0) {
        printf("Failed to bind UDP socket\n");
        net_close_socket(udp_sock);
        net_close_socket(tcp_sock);
        net_cleanup();
        return 1;
    }

    /* Retrieve the port the OS assigned */
    {
        struct sockaddr_in actual_addr;
        if (net_udp_getsockname(udp_sock, &actual_addr) != 0) {
            printf("Failed to get UDP socket name\n");
            net_close_socket(udp_sock);
            net_close_socket(tcp_sock);
            net_cleanup();
            return 1;
        }
        uint16 local_udp_port = ntohs(actual_addr.sin_port);
        sprintf(line, "UDP_PORT %u\n", (unsigned int)local_udp_port);
        net_tcp_send(tcp_sock, line, strlen(line));
        read_line(tcp_sock, line, sizeof(line));
        printf("Master: %s", line);
        if (strncmp(line, "ERR", 3) == 0) {
            printf("Failed to register UDP port\n");
            net_close_socket(udp_sock);
            net_close_socket(tcp_sock);
            net_cleanup();
            return 1;
        }
        printf("UDP port %u registered with master\n", local_udp_port);
    }

    /* Request match */
    sprintf(line, "MATCH %s US-EAST\n", session_token);
    net_tcp_send(tcp_sock, line, strlen(line));
    while (1) {
        read_line(tcp_sock, line, sizeof(line));
        printf("Master: %s", line);
        if (strncmp(line, "MATCH_FOUND", 11) == 0) {
            sscanf(line, "MATCH_FOUND %u %s %hu", &match_id, server_ip, &server_port);
            break;
        }
        if (strncmp(line, "ERR", 3) == 0) {
            net_close_socket(tcp_sock);
            net_cleanup();
            return 1;
        }
        /* 'QUEUED' or other messages - just wait */
    }
    net_close_socket(tcp_sock);
    printf("Match %u assigned to server %s:%u\n", match_id, server_ip, server_port);

    /* Connect to dedicated server via UDP */
    memset(&srv_addr, 0, sizeof(srv_addr));
    srv_addr.sin_family = AF_INET;
    srv_addr.sin_port = net_htons(server_port);
    srv_addr.sin_addr.s_addr = inet_addr(server_ip);

    printf("Sending inputs (demo). Press Ctrl+C to exit.\n");

    {
        fd_set fds;
        struct timeval tv;
        float t = 0.0f;
        uint8 packet[10];

        while (1) {
            /* Send only raw inputs - NO player ID, NO position */
            packet[0] = 0x01;
            *(float*)(packet + 1) = stick_x;
            *(float*)(packet + 5) = stick_y;
            packet[9] = buttons;
            net_udp_sendto(udp_sock, packet, 10, &srv_addr);

            /* Check for incoming game state */
            net_fd_zero(&fds);
            net_fd_set(udp_sock, &fds);
            tv.tv_sec = 0;
            tv.tv_usec = 10000;
            if (net_select(0, &fds, NULL, NULL, &tv) > 0) {
                uint8 buf[128];
                struct sockaddr_in from;
                int n = net_udp_recvfrom(udp_sock, buf, sizeof(buf), &from);
                if (n > 0 && buf[0] == 0x10) {
                    float bx = *(float*)(buf + 1);
                    float by = *(float*)(buf + 5);
                    float px = *(float*)(buf + 9);
                    float py = *(float*)(buf + 13);
                    printf("Ball: %.2f,%.2f  MyPos: %.2f,%.2f\n", bx, by, px, py);
                }
            }

            /* Demo input: sine wave movement */
            t += 0.05f;
            stick_x = 10.0f * sinf(t);
            stick_y = 10.0f * cosf(t);
            if (t > 3.14f) buttons ^= 1;  /* toggle jump every ~3 seconds */
        }
    }

    net_close_socket(udp_sock);
    net_cleanup();
    return 0;
}