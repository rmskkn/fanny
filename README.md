# Fanny — a versatile USB PWM fan controller

![PCB 3D view](hardware/pcb_3d.png)

USB-controlled controller for four 12 V PC fans and six analog sensor inputs.
It is a carrier board for an STM32F103C8T6 "Blue Pill" module, which shows up
on the host as a USB CDC serial port (`/dev/ttyACM*`, `COMx`).

```
stm32-fan-controller/
├── firmware/          libopencm3 firmware (C11, arm-none-eabi-gcc)
│   ├── Core/          main loop, clocks, SysTick, linker script, board pin map
│   ├── Drivers/       libopencm3 git submodule
│   ├── USB_DEVICE/    USB CDC-ACM device
│   ├── App/           fan, pwm, tach, protocol, thermal (analog), safety
│   └── tests/         host-side unit tests (`make test`)
├── hardware/
│   ├── kicad/         KiCad 10 project (schematic, symbol lib, board outline) — source of truth, hand-edited
│   ├── bom.csv        bill of materials
│   ├── pcb_3d.png     3D render of the placed board
│   ├── pcb_top.png    placement plot, top view
│   └── datasheets/    links to the key datasheets
└── host/              fanctl.py command-line client (pyserial)
```

## Prerequisites

1. KiCad 10.0.7 (`kicad-cli`)

   ```sh
   # Arch
   sudo pacman -S kicad

   # Ubuntu 24.04
   sudo add-apt-repository ppa:kicad/kicad-10.0-releases
   sudo apt update
   sudo apt install kicad
   ```

2. arm-none-eabi-gcc, OpenOCD

   ```sh
   # Arch
   sudo pacman -S arm-none-eabi-gcc arm-none-eabi-newlib openocd

   # Ubuntu 24.04
   sudo apt install gcc-arm-none-eabi openocd
   ```

3. Python 3, pyserial

   ```sh
   # Arch
   sudo pacman -S python python-pyserial

   # Ubuntu 24.04
   sudo apt install python3 python3-serial
   ```

4. rsvg-convert

   ```sh
   # Arch
   sudo pacman -S librsvg

   # Ubuntu 24.04
   sudo apt install librsvg2-bin
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

```mermaid
flowchart LR
    classDef power fill:#fde3d8,stroke:#e2572b,color:#111
    classDef usb fill:#dbe7ff,stroke:#2f6fed,color:#111
    classDef mcu fill:#e8d7f5,stroke:#5b4b8a,color:#111
    classDef pwm fill:#d7f5df,stroke:#2f9e44,color:#111
    classDef dcmode fill:#fdecc3,stroke:#c77f00,color:#111
    classDef tach fill:#d7f5df,stroke:#0f9b8e,color:#111
    classDef analog fill:#fbd9e8,stroke:#c2255c,color:#111

    subgraph PWR["Power (12 V)"]
        PSU["12 V PSU"] --> J1["J1 Mate-N-Lok"] --> F1["F1 PTC 2.5A"] --> D1["D1 TVS"] --> V12["+12V"]
        V12 --> F25["F2..F5 1.1A PTC x4"] --> FANV["fan +12V (x4)"]
        V12 --> DIV["47k/10k divider"] --> PB0["PB0 ADC"]
    end

    subgraph USB3V3["USB / 3V3"]
        USB["USB (Blue Pill microB)"] --> P5V["+5V"] --> LDO["Blue Pill LDO"] --> V33["+3V3"] --> FB1["FB1"] --> V33A["+3V3A"]
    end

    subgraph MCU["Blue Pill module (J2, J3)"]
        subgraph PWMSIG["PWM signal, fan pin 4"]
            TIM4["TIM4 PB6..PB9 25kHz"] --> BSS["BSS138 open drain"] --> FPWM["fan PWM pin"]
        end
        subgraph DCMODE["DC-mode motor switch, per channel"]
            QINV["Q_inv BSS138"] --> QEN["Q_en, gated FANn_DCEN"] --> QPNP["Q_pnp MMBT3906"] --> QHS["Q_hs AO3401A"] --> FSW["fan +12V motor switch"]
            GPIO["PA6/7/8, PB1 GPIO"] --> QEN
        end
        TIM4 -. reuses PWM signal .-> QINV
        subgraph TACHLANE["Tach"]
            FT["fan tach"] --> PU["10k pull-up"] --> RC["RC filter"] --> EXTI["EXTI PB12..PB15"]
        end
    end

    subgraph ANALOG["Analog inputs"]
        SENS["sensors"] --> RCB["1k + 100n + BAT54S"] --> ADC["PA0..PA5 ADC x6"]
    end

    class PSU,J1,F1,D1,V12,F25,FANV,DIV power
    class PB0 mcu
    class USB,P5V,LDO,V33,FB1,V33A usb
    class TIM4,BSS,FPWM pwm
    class QINV,QEN,QPNP,QHS,FSW,GPIO dcmode
    class FT,PU,RC,EXTI tach
    class SENS,RCB,ADC analog
