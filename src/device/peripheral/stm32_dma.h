/*
 * The STM32F7 DMA controllers DMA1 and DMA2 (RM0410 chapter 8): the stream
 * registers and the interrupt status flags, and the transfers the firmware
 * hands them for the modelled peripherals: an SPI exchange (a TX stream
 * memory-to-SPI and an RX stream SPI-to-memory) runs at once, in full, when
 * its TX stream is enabled; both streams then report transfer complete.
 * Lightweight: streams of other peripherals (UART RX DMA ...) stay enabled
 * and never move data; no FIFO thresholds, bursts or double buffering.
 */
#ifndef ATHRILL_STM32_DMA_H
#define ATHRILL_STM32_DMA_H

#include "std_types.h"

std_bool stm32_dma_owns(uint32 addr);
uint32 stm32_dma_read(uint32 addr);
void stm32_dma_write(uint32 addr, uint32 size, uint32 value);
void stm32_dma_reset(void);

#endif
