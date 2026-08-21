# SPDX-License-Identifier: Apache-2.0
#
# dmx-z — shared DMX512 driver build module

zephyr_include_directories(${CMAKE_CURRENT_SOURCE_DIR})

if(CONFIG_DMX_TX)
  target_sources(app PRIVATE dmx_z_tx.c)
endif()

if(CONFIG_DMX_RX)
  target_sources(app PRIVATE dmx_z_rx.c)
endif()

target_sources(app PRIVATE fixture.c)

target_compile_definitions(app PRIVATE
	CONFIG_DMX_UART_NUM=${CONFIG_DMX_UART_NUM}
	CONFIG_DMX_BAUD=${CONFIG_DMX_BAUD}
	CONFIG_DMX_BREAK_BITS=${CONFIG_DMX_BREAK_BITS}
	CONFIG_DMX_MAB_US=${CONFIG_DMX_MAB_US}
	CONFIG_DMX_PERIOD_MS=${CONFIG_DMX_PERIOD_MS}
	CONFIG_DMX_TXD_INVERT=$<BOOL:${CONFIG_DMX_TXD_INVERT}>
	CONFIG_DMX_TX_THREAD_PRIO=${CONFIG_DMX_TX_THREAD_PRIO}
	CONFIG_DMX_TX_THREAD_STACK_SIZE=${CONFIG_DMX_TX_THREAD_STACK_SIZE}
)