```

### Pin map

| Function | Pins | Peripheral |
|---|---|---|
| Fan 1–4 PWM signal (also the motor-switch chop signal, DC mode) | PB6, PB7, PB8, PB9 | TIM4 CH1–4, 25 kHz |
| Fan 1–4 DC-mode enable | PA6, PA7, PA8, PB1 | plain GPIO, push-pull |
| Fan 1–4 tach | PB12–PB15 | EXTI12–15, falling edge |
| AIN1–AIN6 | PA0–PA5 | ADC1 IN0–IN5 |
| 12 V sense | PB0 | ADC1 IN8 |
| USB | PA11 (D−), PA12 (D+) | USB FS |
| SWD | PA13, PA14 | Blue Pill 4-pin SWD header |
| Status LED | PC13 | Blue Pill on-board LED, active low |
| Clock | PD0/PD1 | Blue Pill 8 MHz crystal, PLL to 72 MHz |

The DC-mode enable pins are plain GPIO, not a timer remap: no `AFIO_MAPR`
write, JTAG and SWD both stay as the Blue Pill module provides them.

### Blue Pill module

The module plugs into two 1×20 female headers (J2, J3) with rows 15.24 mm
apart. Pin 1 of each row is at the module's USB end. With the USB end up and
the components facing you, J2 is the right row and J3 the left row:

| Pin | J2 (row A, right) | J3 (row B, left) |
|---|---|---|
| 1–4 | GND, GND, 3V3, NRST | PB12, PB13, PB14, PB15 |
| 5–10 | PB11, PB10, PB1, PB0, PA7, PA6 | PA8, PA9, PA10, PA11, PA12, PA15 |
| 11–16 | PA5, PA4, PA3, PA2, PA1, PA0 | PB3, PB4, PB5, PB6, PB7, PB8 |
| 17–20 | PC15, PC14, PC13, VBAT | PB9, 5V, GND, 3V3 |

The pinout follows the
[DeepBlueMbedded Blue Pill diagram](https://deepbluembedded.com/stm32-blue-pill-pinout-programming-guide/).
Check it against your module before laying out the board.

The module provides the MCU, the 8 MHz crystal, the 3.3 V regulator, the PC13
LED, the reset button, the BOOT jumpers, the SWD header and the micro-B USB
connector. The module runs from USB only, through its own micro-B connector —
the carrier has no USB connector or wiring of its own. The 12 V input powers
only the fans. The module's 3V3 pin powers the carrier's 3.3 V logic and
sensor bias.

Things to check on the module:

- **D+ pull-up (R10):** USB needs 1.5 kΩ, but many Blue Pills fit 10 kΩ, and
  some hosts then fail to enumerate. Replace R10 with 1.5 kΩ if needed.
- **MCU:** some modules carry a clone such as CS32F103, CKS32F103 or
  GD32F103 instead of an ST part. The firmware targets the ST part. Clones can
  differ in USB and flash timing.
- **NRST:** the module's SWD header has no reset pin. Use TP4 or the R pin
  for connect-under-reset.

### Fan channel

Each channel drives a standard 4-pin header (1 GND, 2 +12 V, 3 tach, 4 PWM)
and supports both 4-pin PWM fans and 3-pin DC fans on the same header, in
either firmware mode (`pwm`, the default, or `dc`; see [Protocol](#protocol)).

**Supply and motor switch.** Fan pin 2 connects to +12 V through a 1.1 A PTC
(Bourns MF-MSMF110/16), then through a high-side P-channel MOSFET (`Q_hs`,
AO3401A). A shorted fan or cable trips only its own PTC, so the other fans
keep running and F1 does not trip. In `pwm` mode the switch is held fully on
and only pin 4 controls speed, exactly as a 4-pin-only board would behave. In
`dc` mode the switch chops the +12 V supply at the same 25 kHz duty that pin 4
carries, which is how a 3-pin fan (no PWM input, pin 4 unconnected) is
speed-controlled.

**Motor switch gate drive.** The MCU cannot drive the P-FET gate directly, so
a two-stage inverting chain does: a BSS138 (`Q_inv`), gated directly by the
same pin-4 PWM signal (reused, not a separate net), switches an MMBT3906 PNP
(`Q_pnp`), which pulls the P-FET gate toward +12 V to turn it off. A second
BSS138 (`Q_en`) sits in series between `Q_inv` and GND, gated by its own
per-channel GPIO (set by the `mode` command) — both transistors must be on
for the chop to reach `Q_pnp`, so `pwm` mode simply holds `Q_en` off and the
pin-4 signal never reaches the gate chain, regardless of its duty. A 2.2 kΩ
resistor (`R_gpd`) holds the P-FET gate at GND — switch **on** — whenever
either stage is not driven, and a 10 kΩ resistor (`R_dcen`) holds `Q_en`'s
gate low by default. This rest state, not an actively-driven "off", is what
keeps C1 below satisfied regardless of mode. An SS34 freewheel diode (`D_fw`)
across each channel's output absorbs the motor's inductive kick when the
switch opens.

**Speed.** In `pwm` mode the firmware sets speed with a 25 kHz signal on pin
4. Many 4-pin fans keep turning at 0 % PWM, at their minimum speed; the board
cannot stop such a fan. In `dc` mode speed comes from the motor switch
following that same signal instead; see [Known limits](#known-limits) for its
two caveats (start threshold, tach under-read).

**PWM signal transistor.** A BSS138 works as an open-drain driver on fan pin
4. The fan pulls this pin up internally to ≤5.25 V (Intel 4-wire spec), so
the MCU pin never sees the fan's voltage. The transistor inverts the signal,
and the firmware compensates with timer mode PWM2.

**Safe state.** A 100 kΩ pull-down holds the PWM transistor off unless
the MCU drives it, releasing pin 4 (100 % on a released line). `R_gpd` holds
the motor switch on, and `R_dcen` holds `Q_en` off, by the same logic
(previous paragraph). All three rest states agree: a fan runs at 100 % with
no firmware running, in either mode. See
[Failsafe behaviour](#failsafe-behaviour).

**Tach input.** The fan's open-collector output has a 10 kΩ pull-up to 3.3 V,
then 1 kΩ + 1 nF filtering. PB12–PB15 are 5 V tolerant. The motor switch is
high-side, so fan GND stays at board GND even while the switch is open, and
the tach chain sees no overvoltage in `dc` mode. A fan that pulls its tach
output to 12 V internally (outside the spec) would need an extra clamp.

### Compatibility

Verified against the [Intel 4-wire PWM fan spec rev 1.2](https://www.konilabs.net/docs/standards/fan/intel_4wire_pwm_fans_specs_rev1_2.pdf):

| Spec requirement | Board implementation | Status |
|---|---|---|
| Pinout: 1 GND, 2 +12 V, 3 tach, 4 PWM | `Fan_4pin` header wired in that order | Match |
| PWM: open-drain/collector, fan pulls up internally | BSS138 open-drain transistor, MCU only sinks | Match |
| PWM frequency: 21–28 kHz target (25 kHz nominal) | TIM4 at 72 MHz / 2880 = 25.000 kHz exact | Match |
| PWM line released = fan runs full speed (fail-safe) | 100 kΩ pull-down holds transistor off until firmware drives it | Match |
| Tach: open-collector, pulled up, 2 pulses/rev | 10 kΩ pull-up to 3.3 V, `TACH_PULSES_PER_REV = 2` in firmware | Match |
| Tach pull-up rail ≤ 5 V tolerant input | Pulled to 3.3 V; PB12–PB15 are 5 V tolerant | Match |

See [Fan channel](#fan-channel) above for the per-pin circuit detail.

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
- **Module supply:** USB only. The board has no 5 V or 3.3 V regulator. The
  Blue Pill's own LDO makes 3.3 V from USB VBUS.
- **12 V monitoring:** 47 kΩ/10 kΩ divider to PB0. Full scale is about 18.8 V.
- **USB:** Blue Pill micro-B connector only; the carrier has no USB
  connector, header or ESD protection of its own. The firmware pulls D+ low
  for 10 ms at boot to force host re-enumeration.

### Known limits

- **Current:** fan headers are rated 1 A each, and each channel has a 1.1 A
  PTC. Total current is limited to 2.5 A by F1, so four fans at 1 A cannot all
  run at once. At power-up all fans start at 100 % together. Measure the
  combined inrush with the real fans.
- **0 % does not stop every 4-pin fan in `pwm` mode:** see Fan channel. In
  `dc` mode 0 % does stop the fan, because the motor switch cuts the supply.
- **12 V sense without USB:** with 12 V present and USB unplugged, the
  47 kΩ/10 kΩ divider feeds about 2.1 V into the unpowered PB0 pin. The
  current is about 40 µA, far below the STM32's injection limit.
- **3-pin fans (`dc` mode) do not start below roughly 30–40 % duty.** This is
  a motor-startup limit, not a firmware clamp; set a curve minimum above it
  in `fancontrold` (see `fancontrol.conf.example`).
- **Tach under-reads in `dc` mode.** The fan's electronics are unpowered
  during the chopped-off phase, so tach pulses are missing roughly in
  proportion to duty. `stall` is therefore meaningful only near 100 % duty on
  a `dc`-mode channel.
- **25 kHz supply chopping stresses a fan's input capacitor.** Inaudible, but
  the chop reuses the pin-4 signal timer (`SIGNAL_PERIOD` in `App/pwm.c`),
  shared across all four channels, so there is no per-channel frequency to
  lower for one misbehaving fan without also changing the signal frequency on
  every `pwm`-mode channel.
- **USB VID/PID:** the firmware uses ST's VCP IDs (0483:5740). Replace them
  before shipping a product.

## KiCad project

The KiCad files are the source of truth: edit them directly in the KiCad
GUI.

- `hardware/kicad/fan_controller.kicad_sch`: one A1 sheet, grouped by block
  (Power, MCU, Fan 1–4, Analog inputs, Test points). Each pin connects through a net
  label (netlist-style).
- `hardware/kicad/fanctl.kicad_sym`: project symbol library. Pin numbers
  match the footprint pads.
- `hardware/kicad/fan_controller.kicad_pcb`: 81 × 78 mm outline, four M3
  holes, and a `Power` net class (1 mm tracks) for the 12 V and GND nets.
- `hardware/bom.csv`.

The 3D model (`fan_controller.step`, `fan_controller.glb`) lives under
`hardware/kicad/3dmodels/` and is committed. The Blue Pill module has no 3D
model, so only its two sockets appear.

Every footprint on the board points to its model as
`${KIPRJMOD}/3dmodels/<library>.3dshapes/<name>.step`. KiCad resolves
`${KIPRJMOD}` to the project folder, so the 3D viewer and STEP export use
these files without the KiCad 3D library installed. A footprint added later
in the KiCad GUI keeps its library path (`${KICAD10_3DMODEL_DIR}/…`). To use a
local copy, put the `.step` file under `3dmodels/` and change the path in
the footprint's 3D Models tab.

Every footprint is from the standard KiCad 10 libraries.

Workflow: open `fan_controller.kicad_pro`, change the schematic and/or
board, run ERC/DRC, then route. Re-export the BOM (`hardware/bom.csv`) and
the renders below after a change that affects them.

To refresh `hardware/pcb_3d.png`:

```sh
cd hardware/kicad
kicad-cli pcb render --side top --rotate '-20,0,350' --zoom 0.75 --perspective --floor \
    --background transparent --quality high -w 480 -h 500 -o ../pcb_3d.png fan_controller.kicad_pcb
