#include <string.h>
#include <stdio.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <netdb.h>
#include <unistd.h>
#include "harness/unity.h"
#include "../src/smtp_protocol.h"
#include "../src/smtp_session.h"

/* Fake socket state used to feed controlled server replies to the callbacks. */
typedef struct {
  smtp_session_ctx session; /* Session buffer required by smtp_read_line(). */
  const char *input;       /* Bytes the fake server will return. */
  size_t input_len;        /* Length of input, excluding its terminating NUL. */
  size_t position;         /* Next unread byte in input. */
  size_t chunk_size;       /* Maximum bytes returned by one read, or zero for all. */
  size_t hangup_at;        /* Position at which reads start returning EOF. */
  char writes[256];        /* Commands captured from the client. */
  size_t write_len;        /* Number of captured command bytes. */
  int fail_write;          /* Make the write callback simulate a transport error. */
  size_t write_calls;      /* Number of write callback invocations. */
  size_t fail_on_write;    /* Fail on this call, or zero to never fail. */
} fake_transport;

/* Reset a fake connection with a server reply and an optional read chunk size. */
static void init_transport(fake_transport *transport, const char *input,
              size_t chunk_size)
{
  transport->input = input;
  transport->input_len = strlen(input);
  transport->position = 0;
  transport->chunk_size = chunk_size;
  transport->hangup_at = (size_t)-1;
  transport->write_len = 0;
  transport->fail_write = 0;
  transport->write_calls = 0;
  transport->fail_on_write = 0;
}

/* Simulate recv(): return input in chunks, or zero to represent a hangup. */
static ssize_t fake_read(void *ctx, char *buf, size_t max)
{
  fake_transport *transport = ctx;
  size_t available;
  size_t count;

  if (transport->position >= transport->input_len ||
    transport->position >= transport->hangup_at)
    return 0;

  available = transport->input_len - transport->position;
  count = available < max ? available : max;
  if (transport->chunk_size > 0 && count > transport->chunk_size)
    count = transport->chunk_size;
  if (transport->hangup_at - transport->position < count)
    count = transport->hangup_at - transport->position;

  memcpy(buf, transport->input + transport->position, count);
  transport->position += count;
  return (ssize_t)count;
}

/* Simulate send(): capture the command and return its complete length. */
static ssize_t fake_write(void *ctx, const char *buf, size_t len)
{
  fake_transport *transport = ctx;

  transport->write_calls++;
  if (transport->fail_write ||
    (transport->fail_on_write > 0 &&
     transport->write_calls >= transport->fail_on_write) ||
    transport->write_len >= sizeof(transport->writes) ||
    len >= sizeof(transport->writes) - transport->write_len)
    return -1;

  memcpy(transport->writes + transport->write_len, buf, len);
  transport->write_len += len;
  transport->writes[transport->write_len] = '\0';
  return (ssize_t)len;
}

void setUp(void) {}
void tearDown(void) {}

/* Reply parsing accepts three digits and rejects malformed or short lines. */
void test_parse_reply_code(void)
{
  TEST_ASSERT_EQUAL_INT(220, smtp_parse_reply_code("220 ready\r\n"));
  TEST_ASSERT_EQUAL_INT(250, smtp_parse_reply_code("250 OK"));
  TEST_ASSERT_EQUAL_INT(-1, smtp_parse_reply_code(NULL));
  TEST_ASSERT_EQUAL_INT(-1, smtp_parse_reply_code("25"));
  TEST_ASSERT_EQUAL_INT(-1, smtp_parse_reply_code("2x0 bad"));
}

/* A hyphen after the status code means more reply lines are coming. */
void test_reply_line_finality(void)
{
  TEST_ASSERT_FALSE(smtp_is_final_reply_line("250-first\r\n"));
  TEST_ASSERT_TRUE(smtp_is_final_reply_line("250 last\r\n"));
  TEST_ASSERT_FALSE(smtp_is_final_reply_line("250"));
  TEST_ASSERT_FALSE(smtp_is_final_reply_line(NULL));
}

