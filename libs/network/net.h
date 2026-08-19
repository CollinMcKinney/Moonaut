/*
 * net.h - Platform-agnostic networking API.
 * Pure ANSI C89 / ISO C90.
 *
 * To port to a new platform, re-implement net.c using that platform's
 * native sockets (e.g., Xbox, PlayStation, Nintendo SDKs).
 */

#ifndef NET_H
#define NET_H

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <math.h>
#include <stdarg.h>

/* ---------- Platform-specific headers ---------- */
#ifdef _WIN32
  #include <winsock2.h>
  #include <ws2tcpip.h>
  #pragma comment(lib, "ws2_32.lib")
  typedef int socklen_t;
  #define SHUT_RDWR SD_BOTH
#else
  #include <sys/socket.h>
  #include <netinet/in.h>
  #include <arpa/inet.h>
  #include <sys/select.h>
  #include <unistd.h>
  #include <fcntl.h>
  #include <errno.h>
  #define SOCKET int
  #define INVALID_SOCKET (-1)
  #define SOCKET_ERROR (-1)
  #define closesocket close
#endif

/* ---------- Type definitions ---------- */
typedef unsigned char  uint8;
typedef unsigned short uint16;
typedef unsigned int   uint32;
typedef signed int     int32;

#define TRUE  1
#define FALSE 0
typedef int bool;

/* ---------- Socket handle (opaque) ---------- */
typedef struct net_socket_t* net_socket;

/* ---------- Constants ---------- */
#define NET_INVALID_SOCKET ((net_socket)0)
#define MAX_PACKET_SIZE    1400

/* ---------- Initialisation / cleanup ---------- */
void net_init(void);
void net_cleanup(void);

/* ---------- Socket creation ---------- */
net_socket net_create_udp_socket(void);
net_socket net_create_tcp_socket(void);
void       net_close_socket(net_socket sock);

/* ---------- TCP helpers ---------- */
net_socket net_tcp_connect(const char *ip, unsigned short port);
int        net_tcp_send(net_socket sock, const char *data, int len);
int        net_tcp_recv(net_socket sock, char *buf, int size);
int        net_tcp_bind_listen(net_socket sock, unsigned short port, int backlog);
net_socket net_tcp_accept(net_socket listen_sock, struct sockaddr_in *client_addr);

/* ---------- UDP helpers ---------- */
int net_udp_bind(net_socket sock, unsigned short port);
int net_udp_sendto(net_socket sock, const void *data, int len,
                   const struct sockaddr_in *dest);
int net_udp_recvfrom(net_socket sock, void *buf, int size,
                     struct sockaddr_in *src);
int net_udp_getsockname(net_socket sock, struct sockaddr_in *addr);

/* ---------- Select / polling ---------- */
void net_fd_set(net_socket sock, void *set);
void net_fd_clear(net_socket sock, void *set);
int  net_fd_isset(net_socket sock, void *set);
void net_fd_zero(void *set);
int  net_select(int nfds, void *readfds, void *writefds,
                void *exceptfds, struct timeval *timeout);

/* ---------- Utility ---------- */
unsigned short net_htons(unsigned short hostshort);
unsigned long  net_htonl(unsigned long hostlong);
const char*    net_inet_ntoa(struct in_addr in);

#endif /* NET_H */