```

To refresh `hardware/pcb_top.png`:

```sh
kicad-cli pcb export svg --layers Edge.Cuts,F.Cu,F.SilkS,F.Fab --exclude-drawing-sheet \
    --page-size-mode 2 -o top.svg hardware/kicad/fan_controller.kicad_pcb
rsvg-convert -w 1400 -b white top.svg -o hardware/pcb_top.png && rm top.svg
```

Checked with KiCad 10.0.7:

- `kicad-cli sch erc --severity-all` reports 11 errors, 5 warnings. All
  pre-existing (not introduced by the `dc`-mode feature): unused pins on
  `J1`, `D1`, `C1-C5`, `R2` (VIN/analog decoupling group — not yet given
  `no_connect` flags), one spare BluePill pin, one unresolved
  input-power-pin-not-driven quirk on a GND symbol. Tracked in `AGENTS.md`.
- `kicad-cli pcb drc --severity-all` reports 0 errors, 21 warnings
  (silkscreen clearance/overlap in the densest rows and a few dangling-track
  notices; cosmetic, not a fab blocker).
- Every footprint loads from the standard libraries and matches its library
  copy.
- Every symbol pin number matches a pad in its footprint, and every pad has a pin.

### BOM summary (plus the Blue Pill module)

`hardware/bom.csv` is regenerated from the schematic (`sch export bom`,
grouped by value/footprint), with MPN/notes merged in by hand. This table
reflects the board as built, including the per-channel `dc`-mode motor
switch (`Q_inv`/`Q_en`/`Q_hs`/`Q_pnp`, see `hardware/3pin_dc_mode_plan.md`):

| Qty | Part | Package | Use | DigiKey |
|---|---|---|---|---|
| 1 | STM32F103C8T6 Blue Pill | module | MCU, plugs into J2/J3 | not stocked at DigiKey; buy from module sellers |
| 12 | BSS138 | SOT-23 | PWM signal transistors (Q1-4); `Q_inv`/`Q_en` cascode motor-switch gate ×4 channels (Q7,10,13,16,17-20) | [BSS138](https://www.digikey.com/en/products/result?keywords=BSS138) |
| 4 | AO3401A | SOT-23 | `Q_hs`: high-side P-MOSFET motor switch | [AO3401A](https://www.digikey.com/en/products/result?keywords=AO3401A) |
| 4 | MMBT3906 | SOT-23 | `Q_pnp`: PNP level-shift driver for `Q_hs` gate | [MMBT3906](https://www.digikey.com/en/products/result?keywords=MMBT3906) |
| 4 | SS34 | SMA | flyback diode across fan motor (`dc` mode) | [SS34](https://www.digikey.com/en/products/result?keywords=SS34) |
| 4 | 0 Ω jumper (DNP) | 1206 | `JP1-4`: optional full-bypass override, see `3pin_dc_mode_plan.md` | generic 1206 0 Ω bridge/jumper |
| 6 | BAT54S | SOT-23 | analog input clamps | [BAT54S,215](https://www.digikey.com/en/products/result?keywords=BAT54S%2C215) |
| 1 | SMBJ13A | SMB | 12 V TVS | [SMBJ13A](https://www.digikey.com/en/products/result?keywords=SMBJ13A) |
| 1 | MF-MSMF250/16X-2 | 1812 | 2.5 A PTC fuse | [MF-MSMF250/16X-2](https://www.digikey.com/en/products/result?keywords=MF-MSMF250%2F16X-2) |
| 4 | MF-MSMF110/16-2 | 1812 | 1.1 A per-channel PTC fuses | [MF-MSMF110/16-2](https://www.digikey.com/en/products/result?keywords=MF-MSMF110%2F16-2) |
| 4 | Molex 47053-1000 | 4-pin | fan headers | [0470531000](https://www.digikey.com/en/products/result?keywords=0470531000) |
| 2 | 1×20 female header | 2.54 mm | Blue Pill socket | [PPTC201LFBN-RC](https://www.digikey.com/en/products/result?keywords=PPTC201LFBN-RC) (Sullins, or any 1×20 socket) |
| 1 | TE Connectivity 350424-1 | 4-pin, 5.08 mm | 12 V input, Mate-N-Lok | [350424-1](https://www.digikey.com/en/products/result?keywords=350424-1) |
| 6 | 1×3 headers | 2.54 mm | sensor inputs | any 2.54 mm pin header |
| 6 | test pads | 1.5 mm | +12V, +3V3, GND, NRST, FAN1_PWM, FAN1_TACH | — (PCB pads, not a part) |
| — | resistors, capacitors, ferrite | 0603 / 1206 / 6.3 mm | passives | [BLM18AG601SN1D](https://www.digikey.com/en/products/result?keywords=BLM18AG601SN1D) (FB1); others generic, see `bom.csv` |

The MPNs in the BOM are examples. Check stock and second sources before ordering.
The DigiKey links are keyword searches by MPN, so they still work when DigiKey
part numbers change. Pick the exact package and reel option from the results.

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
| `Core/main.c` | 72 MHz clock, SysTick, init order, main loop, status LED (Blue Pill PC13) |
| `App/pwm.c` | TIM4 fan PWM signal (25 kHz, also the motor-switch chop signal) and the per-channel DC-mode-enable GPIOs. Starts the timer at 100 % and the enables low before taking the pins |
| `App/fan.c` | Per-channel mode and duty, mapped onto the signal PWM and the DC-mode enable |
| `App/tach.c` | Tach pulse count per 1 s window, converted to RPM (2 pulses/rev) |
| `App/thermal.c` | ADC1 continuous scan + DMA: AIN1–6, 12 V, MCU temperature, VREFINT. 1 ms IIR filter, mV conversion |
| `App/safety.c` | IWDG (1 s), host-loss failsafe, stall detection |
| `App/protocol.c` | Line-based command parser, hardware-independent so it can be unit tested |
| `USB_DEVICE/usb_cdc.c` | CDC-ACM descriptors, RX ring buffer, TX with timeout. Output is dropped while the port is closed |

### Failsafe behaviour

All fans run at 100 % whenever the controller is not in control, in both
`pwm` and `dc` mode. Fan power comes straight from 12 V, so this needs only
12 V present. The PWM signal line, the motor switch, and the DC-mode enable
all rest in their "fan on" state (released pin 4; `R_gpd` holds the motor
switch on; `R_dcen` holds the enable off), so either mode reaches 100 % the
same way.

| Case | Result |
|---|---|
| Power-up, before the host's first command | 100 % |
| USB unplugged or host powered off | Module unpowered, both switches rest on, 100 % |
| Module pulled out of its socket | Both switches rest on, 100 % |
| Supply sags below the STM32's reset threshold (about 1.9 V) | MCU held in reset, both switches rest on, 100 % |
| Firmware hangs | Watchdog (1 s) resets the MCU, both switches rest on during reset, 100 % |
| No valid command for 10 s, module still powered | Firmware failsafe sets 100 %, LED blinks fast, mode unchanged |

`fanctl.py watch` or a control daemon keeps the 10 s timer from expiring.
A fan is flagged `stall=1` when its duty is above 0 but its tach reads 0 RPM
for 3 s. Fans without a tach wire always show this flag.

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
python3 host/fanctl.py set 4 0       # fan 4 to 0 % (some fans keep turning slowly)
python3 host/fanctl.py -p /dev/ttyACM1 set 2 40   # board on another port
```

