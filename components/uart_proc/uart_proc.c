/**
 * @file uart_proc.c 
 * @author Luong Huu Phuc
 * @date 2026/02/28
 */
#ifdef __cplusplus
extern "C" {
#endif // __cplusplus

#include "uart_proc.h"  // File header nay da nhung include thu vien cong khai cua btcfg & FreeRTOS

#include "stdio.h"
#include "string.h"

#include "driver/uart.h"
#include "driver/gpio.h"
#include "esp_err.h"
#include "esp_log.h"

#include "uart_cfg.h"   // queue & UART config
#include "sensor_pkt.h" // Packet parser

static const char *NAMES = "DATA_UART_PROC";
static uint8_t uart_rx_buf[BUF_SIZE];  // Buffer luu tru data nhan duoc qua uart

TaskHandle_t uart_rx_handle = NULL;

#ifdef BLUETOOTH_CFG_USING
TaskHandle_t uart_tx_ble_handle = NULL;
QueueHandle_t ble_tx_queue_handle = NULL; // Hang doi trung gian de truyen du lieu giua 2 Core

/**
 * -> Tao mang tinh phang 2 chieu nam trong phan vung RAM global public (BSS/Data Segment)
 * -> Cap phat phang 2 o nho tinh cong khai trong RAM, moi o nho chua 256 bytes
 * -> Tach biet voi phan vung Stack, 2 bien nay nam o 3 phan vung RAM khac nhau, khong 
 * xay ra viec 1 struct bi tranh chap o nho (do cac bien trong 1 struct se nam canh nhau).
 * Khi Core 0 dang goi memcpy de nhet data vao, Core 1 lai nhay vao doc truong type, len cua cung 1 struct
 */
__attribute__((aligned(4))) uint8_t s_ping_pong_data[PING_PONG_BUF_NUM][PING_PONG_BUF_SIZE];
__attribute__((aligned(4))) uint16_t s_ping_pong_len[PING_PONG_BUF_NUM];
__attribute__((aligned(4))) uint8_t s_ping_pong_type[PING_PONG_BUF_NUM];

// Flag so le chi dinh vi tri ghi/doc
volatile uint8_t s_write_index = 0;
#endif // BLUETOOTH_CFG_USING

#ifdef VERIFY_PACKET_MODE
/**
 * Chi so Counter chi la tuong doi. Packet gui di tu STM32 luon la 32
 * (Thuc te trong 1s, STM32 phat ra co 31.25 blocks). Khi sang phia ESP32,
 * dem bang cua so kiem tra 1s. Khi goi tin thu 32 tu STM32 gui di (tai 1024ms),
 * dieu nay vuot qua moc cua so 1000ms cua giay truoc. Goi nay bi day sang giay thu 2.
 * Dan den ESP32 thi thoang lai chi co 31 (mac du nhan khong thieu bytes nao). 
 * -> Sampling Window Jitter (Truot cua so pha dem).
 */
static uint32_t s_audio_block = 0;  // Bien dem so packet audio nhan duoc
static uint32_t s_bio_block = 0;    // Bien dem so packet bio nhan duoc
static uint8_t s_last_audio_seq = 0xFF;
static uint8_t s_last_bio_seq = 0xFF;

#ifdef NEED_TO_DEBUG
static uint32_t s_loss_block = 0;   // Bien dem so packet bi mat trong packet
static uint32_t s_loss_total = 0;   // Bien luu tong loss tich luy
#endif // NEED_TO_DEBUG

/*-----------------------------------------------------------*/

/**
 * @brief Ham callback xu ly khi bo Parser giai ma thanh cong 1 goi tin nhi phan thi verify packet do
 * @note Ham nay thuc thi truc tiep ngay tai context cua Task nhan du lieu UART
 */
static void packet_verify(const uint8_t *buf, uint16_t buf_len, uint8_t type){
  bnr_pkt_header_t *hdr = (bnr_pkt_header_t*)buf;

  if(type == PKT_TYPE_AUDIO){
    s_audio_block++;

#ifdef NEED_TO_DEBUG // Chi dung neu Debug de giam bot tai tinh toan cho CPU
    if(s_last_audio_seq != 0xFF){
      uint8_t expected = s_last_audio_seq + 1;

      if(hdr->seq != expected){
        uint8_t lost = (uint8_t)(hdr->seq - expected);
        s_loss_block = s_loss_total += lost;
        ESP_LOGW(NAMES, "AUDIO LOSS: exp=%u got=%u lost=%u", expected, hdr->seq, lost);
      }
    }
#endif // NEED_TO_DEBUG
    s_last_audio_seq = hdr->seq;
  }

  else if(type == PKT_TYPE_BIO){
    s_bio_block++;

#ifdef NEED_TO_DEBUG // Chi dung neu Debug de giam bot tai tinh toan cho CPU
    if(s_last_bio_seq != 0xFF){
      uint8_t expected = s_last_bio_seq + 1;
      
      if(hdr->seq != expected){
        uint8_t lost = (uint8_t)(hdr->seq - expected);
        s_loss_block = s_loss_total += lost;
        ESP_LOGW(NAMES, "BIO LOSS: exp=%u got=%u lost=%u", expected, hdr->seq, lost);
      }
    }
#endif // NEED_TO_DEBUG
    s_last_bio_seq = hdr->seq;
  }

#ifdef BLUETOOTH_CFG_USING

  uint8_t current_write = s_write_index;
  if(buf_len < PING_PONG_BUF_SIZE){
    /* 1. Ghi data vao o RAM tinh phang co dinh toan cuc */
    s_ping_pong_len[current_write] = buf_len;
    s_ping_pong_type[current_write] = type;

    // Copy truc tiep goi tin nhi phan vao phan vung RAM tinh toan cuc
    memcpy(s_ping_pong_data[current_write], buf, buf_len);

    /* 2. Lay dia chi cua vung nho vat ly cua dong mang vua ghi xong */
    uint8_t *ptr_data_clean = s_ping_pong_data[current_write];

    // Doi sang bo dem doi ung de cho chu ky sau 
    s_write_index = (current_write == 0) ? 1 : 0;

    // Truyen con tro ptr_data_clean (4 bytes) vao Queue
    xQueueSend(ble_tx_queue_handle, &ptr_data_clean, pdMS_TO_TICKS(10));
  }
#endif // BLUETOOTH_CFG_USING
}
#endif // VERIFY_PACKET_MODE

/*-----------------------------------------------------------*/

#ifdef BLUETOOTH_CFG_USING
void uart_tx_ble_task(const void *pvParameters){
  (void)pvParameters;
  uint8_t *ptr_rx_data = NULL; // Bien con tro dung de hung dia chi RAM rut ra tu Queue

  while(1){
    if(xQueueReceive(ble_tx_queue_handle, &ptr_rx_data, portMAX_DELAY)){

      /* Dua vao dia chi con tro p_rx_data rut ra, ta truy nguoc lai chi so dong (0 hoac 1) cua mang tinh */
      uint8_t read_idx = (ptr_rx_data == s_ping_pong_data[0]) ? 0 : 1;

      /* Xac dinh vi tri do dem  */
      uint8_t current_type = s_ping_pong_type[read_idx];

      /* Xu ly bat dong bo: Bat lai advertising an toan tai Core 1 (khong de tai GAP event nua) */
      if(current_type == DISCONNECT_TO_CENTRAL_FLAG){
        BTCFG_LOGI("[BLE_TASK] Restarting advertising safely from Core 1 Task...");
        s_ping_pong_type[read_idx] = 0;

        /* Viec gui lenh gia vao queue tx cua ble khong anh huong den viec gui data, data chi duoc gui neu con connected */
        /* De an toan thi xoa sach cac goi tin du lieu cu sinh ra luc disconnect de giai phong RAM cho Queue */
        xQueueReset(ble_tx_queue_handle);

        btcfg_peripheral_start_advertising();
        continue;
      }

      // Neu co thiet bi ngoai ket noi den va da kich hoat flag nhan Notify chu dong (CCCD)
      if(btcfg_peripheral_is_connected() && g_btcfg_peripheral_ctx.srv_notify_enabled){
        
        // Truyen truc tiep dia chi con tro vua rut duoc vao dac tinh BLE
        if(btcfg_peripheral_srv_notify_characteristic( g_btcfg_peripheral_ctx.tx_attr_handle, ptr_rx_data, s_ping_pong_len[read_idx]) != BTCFG_OK){
          ESP_LOGW(NAMES, "[BLE_TASK] Notify Characteristic failed !");
        }
      }
      else s_ping_pong_type[read_idx] = 0;
    } 
        
    // Ep Core 1 nhuong RAM cho Core 0 don dep hang doi ngat va chay luong IDLE0 
    // Voi cau hinh config_RATE_TICK = 1000 -> lenh nay giai phong luong trong dung 1ms
    // hoan toan khong anh huong den chu ky 32ms cua du lieu.
    vTaskDelay(pdMS_TO_TICKS(2));
  }
}

/*-----------------------------------------------------------*/

void uart_rx_task(const void *pvParameters){
  (void)pvParameters;

  /* UART init tai task luon de UART ISR tu dong ghim tai Core no khoi tao */
  if(uart_cfg_init() != FN_OK){
    ESP_LOGE(NAMES, "uart_cfg_init on Core 1 failed !");
    vTaskDelete(NULL);
  }
  ESP_LOGI(NAMES, "Hardware UART1 ISR allocated on Core 1 !");

#ifdef NEED_TO_DEBUG
  static TickType_t last_tick = 0;
#endif // NEED_TO_DEBUG

  uart_event_t event; // Dung Event queue thay vi polling lien tuc

  while(1){
    /* Chan luong an toan doi su kien nap byte tu UART driver phan cung vao Queue */
    if(xQueueReceive(uart_rx_queue_handle, &event, portMAX_DELAY)){
      switch(event.type){

        case UART_DATA:{
          size_t buffered_size = 0;
          uart_get_buffered_data_len(MY_UART_PORT, &buffered_size);

          if(buffered_size > 0){
            // Gioi han so luong byte doc toi da bang kich thuoc that cua mang tinh uart_rx_buf
            int bytes_to_read = (buffered_size > (BUF_SIZE - 1)) ? (BUF_SIZE - 1) : buffered_size;

            /* Doc data tu UART1 RingBuffer RX roi ghi vao buffer RAM uart_rx_buf */
            int len = uart_read_bytes(MY_UART_PORT, uart_rx_buf, bytes_to_read, pdMS_TO_TICKS(UART_READ_TIMEOUT_MS));
            if(len > 0){

              // Nap du lieu tho nay vao Parser - ham nay se tu dong kich hoat callback `packet_verify` de phat BLE Notification
              ss_pkt_packet_parser_feed(uart_rx_buf, len, packet_verify);

              // ESP_LOGI(NAMES, "[UART_TASK] Packet parsed");

#ifdef NEED_TO_DEBUG // Chi dung neu Debug de giam bot tai tinh toan cho CPU
              TickType_t now = xTaskGetTickCount();
              if((now - last_tick) >= pdMS_TO_TICKS(1000)){
                /* Ky vong 1s: 32 block cho moi Audio va Bio, Loss = 0, seq moi lan cach nhau 32 */
                ESP_LOGI(NAMES, "Streaming to BLE -> Audio_block=%lu/s Bio_block=%lu/s Loss_block=%lu | Total_loss=%lu", 
                        s_audio_block, s_bio_block, s_loss_block, s_loss_total);
                s_audio_block = s_bio_block = s_loss_block = 0;
                last_tick = now;
              }
#endif // NEED_TO_DEBUG
            }
          }
          break;
        }
          
        case UART_FIFO_OVF:{
          ESP_LOGW(NAMES, "UART FIFO overflow detected in BLE Task ! Flushing input buffer...");
          uart_flush_input(MY_UART_PORT); // Clear input buffer, xoa tat ca data trong ring buffer
          xQueueReset(uart_rx_queue_handle);
          break;
        }

        case UART_BUFFER_FULL:{
          ESP_LOGW(NAMES, "UART Buffer overflow detected in BLE Task ! Flushing input buffer...");
          uart_flush_input(MY_UART_PORT);
          xQueueReset(uart_rx_queue_handle);
          break;
        }

        case UART_FRAME_ERR:{
          ESP_LOGW(NAMES, "UART Frame Error detected in BLE Task.");
          break;
        }

        default: break;
      }
    }
  }
}

#else /* Neu khong gui data len BLE ma chi parse va check packt ngay tai UART */
void uart_receive_data_task(void const *pvParameters){
  (void)pvParameters;

#if defined(VERIFY_ASCII_MODE) || defined(VERIFY_PACKET_MODE)
  static TickType_t last_tick = 0;
#endif // VERIDY_MODE

  uart_event_t event; // Dung Event queue thay vi polling lien tuc

  while(1){
    /* Truyen dia chi RAM cua event vao, data sau khi nhan duoc tu queue cua UART driver se do thang vao day */
    if(xQueueReceive(uart_rx_queue_handle, &event, portMAX_DELAY)){
      switch(event.type){

        case UART_DATA:{
          /**
           * Thao tac doc du lieu an toan tu RX RingBuffer vao mang tinh toan cuc
           * Su dung event.size de chi doc dung luong byte ma driver thong bao
           * ngan chan viec ham bi block vo ly hoac qua tai dung luong Stack
           */
          size_t buffered_size = 0;
          uart_get_buffered_data_len(MY_UART_PORT, &buffered_size);

          if(buffered_size > 0){
            
            // Gioi han so luong byte doc toi da bang kich thuoc that cua mang tinh
            int bytes_to_read = (buffered_size > (BUF_SIZE - 1)) ? (BUF_SIZE - 1) : buffered_size;

            /* Doc data tu UART1 roi ghi vao buffer RAM uart_rx_buf */
            int len = uart_read_bytes(MY_UART_PORT, uart_rx_buf, bytes_to_read, pdMS_TO_TICKS(UART_READ_TIMEOUT_MS));
            if(len > 0){

/* In ra su dung ham custom */
#ifdef UART_ESP_PRINTF_USING
              uart_esp_printf("%s", (char*)uart_rx_buf);

/* Thay vi in ra data ASCII ra thi chi can dem so block nhan duoc 1s (Ky vong: 32 blocks/s (1 block = 32 samples)) */
#elif defined(VERIFY_ASCII_MODE)
              uart_rx_buf[len] = '\0';

              static uint32_t samples_count = 0;
              static uint32_t total_bytes = 0;

#ifdef NEED_TO_DEBUG
              static TickType_t debug_tick = 0;
#endif // NEED_TO_DEBUG

              total_bytes += len;

              // Trong 1s -> 32 blocks (1024 samples/s) can phai parse theo kieu doc '\n' de tach block
              for(int i = 0; i < len; i++){
                if(uart_rx_buf[i] == '\n'){
                  samples_count++;
                }
              }
              
              TickType_t now = xTaskGetTickCount();
              if((now - last_tick) > pdMS_TO_TICKS(1000)){
                uint32_t block_count = samples_count / 31;
                ESP_LOGI(NAMES, "Total samples =%lu | Total blocks=%lu | Bytes=%lu\r\n", samples_count, block_count, total_bytes);
                total_bytes = 0;
                samples_count = 0;
                last_tick = now;
              }

              /* Bytes: 13Kb ~ 14Kbytes trong khi UART0 mac dinh baudrate 115200 -> Toi da 11520 bytes/s -> Crash */
              /* Samples/s: 954 ~ 1022 samples (Chuan tan so lay mau 1000Hz) */

#ifdef NEED_TO_DEBUG
              if((now - debug_tick) > pdMS_TO_TICKS(5000)){
                ESP_LOGD(NAMES, "Stack remain: %u\r\n", uxTaskGetStackHighWaterMark(uart_task_handle));
                debug_tick = now;
              }
#endif // NEED_TO_DEBUG 

/* Thay vi in ASCII thi gui binary va kiem tra packet */
#elif defined(VERIFY_PACKET_MODE)

              ss_pkt_packet_parser_feed(uart_rx_buf, len, packet_verify);

              TickType_t now = xTaskGetTickCount();
              if((now - last_tick) >= pdMS_TO_TICKS(1000)){
                /* Ky vong 1s: 32 block cho moi Audio va Bio, Loss = 0, seq moi lan cach nhau 32 */
                ESP_LOGI(NAMES, "Audio_block=%lu/s Bio_block=%lu/s Loss_block=%lu | Total_loss=%lu | seq A=%u B=%u", 
                        s_audio_block, s_bio_block, s_loss_block, 
                        s_loss_total, s_last_audio_seq, s_last_bio_seq);
                s_audio_block = s_bio_block = s_loss_block = 0;
                last_tick = now;
              }

/* In data ra UART0 duoi dang ASCII bang cac ham cua thu vien */
#else
              /* Test1: Ton stack (1100 ~ 1500 bytes) */
              // printf("%s", (char*)uart_rx_buf);

              /* Test2: 50 bytes, ghi thang ra UART0 nhung khong format data (raw bytes) -> Khong duoc do can init UART0 nhung UART0 do VFS quan ly */
              // uart_write_bytes(MONITOR_PORT, uart_rx_buf, len);

              /* Test3: ~200 bytes stack, raw write qua VFS, khong format */
              fwrite(uart_rx_buf, 1, len, stdout); // Ghi vao newlib stdout buffer
              fflush(stdout); // Day data tu newlib buffer xuong VFS Layer, khong dung buffer thi comment lai
#endif // UART_ESP_PRINTF_USING
              // taskYIELD();
            }
          }
          break;
        }

        case UART_FIFO_OVF:{
          ESP_LOGW(NAMES, "UART FIFO overflow - flush");
          uart_flush_input(MY_UART_PORT); // Clear input buffer, xoa tat ca data trong ring buffer
          xQueueReset(uart_rx_queue_handle);
          break;
        }

        case UART_BUFFER_FULL:{
          ESP_LOGW(NAMES, "UART Ring buffer full - flush");
          uart_flush_input(MY_UART_PORT);
          xQueueReset(uart_rx_queue_handle);
          break;
        }

        case UART_FRAME_ERR:{
          ESP_LOGW(NAMES, "UART Frame error");
          break;
        }

        default: break;
      }
    }
  }
}
#endif // BLUETOOTH_CFG_USING

#ifdef __cplusplus
}
#endif // __cplusplus