/* Command and header builders produce the exact SMTP text and CRLF endings. */
void test_build_commands_and_headers(void)
{
  char output[256];

  TEST_ASSERT_EQUAL_INT(0, smtp_build_helo(output, sizeof(output), "client"));
  TEST_ASSERT_EQUAL_STRING("HELO client\r\n", output);
  TEST_ASSERT_EQUAL_INT(0, smtp_build_mail_from(output, sizeof(output), "a@example.com"));
  TEST_ASSERT_EQUAL_STRING("MAIL FROM:<a@example.com>\r\n", output);
  TEST_ASSERT_EQUAL_INT(0, smtp_build_rcpt_to(output, sizeof(output), "b@example.com"));
  TEST_ASSERT_EQUAL_STRING("RCPT TO:<b@example.com>\r\n", output);
  TEST_ASSERT_EQUAL_INT(0, smtp_build_data_cmd(output, sizeof(output)));
  TEST_ASSERT_EQUAL_STRING("DATA\r\n", output);
  TEST_ASSERT_EQUAL_INT(0, smtp_build_headers(output, sizeof(output),
                        "a@example.com", "b@example.com", "subject"));
  TEST_ASSERT_EQUAL_STRING("From: a@example.com\r\n"
               "To: b@example.com\r\n"
               "Subject: subject\r\n\r\n", output);
}

/* Builders must reject null arguments, zero-sized buffers, and truncation. */
void test_builders_reject_invalid_output_arguments(void)
{
  char output[8];

  TEST_ASSERT_EQUAL_INT(-1, smtp_build_helo(NULL, sizeof(output), "client"));
  TEST_ASSERT_EQUAL_INT(-1, smtp_build_helo(output, 0, "client"));
  TEST_ASSERT_EQUAL_INT(-1, smtp_build_helo(output, sizeof(output), NULL));
  TEST_ASSERT_EQUAL_INT(-1, smtp_build_helo(output, sizeof(output), "client"));
  TEST_ASSERT_EQUAL_INT(-1, smtp_build_mail_from(output, sizeof(output), "from"));
  TEST_ASSERT_EQUAL_INT(-1, smtp_build_mail_from(output, sizeof(output), NULL));
  TEST_ASSERT_EQUAL_INT(-1, smtp_build_rcpt_to(output, sizeof(output), "to"));
  TEST_ASSERT_EQUAL_INT(-1, smtp_build_rcpt_to(output, sizeof(output), NULL));
  TEST_ASSERT_EQUAL_INT(-1, smtp_build_data_cmd(output, 5));
  TEST_ASSERT_EQUAL_INT(-1, smtp_build_data_cmd(NULL, sizeof(output)));
  TEST_ASSERT_EQUAL_INT(-1, smtp_build_headers(output, sizeof(output),
                         "from", "to", "a subject that is too long"));
  TEST_ASSERT_EQUAL_INT(-1, smtp_build_headers(NULL, sizeof(output),
                         "from", "to", "subject"));
}

/* A leading dot is doubled so a body line cannot look like SMTP's terminator. */
void test_dot_stuff_line(void)
{
  char output[32];

  TEST_ASSERT_EQUAL_INT(0, smtp_dot_stuff_line("hello", output, sizeof(output)));
  TEST_ASSERT_EQUAL_STRING("hello", output);
  TEST_ASSERT_EQUAL_INT(0, smtp_dot_stuff_line(".hello", output, sizeof(output)));
  TEST_ASSERT_EQUAL_STRING("..hello", output);
  TEST_ASSERT_EQUAL_INT(-1, smtp_dot_stuff_line(NULL, output, sizeof(output)));
  TEST_ASSERT_EQUAL_INT(-1, smtp_dot_stuff_line("hello", output, 0));
  TEST_ASSERT_EQUAL_INT(-1, smtp_dot_stuff_line(".hello", output, 4));
  TEST_ASSERT_EQUAL_INT(-1, smtp_dot_stuff_line("hello", output, 4));
  TEST_ASSERT_EQUAL_INT(-1, smtp_dot_stuff_line("hello", NULL, sizeof(output)));
}

