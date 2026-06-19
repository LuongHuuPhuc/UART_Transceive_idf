/**
 * @file uart_proc.h
 * @author Luong Huu Phuc
 * @date 2026/02/28
 * 
 * @note File chua cac task hay xu ly chinh lien quan den UART
 * den cac ngoai vi khac, khong thuc hien cau hinh dai dong o day
 */

#ifndef _UART_PROC_INCLUDE_H_
#define _UART_PROC_INCLUDE_H_

#pragma once 

#ifdef __cplusplus
extern "C" {
#endif // __cplusplus

#include "stdio.h"
#include "stdbool.h"
#include "stdbool.h"

#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"

#include "wireless_includes.h" /* include & macros cua BLE vao file chung */

#define BLE_TX_QUEUE_SIZE     (20)  // Kich thuoc hang doi packet cua BLE
#define UART_READ_TIMEOUT_MS  (10)  // Thoi gian wait toi da cua UART cho RX Ringbuffer
#define PING_PONG_BUF_SIZE    256
#define PING_PONG_BUF_NUM     2

extern TaskHandle_t uart_rx_handle;

#ifdef BLUETOOTH_CFG_USING
extern TaskHandle_t uart_tx_ble_handle;
extern QueueHandle_t ble_tx_queue_handle;

/**
 * Debug note: Voi struct cu truoc day, bien s_ping_pong_buffer dinh nghia dang struct chua mang tinh ben trong
 * Khi compiler toi uu hoa ma nguon, neu struct khong dat co __attribute__((packed)), no se tu dong chen
 * cac byte dem trong ngam vao struct de toi uu hoa bo nho 32-bit. Su sai lech kich thuoc khien lenh memcpy
 * boc thua byte, lan san truc tiep sang vung Stack nhay cam cua RTOS dang neo giu Core 0, kich no
 * co che bao ve bao ve Canary Watchpoint.
 */
extern uint8_t s_ping_pong_data[PING_PONG_BUF_NUM][PING_PONG_BUF_SIZE];
extern uint16_t s_ping_pong_len[PING_PONG_BUF_NUM];
extern uint8_t s_ping_pong_type[PING_PONG_BUF_NUM];
extern volatile uint8_t s_write_index;
extern volatile bool s_pkt_rdy;
#endif // BLUETOOTH_CFG_USING

/**
 * @note 
 * Trong ham uart_read_bytes de doc data tu UART, no hoat dong theo co che Timeout.
 * - Ngay khi Task thuc day, chip se nhin vao RingBuffer phan cung. Neu RingBuffer 
 * dang co it hon so bytes_to_read (gioi han so bytes ma ham co the doc) thi ham se doi.
 * No chi doi dung 1 khoang thoi gian la da set la UART_READ_TIMEOUT_MS. Neu het tgian do
 * ma STM32 van chua phun du so bytes, ham `uart_read_bytes` lap tuc "bo cuoc", bo ngay 
 * so luong bytes dang co hien tai ra nao vao uart_rx_buf va tra ve gia tri len. Day chinh la
 * ly do so luong bytes doc duoc bi troi sut ngau nhien theo thoi gian. 
 */
#ifdef BLUETOOTH_CFG_USING
/* Task 1: Xu ly data nhan duoc tu UART1 */
void uart_rx_task(const void *pvParameters);

/* Task 2: dua data nhan duoc tu UART vao BLE */
void uart_tx_ble_task(const void *pvParameters);

#else
/* Task nhan data thong qua UART va xu ly ngay tai cho */
void uart_receive_data_task(void const *pvParameters);
#endif // BLUETOOTH_CFG_USING

#ifdef __cplusplus
}
#endif // __cplusplus
#endif // _UART_PROC_INCLUDE_H_