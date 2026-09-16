#ifndef SMTP_SESSION_H
#define SMTP_SESSION_H

#include <stddef.h>
#include <sys/types.h>
#include "smtp_socket.h"

/* Functions to help run an SMTP session without 
*  touching sockets. Call backs are used instead
*  and a per session state is established.
*/
typedef struct {smtp_socket_ctx *sockctx; char buf[2048]; size_t buf_len;} smtp_session_ctx;
typedef ssize_t (*smtp_read_cb)(void *ctx, char *buf, size_t max);
typedef ssize_t (*smtp_write_cb)(void *ctx, const char *buf, size_t len);

int smtp_read_line(smtp_read_cb r, void *ctx, char *line, size_t line_sz);

int smtp_read_reply(smtp_read_cb r, void *ctx, char *lastline, size_t lastline_sz);

int smtp_send_cmd(smtp_read_cb r, smtp_write_cb w, void *ctx, const char *cmd, int expect);

int smtp_send_message(smtp_read_cb r, smtp_write_cb w, void *ctx, const char *from,
                      const char *to, const char *subject, const char *body);

#endif