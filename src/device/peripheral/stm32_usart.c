/*
 * STM32F7 USART, transmit only so far: a byte written to TDR goes to the
 * console at once, so ISR always reports TXE and TC (STM32F7 reference
 * manual, USART register map: ISR at 0x1C, TDR at 0x28).
 */
#include "stm32_usart.h"
#include "concrete_executor/target/dbg_target_serial.h"

#define USART_ISR 0x1CU
#define USART_TDR 0x28U
#define USART_ISR_TC (1U << 6)
#define USART_ISR_TXE (1U << 7)

uint8 stm32_usart3_data[STM32_USART_SIZE];

static uint32 read_register(uint32 offset)
{
	if (offset == USART_ISR) {
		return USART_ISR_TXE | USART_ISR_TC;
	}
	return 0U;
}

static void write_register(uint32 offset, uint32 value)
{
	if (offset == USART_TDR) {
		(void)dbg_serial_putchar(0, (uint8)value);
	}
}

static Std_ReturnType get8(MpuAddressRegionType *r, CoreIdType c, AthrillAddrType a, uint8 *d)
{
	uint32 offset = (uint32)(a - r->start);

	(void)c;
	*d = (uint8)(read_register(offset & ~3U) >> (8U * (offset & 3U)));
	return STD_E_OK;
}

static Std_ReturnType get16(MpuAddressRegionType *r, CoreIdType c, AthrillAddrType a, uint16 *d)
{
	uint32 offset = (uint32)(a - r->start);

	(void)c;
	*d = (uint16)(read_register(offset & ~3U) >> (8U * (offset & 2U)));
	return STD_E_OK;
}

static Std_ReturnType get32(MpuAddressRegionType *r, CoreIdType c, AthrillAddrType a, uint32 *d)
{
	(void)c;
	*d = read_register((uint32)(a - r->start));
	return STD_E_OK;
}

static Std_ReturnType put8(MpuAddressRegionType *r, CoreIdType c, AthrillAddrType a, uint8 d)
{
	(void)c;
	write_register((uint32)(a - r->start), d);
	return STD_E_OK;
}

static Std_ReturnType put16(MpuAddressRegionType *r, CoreIdType c, AthrillAddrType a, uint16 d)
{
	(void)c;
	write_register((uint32)(a - r->start), d);
	return STD_E_OK;
}

static Std_ReturnType put32(MpuAddressRegionType *r, CoreIdType c, AthrillAddrType a, uint32 d)
{
	(void)c;
	write_register((uint32)(a - r->start), d);
	return STD_E_OK;
}

static Std_ReturnType get_pointer(MpuAddressRegionType *r, CoreIdType c, AthrillAddrType a, uint8 **d)
{
	(void)c;
	*d = &r->data[a - r->start];
	return STD_E_OK;
}

MpuAddressRegionOperationType stm32_usart_ops = { get8, get16, get32, put8, put16, put32, get_pointer };
