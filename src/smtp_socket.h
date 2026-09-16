#ifndef SMTP_SOCKET_H
#define SMTP_SOCKET_H

#include <stddef.h>
#include <sys/types.h>
/* Functions that actually use the socket (connect and close)
*  plus two wrappers for recv and send. 
*/
typedef struct {int sock;} smtp_socket_ctx;

int smtp_socket_connect(smtp_socket_ctx *ctx, const char *host, const char *port);

ssize_t smtp_socket_read(void *ctx, char *buf, size_t max);
ssize_t smtp_socket_write(void *ctx, const char *buf, size_t len);

void smtp_socket_close(smtp_socket_ctx *ctx);

#endif