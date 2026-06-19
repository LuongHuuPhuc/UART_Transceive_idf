/**
 * @file sensor_pkt.c
 * @author LuongHuuPhuc
 */

#ifdef __cplusplus
extern "C" {
#endif // __cplusplus

#include "sensor_pkt.h"
#include "string.h"
#include "esp_log.h"

static const char *TAG = "SS_PKT_PARSER";

/* ====== Packet parser state machine ====== */
enum __parse_state_type {
  PARSE_WAIT_HEADER = 0,
  PARSE_READ_HEADER,
  PARSE_READ_PAYLOAD,
  PARSE_READ_FOOTER
};
typedef enum __parse_state_type ss_pkt_parse_state_t;

struct __packet_parser_type {
  uint8_t buf[PKT_SIZE_MAX + 8];  /* Buffer dem chua data doc duoc (+8 safety margin) */
  uint16_t pos;                   /* Bien luu vi tri doc (tang dan) */
  ss_pkt_parse_state_t state;
  uint16_t payload_size;
};
typedef struct __packet_parser_type ss_pkt_parser_t;
static ss_pkt_parser_t s_parser = {0};

/*-----------------------------------------------------------*/

/* Tinh toan payload size dua vao type */
static uint16_t get_payload_size(uint8_t type){
  switch(type){
    case PKT_TYPE_AUDIO: return (sizeof(ss_pkt_audio_t) - sizeof(bnr_pkt_header_t) - sizeof(bnr_pkt_footer_t));
    case PKT_TYPE_BIO: return (sizeof(ss_pkt_bio_t) - sizeof(bnr_pkt_header_t) - sizeof(bnr_pkt_footer_t));
    default: return 0;
  }
}

/*-----------------------------------------------------------*/

void ss_pkt_packet_parser_reset(void){
  s_parser.pos = 0;
  s_parser.state = PARSE_WAIT_HEADER;
}

/*-----------------------------------------------------------*/

void ss_pkt_packet_parser_feed(const uint8_t *in, int in_len, on_pkt_prcss_cb_t cb){
  for(int i = 0; i < in_len; i++){
    uint8_t byte = in[i]; // Luu tung byte dau vao tai 1 bien

    // Tuyet doi khong cho pos vuot qua mang buf cua Parser (201 + 8 bytes margin)
    if(s_parser.pos >= sizeof(s_parser.buf)) ss_pkt_packet_parser_reset();

    switch(s_parser.state){
      
      /* 1. Cho khi gap duoc header 0xAA */
      case PARSE_WAIT_HEADER:
        if(byte == PKT_HEADER){
          s_parser.pos = 0;
          s_parser.buf[s_parser.pos++] = byte;
          s_parser.state = PARSE_READ_HEADER;
        }
        break;
    
      /* 2. Doc cac thanh phan cua header 0xAA (version, type, payload_len) */
      case PARSE_READ_HEADER:
        s_parser.buf[s_parser.pos++] = byte;

        if(s_parser.pos == sizeof(bnr_pkt_header_t)){ // Neu pos doc du kich thuoc cua header (6 bytes)
          // Tao con tro hdr de duyet qua cac byte trong buf khi doc den header
          bnr_pkt_header_t *hdr = (bnr_pkt_header_t*)s_parser.buf;

          /* Verify version byte */
          if(hdr->version != PKT_VERSION){
            ESP_LOGW(TAG, "[SENSOR_PKT] Unknown version 0x%02X", hdr->version);
            s_parser.state = PARSE_WAIT_HEADER;
            break;
          }

          /* Verify payload size */
          s_parser.payload_size = get_payload_size(hdr->type);
          if(s_parser.payload_size == 0 || hdr->payload_len != s_parser.payload_size){
            ESP_LOGW(TAG, "[SENSOR_PKT] Unknown type:0x%02X, len:%u", hdr->type, hdr->payload_len);
            s_parser.state = PARSE_WAIT_HEADER;
            break;
          }
          // Hop le -> Chuyen qua doc Payload
          s_parser.state = PARSE_READ_PAYLOAD;
        }
        break;

      /* 3. Doc payload (chua data chinh cua packet) cua packet */
      case PARSE_READ_PAYLOAD:
        s_parser.buf[s_parser.pos++] = byte;
        if(s_parser.pos == (sizeof(bnr_pkt_header_t) + s_parser.payload_size)){ // Neu pos doc du kich thuoc cua header + payload
          s_parser.state = PARSE_READ_FOOTER;
        }
        break;

      /* 4. Doc footer 0x22 */
      case PARSE_READ_FOOTER:
        s_parser.buf[s_parser.pos++] = byte;
        if(s_parser.pos == (sizeof(bnr_pkt_header_t) + s_parser.payload_size + sizeof(bnr_pkt_footer_t))){
          // Tao con tro ftr de duyet qua cac byte trong buf khi doc den footer
          bnr_pkt_footer_t *ftr = (bnr_pkt_footer_t*)(s_parser.buf + sizeof(bnr_pkt_header_t) + s_parser.payload_size);

          /* Verify footer byte */
          if(ftr->footer != PKT_FOOTER){
            ESP_LOGW(TAG, "[SENSOR_PKT] Bad footer 0x%02X", ftr->footer);
            ss_pkt_packet_parser_reset();
            break;
          }

          /* Verify CRC16 (CRC duoc tinh tren header + payload) */
          uint16_t crc_calc = pkt_crc16(s_parser.buf, sizeof(bnr_pkt_header_t) + s_parser.payload_size);
          if(crc_calc != ftr->crc16){
            ESP_LOGW(TAG, "[SENSOR_PKT] CRC fail calc=0x%04X recv=0x%04X", crc_calc, ftr->crc16);
            s_parser.state = PARSE_WAIT_HEADER;
            break;
          }

          /* Packet hop le -> callback */
          bnr_pkt_header_t *hdr = (bnr_pkt_header_t*)s_parser.buf;
          if(cb != NULL) cb(s_parser.buf, s_parser.pos, hdr->type);

          /* Hoan thanh 1 chu ky parse -> Dua ve trang thai tim Header goi tiep theo */
          ss_pkt_packet_parser_reset();
        }
        break;

      default:
        ss_pkt_packet_parser_reset();
        break;
    }

    /* Buffer overflow guard */
    if(s_parser.pos >= sizeof(s_parser.buf)){
      ESP_LOGE(TAG, "[SENSOR_PKT] Parse overflow, reset");
      ss_pkt_packet_parser_reset();
    }
  }
}

#ifdef __cplusplus
}
#endif // __cplusplus