/* A normal one-line greeting is parsed and returned without its CRLF. */
void test_read_reply_single_line(void)
{
  fake_transport transport;
  char lastline[64];

  memset(&transport, 0, sizeof(transport));
  init_transport(&transport, "220 ready\r\n", 0);
  TEST_ASSERT_EQUAL_INT(220, smtp_read_reply(fake_read, &transport, lastline, sizeof(lastline)));
  TEST_ASSERT_EQUAL_STRING("220 ready", lastline);
}

/* All lines in a multiline reply are consumed; the final line supplies the code. */
void test_read_reply_multiline(void)
{
  fake_transport transport;
  char lastline[64];

  memset(&transport, 0, sizeof(transport));
  init_transport(&transport, "250-first\r\n250 second\r\n", 0);
  TEST_ASSERT_EQUAL_INT(250, smtp_read_reply(fake_read, &transport, lastline, sizeof(lastline)));
  TEST_ASSERT_EQUAL_STRING("250 second", lastline);
  TEST_ASSERT_EQUAL_UINT(0, transport.session.buf_len);
}

/* The line reader must reconstruct a reply split across several reads. */
void test_read_reply_arrives_a_few_bytes_at_a_time(void)
{
  fake_transport transport;
  char lastline[64];

  memset(&transport, 0, sizeof(transport));
  init_transport(&transport, "250 OK\r\n", 2);
  TEST_ASSERT_EQUAL_INT(250, smtp_read_reply(fake_read, &transport, lastline, sizeof(lastline)));
  TEST_ASSERT_EQUAL_STRING("250 OK", lastline);
}

/* A line with no newline before the 2048-byte session buffer is rejected. */
void test_read_reply_buffer_cannot_hold_line(void)
{
  char input[2049];
  fake_transport transport;
  char line[64];

  memset(input, 'x', sizeof(input) - 1);
  input[sizeof(input) - 1] = '\0';
  memset(&transport, 0, sizeof(transport));
  init_transport(&transport, input, 0);
  TEST_ASSERT_EQUAL_INT(-1, smtp_read_line(fake_read, &transport, line, sizeof(line)));
  TEST_ASSERT_EQUAL_UINT(0, transport.session.buf_len);
}

/* EOF before a complete line represents a server hangup and must fail the read. */
void test_read_reply_server_hangs_up_in_middle_of_session(void)
{
  fake_transport transport;
  char lastline[64];

  memset(&transport, 0, sizeof(transport));
  init_transport(&transport, "250-partial", 0);
  transport.hangup_at = strlen("250-partial");
  TEST_ASSERT_EQUAL_INT(-1, smtp_read_reply(fake_read, &transport, lastline, sizeof(lastline)));
}

/* Every command test below uses the same assertion for an unexpected status. */
static void assert_wrong_status(const char *reply, int expected)
{
  fake_transport transport;

  memset(&transport, 0, sizeof(transport));
  init_transport(&transport, reply, 0);
  TEST_ASSERT_EQUAL_INT(-1, smtp_send_cmd(fake_read, fake_write, &transport,
                      "COMMAND\r\n", expected));
}

/* The SMTP session starts only when the server greeting is 220. */
void test_rejects_wrong_greeting_status(void)
{
  fake_transport transport;
  char line[64];

  memset(&transport, 0, sizeof(transport));
  init_transport(&transport, "250 greeting\r\n", 0);
  TEST_ASSERT_NOT_EQUAL(220, smtp_read_reply(fake_read, &transport, line, sizeof(line)));
}

/* Each SMTP phase has its own expected status; any other status stops the session. */
void test_rejects_wrong_helo_status(void) { assert_wrong_status("500 no HELO\r\n", 250); }
void test_rejects_wrong_mail_from_status(void) { assert_wrong_status("530 auth required\r\n", 250); }
void test_rejects_wrong_rcpt_to_status(void) { assert_wrong_status("550 rejected\r\n", 250); }
void test_rejects_wrong_data_status(void) { assert_wrong_status("250 not DATA\r\n", 354); }
void test_rejects_wrong_message_completion_status(void) { assert_wrong_status("552 too large\r\n", 250); }

