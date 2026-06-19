/**
 * @file sensor_pkt.h
 * @author LuongHuuPhuc
 * File nay chua cau hinh & function can thiet cho Binary Packet
 */

#ifndef __INCLUDE_SENSOR_PKT_H
#define __INCLUDE_SENSOR_PKT_H

#pragma once 

#ifdef __cplusplus
extern "C" {
#endif // __cplusplus

#include "stdint.h"
#include "stdbool.h"

/**
 * @note
 * Thay vi gui data dang ASCII di qua UART, STM32 se dong goi thanh chuoi BINARY
 * de tang toc do va giam kich thuoc du lieu -> Gui qua UART toi ESP32
 * Tai ESP32 se checksum, check CRC va gui thang BLE (Khong parse tai day).
 * Packet se gui den App va parse sau do ve waveform.
 *
 * @details
 * Tinh toan size 1 packet (1 block)
 * - INMP441 (PCG): 32 samples x 4 bytes (int32, 24-bit data) = 128 bytes
 * - MAX30102 (PPG): 32 samples x 4 bytes (uint32 IR) = 128 bytes
 * - AD8232 (ECG): 32 samples x 2 bytes (int16) = 64 bytes
 * -> Payload tong: 320 bytes
 * -----
 * - Header: 6 bytes
 * - CRC16: 2 bytes
 * - Footer: 1 byte
 * -> Tong 1 packet = 329 bytes
 * Nhung BLE co gioi han kich thuoc toi da cua 1 packet: 
 * BLE 5.0 cua ESP32S3 co MTU (Maximum Transmission Unit): 247 bytes
 * BLE 4.0 - 4.2: MTU mac dinh la 23 bytes (3 bytes header + 20 payload)
 * -> Data vuot qua gioi han cua MTU phai chia thanh nhieu goi nho, gay fragment
 * -----
 * Tinh toan ban dau: Voi Fs = 1000Hz (1ms/sample) voi 1 block = 32 samples (32ms) 
 * -> 1s = ~31.25 blocks (~1024 samples)
 * -> 32 packets/s (1 packet = 329 bytes) -> 10.5KB/s -> Baudrate 460800 OK ! (~22.7% bandwidth UART)
 * Tuy nhien bandwidth thuc te cua BLE 5.x co the len toi 1.4Mbps (~175KB/s),
 * toc do cao thi cung khong the ganh duoc suc nang cua 1 Packet vuot qua MTU !
 * -> Tach thanh 2 packet: Audio rieng, Bio rieng
 * -----
 * - Packet 1 - INMP441 (audio): 32 samples x 4 bytes = 128 + header/CRC/footer = 137 bytes
 *		+ 1 frame UART = 11bit (8-bit payload + 2 bit Start/Stop + 1 Parity) -> (137 x 11) / 460800 = 3.27ms 
 * - Packet 2 - MAX30102 + AD8232 (bio): 32 samples x (4 + 2) bytes = 192 + header/CRC/footer = 201 bytes
 * 		+ 1 frame UART = 11bit -> (192 x 11) / 460800 = 4.58ms
 * -> Tong 1 block (2 packet) = 338 bytes/32ms 
 * -> Tong transmit 2 packet = 7.85ms -> 24.15ms idle
 * -> 1s = ~32 blocks (Can gui 64 lan lien = 64 packets) -> 10.8KB/s -> Baudrate 460800 OK ! (~23.5% bandwidth UART)
 */

/* ============ Hang so & cau truc cua packet (giong voi ben phat STM32) ============ */

#define PKT_HEADER			0xAA 	/* Header */
#define PKT_FOOTER			0x55 	/* Footer */
#define PKT_VERSION			0x01 	/* Version 1 */
#define PKT_SAMPLES			32	 	/* 32ms = 1 block = 32 samples */

#define PKT_TYPE_AUDIO	0x01  /* INMP441 (1)*/
#define PKT_TYPE_BIO		0x02  /* MAX30102 + AD8232 (2) */

#pragma pack(1) // Ep cau truc theo don vi 1 byte

/* ====== Data duoi format Binary packet (giong voi STM32)====== */
struct __binary_pkt_header {
	uint8_t header;         /* 0xAA */
	uint8_t version;		    /* PKT_VERSION */
	uint8_t type;			      /* PKT_TYPE_xxx */
	uint8_t seq;			      /* Sequence number ID 0...255 rollover (ghi de neu tran so) */
	uint16_t payload_len;   /* So bytes payload theo sau */
};
/* Common header (6 bytes) */
typedef struct __binary_pkt_header bnr_pkt_header_t;

struct __binary_pkt_footer {
	uint16_t crc16;  /* CRC16-CCITT kiem tra phan du theo chu ky 16-bit */
	uint8_t footer;  /* 0x55 */
};
/* Common footer (3 bytes) */
typedef struct __binary_pkt_footer bnr_pkt_footer_t;

/* PACKET 1: Audio (INMP441) - Size: 137 bytes */
typedef struct {
	bnr_pkt_header_t hdr;		    // 6 bytes
	int32_t pcg[PKT_SAMPLES]; 	// 32 x 4 = 128 bytes (24-bit data trong int32) -> Hex: 0x80
	bnr_pkt_footer_t ftr;		    // 3 bytes
} ss_pkt_audio_t;

/* PACKET 2: Bio (AD8232 + MAX30102) - Size: 201 bytes */
typedef struct {
	bnr_pkt_header_t hdr;		  // 6 bytes
	uint32_t ir[PKT_SAMPLES];	// MAX30102 IR: 32 x 4 = 128 bytes
	int16_t ecg[PKT_SAMPLES];	// AD8232 ECG: 32 x 2 = 64 bytes
	/* Tong 192 bytes -> Hex: 0xC0 */
	
	bnr_pkt_footer_t ftr;		  // 3 bytes
} ss_pkt_bio_t;

#pragma pack()

/* Max size de cap phat buffer chung neu can */
#define PKT_SIZE_AUDIO		sizeof(ss_pkt_audio_t) // 137
#define PKT_SIZE_BIO		  sizeof(ss_pkt_bio_t)   // 201
#define PKT_SIZE_MAX		  PKT_SIZE_BIO

/*-----------------------------------------------------------*/

/**
 * @brief Thuc hien tinh toan CRC16 cho Parse binary packet nhan duoc o phia ESP32
 *
 * @param[in] data Con tro tro den mang gia tri dau vao
 * @param[in] len Chieu dai cua mang du lieu
 *
 * @details
 * Nguyen ly:
 * - Chuoi data dau vao duoc xem nhu la 1 so nhi phan khong lo
 * - Thuat toan se lay chuoi du lieu nay chia cho 1 da thuc sinh.
 * (Da thuc nay co gia tri Hex tuong duong: 0x1021)
 * - Thuc hien chia bang cach dung lien tiep cac phep tru XOR
 * - Phan du cua phep chia chinh la ma CRC duoc sinh ra.
 *
 * Truyen nhan:
 * 	- Ben gui: Ap dung thuat toan chia da thuc len chuoi du lieu goc
 * 	Phan du (thuong la 16-bit) thu duoc se gan vao cuoi ban tin (footer)
 * 	- Ben nhan: Thuc hien lai phep tinh tuong tu tren toan bo goi tin
 * 	nhan duoc (bao gom ca chuoi CRC di kem).
 * 	Neu ket qua phep chia bang 0, du lieu duoc xem la toan ven va khong co loi
 */
__attribute__((always_inline))
static inline uint16_t pkt_crc16(const uint8_t *data, uint16_t len){
	uint16_t crc = 0xFFFF; // Gia tri khoi tao, mot so co the dung 0x0000

	for(uint16_t i = 0; i < len; i++){
		crc ^= (uint16_t)data[i] << 8; // Thuc hien XOR data va crc hien tai (ban dau la 0xFFFF)

		for(uint8_t j = 0; j < 8; j ++){ // Lap 8 lan cho 8 bit cua byte
			crc = (crc & 0x8000) ? (crc << 1) ^ 0x1021 : (crc << 1);
		}
	}
	return crc;
}

/* Packet process callback function (Out: void type, In: integer type) - dinh nghia ham xu ly o noi can dung */
typedef void (*on_pkt_prcss_cb_t)(const uint8_t *buf, uint16_t len, uint8_t type);

/**
 * @brief Ham thuc hien parse binary packet nhan duoc
 * 
 * @param[in] in Con tro den du lieu cua packet can parse
 * @param[in] in_len Chieu dai/kich thuoc packet can parse
 * @param[in] cb Ham callback truyen vao de xu ly
 * 
 * @note Sau nay khi muon dua Binary packet ra BLE, van can dung ham nay vi 2 nguyen nhan:
 * \note 1. [Giai quyet van de Stream Fragmentation cua UART]. Vi UART la giao thuc truyen thong 
 * theo dang Stream nghia la no chi quan tam truyen tung byte 1, hoan toan khong co khai niem "Packet".
 * Khi goi ham `uart_read_bytes` de doc bytes, driver UART cua ESP32 se boc bat ky so luong byte nao 
 * dang co san trong RingBuffer (neu Rx Ringbufer timeout)
 * => uart_rx_buf nhan duoc se co tinh trang: "Rau ong no cam vao cam ba kia" hoac 1 packet bi
 * chia ra lam 2 dot doc -> Data khi day thang ra BLE roi thu ben App -> Data hong -> Khong Parse duoc
 * 
 * \note 2. [Bo loc khang nhieu cho song BLE]. Song BLE hoat dong o tan so cao, bang thong gioi han (MTU 247).
 * Khong the lang phi de ban nhung byte rac ra khong trung.
 * - Khi van hanh thuc te, cac cu song dien phan cung (STM32 reset, nhieu nguon, Switch,...) se phong ra byte rac
 * ngau nhien. Nho co ham nay, toan bo byte rac se bi doi nguoc lai o cac state machine khi parse header, CRC, Version,...
 * Chi nhung packet chuan xac 100% moi co the di tiep
 */
void ss_pkt_packet_parser_feed(const uint8_t *in, int in_len, on_pkt_prcss_cb_t cb);

/**
 * @brief Ham reset struct du lieu cua s_parser
 */
void ss_pkt_packet_parser_reset(void);

#ifdef __cplusplus
}
#endif // __cplusplus

#endif // __INCLUDE_SENSOR_PKT_H