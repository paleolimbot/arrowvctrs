
#pragma once

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#include "abi.h"

int arrow_array_copy_ptype(struct ArrowArray* out, struct ArrowArray* array);

#ifdef __cplusplus
}
#endif