#include "smtp_socket.h"
#include "smtp_session.h"
#include "smtp_protocol.h"
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <getopt.h>

static void print_usage(const char *prog) {
    fprintf(stderr,
        "Usage: %s -f <from> -t <to> [-s subject] [-b body] [-p port]\n"
        "              [-H helo-host] <server>\n",
        prog);
}

#ifdef TEST
#define main main_exclude
#endif

int main(int argc, char *argv[])
{
    if (argc == 1) {
        print_usage(argv[0]);
        return 0;
    }

    const char *port      = "587";
    const char *from      = NULL;
    const char *to        = NULL;
    const char *subject   = "";
    const char *body      = NULL;
    const char *helo_host = "localhost";

    int opt;
    while ((opt = getopt(argc, argv, "f:t:s:b:p:H:")) != -1) {
        switch (opt) {
            case 'f': from = optarg; break;
            case 't': to = optarg; break;
            case 's': subject = optarg; break;
            case 'b': body = optarg; break;
            case 'p': port = optarg; break;
            case 'H': helo_host = optarg; break;
            default:
                print_usage(argv[0]);
                return 1;
        }
    }

    if (!from || !to) {
        print_usage(argv[0]);
        return 1;
    }

    if (optind >= argc || optind != argc - 1) {
        print_usage(argv[0]);
        return 1;
    }

    const char *server = argv[optind];

    // Create socket context and connect 
    smtp_socket_ctx sockctx;
    smtp_session_ctx session = { .sockctx = &sockctx };
    if (smtp_socket_connect(&sockctx, server, port) < 0) {
        fprintf(stderr, "SMTP: connection failed\n");
        return 2;
    }

    // Set callbacks 
    smtp_read_cb  read_cb  = smtp_socket_read;
    smtp_write_cb write_cb = smtp_socket_write;

    char cmd[256];

    // Check greeting 
    char greet[256];
    int code = smtp_read_reply(smtp_socket_read, &session, greet, sizeof(greet));
    if (code != 220) {
        fprintf(stderr, "SMTP: expected 220 greeting but got %d\n", code);
        smtp_socket_close(&sockctx);
        return 2;
    }

    // HELO 
    smtp_build_helo(cmd, sizeof(cmd), helo_host);
    if (smtp_send_cmd(read_cb, write_cb, &session, cmd, 250) < 0) {
        smtp_socket_close(&sockctx);
        return 2;
    }

    // MAIL FROM 
    smtp_build_mail_from(cmd, sizeof(cmd), from);
    if (smtp_send_cmd(read_cb, write_cb, &session, cmd, 250) < 0) {
        smtp_socket_close(&sockctx);
        return 2;
    }

    // RCPT TO 
    smtp_build_rcpt_to(cmd, sizeof(cmd), to);
    if (smtp_send_cmd(read_cb, write_cb, &session, cmd, 250) < 0) {
        smtp_socket_close(&sockctx);
        return 2;
    }

    // DATA 
    smtp_build_data_cmd(cmd, sizeof(cmd));
    if (smtp_send_cmd(read_cb, write_cb, &session, cmd, 354) < 0) {
        smtp_socket_close(&sockctx);
        return 2;
    }

    // Message body 
    if (smtp_send_message(read_cb, write_cb, &session,
                          from, to, subject, body) < 0) {
        smtp_socket_close(&sockctx);
        return 2;
    }

    // QUIT 
    write_cb(&session, "QUIT\r\n", 6);
    smtp_read_reply(read_cb, &session, cmd, sizeof(cmd));

    smtp_socket_close(&sockctx);
    return 0;
}