/**
 * @file function_err_types.h
 * Chua cac macros va define de xu ly error (neu co)
 */

#ifndef _FUNC_TYPES_H
#define _FUNC_TYPES_H

#pragma once 

#ifdef __cplusplus
extern "C" {
#endif // __cplusplus

#include <stdio.h>
#include <stdint.h>

typedef int32_t fn_status_t;  /* Function error type */

/* General function error constants */
#define FN_OK                    0    /* !< Ham tra ve ket qua thanh cong */
#define FN_ERR_FAIL             -1    /* !< Ham tra ve ket qua loi */
#define FN_ERR_INVALID_ARG      -2  
#define FN_ERR_TIMEOUT          -3

#if defined(NEED_TO_DEBUG)
#define FN_ERROR_CHECK(x) do { \
  fn_status_t __err = (x); \
  if(_err != FN_OK){ \
    printf("[FN ERR] Error %d at %s: %d\r\n", __err, __FILE__, __LINE__); \
    while(1){} \
  } \
} while(0)
#endif // NEED_TO_DEBUG 
 
#define ERROR_HANDLER() do{ \
  printf("[CRITICAL] Halting CPU...\n"); \
  while(1){ \
    /* ... */ \
    vTaskDelay(pdMS_TO_TICKS(1000)); \
  } \
} while(0)

#ifdef __cplusplus
}
#endif // __cplusplus
#endif // _FUNC_TYPES_H