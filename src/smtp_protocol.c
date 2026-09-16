#define _GNU_SOURCE
#include "smtp_protocol.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>

// Extract 3 digit code from line
int smtp_parse_reply_code(const char *line)
{
    if (!line || strlen(line) < 3)
        return -1;

    if (!isdigit(line[0]) || !isdigit(line[1]) || !isdigit(line[2]))
        return -1;

    return (line[0] - '0') * 100 +
           (line[1] - '0') * 10 +
           (line[2] - '0');
}

// Determine final reply
int smtp_is_final_reply_line(const char *line)
{
    if (!line || strlen(line) < 4)
        return 0;

    /* Fourth character determines continuation */
    return (line[3] != '-');
}

// Build helo command
int smtp_build_helo(char *out, size_t out_sz, const char *hostname)
{
    if (!out || out_sz == 0 || !hostname)
        return -1;

    int n = snprintf(out, out_sz, "HELO %s\r\n", hostname);
    return (n < 0 || (size_t)n >= out_sz) ? -1 : 0;
}

// Build mail from command
int smtp_build_mail_from(char *out, size_t out_sz, const char *from)
{
    if (!out || out_sz == 0 || !from)
        return -1;

    int n = snprintf(out, out_sz, "MAIL FROM:<%s>\r\n", from);
    return (n < 0 || (size_t)n >= out_sz) ? -1 : 0;
}

// Build smtp rcpt command
int smtp_build_rcpt_to(char *out, size_t out_sz, const char *to)
{
    if (!out || out_sz == 0 || !to)
        return -1;

    int n = snprintf(out, out_sz, "RCPT TO:<%s>\r\n", to);
    return (n < 0 || (size_t)n >= out_sz) ? -1 : 0;
}

// Build DATA command to transition to msg body
int smtp_build_data_cmd(char *out, size_t out_sz)
{
    if (!out || out_sz == 0)
        return -1;

    int n = snprintf(out, out_sz, "DATA\r\n");
    return (n < 0 || (size_t)n >= out_sz) ? -1 : 0;
}

// Dot-stuff a body line to prevent accidental termination
int smtp_dot_stuff_line(const char *line, char *out, size_t out_sz)
{
    if (!line || !out || out_sz == 0)
        return -1;

    if (line[0] == '.') {
        int n = snprintf(out, out_sz, ".%s", line);
        return (n < 0 || (size_t)n >= out_sz) ? -1 : 0;
    }

    int n = snprintf(out, out_sz, "%s", line);
    return (n < 0 || (size_t)n >= out_sz) ? -1 : 0;
}

int smtp_build_headers(char *out, size_t out_sz, const char *from,
                       const char *to, const char *subject)
{
    if (!out || out_sz == 0 || !from || !to || !subject)
        return -1;

    int n = snprintf(out, out_sz,
                     "From: %s\r\n"
                     "To: %s\r\n"
                     "Subject: %s\r\n"
                     "\r\n",
                     from, to, subject);

    return (n < 0 || (size_t)n >= out_sz) ? -1 : 0;
}