- Each command sets one fan or all fans. Lists such as `1,2` and ranges such as `1-3` are not accepted.
- To check a fan, run `python3 host/fanctl.py get`. Read the `fan N mode=… duty=… rpm=…` line.
- Settings hold only while the host keeps talking to the board. When no
  command arrives for 10 s, all fans return to 100 %. Keep `fanctl.py watch`
  or a control daemon running to hold the values.
- `mode <1-4|all> pwm|dc` picks the speed-control path for a channel (see
  [Fan channel](#fan-channel)). It defaults to `pwm` at every boot and after
  every reconnect, so a 3-pin fan's channel needs `mode N dc` resent each time.

The `FanController` class in `fanctl.py` can be imported to build a control
daemon, for example one that maps an NTC reading from `ain` to a fan duty.
For a 10 kΩ NTC on the 10 kΩ bias resistor, the resistance is
R<sub>NTC</sub> = 10 kΩ × mV / (VDDA − mV).

### Fan control daemon

`host/fancontrold.py` is that daemon. It reads an INI config
(`host/fancontrol.conf.example`) that sets each of the four channels to a
constant duty, a temperature curve driven by one AIN input, or a curve
driven by an NVIDIA GPU's temperature (`sensor = gpuN`, read via
`nvidia-smi`, using the max of core and memory temperature). A managed
channel also takes `type = pwm` (default, 4-pin) or `type = dc` (3-pin); the
daemon sends `mode` once at startup and again after every reconnect, since a
firmware reset returns every channel to `pwm`. Every cycle it re-sends each
managed channel's duty, which doubles as the ping that holds off the
firmware's 10 s failsafe. A channel with no `[fanN]` section is left alone.
`nvidia-smi` must be on `PATH`; no extra Python dependency is needed.

For a `gpuN` sensor, install the proprietary NVIDIA driver, which provides
`nvidia-smi`:

```sh
# Arch
sudo pacman -S nvidia nvidia-utils

# Ubuntu 24.04
sudo apt install nvidia-driver-550
```

Reboot machine to load nvidia modules.
Verify with `nvidia-smi --query-gpu=index,temperature.gpu,temperature.memory --format=csv`.

```sh
cp host/fancontrol.conf.example ~/.config/fancontrol.conf
# edit ~/.config/fancontrol.conf for your sensors and curves
python3 host/fancontrold.py                 # foreground, Ctrl-C to stop
```

To run it as a systemd user service:

```sh
mkdir -p ~/.config/systemd/user
cp host/systemd/fancontrold.service ~/.config/systemd/user/
# edit ExecStart in that file to this repo's checkout path
systemctl --user daemon-reload
systemctl --user enable --now fancontrold.service
loginctl enable-linger "$USER"   # only if the board must run with no user session logged in
```

## License

Copyright © 2026 Roman Skakun.

This project — hardware design files, firmware, and host tool — is licensed
under [Creative Commons Attribution-NonCommercial-ShareAlike 4.0 International](LICENSE)
(CC BY-NC-SA 4.0).

You may build, modify, and share this design for personal and other
non-commercial purposes, with attribution, and under the same license.
Commercial use — including selling assembled boards, bare PCBs, or kits — is
not permitted without a separate written license from the copyright holder.

`firmware/Drivers/libopencm3` is a submodule under its own license (LGPL-3.0)
and is not covered by the above.
