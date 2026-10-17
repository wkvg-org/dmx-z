/*
 * SPDX-License-Identifier: Apache-2.0
 *
 * DMX512 transmitter — extracted from club-ctrl drivers/dmx/dmx.c.
 *
 * 512-slot universe transmitted at CONFIG_DMX_PERIOD_MS Hz over UART
 * (250000 baud, 8N2, RS485 half-duplex) with hardware BREAK generation.
 */

#include "dmx_z.h"

#include <zephyr/kernel.h>
#include <zephyr/device.h>
#include <zephyr/drivers/uart.h>
#include <zephyr/logging/log.h>
#include <zephyr/sys/printk.h>

#include <hal/uart_ll.h>

LOG_MODULE_REGISTER(dmx_tx, LOG_LEVEL_INF);

#define DMX_UART_NODE DT_ALIAS(dmx_uart)
BUILD_ASSERT(DT_NODE_HAS_STATUS_OKAY(DMX_UART_NODE),
	     "dmx-uart alias missing or disabled");

static const struct device *const dmx_uart = DEVICE_DT_GET(DMX_UART_NODE);
static uint8_t universe[DMX_UNIVERSE_SIZE];
K_MUTEX_DEFINE(universe_lock);

static void dmx_tx_frame(void)
{
	uint8_t snapshot[DMX_UNIVERSE_SIZE];
	uart_dev_t *hw = UART_LL_GET_HW(CONFIG_DMX_UART_NUM);

	k_mutex_lock(&universe_lock, K_FOREVER);
	memcpy(snapshot, universe, sizeof(snapshot));
	k_mutex_unlock(&universe_lock);

	while (!uart_ll_is_tx_idle(hw)) {
		k_yield();
	}
	uart_ll_tx_break(hw, 0);
	k_busy_wait(DMX_MAB_US);

	uart_poll_out(dmx_uart, 0x00);
	for (size_t i = 0; i < DMX_UNIVERSE_SIZE; i++) {
		uart_poll_out(dmx_uart, snapshot[i]);
	}
	uart_ll_tx_break(hw, CONFIG_DMX_BREAK_BITS);
}

static void dmx_tx_thread_fn(void *a, void *b, void *c)
{
	ARG_UNUSED(a); ARG_UNUSED(b); ARG_UNUSED(c);
	while (true) {
		if (dmx_is_tx_active()) {
			dmx_tx_frame();
			k_msleep(CONFIG_DMX_PERIOD_MS);
		} else {
			k_msleep(10);
		}
	}
}

#define DMX_TX_STACK_SIZE CONFIG_DMX_TX_THREAD_STACK_SIZE
#define DMX_TX_PRIO       K_PRIO_COOP(CONFIG_DMX_TX_THREAD_PRIO)
K_THREAD_STACK_DEFINE(dmx_tx_stack, DMX_TX_STACK_SIZE);
static struct k_thread dmx_tx_thread_data;

int dmx_tx_init(void)
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

	if (IS_ENABLED(CONFIG_DMX_TXD_INVERT)) {
		uart_dev_t *hw = UART_LL_GET_HW(CONFIG_DMX_UART_NUM);
		uart_ll_inverse_signal(hw, UART_SIGNAL_TXD_INV);
	}

	k_thread_create(&dmx_tx_thread_data, dmx_tx_stack,
			K_THREAD_STACK_SIZEOF(dmx_tx_stack),
			dmx_tx_thread_fn, NULL, NULL, NULL,
			DMX_TX_PRIO, 0, K_NO_WAIT);
	k_thread_name_set(&dmx_tx_thread_data, "dmx_tx");

	started = true;
	LOG_INF("dmx tx up: 250k 8N2, break %u bits, %d Hz",
		CONFIG_DMX_BREAK_BITS, 1000 / CONFIG_DMX_PERIOD_MS);
	return 0;
}

void dmx_tx_set_slot(uint16_t ch, uint8_t value)
{
	if (ch < 1 || ch > DMX_UNIVERSE_SIZE) {
		return;
	}
	k_mutex_lock(&universe_lock, K_FOREVER);
	universe[ch - 1] = value;
	k_mutex_unlock(&universe_lock);

	printk("[DMX TX] ch %u = %u\n", ch, value);
}

void dmx_tx_set_slots(uint16_t start, const uint8_t *buf, size_t n)
{
	if (start < 1 || start > DMX_UNIVERSE_SIZE || buf == NULL || n == 0) {
		return;
	}
	size_t max = DMX_UNIVERSE_SIZE - (start - 1);
	if (n > max) {
		n = max;
	}
	k_mutex_lock(&universe_lock, K_FOREVER);
	memcpy(&universe[start - 1], buf, n);
	k_mutex_unlock(&universe_lock);

	for (size_t i = 0; i < n; i++) {
		printk("[DMX TX] ch %u = %u\n", (unsigned int)(start + i), buf[i]);
	}
}

void dmx_tx_blackout(void)
{
	k_mutex_lock(&universe_lock, K_FOREVER);
	memset(universe, 0, sizeof(universe));
	k_mutex_unlock(&universe_lock);
}

void dmx_tx_get_slots(uint8_t *buf, size_t n)
{
	if (!buf) {
		return;
	}
	if (n > sizeof(universe)) {
		n = sizeof(universe);
	}
	k_mutex_lock(&universe_lock, K_FOREVER);
	memcpy(buf, universe, n);
	k_mutex_unlock(&universe_lock);
}

void dmx_tx_load_slots(const uint8_t *buf, size_t n)
{
	if (!buf) {
		return;
	}
	if (n > sizeof(universe)) {
		n = sizeof(universe);
	}
	k_mutex_lock(&universe_lock, K_FOREVER);
	memcpy(universe, buf, n);
	if (n < sizeof(universe)) {
		memset(universe + n, 0, sizeof(universe) - n);
	}
	k_mutex_unlock(&universe_lock);
}
