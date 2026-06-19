/**
 * @file main.c 
 * @author Luong Huu Phuc
 * @date 2026/02/28
 */
#ifdef __cplusplus
extern "C" {
#endif // __cplusplus

#include "stdio.h"

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/idf_additions.h" // Goi API xTaskCreatePinnedToCore() cua ESP-IDF (FreeRTOS goc khong co)
#include "esp_err.h"
#include "esp_log.h"

#include "uart_cfg.h"
#include "uart_proc.h"  // File header nay da nhung include thu vien cong khai cua btcfg

static const char *MAIN = "MAIN_APP";

#ifdef BLUETOOTH_CFG_USING
static const char *PEIRPHERAL_DEVNAME = "ESP32_PERIPHERAL_PHUC";

/* ============================ BLE PERIPHERAL APPLICATION CALLBACK ============================ */

/* Callback application layer xu ly su kien ket noi thanh cong cua GAP */
static void on_peripheral_connected(uint16_t conn_handle){
  ESP_LOGI(MAIN, "[APP Peripheral] Connected! Con_handle=%u", conn_handle);
}

/*-----------------------------------------------------------*/

/* Callback application layer xu ly su kien mat ket noi cua GAP */
static void on_peripheral_disconnected(void){
  ESP_LOGW(MAIN, "[APP Peripheral] Disconnected...");

  /* Dung double-buffer: gui lenh gia sang Core 1 xu ly bat dong bo */
  uint8_t current_write = s_write_index;
  s_ping_pong_len[current_write] = 0;
  s_ping_pong_type[current_write] = DISCONNECT_TO_CENTRAL_FLAG;

  // Doi sang bo dem con lai 
  s_write_index = (current_write == 0) ? 1 : 0;

  // Lay dia chi cua mang chua co 0xFF de day vao Queue
  uint8_t *ptr_discn = s_ping_pong_data[current_write]; 
  xQueueSend(ble_tx_queue_handle, &ptr_discn, pdMS_TO_TICKS(10));
}

/* -------------------------- GATT SERVER Callback (Peripheral lam Server) -------------------------- */

/* Callback application layer GATT Server xu ly Read-Request tu GATT Client gui den */
static void on_peripheral_srv_read(uint16_t attr_handle, uint16_t conn_handle){
  ESP_LOGI(MAIN, "[APP Peripheral - Server] Client requested READ to on Attribute Handle: 0x%04X (Conn_handle:%u)", attr_handle, conn_handle);
}

/*-----------------------------------------------------------*/

/* Callback application layer GATT Server xu ly Write-Request tu GATT Client gui den */
static void on_peripheral_srv_write(uint16_t attr_handle, const uint8_t *data, uint16_t length){
  ESP_LOGI(MAIN, "[APP Peripheral - Server] Client requested WRITE with %u bytes to on Attribute Handle: 0x%04X", length, attr_handle);
  // Thuc hien phan tich du lieu nhan duoc tai day voi buffer data ...
}

/* -------------------------- GATT CLIENT Callback (Peripheral lam Client) -------------------------- */

/* Callback application layer GATT Client xu ly Notify duoc ban ra tu GATT Server */
static void on_peripheral_clt_notify_event(uint16_t conn_handle, uint16_t attr_handle, const uint8_t *data, uint16_t len){
  ESP_LOGI(MAIN, "[APP Peripheral - Client] GATT Notification from remote Server. Attribute Handle: 0x%04X, len: %u", attr_handle, len);
  // Thuc hien giai ma dong byte cua thiet bi khac gui ve day ...
}

/*-----------------------------------------------------------*/

/* Callback application layer GATT Client xu ly phan hoi lai lenh Read-Request tu GATT Server */
static void on_peripheral_clt_read_resp(uint16_t conn_handle, uint16_t err_code, uint16_t attr_handle, const uint8_t *data, uint16_t len){
  if(err_code == 0){
    ESP_LOGI(MAIN, "[APP Peripheral - Client] Read response OK from remote Server. Attribute Handle: 0x%04X, len: %u", attr_handle, len);
  }
  else{
    ESP_LOGE(MAIN, "[APP Peripheral - Client] Read response error ! Error code: %u", err_code);
  }
}
#endif // BLUETOOTH_CFG_USING

/*-----------------------------------------------------------*/

void app_main(void){

#ifdef BLUETOOTH_CFG_USING
  /* Dang ky cac ham su kien ket noi GAP Peripheral len Driver BLE */
  btcfg_peripheral_register_connect_callback(on_peripheral_connected);
  btcfg_peripheral_register_disconnect_callback(on_peripheral_disconnected);

  /* Dang ky cac ham xu ly du lieu GATT Server */
  btcfg_peripheral_srv_register_read_callback(on_peripheral_srv_read);
  btcfg_peripheral_srv_register_write_callback(on_peripheral_srv_write);

  /* Dang ky cac ham don nhan du lieu tuong tac GATT Client tu xa */
  btcfg_peripheral_clt_register_notify_event_callback(on_peripheral_clt_notify_event);
  btcfg_peripheral_clt_register_read_response_callback(on_peripheral_clt_read_resp);

  /* BLE init */
  if(btcfg_peripheral_init(PEIRPHERAL_DEVNAME) != BTCFG_OK){
    ESP_LOGE(MAIN, "[APP] BLE Stack for Peripheral init failed !");
    ERROR_HANDLER();
  }

  /* Khoi tao Queue trung gian ket noi 2 loi */
  ble_tx_queue_handle = xQueueCreate(BLE_TX_QUEUE_SIZE, sizeof(uint8_t*)); // sizeof(uint8_t*) kich thuoc con tro den 1 bytes - chinh la dia chi vat ly (4 bytes)
  if(ble_tx_queue_handle == NULL){
    ESP_LOGE(MAIN, "[APP] Create BLE TX Queue failed !");
    ERROR_HANDLER();
  }
  
  /* Task 1: Ghim Core 1 - Dam nhan toan bo ganh nang dong goi va day lenh vo tuyen BLE */
  xTaskCreatePinnedToCore((TaskFunction_t)uart_tx_ble_task, "uart_tx_ble_task", 1024 * 4, NULL, 3, &uart_tx_ble_handle, 1);

  /**
   * Task 2: Ghim Core 0 - Cung nhan voi ISR UART phan cung. Khong sinh ra Cross-core Interrupt
   * Tuy nhien tren Core 0 cung dang chay NimBLE Host Stack. Task nay se chiem dung Core 0 de xu
   * ly su kien mang. UART xu ly nhieu data, nang va lien tuc thi nen thu de sang Core 1 cung voi ISR Handler.
   */
  xTaskCreatePinnedToCore((TaskFunction_t)uart_rx_task, "uart_rx_task", 1024 * 8, NULL, 4, &uart_rx_handle, 1);

#else
  /**
   * Ghim chat task vao Core 0 giup triet tieu Race Condition
   * Neu dung xTaskCreate, mac dinh FreeRTOS se tu dong phan phoi Task chay tren bat ky core nao dang ranh (chuyen giua Core 1 va Core 0)
   * Driver UART phan cung ESP32-S3 mac dinh dang ky ngat tai Core 0. Khi task chay tren core 1 hoac bi chuyen qua lai
   * ham xQueueReceive() se kich hoac 1 lenh ngat cheo nhan (Cross-core Interrupt). 
   * Khi luong UART chay voi toc do cao (460800 baud), su tranh chap tai nguyen (Critical Section) giua xQueueReceive()
   * va Vector ISR UART lam hong quan ly vung nho cua Core 0 -> Log bao loi "Stack canary watchpoint triggered (IDLE0)"
   */
  // xTaskCreate((void*)uart_receive_data_task, "uart_rx_task", 1024 * 4, NULL, 3, &uart_task_handle);
  xTaskCreatePinnedToCore((void*)uart_receive_data_task, "uart_receive_data_task", 1024 * 4, NULL, 3, &uart_rx_handle, 0);
#endif // BLUETOOTH_CFG_USING
}

#ifdef __cplusplus
}
#endif // __cplusplus

