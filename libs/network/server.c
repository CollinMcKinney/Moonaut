/*
 * server.c - Secure Dedicated Game Server.
 * Uses net.h for all network I/O.
 */

#include "net.h"

#define GAME_UDP_PORT  7777
#define MASTER_IP      "127.0.0.1"
#define MASTER_UDP_PORT 5556
#define MAX_PLAYERS    8
#define TICK_MS        16

struct authorized_player {
    uint32 player_id;
    struct sockaddr_in addr;
    uint8 active;
    uint8 in_match;
    float pos_x, pos_y;
    float last_input_time;
};

static struct authorized_player authorized[MAX_PLAYERS];
static uint8 authorized_count = 0;
static uint32 current_match_id = 0;

static net_socket game_sock = NET_INVALID_SOCKET;
static struct sockaddr_in master_addr;
static int server_running = 1;
static float ball_x = 0.0f, ball_y = 0.0f;
static float ball_vx = 0.5f, ball_vy = 0.3f;
static time_t match_start_time = 0;

/* ---------- Network ---------- */
void init_server_network(void) {
    net_init();
    game_sock = net_create_udp_socket();
    if (game_sock == NET_INVALID_SOCKET) { perror("udp socket"); exit(1); }
    if (net_udp_bind(game_sock, GAME_UDP_PORT) != 0) {
        perror("udp bind"); exit(1);
    }
    memset(&master_addr, 0, sizeof(master_addr));
    master_addr.sin_family = AF_INET;
    master_addr.sin_port = net_htons(MASTER_UDP_PORT);
    master_addr.sin_addr.s_addr = inet_addr(MASTER_IP);
    printf("Dedicated server on port %d\n", GAME_UDP_PORT);
}

void send_heartbeat(void) {
    uint8 packet[64];
    int pos = 0;
    packet[pos++] = 'H';
    strcpy((char*)(packet + pos), "US-EAST"); pos += (int)strlen("US-EAST") + 1;
    packet[pos++] = 4;
    *(uint16*)(packet + pos) = net_htons(GAME_UDP_PORT); pos += 2;
    net_udp_sendto(game_sock, packet, pos, &master_addr);
}

void send_match_result(void) {
    uint8 packet[256];
    int pos = 0, i;
    packet[pos++] = 'R';
    *(uint32*)(packet + pos) = current_match_id; pos += 4;
    packet[pos++] = 0;
    for (i = 0; i < authorized_count; i++) {
        *(uint32*)(packet + pos) = authorized[i].player_id; pos += 4;
        *(int32*)(packet + pos) = (int32)(authorized[i].pos_x * 10); pos += 4;
    }
    *(uint32*)(packet + pos) = (uint32)time(NULL); pos += 4;
    net_udp_sendto(game_sock, packet, pos, &master_addr);
    printf("Match %u result sent\n", current_match_id);
}

/* ---------- Secure assignment ---------- */
void handle_assignment(uint8 *data, int len) {
    uint32 match_id;
    uint8 num_players;
    int pos = 1;
    int i;
    if (len < 6) return;
    match_id = *(uint32*)(data + pos); pos += 4;
    num_players = data[pos++];
    if (num_players > MAX_PLAYERS) num_players = MAX_PLAYERS;

    current_match_id = match_id;
    authorized_count = num_players;
    memset(authorized, 0, sizeof(authorized));
    match_start_time = 0;

    for (i = 0; i < num_players; i++) {
        authorized[i].player_id = *(uint32*)(data + pos); pos += 4;
        memset(&authorized[i].addr, 0, sizeof(struct sockaddr_in));
        authorized[i].addr.sin_family = AF_INET;
        authorized[i].addr.sin_addr.s_addr = *(uint32*)(data + pos); pos += 4;
        authorized[i].addr.sin_port = *(uint16*)(data + pos); pos += 2;
        authorized[i].active = 1;
        authorized[i].in_match = 0;
        authorized[i].pos_x = ((float)(i - (num_players/2))) * 5.0f;
        authorized[i].pos_y = 0.0f;
        printf("Authorized player %u from %s:%u\n",
               authorized[i].player_id,
               net_inet_ntoa(authorized[i].addr.sin_addr),
               ntohs(authorized[i].addr.sin_port));
    }
}

