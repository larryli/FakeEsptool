/*
 * fesptool_hal.h - Minimal stub for unit testing
 *
 * Provides FESP_HAL_LOGD and FESP_HAL_LOG_HAS_DEBUG macros
 * so modules that include fesptool_hal.h can compile in test context.
 */

#ifndef FESPTOOL_HAL_STUB_H
#define FESPTOOL_HAL_STUB_H

#define FESP_HAL_LOG_HAS_DEBUG 0
#define FESP_HAL_LOGD(TAG, ...) ((void)0)

#endif /* FESPTOOL_HAL_STUB_H */
