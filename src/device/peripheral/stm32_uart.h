/*
 * The STM32F7 USART/UARTs USART1 to UART8 (RM0410 chapter 34) in
 * asynchronous mode: the register file, the transmitter (TDR and the shift
 * register) and the receiver (RDR) timed by the baud rate and frame format,
 * the status flags and the interrupt.
 *
 * Where the bytes go and come from:
 *   - the console UART (DEVICE_CONFIG_STM32_CONSOLE_UART, 1 to 8; USART3
 *     by default) transmits to standard output and receives from the
 *     debugger's serial input and from a file (DEVICE_CONFIG_STM32_UART_RX_FILE),
 *     one character per frame time while RDR is free and a reader listens
 *     (RXNEIE or DMAR set), so the same file arrives at the same virtual
 *     times on every run;
 *   - a UART with a chip on it (stm32_uart_attach: a GPS receiver, a TCP
 *     client of stm32_uart_tcp.c ...)
 *     hands the chip what it sends and receives what the chip sends back
 *     (stm32_uart_chip_send), timed as the console's input;
 *   - the other UARTs are unconnected lines: what they send is dropped and
 *     nothing arrives.
 * With DMAR set, a received character goes to the DMA stream that reads
 * RDR (stm32_dma_peripheral_rx) when one is enabled.
 * Lightweight on purpose: no overrun (input waits for RDR), no parity or
 * framing errors, no hardware flow control, LIN, smartcard, IrDA or
 * transmit DMA (their registers are kept).
 */
#ifndef ATHRILL_STM32_UART_H
#define ATHRILL_STM32_UART_H

#include "std_types.h"
#include "cpu.h"
#include "std_device_ops.h"

/* Whether addr is in a UART's register block. */
std_bool stm32_uart_owns(uint32 addr);
uint32 stm32_uart_read(uint32 addr);
void stm32_uart_write(uint32 addr, uint32 size, uint32 value);
/* console: the UART number (1 to 8) of the console; rx_file: its input file or NULL. */
void stm32_uart_reset(uint32 console, const char *rx_file);
/* The UARTs up to the device clock, input, interrupts, the clocks to the next event. */
void stm32_uart_tick(DeviceClockType *clock);
/* A character for the console UART (the debugger's serial input); STD_E_LIMIT when the queue is full. */
Std_ReturnType stm32_uart_console_input(uint8 data);

/* A chip on a UART line. */
typedef struct {
	/* a character the UART sent leaves its shift register */
	void (*transmit)(void *chip, uint8 data);
} Stm32UartChipOps;

/* Puts chip on UARTnumber (1 to 8; after the UARTs' reset). */
void stm32_uart_attach(uint32 number, const Stm32UartChipOps *ops, void *chip);
/* A character from the chip on UARTnumber; STD_E_LIMIT when the UART's queue is full. */
Std_ReturnType stm32_uart_chip_send(uint32 number, uint8 data);

#endif
