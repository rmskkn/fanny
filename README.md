# STM32F103 PWM fan controller

USB-controlled controller for four 12 V PC fans and six analog sensor inputs.
It is built around an STM32F103C8T6 that shows up on the host as a USB CDC
serial port (`/dev/ttyACM*`, `COMx`).

```
stm32-fan-controller/
├── firmware/          libopencm3 firmware (C11, arm-none-eabi-gcc)
│   ├── Core/          main loop, clocks, SysTick, linker script, board pin map
│   ├── Drivers/       libopencm3 git submodule
│   ├── USB_DEVICE/    USB CDC-ACM device
│   ├── App/           fan, pwm, tach, protocol, thermal (analog), safety
│   └── tests/         host-side unit tests (`make test`)
├── hardware/
│   ├── kicad/         KiCad 8 project (schematic, symbol lib, board outline)
│   ├── tools/         gen_kicad.py: generates the KiCad files and bom.csv
│   ├── bom.csv        bill of materials (generated)
│   └── datasheets/    links to the key datasheets
└── host/              fanctl.py command-line client (pyserial)
```

This tree follows the suggested layout with two changes:

- **libopencm3 instead of the STM32Cube HAL.** The suggested `Core/`,
  `Drivers/` and `USB_DEVICE/` folders are STM32CubeMX output. CubeMX is not
  available here, so the firmware uses libopencm3. Its CDC stack is small and
  the build is a plain Makefile. The folder roles are the same.
- **`hardware/kicad/` instead of separate `schematic/` and `pcb/` folders.**
  KiCad keeps a project's `.kicad_pro`, `.kicad_sch` and `.kicad_pcb` files in
  one directory.

## Hardware design

### Block overview

```
 12 V (PSU) ─ F1 PTC ─ D1 TVS ─┬─────────────── +12V ──────────────┐
                               │                                   │ ×4
 USB VBUS ── D2 BAT54C (OR) ─ R1 47R ┴─ D3 13V ─ U1 AMS1117-3.3 ─ +3V3 ─ FB1 ─ +3V3A
                                                                   │
            ┌──────────── STM32F103C8T6 (U2) ─────────────┐        │
 USB ─ U3 ESD ─ PA11/PA12           TIM2 PA15 PB3 PB10 PB11 ─► BSS138 ─► AO3401A high-side ─► PTC ─► fan +12V
            │                       TIM4 PB6..PB9 (25 kHz) ─► BSS138 open drain ─► fan PWM pin
            │                       EXTI PB12..PB15 ◄─ RC ◄─ 10k pull-up ◄─ fan tach
 sensors ─ 1k + 100n + BAT54S ─► PA0..PA5 (ADC)    PB0 ◄─ 47k/10k ◄─ +12V
```

### Pin map

| Function | Pins | Peripheral |
|---|---|---|
| Fan 1–4 supply switch | PA15, PB3, PB10, PB11 | TIM2 CH1–4 (full remap), 30 Hz |
| Fan 1–4 PWM signal | PB6, PB7, PB8, PB9 | TIM4 CH1–4, 25 kHz |
| Fan 1–4 tach | PB12–PB15 | EXTI12–15, falling edge |
| AIN1–AIN6 | PA0–PA5 | ADC1 IN0–IN5 |
| 12 V sense | PB0 | ADC1 IN8 |
| USB | PA11 (D−), PA12 (D+) | USB FS |
| SWD | PA13, PA14 | JTAG disabled to free PA15/PB3 |
| Status LED | PC13 | active low |
| Clock | PD0/PD1 | 8 MHz crystal, PLL to 72 MHz |

### Fan channel

Each channel drives a standard 4-pin header (1 GND, 2 +12 V, 3 tach, 4 PWM).
The channel has two control paths, and the firmware selects between them per channel:

- **`pwm` mode (4-pin fans):** the high-side MOSFET stays fully on. Speed is
  set by a 25 kHz signal on pin 4. Duty 0 % also switches the supply off,
  because many 4-pin fans keep turning at 0 % PWM.
- **`dc` mode (3-pin fans):** the MOSFET chops the 12 V supply at 30 Hz, and
  the PWM pin is held high.

