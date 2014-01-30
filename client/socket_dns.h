#ifndef _SOCKET_DNS
#define _SOCKET_DNS 1

#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/nameser.h>
#include <resolv.h>
#include <arpa/inet.h>

#ifdef __cplusplus
extern "C" {
#endif

void init_dns(const char* root_domain);
int socket_dns(int __domain, int __type, int __protocol);
int bind_dns(int sockfd, const struct sockaddr *addr, socklen_t addrlen);
ssize_t sendto_dns(int sockfd, const void *buf, size_t len, int flags, const struct sockaddr *dest_addr, socklen_t addrlen);
ssize_t recvfrom_dns(int sockfd, void *buf, size_t len, int flags, struct sockaddr *src_addr, socklen_t *addrlen);

#ifdef __cplusplus
}
#endif

#endif
