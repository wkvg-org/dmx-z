/*
 * SPDX-License-Identifier: Apache-2.0
 *
 * dmx-z — runtime TX/RX direction selection.
 *
 * When CONFIG_DMX_RUNTIME_MODE is enabled the transmitter and receiver
 * threads are both kept alive but gated on the mode selected here, so the
 * DMX direction can be flipped at runtime without reinitialising the UART.
 * When it is disabled both directions report "active" so the original
 * single-direction behaviour is preserved.
 */

#include "dmx_z.h"

#include <zephyr/kernel.h>
#include <zephyr/sys/atomic.h>

#if defined(CONFIG_DMX_RUNTIME_MODE)

static atomic_t dmx_mode = ATOMIC_INIT(DMX_MODE_TX);

int dmx_set_mode(enum dmx_mode mode)
{
	atomic_set(&dmx_mode, mode);
	return 0;
}

enum dmx_mode dmx_get_mode(void)
{
	return (enum dmx_mode)atomic_get(&dmx_mode);
}

bool dmx_is_tx_active(void)
{
	return dmx_get_mode() == DMX_MODE_TX;
}

bool dmx_is_rx_active(void)
{
	return dmx_get_mode() == DMX_MODE_RX;
}

#else /* !CONFIG_DMX_RUNTIME_MODE */

int dmx_set_mode(enum dmx_mode mode)
{
	ARG_UNUSED(mode);
	return 0;
}

enum dmx_mode dmx_get_mode(void)
{
	return DMX_MODE_TX;
}

bool dmx_is_tx_active(void)
{
	return true;
}

bool dmx_is_rx_active(void)
{
	return true;
}

#endif /* CONFIG_DMX_RUNTIME_MODE */
