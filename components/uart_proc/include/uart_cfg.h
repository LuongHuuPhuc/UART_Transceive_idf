/**
 * @file uart_cfg.h
 * @author Luong Huu Phuc
 * @date 2026/02/28
 * 
 * @note File chi chua cac ham cau hinh lien quan den UART
 * Khong trien khai task o day
 */

#ifndef _UART_CFG_INCLUDE_H_
#define _UART_CFG_INCLUDE_H_

#pragma once 

#ifdef __cplusplus
extern "C" {
#endif // __cplusplus

#include "stdio.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/queue.h"

#include "func_types.h"
#include "cm_macros.h"

/**
 * @note
 * ESP-IDF khong expose ro rang kieu API "UART_DMA" nhu STM32 HAL, nhung thuc chat duoi driver
 * cua ESP32 van dung: 
 *  - Hardware FIFO UART (~128 bytes)
 *  - ISR (Trigger khi FIFO day hoac timeout)
 *  - Ring Buffer trong RAM (do nguoi dung cap phat)
 *  - Copy bang interrupt backend
 * Nen hieu nang van rat cao. CPU khong polling tung byte, khong busy wait, ISR chi 
 * copy FIFO -> Ring buffer -> Task doc sau 
 */

#define MY_UART_PORT          UART_NUM_1 // Dung UART1, khong dung UART0 (vi cai nay la Default cho Type C USB)
#define MONITOR_PORT          UART_NUM_0 // Dung de log data len monitor (default UART)
#define UART_TX_PIN           GPIO_NUM_4 // GP4 -> A3 (Khong dung vi Simplex)
#define UART_RX_PIN           GPIO_NUM_5 // GP5 -> A2 (ESP32 RX nhan tu TX STM32)
#define UART_BAUD_RATE        460800

#define MIN(a, b)             ((a) < (b) ? (a) : (b))
#define BUF_SIZE              512
#define UART_RX_RING_BUF_SIZE (BUF_SIZE * 16) // 8192 bytes ring buffer
#define UART_EVENT_QUEUE_SIZE (50)  // Chua duoc burst event

extern QueueHandle_t uart_rx_queue_handle;

/* ============================ FUNCTION PROTOTYPE ============================ */

/* Khoi tao UART Port */
fn_status_t uart_cfg_init(void);

#ifdef UART_ESP_PRINTF_USING
/**
 * Dinh nghia ham thread-safe printf qua uart + Mutex bao ve 
 * @note
 * Tu ESP-IDF ver5.x tro di, ham printf() da duoc _lock_acquire truoc khi
 * write vao UART0 nen khong can dung mutex boc ngoai. Nen co the dung 
 * printf() luon thay cho ham nay cung duoc  
 */
__attribute__((unused)) 
void uart_esp_printf(const char *fmt,...);
#endif // UART_ESP_PRINTF_USING

#ifdef __cplusplus
}
#endif // __cplusplus
#endif // _UART_CFG_INCLUDE_H_