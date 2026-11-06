#include "athrill_host_function.h"
#include "std_errno.h"

#if defined(_WIN32)
#define TEST_EXPORT __declspec(dllexport)
#else
#define TEST_EXPORT __attribute__((visibility("default")))
#endif

TEST_EXPORT std_bool test_sum(AthrillHostCall *call)
{
	uint64 result = call->arg(call, 0U) + call->arg(call, 4U);

	call->set_result(call, result);
	return TRUE;
}

TEST_EXPORT std_bool test_device_write(AthrillHostCall *call)
{
	uint32 value = 0x12345678U;

	if (call->write(call, 0x40000000U, &value, sizeof(value)) != STD_E_OK) {
		return FALSE;
	}
	call->set_result(call, value);
	return TRUE;
}