**High-side switch, AO3401A (P-channel, −30 V, ±12 V V<sub>GS</sub>, ≤52 mΩ at −4.5 V).**
The source connects to +12 V. The drain feeds fan pin 2 through a per-channel PTC. The 3.3 V MCU cannot
drive the gate directly, so a BSS138 pulls the gate low through a resistor divider.

**Gate protection.** The AO3401A is rated only ±12 V gate-source, which
equals the supply voltage. The design therefore never applies the full rail to the gate:

| Part | Value | Role |
|---|---|---|
| R<sub>GS</sub> (gate to source) | 4.7 kΩ | Keeps the FET off by default and discharges the gate |
| R<sub>G</sub> (gate to BSS138 drain) | 4.7 kΩ | Forms a divider with R<sub>GS</sub>: V<sub>GS</sub> = −12 V × 4.7/9.4 = **−6 V** when on. It also limits zener current |
| D<sub>Z</sub> BZT52C10 (K at source, A at gate) | 10 V | Clamps V<sub>GS</sub> to −10 V when the 12 V rail spikes above ~20 V. Its forward drop clamps positive V<sub>GS</sub> to +0.7 V |
| D1 SMBJ13A | rail TVS | Limits rail surges seen by V<sub>DS</sub> and by the gate network |

At −6 V the FET is well enhanced, so a 1 A fan dissipates about 50 mW. The
divider draws 1.3 mA per channel while the fan is on. Gate switching takes
a few microseconds through 4.7 kΩ, so `dc` mode uses a 30 Hz chopping frequency
to keep switching loss negligible. Raising the frequency needs a stronger gate driver.

**Channel fuse.** A 1.1 A PTC (Bourns MF-MSMF110/16) sits between the FET drain
and fan pin 2. A shorted fan or cable trips only its own channel, so the other
fans keep running and F1 does not trip.

**Freewheel diode** SS14 is fitted across each fan output. It absorbs the
inductive kick from the motor and cable when the switch opens.

**PWM signal buffer.** A BSS138 works as an open-drain driver on fan pin 4.
The fan pulls this pin up internally to ≤5.25 V (Intel 4-wire spec), so the
MCU pin never sees the fan's voltage. The buffer inverts the signal, and the
firmware compensates with timer mode PWM2.

**Safe state while the MCU is in reset or unprogrammed:**
- A 10 kΩ pull-up to 3.3 V on each supply BSS138 gate turns the supply FET on.
- A 100 kΩ pull-down on each PWM BSS138 gate releases the PWM line, so the fan reads 100 %.

With both, all fans run at full speed before the firmware starts.

**Tach input.** The fan's open-collector output has a 10 kΩ pull-up to 3.3 V,
then 1 kΩ + 1 nF filtering. PB12–PB15 are 5 V tolerant. A fan that pulls its
tach output to 12 V internally (outside the spec) would need an extra clamp.

### Analog inputs (AIN1–AIN6)

Each input is a 3-pin header: 3V3A, SIG, GND. The conditioning chain is:

- A 10 kΩ pull-up to 3V3A. With a 10 kΩ NTC to GND it forms a ratiometric
  temperature divider. Leave it unfitted for voltage-output sensors.
- 1 kΩ series resistor and 100 nF to GND, for anti-aliasing and sample charge.
- A BAT54S clamp to GND and 3V3A. PA0–PA5 are **not** 5 V tolerant.

The ADC references VDDA. The firmware measures VREFINT to report millivolts
that are corrected for the actual supply voltage.

### Power

- **12 V input:** screw terminal, then PTC (2.5 A hold), then SMBJ13A TVS.
  With reversed polarity, the TVS conducts and trips the PTC.
- **MCU supply:** a BAT54C diode-OR of USB VBUS and +12 V feeds an
  AMS1117-3.3. The board can be programmed over USB alone, without 12 V.
  The 12 V branch goes through a 47 Ω resistor (1206), and a BZT52C13 zener
  clamps the LDO input. The TVS lets the rail reach 21.5 V during a surge.
  The resistor and zener keep the LDO input below its 15 V limit. The resistor
  also drops about 1.9 V at 40 mA, so the LDO dissipates about 0.3 W.
- **LDO output capacitor:** 22 µF tantalum. The AMS1117 needs output ESR for
  stability, and a low-ESR ceramic alone can make it oscillate.
