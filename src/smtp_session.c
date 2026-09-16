#include "smtp_protocol.h"
#include "smtp_session.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>

int smtp_read_line(smtp_read_cb read_cb, void *ctx,
                   char *line, size_t line_sz)
{
    smtp_session_ctx *s = ctx;   // session context with buffer

    for (;;) {

        // Check if buffer already contains a full line
        for (size_t i = 0; i < s->buf_len; i++) {
            if (s->buf[i] == '\n') {

                // Copy up to newline
                size_t copy_len = (i < line_sz - 1) ? i : (line_sz - 1);
                memcpy(line, s->buf, copy_len);
                line[copy_len] = '\0';

                // Remove CR if present
                if (copy_len > 0 && line[copy_len - 1] == '\r')
                    line[copy_len - 1] = '\0';

                // Shift remaining bytes down
                size_t remaining = s->buf_len - (i + 1);
                memmove(s->buf, s->buf + i + 1, remaining);
                s->buf_len = remaining;

                return 0;
            }
        }

        // More data is still coming
        ssize_t n = read_cb(ctx, s->buf + s->buf_len, sizeof(s->buf) - s->buf_len);

        if (n <= 0) {
            fprintf(stderr, "SMTP: read failed\n");
            return -1;
        }

        s->buf_len += (size_t)n;

        // Overflow protection
        if (s->buf_len >= sizeof(s->buf)) {
            fprintf(stderr, "SMTP: line too long\n");
            s->buf_len = 0;
            return -1;
        }
    }
}

// Read reply reads from socket, returns numberic code
// and stores final line text for error messages 
int smtp_read_reply(smtp_read_cb read_cb, void *ctx,
                    char *lastline, size_t lastline_sz)
{
    char line[2048];
    int code = -1;

    for (;;) {

        // Read exactly one line using the buffered line reader 
        if (smtp_read_line(read_cb, ctx, line, sizeof(line)) < 0) {
            fprintf(stderr, "SMTP: failed to read reply line\n");
            return -1;
        }

        // Parse the numeric code 
        code = smtp_parse_reply_code(line);
        if (code < 0) {
            fprintf(stderr, "SMTP: malformed reply: %s\n", line);
            return -1;
        }

        // Check if this is the final reply line 
        if (smtp_is_final_reply_line(line)) {
            if (lastline && lastline_sz > 0) {
                strncpy(lastline, line, lastline_sz - 1);
                lastline[lastline_sz - 1] = '\0';
            }
            return code;
        }

        // If not the final line, continue reading more lines
    }
}

// Send a command and expect a specific reply code
int smtp_send_cmd(smtp_read_cb read_cb, smtp_write_cb write_cb,
                  void *ctx, const char *cmd, int expect)
{
    // Send the command line through the transport
    ssize_t n = write_cb(ctx, cmd, strlen(cmd));
    if (n < 0) {
        fprintf(stderr, "SMTP: write failed\n");
        return -1;
    }

    // Read the reply using the session-layer reply reader
    char lastline[2048];
    int code = smtp_read_reply(read_cb, ctx, lastline, sizeof(lastline));
    if (code < 0) {
        // error msg already printed from smtp_read_reply
        return -1;
    }

    // Check expected reply code
    if (code != expect) {
        fprintf(stderr,
                "SMTP: expected %d but server replied %d: %s\n",
                expect, code, lastline);
        return -1;
    }

    return 0;
}

// Build the message
int smtp_send_message(smtp_read_cb read_cb, smtp_write_cb write_cb,
                      void *ctx, const char *from, const char *to,
                      const char *subject, const char *body)
{
    char hdr[2048];

    // Build headers using protocol helper 
    if (smtp_build_headers(hdr, sizeof(hdr), from, to, subject) < 0) {
        fprintf(stderr, "SMTP: header build failed\n");
        return -1;
    }

    // Send headers 
    if (write_cb(ctx, hdr, strlen(hdr)) < 0) {
        fprintf(stderr, "SMTP: write headers failed\n");
        return -1;
    }

    // Send the body 
    if (body) {
        char *tmp = strdup(body);
        /* GCOVR_EXCL_START */
        if (!tmp) {         // branch calls a library dependency so exclude from test
            fprintf(stderr, "SMTP: strdup failed\n");
            return -1;
        }
        /* GCOVR_EXCL_STOP */

        char *line = strtok(tmp, "\n");
        while (line) {
            char stuffed[2048];
            if (smtp_dot_stuff_line(line, stuffed, sizeof(stuffed)) < 0) {
                fprintf(stderr, "SMTP: dot‑stuff failed\n");
                free(tmp);
                return -1;
            }

            if (write_cb(ctx, stuffed, strlen(stuffed)) < 0 ||
                write_cb(ctx, "\r\n", 2) < 0) {
                fprintf(stderr, "SMTP: write body line failed\n");
                free(tmp);
                return -1;
            }

            line = strtok(NULL, "\n");
        }

        free(tmp);
    } else {
        // Read body from stdin
        char linebuf[2048];
        while (fgets(linebuf, sizeof(linebuf), stdin)) {
            size_t len = strlen(linebuf);
            if (len > 0 && linebuf[len - 1] == '\n')
                linebuf[len - 1] = '\0';

            char stuffed[2048];
            if (smtp_dot_stuff_line(linebuf, stuffed, sizeof(stuffed)) < 0) {
                fprintf(stderr, "SMTP: dot‑stuff failed\n");
                return -1;
            }

            if (write_cb(ctx, stuffed, strlen(stuffed)) < 0 ||
                write_cb(ctx, "\r\n", 2) < 0) {
                fprintf(stderr, "SMTP: write stdin line failed\n");
                return -1;
            }
        }
    }

    /* End of DATA block */
    if (write_cb(ctx, ".\r\n", 3) < 0) {
        fprintf(stderr, "SMTP: write terminator failed\n");
        return -1;
    }

    /* Expect final 250 reply */
    char lastline[2048];
    int code = smtp_read_reply(read_cb, ctx, lastline, sizeof(lastline));
    if (code != 250) {
        fprintf(stderr, "SMTP: expected 250 after message but got %d: %s\n",
                code, lastline);
        return -1;
    }

    return 0;
}
