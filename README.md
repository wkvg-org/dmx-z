# dmx-z

Shared DMX512 (ANSI E1.11) driver for the ESP32 family running Zephyr
RTOS, used by the Wolfenbütteler Kultur- und Veranstaltungsgesellschaft
venue/home-automation lighting system.

dmx-z is **not** a standalone application — it is a Zephyr *module* that
you include into an application. It provides three cooperating pieces:

| Component | Source | Purpose |
|-----------|--------|---------|
| Transmitter | `dmx_z_tx.c` | RS485 master — keeps a 512-slot universe and transmits it continuously with hardware BREAK generation |
| Receiver | `dmx_z_rx.c` | RS485 slave — polls the UART RX FIFO, detects BREAK for frame sync and maintains a 512-slot universe snapshot |
| Fixture | `fixture.c` | Generic N-channel DMX device with a configurable start address that dispatches a per-channel callback |

## Features

- **TX**: 250000 baud, 8N2, RS485 half-duplex. 512-slot universe refreshed
  at a configurable rate (default 40 Hz). Hardware BREAK/MAB via the ESP32
  UART low-level HAL (`uart_ll`), so the CPU does not busy-loop the BREAK.
- **RX**: polls the UART RX FIFO, uses BREAK (framing-error) detection for
  frame synchronisation and tolerates spurious mid-frame framing errors
  typical of noisy RS485 buses. Slots are published through a mutex-protected
  snapshot that can be read from any thread.
- **Fixture**: maps a contiguous channel range onto an application callback,
  with bounds-checking against the 512-slot universe.
- **ESP32-only**: relies on `hal_espressif` (the ESP32 UART LL/HAL). TXD
  inversion is configurable for MAX485-style auto-DE transceivers.

## DMX512 frame format

The driver implements ANSI E1.11 framing:

```
BREAK (≥ 88 µs) → MAB (≥ 8 µs) → START CODE (0x00) → 512 slots
```

The transmitter sends BREAK + MAB + start code + all 512 slots on every
refresh cycle. The receiver treats a framing error outside of a frame as the
start of a new frame.

## API

All functions are declared in [`dmx_z.h`](dmx_z.h).

### Transmitter

```c
int  dmx_tx_init(void);                              // configure UART, start TX thread
void dmx_tx_set_slot(uint16_t ch, uint8_t value);    // set one slot (1..512)
void dmx_tx_set_slots(uint16_t start, const uint8_t *buf, size_t n);
void dmx_tx_blackout(void);                          // zero the whole universe
```

`dmx_tx_set_slot()` / `dmx_tx_set_slots()` are 1-based channel numbers,
matching DMX convention (channel 1 is the first slot after the start code).

### Receiver

```c
int      dmx_rx_init(void);                          // configure UART, start RX thread
bool     dmx_rx_frame_ready(void);                   // true once per complete frame (read-once)
uint8_t  dmx_rx_get_slot(uint16_t ch);               // 1-based
void     dmx_rx_get_slots(uint16_t start, uint8_t *buf, size_t n);
```

`dmx_rx_frame_ready()` returns `true` when a complete frame has arrived since
the previous call and resets itself — use it to gate per-frame processing.

### Fixture abstraction

```c
typedef void (*dmx_fixture_channel_cb)(uint8_t channel, uint8_t value);

struct dmx_fixture_cfg {
    uint16_t start_addr;          // 1..512
    uint8_t  num_channels;
    dmx_fixture_channel_cb on_channel;
};

void dmx_fixture_process(const struct dmx_fixture_cfg *cfg);
```

`channel` passed to the callback is 0-based within the fixture range; `value`
is the raw DMX slot value (0..255).

## Integration

dmx-z is designed to live **next to** the application that uses it (a sibling
directory), and is pulled in via the application's build files.

### 1. Directory layout

```
<workspace>/
├── dmx-z/                 # this repository
└── my-app/
    ├── CMakeLists.txt
    ├── Kconfig
    ├── prj.conf
    └── src/main.c
```

### 2. Kconfig

Source dmx-z's Kconfig from the application's `Kconfig`:

```
source "Kconfig.zephyr"
rsource "../dmx-z/Kconfig"
```

### 3. CMake

Add the module from the application's `CMakeLists.txt`:

```cmake
add_subdirectory(../dmx-z ${CMAKE_BINARY_DIR}/dmx-z)
```

The dmx-z `CMakeLists.txt` only compiles the TX/RX sources that are enabled
via `CONFIG_DMX_TX` / `CONFIG_DMX_RX` and always compiles the fixture
abstraction. It also exports its configuration as compile-time definitions.

### 4. Devicetree

The driver locates its UART through the `dmx-uart` alias. Your board overlay
(or devicetree) must provide it, e.g.:

```devicetree
/ {
    aliases {
        dmx-uart = &uart1;
    };
};

&uart1 {
    status = "okay";
    current-speed = <250000>;
};
```

The alias must point to the same UART peripheral as `CONFIG_DMX_UART_NUM`.

### 5. Fetch the ESP32 HAL

The ESP32 UART LL header (`hal/uart_ll.h`) is provided by the Espressif HAL:

```bash
west blobs fetch hal_espressif
```

## Configuration

All options live under `DMX_Z` in Kconfig.

| Option | Default | Range | Description |
|--------|---------|-------|-------------|
| `DMX_Z` | y | — | Master switch for the dmx-z driver |
| `DMX_TX` | y | — | Enable the transmitter (RS485 master) |
| `DMX_RX` | y | — | Enable the receiver (RS485 slave) |
| `DMX_UART_NUM` | 1 | 0–2 | ESP32 hardware UART number; must match the `dmx-uart` alias |
| `DMX_BAUD` | 250000 | — | DMX512 baud rate (do not change) |
| `DMX_BREAK_BITS` | 30 | 22–100 | BREAK duration in UART bit-times (≥ 22 for 88 µs at 250k) |
| `DMX_MAB_US` | 20 | 8–1000 | Mark-After-Break duration in µs (≥ 8 per spec) |
| `DMX_PERIOD_MS` | 25 | 10–1000 | Frame refresh interval (25 ms = 40 Hz) |
| `DMX_TXD_INVERT` | y | — | Invert TXD for MAX485-style auto-DE transceivers |
| `DMX_TX_THREAD_PRIO` | 7 | -1–15 | Cooperative TX thread priority (lower = higher priority) |
| `DMX_TX_THREAD_STACK_SIZE` | 1536 | — | TX thread stack size in bytes |
| `DMX_FIXTURE_START_ADDR` | 1 | 1–504 | First channel of a fixture (used by consumers) |

Enable/disable TX and RX independently so a pure transmitter or pure receiver
does not pull in the other half.

## Hardware requirements

- Any ESP32 (S2/S3/C3/etc.) target supported by Zephyr with an available UART.
- An RS485 transceiver on the DMX UART. For MAX485-style transceivers with
  auto-direction-enable, keep `CONFIG_DMX_TXD_INVERT=y` so the idle-high TXD
  holds the transceiver in receive mode.
- For a receiver, keep TX idle-high (or drive DE low) so the bus is not driven.

## Projects using dmx-z

- [`club-ctrl`](https://github.com/wkvg-org) — DMX TX master + MQTT (transmitter).
- [`dmx-fixture`](https://github.com/wkvg-org/dmx-fixture) — 8-relay actuator
  (receiver).

## License

SPDX-License-Identifier: Apache-2.0

## Copyright

Copyright (C) 2025 - 2026 Wolfenbütteler Kultur- und Veranstaltungsgesellschaft
gemeinnützige UG (haftungsbeschränkt)