- **12 V monitoring:** 47 kΩ/10 kΩ divider to PB0. Full scale is about 18.8 V.
- **USB:** micro-B connector and a 4-pin header wired in parallel. Use the
  header for a cable to an internal motherboard USB 2.0 port. A fixed 1.5 kΩ
  D+ pull-up is fitted. The firmware pulls D+ low for 10 ms at boot to force re-enumeration.

### Known limits

- **Current:** fan headers are rated 1 A each, and each channel has a 1.1 A
  PTC. Total current is limited to 2.5 A by F1, so four fans at 1 A cannot all
  run at once. At power-up all fans start at 100 % together. Measure the
  combined inrush with the real fans.
- **`dc` mode tach:** with the supply chopped, the tach reading is only valid
  at 100 % duty.
- **USB VID/PID:** the firmware uses ST's VCP IDs (0483:5740). Replace them
  before shipping a product.

## KiCad project

`hardware/tools/gen_kicad.py` is the single source of truth for the netlist.
It writes the following:

- `hardware/kicad/fan_controller.kicad_sch`: one A1 sheet, grouped by block
  (Power, MCU, USB, Fan 1–4, Analog inputs, Test points). Each pin connects through a net
  label (netlist-style).
- `hardware/kicad/fanctl.kicad_sym`: project symbol library. Pin numbers
  match the footprint pads.
- `hardware/kicad/fan_controller.kicad_pcb`: 100 × 80 mm outline, four M3
  holes, and a `Power` net class (1 mm tracks) for the 12 V and GND nets.
- `hardware/bom.csv`.

Every footprint is from the standard KiCad 8 libraries. Workflow:

1. Change parts or nets in `gen_kicad.py`, then run `python3 hardware/tools/gen_kicad.py`.
   The script refuses to generate output when a net has only one part, or
   when a part's pins do not match its symbol.
2. Open `fan_controller.kicad_pro`. Run ERC, then use Tools → Update PCB from
   Schematic (F8), then place and route.

Once you start editing the schematic by hand in KiCad, stop running the
generator, because it overwrites the files.

Checked with KiCad 8.0.9 (`kicad/kicad:8.0` Docker image):

- `kicad-cli sch erc --severity-all` reports 0 errors and 0 warnings.
- Every footprint loads from the standard libraries.
- Every symbol pin number matches a pad in its footprint, and every pad has a pin.

### BOM summary (136 parts, 53 lines; full list in `hardware/bom.csv`)

| Qty | Part | Package | Use |
|---|---|---|---|
| 1 | STM32F103C8T6 | LQFP-48 | MCU |
| 4 | AO3401A | SOT-23 | high-side fan switches |
| 8 | BSS138 | SOT-23 | gate level shifters, PWM buffers |
| 4 | BZT52C10 | SOD-123 | gate-source clamps |
| 1 | BZT52C13 | SOD-123 | LDO input clamp |
| 4 | SS14 | SMA | freewheel diodes |
| 6 | BAT54S | SOT-23 | analog input clamps |
| 1 | BAT54C | SOT-23 | supply diode-OR |
| 1 | SMBJ13A | SMB | 12 V TVS |
| 1 | MF-MSMF250/16X-2 | 1812 | 2.5 A PTC fuse |
| 4 | MF-MSMF110/16-2 | 1812 | 1.1 A per-channel PTC fuses |
| 1 | AMS1117-3.3 | SOT-223 | 3.3 V LDO |
| 1 | USBLC6-2SC6 | SOT-23-6 | USB ESD |
| 1 | 8 MHz crystal, CL 18 pF | 3225 | HSE |
| 4 | Molex 47053-1000 | 4-pin | fan headers |
| 1 | Molex 105017-0001 | micro-B | USB |
| 6 / 1 / 1 / 1 | 1×3 / 1×4 / 1×5 headers, 2-pin terminal | 2.54 / 5.08 mm | sensors / USB internal / SWD / 12 V |
| 6 | test pads | 1.5 mm | +12V, +3V3, GND, NRST, FAN1_PWM, FAN1_TACH |
| 78 | resistors, capacitors, ferrite, LED | 0603 / 1206 / 3528 | passives |

The MPNs in the BOM are examples. Check stock and second sources before ordering.

## Firmware

### Build