/**
 * TODO: Giai doan 1 (test):
 * Hien tai, UART xuat ra tu STM32 dang theo format cua CSV
 * -> Gui theo Block (32 samples) nhu the den ESP32 roi Notify sang App
 * - App Android luc nay: 
 *  - Nhan chuoi, tach dong theo `\n`, tach cot theo `,`
 *  - Parse thanh so roi day vao buffer de ve graph
 * -> Nhung lau dai thi khong nen lam nhu the vi 
 *  - Ton bang thong hon Binary, Parse string nang hon, kho kiem soat frame hon
 * 
 * Giai doan 2: 
 * - Khi moi thu o format CSV da on roi thi moi chuyen qua Binary
 * 
 * ### DEBUG LOG 1 ###
 * --- 0x40382112: xPortSetInterruptMaskFromISR at...
 * --- (inlined by) xPortEnterCriticalTimeout at...
 * --- 0x403834db: vPortEnterCritical at...
 * --- (inlined by) prvTaskEnterCriticalSafeSMPOnly...
 * --- (inlined by) vTaskSwitchContext...
 * --- 0x40382420: _frxt_dispatch at...
 * --- 0x40382416: _frxt_int_exit at...
 * 
 * - Thi thoang dang chay thi bi loi `Guru Meditation Error (IDLE0)` khien he thong bi reset.
 * Ly do: Khi ghim task UART o core 0, dung khoanh khac ket noi voi Serial Bluetooth Terminal, 
 * co Subscribe bat len. He thong vua phai ganh Vector ngat phan cung cua UART do bo byte, vua 
 * phai ganh ngat dong goi RF vat ly cua BLE stack, lai vua phai thuc thi lenh khoa Mutex de 
 * bao ve mang nhi phan.
 *  
 * -> Su chong cheo ngat nay (Interrupt Nested) o tan suat cuc cao tai Core 0 da lam ham
 * xPortSetInterruptMaskFromISR bi khoa cung qua thoi han cho phep cua Watchdog, truc tiep 
 * pha huy stack cua Task, làm sập Task IDLE của RTOS (IDLE0). Task IDLE không được chạy trong
 * 1 khoảng thời gian nhất định (thường là vài giây), Task Watchdog Timer sẽ báo lỗi.
 * 
 * -> Kha nang cung co the xay ra la neu de log level cua NimBLE cao qua (Info tro len) thi 
 * moi lan co su kien xay ra, task NimBLE Host lai in log muc thao ra lien tuc toc do cao co 
 * the gay ra qua tai chuong trinh -> Reset
 * 
 * -> Xu ly: Chuyen task sang Core 1 (thu nghiem)
 * - Toan bo ap xu ly vong lap Parser du lieu nhi phan chuyen sang Core 1 (App), 
 * Core 0 xu ly Vector Ngat phan cung va BLE Stack (NimBLE mac dinh Host Task Stack = 4096)
 *
 * ### DEBUG LOG 2 ###
 * - Thu doi sang Core 1 chay APP va giam log level cua NimBLE Host xuong thi van bi Reset chuong trinh.
 *
 * Guru Meditation Error: Core  0 panic'ed (Unhandled debug exception).
 * Debug exception reason: Stack canary watchpoint triggered (IDLE0)
 * 
 * -> Ly do: Ham khong chet o Log, khong chet vi BLE nang ma chet o `vTaskSwitchContext()`
 * Ly do doi sang Core 1 roi van loi IDLE Task tai Core0 la vi co che Cross-core Interrupt
 * (Ngat cheo nhan) cua FreeRTOS + Baudrate cao.
 * 
 * 1. Tai Core 0: Driver UART phan cung cua ESP32-S3 mac dinh dang ky Vector Ngat (ISR) vat ly
 * tai Core 0. Moi khi dong bytes chay tu STM32, ISR trigger tren Core 0, boc du lieu vao uart_queue_handle
 * 
 * 2. Tai Core 1: Task goi ham `xQueueReceive(uart_queue_handle,...)`. Vi queue la tai nguyen dung chung 
 * bao ve Critical Section da loi, Core 1 muon lay du lieu ra can phai phat 1 lenh ngat cheo nhan (Cross-core Interrupt)
 * go cua sang Core 0.
 * 
 * 3. Baudrate 460800: Tan suat du lieu nhoi nhet lien tuc, don dap. Core 1 lien tuc trigger Cross-Core Interrupt,
 * ep Core 0 vao Critical Section (co loi `vPortEnterCritical`) de phuc vu lenh xQueueReceive
 * 
 * 4. BLE kich hoat: Khi goi tin nhi phan xuat hien tai Core 1, goi lenh phat BLE. Ma luong NimBLE Host Stack
 * cua ESP-IDF mac dinh chay tren Core 0. Core 1 tiep tuc doi nguoc mot cu Ngat cheo nhan thu 2 sang Core 0
 * de giuc RF lam viec 
 * 
 * -> Core 0 bi ket cung trong Critical Section (vTaskSwitchContext) do Cross-core Interrupt don dap
 * tu Core 1 gui sang. Nhieu den muc ngay ca Task IDLE0 khong chay duoc -> WatchDog phat hien RAM cua IDLE0
 * bi bo doi qua thoi han -> Reset chip
 * 
 * -> Xu ly: Tach ra lam 2 task rieng biet & su dung Queue de day data vao BLE truoc khi ban ra
 * -> Task nhan va parse data UART tai Core 0 (chung voi ISR UART phan cung de loai bo hien tuong ngat cheo nhan)
 * -> Task BLE tai Core 1 ganh toan bo viec dequeue va gui len BLE
 * 
 * ### DEBUG LOG 3
 * - Nhan ra chua gui data ra, moi chi thuc hien ket noi/ngat ket noi, bat tat notify don gian thoi thi sau 1 luc
 * chuong trinh bi reset.
 * -> Ham `btcfg_peripheral_gap_event` thuc chat dang chay truc tiep duoi 1 High-Priority ISR cua mach Radio BLE 
 * tren Core 0. Khi co su kien BLE_GAP_EVENT_DISCONNECT, Mach Radio phat ngat -> CPU Core 0 nhay vao ham GAP Event
 * Trong case nay, thuc hien lenh start_advertising(). No goi 1 API he thong nang la ble_gap_adv_start(). Ham nay 
 * ngam thuc hien viec xin cap bo nho dem, cau hinh thanh ghi RF va goi 1 lenh hoan doi ngu canh Task (Yield/Context Switch)
 * de giuc NimBLE Host Engine chay nen cap nhat trang thai.
 * -> Do CPU dang nam trong ngu canh ngat (Interrupt Context) cua chinh NimBLE, viec ep RTOS, chuyen doi context 
 * ngay trong ngat cua no tao ra 1 vong lap de quy vo han (Deadlock). CPU Core 0 bi khoa chat cung, Task IDLE0 bi bo doi,
 * Watchdog reset chip
 * 
 * -> Xu ly: Chuyen qua lenh Advertising ra Application Layer. Xu ly bat dong bo: Tai ngat ta chi bao hieu, viec bat quang ba
 * lai se do 1 Task ung dung thuc thi. Trong ham callback xu ly su kien disconnect cung duoc goi truc tiep tu ngat nen 
 * ta khong goi advertising o day. Tan dung hang doi 
 * 
 * # DEBUG LOG 4
 * -> Chuyen qua dung mang tinh phang 2 chieu
 */