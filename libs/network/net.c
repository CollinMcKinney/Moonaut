/*
 * net.c - Cross-platform implementation using BSD Sockets / Winsock2.
 * Compiles on Windows, Linux, and macOS.
 */

#include "net.h"

/* ---------- Internal socket struct ---------- */
struct net_socket_t {
    SOCKET fd;
};

/* ---------- Global init / cleanup ---------- */
void net_init(void) {
#ifdef _WIN32
    WSADATA wsa;
    WSAStartup(MAKEWORD(2,2), &wsa);
#endif
}

void net_cleanup(void) {
#ifdef _WIN32
    WSACleanup();
#endif
}

/* ---------- Socket creation ---------- */
net_socket net_create_udp_socket(void) {
    struct net_socket_t *s = (struct net_socket_t*)malloc(sizeof(struct net_socket_t));
    if (!s) return NET_INVALID_SOCKET;
    s->fd = socket(AF_INET, SOCK_DGRAM, 0);
    if (s->fd == INVALID_SOCKET) { free(s); return NET_INVALID_SOCKET; }
    return s;
}

net_socket net_create_tcp_socket(void) {
    struct net_socket_t *s = (struct net_socket_t*)malloc(sizeof(struct net_socket_t));
    if (!s) return NET_INVALID_SOCKET;
    s->fd = socket(AF_INET, SOCK_STREAM, 0);
    if (s->fd == INVALID_SOCKET) { free(s); return NET_INVALID_SOCKET; }
    return s;
}

void net_close_socket(net_socket sock) {
    if (sock) {
        closesocket(sock->fd);
        free(sock);
    }
}

/* ---------- TCP helpers ---------- */
net_socket net_tcp_connect(const char *ip, unsigned short port) {
    net_socket s = net_create_tcp_socket();
    if (s == NET_INVALID_SOCKET) return NET_INVALID_SOCKET;
    struct sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_port = net_htons(port);
    addr.sin_addr.s_addr = inet_addr(ip);
    if (connect(s->fd, (struct sockaddr*)&addr, sizeof(addr)) == SOCKET_ERROR) {
        net_close_socket(s);
        return NET_INVALID_SOCKET;
    }
    return s;
}

int net_tcp_send(net_socket sock, const char *data, int len) {
    if (!sock) return -1;
    return send(sock->fd, data, len, 0);
}

int net_tcp_recv(net_socket sock, char *buf, int size) {
    if (!sock) return -1;
    return recv(sock->fd, buf, size, 0);
}

int net_tcp_bind_listen(net_socket sock, unsigned short port, int backlog) {
    if (!sock) return -1;
    int opt = 1;
    setsockopt(sock->fd, SOL_SOCKET, SO_REUSEADDR, (char*)&opt, sizeof(opt));
    struct sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_port = net_htons(port);
    addr.sin_addr.s_addr = INADDR_ANY;
    if (bind(sock->fd, (struct sockaddr*)&addr, sizeof(addr)) == SOCKET_ERROR)
        return -1;
    if (listen(sock->fd, backlog) == SOCKET_ERROR)
        return -1;
    return 0;
}

net_socket net_tcp_accept(net_socket listen_sock, struct sockaddr_in *client_addr) {
    if (!listen_sock) return NET_INVALID_SOCKET;
    socklen_t len = sizeof(*client_addr);
    SOCKET newfd = accept(listen_sock->fd, (struct sockaddr*)client_addr, &len);
    if (newfd == INVALID_SOCKET) return NET_INVALID_SOCKET;
    struct net_socket_t *s = (struct net_socket_t*)malloc(sizeof(struct net_socket_t));
    if (!s) { closesocket(newfd); return NET_INVALID_SOCKET; }
    s->fd = newfd;
    return s;
}

/* ---------- UDP helpers ---------- */
int net_udp_bind(net_socket sock, unsigned short port) {
    if (!sock) return -1;
    struct sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_port = net_htons(port);
    addr.sin_addr.s_addr = INADDR_ANY;
    return bind(sock->fd, (struct sockaddr*)&addr, sizeof(addr));
}

int net_udp_sendto(net_socket sock, const void *data, int len,
                   const struct sockaddr_in *dest) {
    if (!sock) return -1;
    return sendto(sock->fd, (const char*)data, len, 0,
                  (const struct sockaddr*)dest, sizeof(*dest));
}

int net_udp_recvfrom(net_socket sock, void *buf, int size,
                     struct sockaddr_in *src) {
    if (!sock) return -1;
    socklen_t len = sizeof(*src);
    return recvfrom(sock->fd, (char*)buf, size, 0,
                    (struct sockaddr*)src, &len);
}

int net_udp_getsockname(net_socket sock, struct sockaddr_in *addr) {
    if (!sock) return -1;
    socklen_t len = sizeof(*addr);
    return getsockname(sock->fd, (struct sockaddr*)addr, &len);
}

/* ---------- Select wrappers ---------- */
void net_fd_set(net_socket sock, void *set) {
    if (sock) FD_SET(sock->fd, (fd_set*)set);
}
void net_fd_clear(net_socket sock, void *set) {
    if (sock) FD_CLR(sock->fd, (fd_set*)set);
}
int net_fd_isset(net_socket sock, void *set) {
    if (!sock) return 0;
    return FD_ISSET(sock->fd, (fd_set*)set);
}
void net_fd_zero(void *set) {
    FD_ZERO((fd_set*)set);
}
int net_select(int nfds, void *readfds, void *writefds,
               void *exceptfds, struct timeval *timeout) {
    return select(nfds, (fd_set*)readfds, (fd_set*)writefds,
                  (fd_set*)exceptfds, timeout);
}

/* ---------- Utility ---------- */
unsigned short net_htons(unsigned short hostshort) {
    return htons(hostshort);
}
unsigned long net_htonl(unsigned long hostlong) {
    return htonl(hostlong);
}
const char* net_inet_ntoa(struct in_addr in) {
    return inet_ntoa(in);
}