/*
 * SPDX-License-Identifier: Apache-2.0
 *
 * dmx-z — shared DMX512 driver (TX + RX) and fixture abstraction.
 *
 * Transmitter: 40 Hz refresh on UART (250000 baud, 8N2) with
 * hardware BREAK generation via ESP32 UART low-level HAL.
 *
 * Receiver:  polls UART RX FIFO at 250000 baud, detects BREAK for
 * frame synchronisation, and maintains a complete 512-slot universe.
 *
 * Fixture:    generic N-channel DMX device with a configurable start
 * address. Processes received slots and calls a per-channel callback.
 */

#ifndef DMX_Z_H_
#define DMX_Z_H_

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define DMX_UNIVERSE_SIZE 512
#define DMX_BAUD         250000
#define DMX_MAB_US       20

/* ── Transmitter API ────────────────────────────────────────────────── */

int dmx_tx_init(void);
void dmx_tx_set_slot(uint16_t ch, uint8_t value);
void dmx_tx_set_slots(uint16_t start, const uint8_t *buf, size_t n);
void dmx_tx_blackout(void);
void dmx_tx_get_slots(uint8_t *buf, size_t n);
void dmx_tx_load_slots(const uint8_t *buf, size_t n);

/* ── Receiver API ───────────────────────────────────────────────────── */

int dmx_rx_init(void);

/** True when a complete DMX frame has been received since the last
 *  call — resets on read.  Use this to gate per-frame processing. */
bool dmx_rx_frame_ready(void);

uint8_t dmx_rx_get_slot(uint16_t ch);
void dmx_rx_get_slots(uint16_t start, uint8_t *buf, size_t n);

/* ── Fixture abstraction ────────────────────────────────────────────── */

typedef void (*dmx_fixture_channel_cb)(uint8_t channel, uint8_t value);

struct dmx_fixture_cfg {
	uint16_t start_addr;         /* 1..DMX_UNIVERSE_SIZE */
	uint8_t  num_channels;
	dmx_fixture_channel_cb on_channel;
};

void dmx_fixture_process(const struct dmx_fixture_cfg *cfg);

/* ── Runtime TX/RX mode switching ───────────────────────────────────── */
/*
 * When CONFIG_DMX_RUNTIME_MODE is enabled, the transmitter and receiver
 * threads are kept alive and gated on the selected mode, so the DMX
 * direction can be flipped at runtime (e.g. from a web UI) without
 * reinitialising the UART.
 */

enum dmx_mode {
	DMX_MODE_TX,
	DMX_MODE_RX,
};

/** Select the active DMX direction (TX or RX). */
int dmx_set_mode(enum dmx_mode mode);

/** Return the currently selected DMX direction. */
enum dmx_mode dmx_get_mode(void);

/** True while the transmitter is the active direction. */
bool dmx_is_tx_active(void);

/** True while the receiver is the active direction. */
bool dmx_is_rx_active(void);

#endif /* DMX_Z_H_ */