/* A successful command is written completely and accepts the expected reply. */
void test_send_cmd_writes_command_and_accepts_status(void)
{
  fake_transport transport;

  memset(&transport, 0, sizeof(transport));
  init_transport(&transport, "250 OK\r\n", 0);
  TEST_ASSERT_EQUAL_INT(0, smtp_send_cmd(fake_read, fake_write, &transport,
                       "HELO client\r\n", 250));
  TEST_ASSERT_EQUAL_STRING("HELO client\r\n", transport.writes);
}

/* Command sending reports a failed write and a failed reply read. */
void test_send_cmd_rejects_transport_failures(void)
{
  fake_transport transport;

  memset(&transport, 0, sizeof(transport));
  init_transport(&transport, "250 OK\r\n", 0);
  transport.fail_write = 1;
  TEST_ASSERT_EQUAL_INT(-1, smtp_send_cmd(fake_read, fake_write, &transport,
                                           "HELO client\r\n", 250));

  memset(&transport, 0, sizeof(transport));
  init_transport(&transport, "partial", 0);
  TEST_ASSERT_EQUAL_INT(-1, smtp_send_cmd(fake_read, fake_write, &transport,
                                           "HELO client\r\n", 250));
}

/* A reply can be returned without asking the caller to store its final line. */
void test_read_reply_allows_no_lastline_buffer(void)
{
  fake_transport transport;

  memset(&transport, 0, sizeof(transport));
  init_transport(&transport, "250 OK\r\n", 0);
  TEST_ASSERT_EQUAL_INT(250, smtp_read_reply(fake_read, &transport, NULL, 0));
}

/* A complete message writes headers, dot-stuffed body lines, and the terminator. */
void test_send_message_with_body(void)
{
  fake_transport transport;

  memset(&transport, 0, sizeof(transport));
  init_transport(&transport, "250 queued\r\n", 0);
  TEST_ASSERT_EQUAL_INT(0, smtp_send_message(fake_read, fake_write, &transport,
                                             "from@example.com", "to@example.com",
                                             "subject", ".secret\nplain"));
  TEST_ASSERT_NOT_NULL(strstr(transport.writes, "From: from@example.com\r\n"));
  TEST_ASSERT_NOT_NULL(strstr(transport.writes, "..secret\r\nplain\r\n.\r\n"));
}

/* An empty non-null body skips body-line processing but still sends DATA's terminator. */
void test_send_message_with_empty_body(void)
{
  fake_transport transport;

  memset(&transport, 0, sizeof(transport));
  init_transport(&transport, "250 queued\r\n", 0);
  TEST_ASSERT_EQUAL_INT(0, smtp_send_message(fake_read, fake_write, &transport,
                                             "from", "to", "subject", ""));
  TEST_ASSERT_NOT_NULL(strstr(transport.writes, ".\r\n"));
}

/* A null body reads lines from stdin, including a line that has no trailing newline. */
void test_send_message_reads_body_from_stdin(void)
{
  FILE *input = tmpfile();
  int saved_stdin;
  fake_transport transport;

  TEST_ASSERT_NOT_NULL(input);
  fputs(".stdin\nplain", input);
  rewind(input);
  saved_stdin = dup(STDIN_FILENO);
  TEST_ASSERT_TRUE(saved_stdin >= 0);
  TEST_ASSERT_EQUAL_INT(0, dup2(fileno(input), STDIN_FILENO));
  clearerr(stdin);

  memset(&transport, 0, sizeof(transport));
  init_transport(&transport, "250 queued\r\n", 0);
  TEST_ASSERT_EQUAL_INT(0, smtp_send_message(fake_read, fake_write, &transport,
                                             "from", "to", "subject", NULL));
  TEST_ASSERT_EQUAL_INT(0, dup2(saved_stdin, STDIN_FILENO));
  close(saved_stdin);
  fclose(input);
  TEST_ASSERT_NOT_NULL(strstr(transport.writes, "..stdin\r\nplain\r\n.\r\n"));
}

