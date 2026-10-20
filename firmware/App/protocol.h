/*
 * Line-based text protocol over USB CDC. Lines end with LF (CR ignored).
 *
 *   get                      status dump, ends with "ok"
 *   set <1-4|all> <0-100>    fan duty in percent
 *   mode <1-4|all> pwm|dc    pwm: 4-pin fan, dc: 3-pin fan (supply chopping)
 *   ping                     keepalive
 *   version
 *   help
 *
 * Every command answers "ok" or "err <reason>" as its last line.
 * Valid commands reset the host-loss failsafe timer.
 */
#ifndef PROTOCOL_H
#define PROTOCOL_H

typedef void (*protocol_emit_fn)(const char *line);

void protocol_init(protocol_emit_fn emit);
void protocol_feed(char c);

/* Runs one command line (modified in place). Exposed for tests. */
void protocol_exec(char *line);

#endif
