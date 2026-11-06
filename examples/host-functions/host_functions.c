/*
 * Example Arm host implementations for Athrill.
 *
 * Copyright (c) 2026 Hakoniwa Lab, LLC
 * SPDX-License-Identifier: MIT
 */
#include "athrill_host_function.h"

#include <stdlib.h>
#include <string.h>

#if defined(_WIN32)
#define HOST_EXPORT __declspec(dllexport)
#else
#define HOST_EXPORT __attribute__((visibility("default")))
#endif

HOST_EXPORT std_bool athrill_host_memset(AthrillHostCall *call)
{
	uint64 destination = call->arg(call, 0U);
	uint32 value = (uint32)call->arg(call, 1U);
	uint32 size = (uint32)call->arg(call, 2U);
	void *buffer;
	Std_ReturnType err;

	if (size == 0U) {
		call->set_result(call, destination);
		return TRUE;
	}
	buffer = malloc(size);
	if (buffer == NULL) {
		return FALSE;
	}
	memset(buffer, (int)(uint8)value, size);
	err = call->write(call, destination, buffer, size);
	free(buffer);
	if (err != STD_E_OK) {
		return FALSE;
	}
	call->set_result(call, destination);
	return TRUE;
}

static std_bool copy_or_move(AthrillHostCall *call)
{
	uint64 destination = call->arg(call, 0U);
	uint64 source = call->arg(call, 1U);
	uint32 size = (uint32)call->arg(call, 2U);
	void *buffer;
	Std_ReturnType err;

	if (size == 0U) {
		call->set_result(call, destination);
		return TRUE;
	}
	buffer = malloc(size);
	if (buffer == NULL) {
		return FALSE;
	}
	err = call->read(call, source, buffer, size);
	if (err == STD_E_OK) {
		err = call->write(call, destination, buffer, size);
	}
	free(buffer);
	if (err != STD_E_OK) {
		return FALSE;
	}
	call->set_result(call, destination);
	return TRUE;
}

HOST_EXPORT std_bool athrill_host_memcpy(AthrillHostCall *call)
{
	return copy_or_move(call);
}

HOST_EXPORT std_bool athrill_host_memmove(AthrillHostCall *call)
{
	/* The complete guest source is captured before any destination write. */
	return copy_or_move(call);
}
