/*
 * The STM32F7 DMA controllers DMA1 and DMA2 (RM0410 chapter 8): the stream
 * registers and the interrupt status flags, and the transfers the firmware
 * hands them for the modelled peripherals: an SPI exchange (a TX stream
 * memory-to-SPI and an RX stream SPI-to-memory) runs at once, in full, when
 * its TX stream is enabled; both streams then report transfer complete.
 * A UART's received characters go one by one to the stream that reads its
 * RDR (stm32_dma_peripheral_rx), with half transfer and transfer complete
 * as the stream counts down and the circular mode NuttX's serial driver uses.
 * Lightweight: streams of other peripherals stay enabled and never move
 * data; no FIFO thresholds, bursts or double buffering.
 */
#ifndef ATHRILL_STM32_DMA_H
#define ATHRILL_STM32_DMA_H

#include "std_types.h"

std_bool stm32_dma_owns(uint32 addr);
uint32 stm32_dma_read(uint32 addr);
void stm32_dma_write(uint32 addr, uint32 size, uint32 value);
void stm32_dma_reset(void);

/* A received datum of the peripheral register par to the stream that reads it; FALSE: none enabled. */
std_bool stm32_dma_peripheral_rx(uint32 par, uint32 data);

#endif
