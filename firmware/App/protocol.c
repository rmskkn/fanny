#include <stdbool.h>
#include <string.h>

#include "board.h"
#include "fan.h"
#include "protocol.h"
#include "safety.h"
#include "tach.h"
#include "thermal.h"

#define FW_VERSION "fanctl 2.1"
#define LINE_MAX   64
#define MAX_ARGS   3

static protocol_emit_fn emit;
static char line[LINE_MAX];
static unsigned line_len;
static bool line_overflow;

static char *put_str(char *p, const char *s)
{
	while (*s)
		*p++ = *s++;
	return p;
}

static char *put_uint(char *p, unsigned v)
{
	char tmp[10];
	int n = 0;

	do {
		tmp[n++] = (char)('0' + v % 10);
		v /= 10;
	} while (v);
	while (n)
		*p++ = tmp[--n];
	return p;
}

static char *put_kv(char *p, const char *key, unsigned v)
{
	*p++ = ' ';
	p = put_str(p, key);
	*p++ = '=';
	return put_uint(p, v);
}

static bool parse_uint(const char *s, unsigned *out)
{
	unsigned v = 0;

	if (!*s)
		return false;
	for (; *s; s++) {
		if (*s < '0' || *s > '9' || v > 100000)
			return false;
		v = v * 10 + (unsigned)(*s - '0');
	}
	*out = v;
	return true;
}

/* "1".."4" selects one channel, "all" selects every channel. Range is [*first, *last). */
static bool parse_channels(const char *s, unsigned *first, unsigned *last)
{
	unsigned n;

	if (strcmp(s, "all") == 0) {
		*first = 0;
		*last = FAN_COUNT;
		return true;
	}
	if (!parse_uint(s, &n) || n < 1 || n > FAN_COUNT)
		return false;
	*first = n - 1;
	*last = n;
	return true;
}

static void cmd_get(void)
{
	char out[LINE_MAX];
	char *p;

	for (unsigned ch = 0; ch < FAN_COUNT; ch++) {
		p = put_str(out, "fan ");
		p = put_uint(p, ch + 1);
		p = put_str(p, fan_mode(ch) == FAN_MODE_PWM ? " mode=pwm" : " mode=dc");
		p = put_kv(p, "duty", fan_duty(ch));
		p = put_kv(p, "rpm", tach_rpm(ch));
		p = put_kv(p, "stall", safety_fan_stalled(ch));
		*p = '\0';
		emit(out);
	}
	for (unsigned k = 0; k < AIN_COUNT; k++) {
		p = put_str(out, "ain ");
		p = put_uint(p, k + 1);
		p = put_kv(p, "raw", thermal_ain_raw(k));
		p = put_kv(p, "mv", thermal_ain_mv(k));
		*p = '\0';
		emit(out);
	}

	p = put_str(out, "v12");
	p = put_kv(p, "mv", thermal_v12_mv());
	*p = '\0';
	emit(out);

	p = put_str(out, "vdda");
	p = put_kv(p, "mv", thermal_vdda_mv());
	*p = '\0';
	emit(out);

	int t = thermal_mcu_temp_decic();
	p = put_str(out, "mcu temp=");
	if (t < 0) {
		*p++ = '-';
		t = -t;
	}
	p = put_uint(p, (unsigned)t / 10);
	*p++ = '.';
	p = put_uint(p, (unsigned)t % 10);
	*p = '\0';
	emit(out);

	p = put_str(out, "failsafe");
	*p++ = '=';
	p = put_uint(p, safety_failsafe_active());
	*p = '\0';
	emit(out);
}

static const char *cmd_set(char **argv, int argc)
{
	unsigned first, last, duty;

	if (argc != 3 || !parse_channels(argv[1], &first, &last))
		return "usage: set <1-4|all> <0-100>";
	if (!parse_uint(argv[2], &duty) || duty > 100)
		return "duty must be 0-100";
	for (unsigned ch = first; ch < last; ch++)
		fan_set_duty(ch, duty);
	return NULL;
}

static const char *cmd_mode(char **argv, int argc)
{
	unsigned first, last;
	enum fan_mode mode;

	if (argc != 3 || !parse_channels(argv[1], &first, &last))
		return "usage: mode <1-4|all> pwm|dc";
	if (strcmp(argv[2], "pwm") == 0)
		mode = FAN_MODE_PWM;
	else if (strcmp(argv[2], "dc") == 0)
		mode = FAN_MODE_DC;
	else
		return "mode must be pwm or dc";
	for (unsigned ch = first; ch < last; ch++)
		fan_set_mode(ch, mode);
	return NULL;
}

void protocol_exec(char *s)
{
	char *argv[MAX_ARGS];
	int argc = 0;
	const char *err = NULL;

	while (*s) {
		while (*s == ' ' || *s == '\t')
			*s++ = '\0';
		if (!*s)
			break;
		if (argc == MAX_ARGS) {
			emit("err too many arguments");
			return;
		}
		argv[argc++] = s;
		while (*s && *s != ' ' && *s != '\t')
			s++;
	}
	if (argc == 0)
		return;

	if (strcmp(argv[0], "get") == 0 && argc == 1) {
		cmd_get();
	} else if (strcmp(argv[0], "set") == 0) {
		err = cmd_set(argv, argc);
	} else if (strcmp(argv[0], "mode") == 0) {
		err = cmd_mode(argv, argc);
	} else if (strcmp(argv[0], "ping") == 0 && argc == 1) {
	} else if (strcmp(argv[0], "version") == 0 && argc == 1) {
		emit(FW_VERSION);
	} else if (strcmp(argv[0], "help") == 0 && argc == 1) {
		emit("get | set <1-4|all> <0-100> | mode <1-4|all> pwm|dc | ping | version");
	} else {
		err = "unknown command";
	}

	if (err) {
		char out[LINE_MAX];
		char *p = put_str(out, "err ");

		p = put_str(p, err);
		*p = '\0';
		emit(out);
		return;
	}
	safety_host_seen();
	emit("ok");
}

void protocol_init(protocol_emit_fn fn)
{
	emit = fn;
	line_len = 0;
	line_overflow = false;
}

void protocol_feed(char c)
{
	if (c == '\r')
		return;
	if (c != '\n') {
		if (line_len < LINE_MAX - 1)
			line[line_len++] = c;
		else
			line_overflow = true;
		return;
	}

	line[line_len] = '\0';
	if (line_overflow)
		emit("err line too long");
	else
		protocol_exec(line);
	line_len = 0;
	line_overflow = false;
}