```sh
git clone --recurse-submodules git@github.com:rmskkn/fanny.git
cd fanny
make                 # firmware/build/fanctl.elf, .bin (~11.5 KiB flash)
make flash           # OpenOCD + ST-Link over SWD
make test            # host-side protocol and conversion tests
```

The first `make` builds libopencm3 for STM32F1 only. It also runs
`git submodule update --init` if the clone has no submodule checkout.
The same targets work from inside `firmware/`.

### Architecture

The firmware has no RTOS. It runs a single cooperative main loop, and two
interrupts feed it: SysTick for milliseconds and EXTI for tach edges. ADC
results arrive by DMA.

| Module | Responsibility |
|---|---|
| `Core/main.c` | 72 MHz clock, SysTick, init order, main loop, status LED |
| `App/pwm.c` | TIM2 (supply, 30 Hz) and TIM4 (signal, 25 kHz). Starts timers at 100 % before taking the pins |
| `App/fan.c` | Per-channel mode and duty; maps them onto the two PWM paths |
| `App/tach.c` | Tach pulse count per 1 s window, converted to RPM (2 pulses/rev) |
| `App/thermal.c` | ADC1 continuous scan + DMA: AIN1–6, 12 V, MCU temperature, VREFINT. 1 ms IIR filter, mV conversion |
| `App/safety.c` | IWDG (1 s), host-loss failsafe, stall detection |
| `App/protocol.c` | Line-based command parser, hardware-independent so it can be unit tested |
| `USB_DEVICE/usb_cdc.c` | CDC-ACM descriptors, RX ring buffer, TX with timeout. Output is dropped while the port is closed |

### Failsafe behaviour

- After power-up, fans run at 100 % until the host sends its first command.
- If no valid command arrives for 10 s, all fans return to 100 % and the LED
  blinks fast. The `watch` command in `fanctl.py` and any control daemon keep
  the timer from expiring.
- If the main loop hangs, the watchdog resets the MCU. During reset, the
  hardware pull-ups keep the fans at full speed.
- A fan is flagged `stall=1` when its duty is above 0 but its tach reads
  0 RPM for 3 s. Fans without a tach wire always show this flag.

### Protocol

The protocol is text over USB CDC. The baud rate is ignored. Each command
is one line ending in `\n`, and each reply ends with `ok` or `err <reason>`.

```
> set 2 40          duty 0..100 for fan 1..4 or "all"
ok
> mode 3 dc         pwm (4-pin) | dc (3-pin)
ok
> get
fan 1 mode=pwm duty=100 rpm=1590 stall=0
fan 2 mode=pwm duty=40 rpm=780 stall=0
fan 3 mode=dc duty=100 rpm=1200 stall=0
fan 4 mode=pwm duty=100 rpm=0 stall=1
ain 1 raw=2047 mv=1649
...
v12 mv=12043
vdda mv=3302
mcu temp=31.4
failsafe=0
ok
> ping              keepalive
ok
```

## Host tool

```sh
pip install -r host/requirements.txt
python3 host/fanctl.py -p /dev/ttyACM0 get
python3 host/fanctl.py set all 50
python3 host/fanctl.py mode 3 dc
python3 host/fanctl.py watch -i 2
```

### Setting one fan

Use the fan number (1–4) in place of `all`. Fan 1 is header J5, and fan 4 is header J8.

```sh
python3 host/fanctl.py set 1 50      # fan 1 to 50 %
python3 host/fanctl.py set 3 80      # fan 3 to 80 %
python3 host/fanctl.py set 4 0       # fan 4 off (in pwm mode the supply is cut too)
python3 host/fanctl.py -p /dev/ttyACM1 set 2 40   # board on another port
```

- Each command sets one fan or all fans. Lists such as `1,2` and ranges such as `1-3` are not accepted.
- To check a fan, run `python3 host/fanctl.py get`. Read the `fan N duty=… rpm=…` line.
- Settings hold only while the host keeps talking to the board. When no
  command arrives for 10 s, all fans return to 100 %. Keep `fanctl.py watch`
  or a control daemon running to hold the values.

The `FanController` class in `fanctl.py` can be imported to build a control
daemon, for example one that maps an NTC reading from `ain` to a fan duty.
For a 10 kΩ NTC on the 10 kΩ bias resistor, the resistance is
R<sub>NTC</sub> = 10 kΩ × mV / (VDDA − mV).
