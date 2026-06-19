/**
 * @file cm_macros.h
 * Khai bao function, macro,...chung cho chuong trinh
 */
#ifndef __CM_MACROS_H
#define __CM_MACROS_H

#define UART_DRIVER_INSTALL_WITH_QUEUE    1   /* Sau khi install UART voi queue thi khong can xQueueCreate */
#define NEED_TO_DEBUG                     1   /* Dung khi can debug chung */
// #define UART_ESP_PRINTF_USING             1   /* Dung neu can su dung ham printf thu cong */
// #define VERIFY_ASCII_MODE                 1   /* Debug data UART o dang ASCII */
#define VERIFY_PACKET_MODE                1   /* Debug data UART o dang Binary packet */

#endif // __CM_MACROS_H