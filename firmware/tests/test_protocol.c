/*
 * Host-side tests for the command protocol and the pure conversion helpers.
 * Hardware modules are replaced by the fakes below.
 */
#include <stdio.h>
#include <string.h>

#include "board.h"
#include "fan.h"
#include "protocol.h"
#include "safety.h"
#include "tach.h"
#include "thermal.h"

/* ---- fakes ---- */

static unsigned duties[FAN_COUNT] = { 100, 100, 100, 100 };
static enum fan_mode modes[FAN_COUNT];
static int host_seen;

uint32_t millis(void) { return 0; }
bool fan_set_duty(unsigned ch, unsigned d) { duties[ch] = d; return true; }
bool fan_set_mode(unsigned ch, enum fan_mode m) { modes[ch] = m; return true; }
unsigned fan_duty(unsigned ch) { return duties[ch]; }
enum fan_mode fan_mode(unsigned ch) { return modes[ch]; }
unsigned tach_rpm(unsigned ch) { return 1200 + 30 * ch; }
unsigned thermal_ain_raw(unsigned k) { return 2048 + k; }
unsigned thermal_ain_mv(unsigned k) { return 1650 + k; }
unsigned thermal_v12_mv(void) { return 12050; }
unsigned thermal_vdda_mv(void) { return 3300; }
int thermal_mcu_temp_decic(void) { return -5; }
void safety_host_seen(void) { host_seen++; }
bool safety_failsafe_active(void) { return false; }
bool safety_fan_stalled(unsigned ch) { return ch == 3; }

/* ---- harness ---- */

static char out[2048];
static int failures;

static void capture(const char *line)
{
	strcat(out, line);
	strcat(out, "\n");
}

static void run(const char *input)
{
	out[0] = '\0';
	for (const char *p = input; *p; p++)
		protocol_feed(*p);
}

#define CHECK(cond) do { \
	if (!(cond)) { \
		printf("FAIL %s:%d: %s\n  output: %s\n", __FILE__, __LINE__, #cond, out); \
		failures++; \
	} \
} while (0)

int main(void)
{
	protocol_init(capture);

	run("set 2 40\n");
	CHECK(strcmp(out, "ok\n") == 0);
	CHECK(duties[1] == 40 && duties[0] == 100);
	CHECK(host_seen == 1);

	run("set all 0\r\n");
	CHECK(strcmp(out, "ok\n") == 0);
	CHECK(duties[0] == 0 && duties[3] == 0);

	run("  mode   3   dc \n");
	CHECK(strcmp(out, "ok\n") == 0);
	CHECK(modes[2] == FAN_MODE_DC && modes[1] == FAN_MODE_PWM);

	host_seen = 0;
	run("set 5 10\n");
	CHECK(strncmp(out, "err ", 4) == 0);
	run("set 1 101\n");
	CHECK(strcmp(out, "err duty must be 0-100\n") == 0);
	run("set 1 -1\n");
	CHECK(strncmp(out, "err ", 4) == 0);
	run("mode 1 fast\n");
	CHECK(strcmp(out, "err mode must be pwm or dc\n") == 0);
	run("frobnicate\n");
	CHECK(strcmp(out, "err unknown command\n") == 0);
	run("set 1 2 3\n");
	CHECK(strcmp(out, "err too many arguments\n") == 0);
	CHECK(host_seen == 0); /* errors are not keepalives */

	run("\n");
	CHECK(out[0] == '\0');

	char longline[100];
	memset(longline, 'x', sizeof(longline) - 2);
	longline[sizeof(longline) - 2] = '\n';
	longline[sizeof(longline) - 1] = '\0';
	run(longline);
	CHECK(strcmp(out, "err line too long\n") == 0);
	run("ping\n"); /* parser recovers after overflow */
	CHECK(strcmp(out, "ok\n") == 0);

	run("get\n");
	CHECK(strstr(out, "fan 2 mode=pwm duty=0 rpm=1230 stall=0\n") != NULL);
	CHECK(strstr(out, "fan 3 mode=dc") != NULL);
	CHECK(strstr(out, "fan 4 mode=pwm duty=0 rpm=1290 stall=1\n") != NULL);
	CHECK(strstr(out, "ain 6 raw=2053 mv=1655\n") != NULL);
	CHECK(strstr(out, "v12 mv=12050\n") != NULL);
	CHECK(strstr(out, "mcu temp=-0.5\n") != NULL);
	CHECK(strstr(out, "failsafe=0\nok\n") != NULL);

	/* conversion helpers */
	CHECK(tach_calc_rpm(40, 1000) == 1200);
	CHECK(tach_calc_rpm(0, 1000) == 0);
	CHECK(tach_calc_rpm(5, 0) == 0);
	CHECK(thermal_vdda_from_vref(1489) == 3300);
	CHECK(thermal_raw_to_mv(4095, 3300) == 3300);
	CHECK(thermal_mcu_decic(1430) == 250);
	CHECK(thermal_mcu_decic(1344) == 450);

	if (failures) {
		printf("%d check(s) failed\n", failures);
		return 1;
	}
	printf("all protocol tests passed\n");
	return 0;
}
