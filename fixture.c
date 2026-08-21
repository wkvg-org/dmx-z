/*
 * SPDX-License-Identifier: Apache-2.0
 *
 * Generic DMX fixture — processes received DMX slots for a contiguous
 * range of channels and dispatches per-channel callbacks.
 */

#include "dmx_z.h"

#include <zephyr/kernel.h>

void dmx_fixture_process(const struct dmx_fixture_cfg *cfg)
{
	if (cfg == NULL || cfg->on_channel == NULL ||
	    cfg->start_addr < 1 || cfg->num_channels == 0) {
		return;
	}

	if (cfg->start_addr + cfg->num_channels - 1 > DMX_UNIVERSE_SIZE) {
		return;
	}

	for (uint8_t i = 0; i < cfg->num_channels; i++) {
		cfg->on_channel(i, dmx_rx_get_slot(cfg->start_addr + i));
	}
}
