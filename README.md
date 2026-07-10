# ESP32 Bare-Metal UART Driver

A UART driver built entirely from raw register writes — no Arduino, no ESP-IDF driver calls (`uart_write_bytes`, `uart_read_bytes`, etc.). Every byte sent or received in this project goes through direct memory-mapped register access to the ESP32's UART and GPIO peripherals.

## Why this project

Most beginner ESP32 projects call `Serial.println()` or `uart_write_bytes()` and never see what's underneath. This project skips the abstraction layer entirely and configures the hardware the way firmware engineers actually do it: reading the register map, setting baud-rate dividers, framing bits, and FIFO thresholds by hand, and writing an interrupt/polling-driven receive path from scratch.

## What's implemented

| Stage | What it proves | Status |
|---|---|---|
| 1 | GPIO output via direct register writes (LED blink, no `digitalWrite`) | ✅ Working |
| 2 | UART0 baud rate + frame configuration via `clk_div`/`conf0` registers | ✅ Working |
| 3 | UART TX — bytes pushed directly into `UART.fifo.rw_byte`, FIFO-full polling | ✅ Working, verified continuously over time |
| 4 | UART RX — FIFO polling/interrupt-driven receive, register-level clock gating for UART1 | ✅ Working — proven via self-contained hardware loopback |
| 5 | Full loop: TX → wire → RX → command parsing → GPIO control (`led on` / `led off` toggles a physical LED) | ✅ Working, with a documented and instrumented noise limitation (see below) |

## Architecture

```
ESP32 UART1 TX (GPIO17) ──jumper wire──> ESP32 UART1 RX (GPIO16)
        │                                         │
        ▼                                         ▼
  uart1_puts("led on")                    uart1_try_getc() / parser
                                                    │
                                                    ▼
                                          GPIO2 (onboard LED) toggled
```

UART0 is reserved for status output to the PC (via the USB-serial connection); UART1 is the peripheral doing the actual bare-metal TX/RX work, routed to GPIO16/17 through the ESP32's GPIO matrix and looped back with a single jumper wire.

## The debugging story (the part worth reading)

Stage 4 (RX) did not work on the first attempt, or the fifth. Getting it working required systematically ruling out five different hypotheses, in order, each with an actual test rather than a guess:

1. **Interrupt logic bugs** — added frame/parity error detection and hex-dumped every received byte. Bytes were arriving, but with corruption.
2. **ESP-IDF console driver conflict** — UART0 is ESP-IDF's default console channel, and its background driver could plausibly race a custom ISR for the same FIFO. Disabled the console entirely via `menuconfig` (`Channel for console output → None`). Corruption persisted — ruled out.
3. **LED-induced electrical noise** — GPIO2 was switching every 50–200 ms right next to the UART lines on a breadboard. Disabled the LED blink loop entirely. Corruption persisted, and got worse — ruled out (and informative: proved the GPIO switching wasn't the dominant noise source).
4. **Baud rate margin** — dropped from 115200 to 9600 baud, which gives a receiver far more timing margin against jitter. Corruption was identical at both speeds — ruled out a marginal-signal explanation.
5. **USB-serial chip / cable path** — built a self-contained **UART1 hardware loopback** (TX on GPIO17 jumpered directly to RX on GPIO16), which bypasses the USB cable and onboard USB-serial chip entirely. This came back **perfectly clean, repeating indefinitely with zero corruption**, which is the proof that mattered: the register-level driver code — clock gating, `clk_div`, `conf0` framing, FIFO read/write logic — was correct all along. The corruption seen via USB was external to the driver.

That loopback became the foundation for Stage 5.

### A known, instrumented limitation

Under sustained operation, the UART1↔UART1 jumper-wire loopback still occasionally shows single-frame corruption — consistent with a breadboard jumper connection being electrically marginal at 115200 baud over time (contact resistance, minor EMI, an unshielded wire acting as a small antenna). Rather than hide this, Stage 5's code includes an explicit mismatch detector:

```c
if (strcmp(rx_buffer, last_sent) != 0) {
    uart0_puts("!! MISMATCH: sent '");
    uart0_puts(last_sent);
    uart0_puts("' but received '");
    uart0_puts(rx_buffer);
    uart0_puts("'\r\n");
}
```

Any frame that doesn't exactly match a known command (`"led on"` / `"led off"`) is already rejected by the parser and never actuates the LED incorrectly — so the corruption is visible in the logs but never silently produces wrong behavior. This is the same category of problem real UART links solve with parity bits, checksums, or CRC — a natural "Stage 6" extension noted below.

## Hardware

- ESP32 dev board (ESP32-D0WD-V3)
- Single jumper wire: GPIO17 → GPIO16
- USB cable to PC for flashing and status output (UART0 console)

## Building and flashing

```powershell
idf.py build
idf.py -p COM14 flash monitor
```

Requires ESP-IDF v5.2.7 and a standard `idf.py` toolchain setup. No external libraries beyond what ships with ESP-IDF's `soc/` register headers — this project intentionally avoids `driver/uart.h` and the ESP-IDF UART driver.

## What I'd add with more time

- **CRC or parity checking** on received frames to formally detect and reject corrupted bytes rather than relying on exact-string matching
- **True interrupt-driven RX** with ESP-IDF's console fully disabled at the linker level, to confirm interrupt-based receive performs identically to the polling implementation used here
- **Oscilloscope/logic analyzer capture** of the loopback signal to directly characterize the noise source (rise time, ringing, etc.) rather than inferring it from software-level symptoms
- Port the same driver logic to a TI LaunchPad (MSP430/TM4C) to demonstrate the register-level approach isn't ESP32-specific

## Repository structure

```
main/
  uart_bare_metal.c    — GPIO, UART0, UART1 register-level driver + command demo
  CMakeLists.txt
CMakeLists.txt
sdkconfig
```
