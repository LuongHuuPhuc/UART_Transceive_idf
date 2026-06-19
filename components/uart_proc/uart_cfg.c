/**
 * @file uart_cfg.c 
 * @author Luong Huu Phuc
 * @date 2026/02/28
 */
#ifdef __cplusplus
extern "C" {
#endif // __cplusplus

#include "uart_cfg.h"  // Co include FreeRTOS

#include "stdio.h"
#include "stdint.h"
#include "stdarg.h"
#include "string.h"

#include "driver/uart.h"
#include "driver/gpio.h"
#include "esp_err.h"
#include "esp_log.h"

#include "soc/uart_periph.h"
#include "driver/uart_vfs.h" // VFS de anh xa stdout -> UART0 driver

/**
 * @note
 * Voi in data qua UART don thuan:
 * STM32 gui lien tuc 460800 baud (46080 bytes/s)
 * -> Ring Buffer UART1 (8192 bytes)
 * -> fwrite() -> newlib stdout buffer (data bi ghi o buffer nay den khi fflush hoac buffer day) -> Gay delay tren 
 * -> VFS Layer (routing den UART)
 * -> UART0 TX FIFO (128 bytes hardware)
 * -> Monitor hien thi
 * 
 * @warning
 * Nen tach UART thanh 2 kenh rieng (1 kenh de nhan, 1 kenh de dua len monitor)
 * - UART1: Lam port nhan data tu STM32
 * - UART0 (default): Lam port console (in log, nap code, debug)
 * Vi ly do xung dot luong du lieu, qua tai bo dem...
 */

static const char *NAMES = "DATA_UART_CONFIG";
QueueHandle_t uart_rx_queue_handle = NULL;

#ifdef UART_ESP_PRINTF_USING
static SemaphoreHandle_t uart_mutex_handle = NULL;
static uint8_t uart_print_buf[BUF_SIZE];
#endif // UART_ESP_PRINTF_USING

/* ============================ FUNCTION PROTOTYPE DEFINITION ============================ */

/* RTOS like Mutex, Semaphore, Queue,... khoi tao tai ham nay */
static inline fn_status_t uart_cfg_rtos(void){
#ifdef UART_ESP_PRINTF_USING
  /* Mutex cho uart_esp_printf() */
  uart_mutex_handle = xSemaphoreCreateMutex();
  if(uart_mutex_handle == NULL){
    ESP_LOGE(NAMES, "[UART config] Failed to create uart_mutex_handle");
    return FN_ERR_FAIL;
  }
#endif // UART_ESP_PRINTF_USING 

#if !defined(UART_DRIVER_INSTALL_WITH_QUEUE)
  /* Queue cho uart (neu dung rx ring buffer thi khong can khoi tao) */
  uart_rx_queue_handle = xQueueCreate(UART_EVENT_QUEUE_SIZE, sizeof(uart_event_t));
  if(uart_rx_queue_handle == NULL){
    ESP_LOGE(NAMES, "[UART config] Failed to create uart_rx_queue_handle");
    return FN_ERR_FAIL;
  }
#endif // UART_DRIVER_INSTALL_WITH_QUEUE
  return FN_OK;
}

/*-----------------------------------------------------------*/

static void get_uart0_pin(int32_t *tx_pin, int32_t *rx_pin){
  *tx_pin = uart_periph_signal[UART_NUM_0].pins[SOC_UART_TX_PIN_IDX].default_gpio;
  *rx_pin = uart_periph_signal[UART_NUM_0].pins[SOC_UART_RX_PIN_IDX].default_gpio;
}

/*-----------------------------------------------------------*/

#ifdef UART_ESP_PRINTF_USING
__attribute__((unused)) 
void uart_esp_printf(const char *fmt,...){
  va_list args;
  va_start(args, fmt);
  int len = vsnprintf((char*)uart_print_buf, sizeof(uart_print_buf), fmt, args);
  va_end(args);

  if(len <= 0) return;

  // Gioi han len ve dung so byte thuc co trong buffer
  if(len >= (int)sizeof(uart_print_buf)) len = (int)sizeof(uart_print_buf) - 1;
  
  /* Mutex guard handle */
  if((uart_mutex_handle != NULL) && (xTaskGetSchedulerState() == taskSCHEDULER_RUNNING)){
    if(xSemaphoreTake(uart_mutex_handle, pdMS_TO_TICKS(50)) == pdTRUE){
      fwrite(uart_print_buf, 1, len, stdout); // Khong can parse lai format printf (in thang ra UART0)
      xSemaphoreGive(uart_mutex_handle);
    }
  }
  else{
    fwrite(uart_print_buf, 1, len, stdout);
  }
}
#endif // UART_ESP_PRINTF_USING

/*-----------------------------------------------------------*/

fn_status_t uart_cfg_init(void){
  /* 1. Cau hinh thong so UART */

  // UART1 -> Receive data
  uart_config_t uart1_cfg = {
    .baud_rate = UART_BAUD_RATE,
    .parity = UART_PARITY_DISABLE,
    .data_bits = UART_DATA_8_BITS,
    .stop_bits = UART_STOP_BITS_1,
    .flow_ctrl = UART_HW_FLOWCTRL_DISABLE,
    .source_clk = UART_SCLK_DEFAULT
  };

  // UART0 -> Log
  /**
   * @note
   * Mac dinh UART0 khong co TX ring buffer (tx_buffer_size = 0 trong VFS init), 
   * nen moi lan write phai cho FIFO trong moi return -> Them buffer TX de giam ganh nang
   */
  // if(!uart_is_driver_installed(MONITOR_PORT)){
  //   uart_driver_install(MONITOR_PORT, 256, 4096, 0, NULL, 0); // Can install truoc khi thay doi tham so
  // }
  // uart_set_baudrate(MONITOR_PORT, 460800); // Thay doi baudrate cua UART0 (co the thay doi tai menuconfig)

   /**
   * DEBUG: (28/05/2026) 
   * Sau khi thu thay doi toc do baud cua UART0 thanh gia tri custom thi he thong lien tuc bi 
   * bao loi StackOverflow vi ESP-IDF 5.x khi boot da install UART0 tu rat som de dung cho cac
   * tac vu he thong khac (vd: bootloader, driver, newlib, VFS stdout/stderr, monitor,...) voi 
   * baudrate mac dinh 115200. Nhung thay doi baudrate cua UART0 dot ngot trong code (khong phai menuconfig)
   * thi khi do VFS van nghi UART0 la 115200 -> newlib buffer van fflush theo timing cua 115200
   * -> ISR UART0 dang chay voi timing cu, nhan duoc baudrate moi -> ra race condition giua ISR UART0 va VFS 
   * -> corrupt memory ngau nhien -> IDLE bi stack ghi de 
   */

  /**
   * @note
   * stdout cua newlib mac dinh duoc buffer hoa (line/full buffering)
   * Data printf/fwrite co the chua duoc gui ngay xuong UART0, cho den khi:
   * - Co '\n'
   * - fflush(stdout)
   * - buffer day
   * 
   * Pipeline:
   * printf/fwrite
   *  -> newlib stdout FILE buffer
   *  -> VFS Layer
   *  -> UART driver TX ring buffer
   *  -> UART hardware FIFO
   * 
   * - setvbuf() de tat FILE buffer cua stdout giup giam latency khi monitor realtime
   * - uart_vfs_dev_use_driver() cho phep VFS su dung UART driver (ring buffer + ISR)
   * thay vi polling hardware truc tiep (Tu co che busy-wait co ban -> xu ly interrupt va blocking)
   * (Dung cho cac lenh doc `fscanf()` hoac `fgets()`)
   */
  // uart_vfs_dev_use_driver(MONITOR_PORT); // Dung thay cho API cu esp_vfs_dev_uart_use_driver()
  // setvbuf(stdout, NULL, _IONBF, 0); // Tat buffer cua newlib stdout
  
  ESP_ERROR_CHECK(uart_param_config(MY_UART_PORT, &uart1_cfg));

  /* Gan chan UART1 ra GPIO */
  ESP_ERROR_CHECK(uart_set_pin(MY_UART_PORT, UART_TX_PIN, UART_RX_PIN, UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE));

  /**
   * RX timeout cua UART cho 1 byte/symbol truyen xong = 25 -> ISR timeout sau 25-char-time khong co data moi (max = 126)
   * Voi baudrate = 460800 -> 1 char ~ 21.7us -> timeout = 21.7us x 25 = 0.54ms
   */
  ESP_ERROR_CHECK(uart_set_rx_timeout(MY_UART_PORT, 25));

#ifdef UART_DRIVER_INSTALL_WITH_QUEUE
  /**
   * Install driver + set config UART1
   * - rx_buffer_size = UART_RX_RING_BUF_SIZE
   * - tx_buffer_size = 0 (khong can TX buffer vi chi RX)
   * - queue_size = UART_EVENT_QUEUE_SIZE = 20 (hang doi cua queue de dua data vao)
   * - queue_handle = &uart_rx_queue_handle (nhan event tu driver)
   * - intr_alloc_flags = Flag dung de ghim Vector Interrupt tai Core0/Core1
   * Mac dinh UART ISR Handler se duoc chay o cung Core ma ham nay duoc goi
   */
  ESP_ERROR_CHECK(uart_driver_install(MY_UART_PORT, UART_RX_RING_BUF_SIZE, 0, UART_EVENT_QUEUE_SIZE, &uart_rx_queue_handle, 1));
#else
  ESP_ERROR_CHECK(uart_driver_install(MY_UART_PORT, UART_RX_RING_BUF_SIZE, 0, 0, NULL, 0));
#endif // UART_DRIVER_INSTALL_WITH_QUEUE

  /* Tang FIFO threshold de giam so lan ISR trigger, mac dinh ISR sau moi 10 bytes, tang len 120 bytes -> giam 10x ISR */
  ESP_ERROR_CHECK(uart_set_rx_full_threshold(MY_UART_PORT, 120));
  
  /* UART1: Port nhan data tu STM32 */
  uint32_t actual_baud = 0;
  uint32_t out_uart_clk_hz = 0; 
  uart_get_baudrate(MY_UART_PORT, &actual_baud);
  uart_get_sclk_freq(uart1_cfg.source_clk, &out_uart_clk_hz);
  ESP_LOGI(NAMES, "[UART config] Received data on UART1: RX=GPIO%d, TX=GPIO%d | Baudrate set= %d (Actual Baudrate= %lu) | ClockFreq= %lu Hz | RingBuf= %d bytes", 
          UART_RX_PIN, 
          UART_TX_PIN,
          UART_BAUD_RATE,
          actual_baud,
          out_uart_clk_hz,
          UART_RX_RING_BUF_SIZE);

  /* UART0: Port truyen data ra monitor */
  int32_t uart0_tx_pin = 0;
  int32_t uart0_rx_pin = 0;
  uart_get_baudrate(MONITOR_PORT, &actual_baud);
  get_uart0_pin(&uart0_tx_pin, &uart0_rx_pin);
  ESP_LOGI(NAMES, "[UART config] Display data on UART0 (Baudrate: %lu): RX=GPIO%ld, TX=GPIO%ld", actual_baud, uart0_rx_pin, uart0_tx_pin);

#if defined(UART_ESP_PRINTF_USING) || !defined(UART_DRIVER_INSTALL_WITH_QUEUE)
  /* 4. Khoi tao luon RTOS cho UART (neu co) */
  if(uart_cfg_rtos() != FN_OK){
    ESP_LOGE(NAMES, "[UART config] uart_cfg_rtos failed !");
    return FN_ERR_FAIL;
  } 
#endif // UART_ESP_PRINTF_USING

  ESP_LOGI(NAMES, "[UART config] uart_cfg_init OK !");
  return FN_OK;
}

/*-----------------------------------------------------------*/

#ifdef __cplusplus
}
#endif // __cplusplus