/* The stdin path rejects an oversized dotted line and a failed body write. */
void test_send_message_stdin_failures(void)
{
  FILE *input = tmpfile();
  int saved_stdin;
  fake_transport transport;

  TEST_ASSERT_NOT_NULL(input);
  for (size_t i = 0; i < 2047; i++)
    fputc('.', input);
  rewind(input);
  saved_stdin = dup(STDIN_FILENO);
  TEST_ASSERT_TRUE(saved_stdin >= 0);
  TEST_ASSERT_EQUAL_INT(0, dup2(fileno(input), STDIN_FILENO));
  clearerr(stdin);

  memset(&transport, 0, sizeof(transport));
  init_transport(&transport, "250 queued\r\n", 0);
  TEST_ASSERT_EQUAL_INT(-1, smtp_send_message(fake_read, fake_write, &transport,
                                              "from", "to", "subject", NULL));
  TEST_ASSERT_EQUAL_INT(0, dup2(saved_stdin, STDIN_FILENO));
  close(saved_stdin);
  fclose(input);

  input = tmpfile();
  TEST_ASSERT_NOT_NULL(input);
  fputs("stdin body\n", input);
  rewind(input);
  saved_stdin = dup(STDIN_FILENO);
  TEST_ASSERT_TRUE(saved_stdin >= 0);
  TEST_ASSERT_EQUAL_INT(0, dup2(fileno(input), STDIN_FILENO));
  clearerr(stdin);

  memset(&transport, 0, sizeof(transport));
  init_transport(&transport, "250 queued\r\n", 0);
  transport.fail_on_write = 2;
  TEST_ASSERT_EQUAL_INT(-1, smtp_send_message(fake_read, fake_write, &transport,
                                              "from", "to", "subject", NULL));
  TEST_ASSERT_EQUAL_INT(0, dup2(saved_stdin, STDIN_FILENO));
  close(saved_stdin);
  fclose(input);
}

/* Invalid header arguments are rejected before any transport write occurs. */
void test_send_message_rejects_invalid_headers(void)
{
  char long_value[2049];
  fake_transport transport;

  memset(long_value, 'x', sizeof(long_value) - 1);
  long_value[sizeof(long_value) - 1] = '\0';
  memset(&transport, 0, sizeof(transport));
  init_transport(&transport, "250 queued\r\n", 0);
  TEST_ASSERT_EQUAL_INT(-1, smtp_send_message(fake_read, fake_write, &transport,
                                              long_value, "to", "subject", "body"));
}

/* A body line too large for dot-stuffing is rejected and does not leak its copy. */
void test_send_message_rejects_oversized_body_line(void)
{
  char long_body[2049];
  fake_transport transport;

  memset(long_body, 'x', sizeof(long_body) - 1);
  long_body[sizeof(long_body) - 1] = '\0';
  memset(&transport, 0, sizeof(transport));
  init_transport(&transport, "250 queued\r\n", 0);
  TEST_ASSERT_EQUAL_INT(-1, smtp_send_message(fake_read, fake_write, &transport,
                                              "from", "to", "subject", long_body));
}

/* The session reports failures from header, body, CRLF, and terminator writes. */
void test_send_message_rejects_write_failures(void)
{
  fake_transport transport;

  memset(&transport, 0, sizeof(transport));
  init_transport(&transport, "250 queued\r\n", 0);
  transport.fail_on_write = 1;
  TEST_ASSERT_EQUAL_INT(-1, smtp_send_message(fake_read, fake_write, &transport,
                                              "from", "to", "subject", "body"));

  memset(&transport, 0, sizeof(transport));
  init_transport(&transport, "250 queued\r\n", 0);
  transport.fail_on_write = 2;
  TEST_ASSERT_EQUAL_INT(-1, smtp_send_message(fake_read, fake_write, &transport,
                                              "from", "to", "subject", "body"));

  memset(&transport, 0, sizeof(transport));
  init_transport(&transport, "250 queued\r\n", 0);
  transport.fail_on_write = 3;
  TEST_ASSERT_EQUAL_INT(-1, smtp_send_message(fake_read, fake_write, &transport,
                                              "from", "to", "subject", "body"));

  memset(&transport, 0, sizeof(transport));
  init_transport(&transport, "250 queued\r\n", 0);
  transport.fail_on_write = 2;
  TEST_ASSERT_EQUAL_INT(-1, smtp_send_message(fake_read, fake_write, &transport,
                                              "from", "to", "subject", ""));
}