/* ---------- Secure input processing ---------- */
void process_secure_input(struct sockaddr_in *from, uint8 *data, int len) {
    int i;
    float input_x, input_y;
    uint8 buttons;

    if (len < 10 || data[0] != 0x01) return;

    for (i = 0; i < authorized_count; i++) {
        if (authorized[i].active &&
            authorized[i].addr.sin_addr.s_addr == from->sin_addr.s_addr &&
            authorized[i].addr.sin_port == from->sin_port) {
            break;
        }
    }
    if (i == authorized_count) return;

    input_x = *(float*)(data + 1);
    input_y = *(float*)(data + 5);
    buttons = data[9];

    authorized[i].pos_x += input_x * 0.15f;
    authorized[i].pos_y += input_y * 0.15f;
    authorized[i].in_match = 1;
    authorized[i].last_input_time = (float)time(NULL);

    if (authorized[i].pos_x > 50.0f) authorized[i].pos_x = 50.0f;
    if (authorized[i].pos_x < -50.0f) authorized[i].pos_x = -50.0f;
    if (authorized[i].pos_y > 50.0f) authorized[i].pos_y = 50.0f;
    if (authorized[i].pos_y < -50.0f) authorized[i].pos_y = -50.0f;
}

/* ---------- Game tick ---------- */
void game_tick(void) {
    int i, j, pos;
    uint8 out[256];

    ball_x += ball_vx;
    ball_y += ball_vy;
    if (ball_x > 50.0f || ball_x < -50.0f) ball_vx = -ball_vx;
    if (ball_y > 50.0f || ball_y < -50.0f) ball_vy = -ball_vy;

    for (i = 0; i < authorized_count; i++) {
        if (!authorized[i].active || !authorized[i].in_match) continue;

        pos = 0;
        out[pos++] = 0x10;
        *(float*)(out + pos) = ball_x; pos += 4;
        *(float*)(out + pos) = ball_y; pos += 4;
        for (j = 0; j < authorized_count; j++) {
            *(float*)(out + pos) = authorized[j].pos_x; pos += 4;
            *(float*)(out + pos) = authorized[j].pos_y; pos += 4;
        }
        net_udp_sendto(game_sock, out, pos, &authorized[i].addr);
    }

    if (match_start_time == 0) {
        int all_joined = 1;
        for (i = 0; i < authorized_count; i++) {
            if (authorized[i].active && !authorized[i].in_match) {
                all_joined = 0;
                break;
            }
        }
        if (all_joined && authorized_count > 0) {
            match_start_time = time(NULL);
            printf("Match %u started\n", current_match_id);
        }
    }
}

/* ---------- Main loop ---------- */
void main_loop(void) {
    fd_set fds;
    struct timeval tv;
    uint8 buf[MAX_PACKET_SIZE];
    struct sockaddr_in from;
    int n;
    long next_tick = 0;

    while (server_running) {
        net_fd_zero(&fds);
        net_fd_set(game_sock, &fds);
        tv.tv_sec = 0;
        tv.tv_usec = 5000;

        if (net_select(0, &fds, NULL, NULL, &tv) > 0) {
            if (net_fd_isset(game_sock, &fds)) {
                n = net_udp_recvfrom(game_sock, buf, MAX_PACKET_SIZE, &from);
                if (n > 0) {
                    if (buf[0] == 'A') handle_assignment(buf, n);
                    else if (buf[0] == 0x01) process_secure_input(&from, buf, n);
                }
            }
        }

        {
            long now = clock() * 1000 / CLOCKS_PER_SEC;
            if (now >= next_tick) {
                if (authorized_count > 0) game_tick();
                next_tick = now + TICK_MS;
            }
        }

        {
            static time_t last_hb = 0;
            if (time(NULL) - last_hb >= 5) { send_heartbeat(); last_hb = time(NULL); }
        }

        if (match_start_time > 0 && time(NULL) - match_start_time >= 30) {
            send_match_result();
            server_running = 0;
        }
    }
}

int main(void) {
    init_server_network();
    printf("Dedicated server ready. Waiting for assignment...\n");
    main_loop();
    net_close_socket(game_sock);
    net_cleanup();
    return 0;
}