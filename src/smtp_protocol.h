#ifndef SMTP_PROTOCOL_H
#define SMTP_PROTOCOL_H

#include <stddef.h>

/* Basic smtp functions without touching a socket 
*  including helo, mail from, rcpt to, the data cmd, 
*  and msg headers. Dot stuffing is also included as
*  well as to functions to help parse lines.
*/
int smtp_parse_reply_code(const char *line);
int smtp_is_final_reply_line(const char *line);

int smtp_build_helo(char *out, size_t out_sz, const char *hostname);
int smtp_build_mail_from(char *out, size_t out_sz, const char *from);
int smtp_build_rcpt_to(char *out, size_t out_sz, const char *to);
int smtp_build_data_cmd(char *out, size_t out_sz);

int smtp_dot_stuff_line(const char *line, char *out, size_t out_sz);

int smtp_build_headers(char *out, size_t out_sz, const char *from,
                       const char *to, const char *subject);

#endif