/* The final message reply must be valid and must have status 250. */
void test_send_message_rejects_bad_final_reply(void)
{
  fake_transport transport;

  memset(&transport, 0, sizeof(transport));
  init_transport(&transport, "not a reply\r\n", 0);
  TEST_ASSERT_EQUAL_INT(-1, smtp_send_message(fake_read, fake_write, &transport,
                                              "from", "to", "subject", "body"));

  memset(&transport, 0, sizeof(transport));
  init_transport(&transport, "550 rejected\r\n", 0);
  TEST_ASSERT_EQUAL_INT(-1, smtp_send_message(fake_read, fake_write, &transport,
                                              "from", "to", "subject", "body"));
}

/* The socket callbacks move bytes through a real local socket pair. */
void test_socket_read_and_write_callbacks(void)
{
  int sockets[2];
  smtp_socket_ctx socket_context;
  smtp_session_ctx session;
  char buffer[32];
  const char outgoing[] = "reply";

  TEST_ASSERT_EQUAL_INT(0, socketpair(AF_UNIX, SOCK_STREAM, 0, sockets));
  socket_context.sock = sockets[0];
  session.sockctx = &socket_context;
  session.buf_len = 0;

  TEST_ASSERT_EQUAL_INT(5, (int)write(sockets[1], "hello", 5));
  TEST_ASSERT_EQUAL_INT(5, (int)smtp_socket_read(&session, buffer, sizeof(buffer)));
  buffer[5] = '\0';
  TEST_ASSERT_EQUAL_STRING("hello", buffer);

  TEST_ASSERT_EQUAL_INT(5, (int)smtp_socket_write(&session, outgoing, 5));
  TEST_ASSERT_EQUAL_INT(5, (int)read(sockets[1], buffer, sizeof(buffer)));
  buffer[5] = '\0';
  TEST_ASSERT_EQUAL_STRING("reply", buffer);

  close(sockets[1]);
  smtp_socket_close(&socket_context);
}

/* A loopback listener proves that connect stores the connected socket. */
void test_socket_connect_successfully(void)
{
  int listener;
  int peer;
  struct sockaddr_in address;
  socklen_t address_len = sizeof(address);
  char port[16];
  smtp_socket_ctx socket_context = {.sock = -1};

  listener = socket(AF_INET, SOCK_STREAM, 0);
  TEST_ASSERT_TRUE(listener >= 0);
  memset(&address, 0, sizeof(address));
  address.sin_family = AF_INET;
  address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
  address.sin_port = htons(0);
  TEST_ASSERT_EQUAL_INT(0, bind(listener, (struct sockaddr *)&address, sizeof(address)));
  TEST_ASSERT_EQUAL_INT(0, listen(listener, 1));
  TEST_ASSERT_EQUAL_INT(0, getsockname(listener, (struct sockaddr *)&address, &address_len));
  snprintf(port, sizeof(port), "%u", (unsigned)ntohs(address.sin_port));

  TEST_ASSERT_EQUAL_INT(0, smtp_socket_connect(&socket_context, "127.0.0.1", port));
  peer = accept(listener, NULL, NULL);
  TEST_ASSERT_TRUE(peer >= 0);

  close(peer);
  close(listener);
  smtp_socket_close(&socket_context);
}

/* DNS failure is reported without attempting to create a socket. */
void test_socket_connect_rejects_invalid_host(void)
{
  smtp_socket_ctx socket_context = {.sock = -1};

  TEST_ASSERT_EQUAL_INT(-1, smtp_socket_connect(&socket_context,
                                                "invalid.invalid", "25"));
}

