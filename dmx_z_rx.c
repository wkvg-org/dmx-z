/*
 * SPDX-License-Identifier: Apache-2.0
 *
 * DMX512 receiver — polls UART RX FIFO at 250000 baud, detects BREAK
 * for frame synchronisation, and maintains a 512-slot universe snapshot.
 *
 * Frame format (ANSI E1.11):
 *   BREAK (≥ 88 µs) → MAB (≥ 8 µs) → START CODE (0x00) → 512 slots
 */

#include "dmx_z.h"

#include <zephyr/kernel.h>
#include <zephyr/device.h>
#include <zephyr/drivers/uart.h>
#include <zephyr/logging/log.h>

#include <hal/uart_ll.h>

LOG_MODULE_REGISTER(dmx_rx, LOG_LEVEL_INF);

#define DMX_UART_NODE DT_ALIAS(dmx_uart)
BUILD_ASSERT(DT_NODE_HAS_STATUS_OKAY(DMX_UART_NODE),
	     "dmx-uart alias missing or disabled");

static const struct device *const dmx_uart = DEVICE_DT_GET(DMX_UART_NODE);

/* ── Frame state ────────────────────────────────────────────────────── */

enum {
	DMX_RX_IDLE,        /* waiting for BREAK */
	DMX_RX_START_CODE,  /* BREAK seen, expect 0x00 */
	DMX_RX_SLOTS,       /* reading 512 slots */
};

static uint8_t rx_universe[DMX_UNIVERSE_SIZE];
static uint8_t rx_snapshot[DMX_UNIVERSE_SIZE];
static volatile bool rx_frame_ready;
static struct k_mutex rx_snapshot_lock;

/* ── RX thread ──────────────────────────────────────────────────────── */

static void dmx_rx_thread_fn(void *a, void *b, void *c)
{
	ARG_UNUSED(a); ARG_UNUSED(b); ARG_UNUSED(c);
	uart_dev_t *hw = UART_LL_GET_HW(CONFIG_DMX_UART_NUM);
	uint8_t state = DMX_RX_IDLE;
	uint16_t slot_idx = 0;

	/*
	 * The ESP32-S3 UART does not set the brk_det interrupt for a
	 * DMX break (8N2); it raises the framing-error interrupt instead.
	 * The RS485 bus also produces occasional spurious framing errors
	 * mid-frame, so a framing error is only treated as a BREAK when it
	 * is seen outside of a frame (idle / waiting-for-start-code).
	 */

	while (true) {
		/* 1. Drain the RX FIFO — slot 512 always precedes the break
		 *    bytes, so the frame completes before the break is seen. */
		uint32_t avail = uart_ll_get_rxfifo_len(hw);

		while (avail > 0) {
			uint8_t byte;

			uart_ll_read_rxfifo(hw, &byte, 1);
			avail--;

			switch (state) {
			case DMX_RX_IDLE:
				break;

			case DMX_RX_START_CODE:
				state = DMX_RX_SLOTS;
				slot_idx = 0;
				break;

			case DMX_RX_SLOTS:
				if (slot_idx < DMX_UNIVERSE_SIZE) {
					rx_universe[slot_idx++] = byte;
				}
				if (slot_idx >= DMX_UNIVERSE_SIZE) {
					k_mutex_lock(&rx_snapshot_lock,
						     K_FOREVER);
					memcpy(rx_snapshot, rx_universe,
					       sizeof(rx_snapshot));
					rx_frame_ready = true;
					k_mutex_unlock(&rx_snapshot_lock);
					state = DMX_RX_IDLE;
				}
				break;
			}
		}

		/* 2. BREAK (framing error) resets the frame, but only when we
		 *    are not mid-frame — spurious framing errors during slot
		 *    transmission are ignored. */
		if (hw->int_raw.frm_err_int_raw) {
			hw->int_clr.frm_err_int_clr = 1;
			if (state != DMX_RX_SLOTS) {
				state = DMX_RX_START_CODE;
				slot_idx = 0;
			}
		}

		k_msleep(1);
	}
}

#define DMX_RX_THREAD_STACK_SIZE 4096
#define DMX_RX_THREAD_PRIO       6

K_THREAD_STACK_DEFINE(dmx_rx_stack, DMX_RX_THREAD_STACK_SIZE);
static struct k_thread dmx_rx_thread_data;

/* ── Public API ─────────────────────────────────────────────────────── */

int dmx_rx_init(void)
{
	static bool started;

	if (started) {
		return 0;
	}
	if (!device_is_ready(dmx_uart)) {
		return -ENODEV;
	}

	const struct uart_config cfg = {
		.baudrate  = DMX_BAUD,
		.parity    = UART_CFG_PARITY_NONE,
		.stop_bits = UART_CFG_STOP_BITS_2,
		.data_bits = UART_CFG_DATA_BITS_8,
		.flow_ctrl = UART_CFG_FLOW_CTRL_NONE,
	};
	int err = uart_configure(dmx_uart, &cfg);
	if (err) {
		return err;
	}

	k_mutex_init(&rx_snapshot_lock);

	k_thread_create(&dmx_rx_thread_data, dmx_rx_stack,
			K_THREAD_STACK_SIZEOF(dmx_rx_stack),
			dmx_rx_thread_fn, NULL, NULL, NULL,
			DMX_RX_THREAD_PRIO, 0, K_NO_WAIT);
	k_thread_name_set(&dmx_rx_thread_data, "dmx_rx");

	started = true;
	LOG_INF("dmx rx up: 250k 8N2");
	return 0;
}

bool dmx_rx_frame_ready(void)
{
	bool ready = rx_frame_ready;

	rx_frame_ready = false;
	return ready;
}

uint8_t dmx_rx_get_slot(uint16_t ch)
{
	if (ch < 1 || ch > DMX_UNIVERSE_SIZE) {
		return 0;
	}
	k_mutex_lock(&rx_snapshot_lock, K_FOREVER);
	uint8_t val = rx_snapshot[ch - 1];
	k_mutex_unlock(&rx_snapshot_lock);
	return val;
}

void dmx_rx_get_slots(uint16_t start, uint8_t *buf, size_t n)
{
	if (start < 1 || start > DMX_UNIVERSE_SIZE || buf == NULL || n == 0) {
		return;
	}
	size_t max = DMX_UNIVERSE_SIZE - (start - 1);
	if (n > max) {
		n = max;
	}
	k_mutex_lock(&rx_snapshot_lock, K_FOREVER);
	memcpy(buf, &rx_snapshot[start - 1], n);
	k_mutex_unlock(&rx_snapshot_lock);
}
