---
name: fans_verification
description: Verify fan RPM control end to end on the real STM32F103 fan-controller board — build firmware, detect the ST-Link and serial CDC port, flash, then sweep duty on one or more channels over host/fanctl.py and prove RPM tracks duty with stall=0. Use whenever the user asks to verify, test, check, or "run" a fan channel (CHn) against real hardware, after any tach/pwm/control firmware change, or after soldering a new tach bodge wire, even if they just say "run ch2" or "check ch3".
---

# Fan RPM verification on real hardware

Proves the firmware's duty→RPM control loop actually works on the board, not
just that it builds. Five steps: build, detect, flash, sweep, judge.

## 1. Build

```bash
cd firmware && make
```

Must link clean. If it doesn't, stop here — there's nothing to flash and
nothing on the board to verify yet.

## 2. Detect the board

```bash
lsusb | grep -i "0483:374b"      # ST-Link/V2.1, confirms the programmer is connected
ls /dev/ttyACM*                   # CDC serial port(s) the board enumerates as
```

If `ls` shows more than one `/dev/ttyACM*`, or none, don't guess — ask which
one is the board, or note that flashing will re-enumerate it anyway (see
step 4).

## 3. Flash

```bash
cd firmware && make flash
```

This runs OpenOCD + ST-Link, programs `build/fanny.bin`, verifies, and
resets the target. This overwrites whatever firmware is currently running —
say so before running it, the same way any action that changes device state
on hardware the user can't instantly undo deserves a heads-up.

## 4. Re-detect the port

The board's CDC port can renumber after a flash/reset (observed: moved from
`/dev/ttyACM0` to `/dev/ttyACM1` after a reflash). Re-run `ls /dev/ttyACM*`
and use whichever port now answers:

```bash
python3 host/fanctl.py -p /dev/ttyACM<N> get
```

If `get` times out, the port you picked is stale — try the other one before
assuming the board is dead.

## 5. Sweep duty and judge

For the channel(s) in question, sweep duty with a settle wait before each
read:

```bash
python3 host/fanctl.py -p /dev/ttyACM<N> set <ch> 100
sleep 3
python3 host/fanctl.py -p /dev/ttyACM<N> get
python3 host/fanctl.py -p /dev/ttyACM<N> set <ch> 50
sleep 3
python3 host/fanctl.py -p /dev/ttyACM<N> get
python3 host/fanctl.py -p /dev/ttyACM<N> set <ch> 20
sleep 3
python3 host/fanctl.py -p /dev/ttyACM<N> get
python3 host/fanctl.py -p /dev/ttyACM<N> set <ch> 100
sleep 4
python3 host/fanctl.py -p /dev/ttyACM<N> get
```

The sleep matters: a `get` taken immediately after a `set` reads the fan
mid-ramp, not its settled RPM (observed: both channels showing an identical
transient RPM figure right after a 20%→100% jump, before settling a few
seconds later to their real, different RPMs). 3 s is enough for a drop in
duty; the jump back up to 100% from a low duty needs closer to 4 s.

Report a small table: duty, rpm, stall, per channel swept.

### What "pass" means

- RPM drops as duty drops, and climbs back when duty is restored — monotonic
  with duty, not flat or inverted.
- `stall=0` throughout. `stall=1` means the firmware isn't seeing tach
  pulses at all — on this board that has always meant the channel's bodge
  wire isn't connected yet, not a control-loop bug.
- The restored 100% reading should land in the same ballpark as the
  channel's first 100% reading (within normal run-to-run jitter), not drift
  to a different plateau.

A channel reading flat `rpm=0, stall=1` at every duty isn't a failure of
this skill's check — it means that channel has no tach wire soldered yet.
Say so plainly instead of treating it as inconclusive.

## Ramp-up test (1% -> 50%, 5% steps, 2s wait)

A separate scenario from the 100/50/20/100 sweep above: it walks up from a
cold stop through the low-duty range in fine steps, instead of jumping
straight to a mid/high duty. This is the range where start-up behavior
(`fan_set_duty()`'s kick: a fan at 0 rpm requested 1-29% duty runs at 30%
for 2 s first, per `firmware/App/fan.c`) and the stall/no-stall boundary
actually live, so a coarse sweep can miss it.

```bash
python3 host/fanctl.py -p /dev/ttyACM<N> set <ch> 0
sleep 2
for duty in 1 5 10 15 20 25 30 35 40 45 50; do
    python3 host/fanctl.py -p /dev/ttyACM<N> set <ch> "$duty"
    sleep 2
    python3 host/fanctl.py -p /dev/ttyACM<N> get | grep "^fan $ch "
done
```

2 s is shorter than the 3-4 s settle used in the coarse sweep — that's the
point here: this scenario is partly about watching the ramp in progress near
the start-up kick window, not only the settled value. Record rpm and stall
at every step, not just the last one.

### What "pass" means here

- Below whatever this fan's real start threshold is, the fan may legitimately
  stay stopped (`rpm=0, stall=1`) — that is correct behavior, not a failure.
  `SUMMARY_TACHO_TIME_EXT.md` recorded this fan not starting from a stop
  below 10% duty.
- Once it picks up, rpm should climb step over step as duty rises through
  5% increments — no channel should jump straight to its high-duty rpm on
  a 1% or 5% step, and none should stay flat once already spinning.
- A step where the fan was already spinning and `stall` flips to 1, or rpm
  drops to 0, while duty is rising is a real fault — the opposite of a
  normal cold-start no-spin.
- Expect more run-to-run variance here than in the coarse sweep: the
  2 s wait is shorter than the controller's startup-kick window in some
  cases, so a reading can still be mid-transition.

## Reference values (this board, baseline 2026-11-12)

Measured on the current fans/board pairing with all 4 bodge wires soldered.
Use as the expected range when judging a new sweep — a fresh reading within
roughly ±10% of these at the same duty is a pass; a reading far outside this
band (not just below it — also well above) is worth a second look even if
`stall=0`.

| duty | CH1 rpm | CH2 rpm | CH3 rpm | CH4 rpm |
|---|---|---|---|---|
| 100% | 1649 | 1799 | 1769 | 1769 |
| 50% | 1079 | 1199 | 1229 | 1170 |
| 20% | 539 | 629 | 689 | 660 |
| 100% (restored) | 1649 | 1799 | 1740 | 1739 |

These numbers are specific to the fans wired up during this baseline, not a
firmware spec — swapping a fan changes them. If the fans on the board change,
re-run the sweep once with no prior expectation and treat that run as the
new baseline instead of comparing against this table.

## Gotchas

- `host/fanctl.py` defaults to `/dev/ttyACM0` (`DEFAULT_PORT`) — always pass
  `-p` explicitly after a flash, since step 4 above can change which port is
  live.
- A channel that was fine a few checks ago can show `stall=1` later in the
  same session with no firmware or command change — a bodge wire that
  worked once can shake loose on a bench setup. Flag it as a wiring
  observation, don't assume it's a regression in the firmware you just
  flashed.
- Each channel's tach mechanism differs by design, not by bug: CH1/CH2/CH3
  use a hardware timer in external-clock mode (`TIM1`/`TIM2`/`TIM3`), CH4
  uses an EXTI both-edge interrupt (`firmware/App/tach.c`). All four are
  expected to behave the same from the host side (`fanctl.py get/set`); a
  difference in RPM behavior between CH4 and the others is worth separating
  from a difference between, say, CH1 and CH2.