/* A refused local connection exercises failed connect cleanup and reporting. */
void test_socket_connect_rejects_refused_connection(void)
{
  int listener;
  struct sockaddr_in address;
  socklen_t address_len = sizeof(address);
  char port[16];
  smtp_socket_ctx socket_context = {.sock = -1};

  listener = socket(AF_INET, SOCK_STREAM, 0);
  TEST_ASSERT_TRUE(listener >= 0);
  memset(&address, 0, sizeof(address));
  address.sin_family = AF_INET;
  address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
  address.sin_port = htons(0);
  TEST_ASSERT_EQUAL_INT(0, bind(listener, (struct sockaddr *)&address, sizeof(address)));
  TEST_ASSERT_EQUAL_INT(0, getsockname(listener, (struct sockaddr *)&address, &address_len));
  snprintf(port, sizeof(port), "%u", (unsigned)ntohs(address.sin_port));
  close(listener);

  TEST_ASSERT_EQUAL_INT(-1, smtp_socket_connect(&socket_context, "127.0.0.1", port));
}

/* Closing handles valid sockets and safely ignores null or invalid contexts. */
void test_socket_close_handles_socket_states(void)
{
  int sockets[2];
  smtp_socket_ctx socket_context;
  smtp_socket_ctx invalid_context = {.sock = -1};

  TEST_ASSERT_EQUAL_INT(0, socketpair(AF_UNIX, SOCK_STREAM, 0, sockets));
  socket_context.sock = sockets[0];
  smtp_socket_close(&socket_context);
  close(sockets[1]);
  smtp_socket_close(&invalid_context);
  smtp_socket_close(NULL);
}

/* Register every test with Unity and return its pass/fail status to the runner. */
int main(void)
{
  UNITY_BEGIN();
  RUN_TEST(test_parse_reply_code);
  RUN_TEST(test_reply_line_finality);
  RUN_TEST(test_build_commands_and_headers);
  RUN_TEST(test_builders_reject_invalid_output_arguments);
  RUN_TEST(test_dot_stuff_line);
  RUN_TEST(test_read_reply_single_line);
  RUN_TEST(test_read_reply_multiline);
  RUN_TEST(test_read_reply_arrives_a_few_bytes_at_a_time);
  RUN_TEST(test_read_reply_buffer_cannot_hold_line);
  RUN_TEST(test_read_reply_server_hangs_up_in_middle_of_session);
  RUN_TEST(test_rejects_wrong_greeting_status);
  RUN_TEST(test_rejects_wrong_helo_status);
  RUN_TEST(test_rejects_wrong_mail_from_status);
  RUN_TEST(test_rejects_wrong_rcpt_to_status);
  RUN_TEST(test_rejects_wrong_data_status);
  RUN_TEST(test_rejects_wrong_message_completion_status);
  RUN_TEST(test_send_cmd_writes_command_and_accepts_status);
  RUN_TEST(test_send_cmd_rejects_transport_failures);
  RUN_TEST(test_read_reply_allows_no_lastline_buffer);
  RUN_TEST(test_send_message_with_body);
  RUN_TEST(test_send_message_with_empty_body);
  RUN_TEST(test_send_message_reads_body_from_stdin);
  RUN_TEST(test_send_message_stdin_failures);
  RUN_TEST(test_send_message_rejects_invalid_headers);
  RUN_TEST(test_send_message_rejects_oversized_body_line);
  RUN_TEST(test_send_message_rejects_write_failures);
  RUN_TEST(test_send_message_rejects_bad_final_reply);
  RUN_TEST(test_socket_read_and_write_callbacks);
  RUN_TEST(test_socket_connect_successfully);
  RUN_TEST(test_socket_connect_rejects_invalid_host);
  RUN_TEST(test_socket_connect_rejects_refused_connection);
  RUN_TEST(test_socket_close_handles_socket_states);
  return UNITY_END();
}
