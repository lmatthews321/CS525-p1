#define _GNU_SOURCE
#include "smtp_session.h"
#include "smtp_socket.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>
#include <errno.h>
#include <netdb.h>
#include <sys/types.h>
#include <sys/socket.h>


// Connect to server and return a context with the socket 
int smtp_socket_connect(smtp_socket_ctx *ctx, const char *host, const char *port)
{
    struct addrinfo hints, *res, *rp;
    int sock = -1;

    memset(&hints, 0, sizeof(hints));
    hints.ai_family   = AF_UNSPEC;      // IP4 and IP6 addresses
    hints.ai_socktype = SOCK_STREAM;    // TCP
    // DNS resolution
    int err = getaddrinfo(host, port, &hints, &res);
    if (err != 0) {
        fprintf(stderr, "SMTP: getaddrinfo: %s\n", gai_strerror(err));
        return -1;
    }
    // Try each address and create a socket
    for (rp = res; rp != NULL; rp = rp->ai_next) {
        sock = socket(rp->ai_family, rp->ai_socktype, rp->ai_protocol);
        /* GCOVR_EXCL_START */
        if (sock < 0)           // branch  calls a system dependancy so exclude from test
            continue;
        /* GCOVR_EXCL_STOP */
        if (connect(sock, rp->ai_addr, rp->ai_addrlen) == 0)
            break;   // success

        close(sock);
        sock = -1;
    }

    freeaddrinfo(res);  // free memory

    if (sock < 0) {
        fprintf(stderr, "SMTP: connect failed: %s\n", strerror(errno));
        return -1;
    }

    ctx->sock = sock;
    return 0;
}

// Callback read wrapper 
ssize_t smtp_socket_read(void *ctx, char *buf, size_t max)
{
    smtp_session_ctx *s = ctx;
    return recv(s->sockctx->sock, buf, max, 0);
}

// Callback write wrapper 
ssize_t smtp_socket_write(void *ctx, const char *buf, size_t len)
{
    smtp_session_ctx *s = ctx;
    return send(s->sockctx->sock, buf, len, 0);
}

// Close the socket 
void smtp_socket_close(smtp_socket_ctx *ctx)
{
    if (ctx && ctx->sock >= 0)
        close(ctx->sock);
}
