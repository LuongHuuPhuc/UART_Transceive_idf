import asyncio
import struct
import time  # Thu vien dinh thoi phan cung
from bleak import BleakClient, BleakScanner

# Dinh nghia cau hinh chinh xac UUID cua Characteristic TX ben phia ESP32
CHARACTERISTIC_UUID = "6E400003-B5A3-F393-E0A9-E50E24DCCA9E"
DEVICE_TARGET_NAME = "ESP32_PERIPHERAL_PHUC"

# Cac hang so dinh danh cau truc cua Packet 
PKT_HEADER = 0xAA
PKT_VERSION = 0x01
PKT_TYPE_AUDIO = 0x01
PKT_TYPE_BIO = 0x02
PKT_FOOTER = 0x55

audio_counter = 0
bio_counter = 0
last_report_time = time.perf_counter()

def notification_handler(sender: int, data: bytearray):
    """
    Ham callback xu ly bat dong bo khi bat duoc 1 goi byte tu Anten phat sang
    """
    global audio_counter, bio_counter

    if len(data) < 4: # Goi tin toi thieu phai chua Header (1B), Version (1B), Type (1B), seq (1B)
        return
    
    # Boc thu Header & Type o nhung byte dau tien 
    header, version, pkt_type, seq = struct.unpack_from("<BBBB", data, 0)
    footer = data[-1]

    # CHOT khang nhieu bien mang 3 lop 
    if header != PKT_HEADER or footer != PKT_FOOTER:
        print(f"Packet offset is incorrect ! Header: {hex(header)}, Footer: {hex(footer)}, Len: {len(data)}")
        return 

    if version != PKT_VERSION:
        print(f"Packet version incorrect ! Got: {hex(version)}, Expected: {PKT_VERSION}")
        return
    
    # Sau khi vuot qua dieu kien kiem tra, tien hanh phan tich du lieu sach
    if pkt_type == PKT_TYPE_AUDIO: 
        audio_counter += 1

        # Co the boc tach tiep goi tin o day 
        pass

    elif pkt_type == PKT_TYPE_BIO:
        bio_counter += 1

        # Co the boc tach tiep goi tin o day 
        pass

async def monitor_performance():
    """
    Task chay ngam dinh ky 1s de in bao cao Counter len Terminal doi chung voi ESP32
    """
    global audio_counter, bio_counter, last_report_time
    while True:
        # Ep vong lap nghi 1s tuong doi
        await asyncio.sleep(1.0)

        # Do chinh xac tuyet doi khoang thoi gian vat ly thuc te 
        current_time = time.perf_counter()
        elapsed_time = current_time - last_report_time

        if elapsed_time > 0:
            normalized_audio = round(audio_counter / elapsed_time)
            normalized_bio = round(bio_counter / elapsed_time)
            print(f"[BLE Central Monitor] -> Audio_block: {normalized_audio}/s, | Bio_block: {normalized_bio}/s")
        
        # Reset counter va cap nhat lai moc thoi gian phan cung cho chu ky sau
        audio_counter = 0
        bio_counter = 0
        last_report_time = current_time

async def main():
    global last_report_time

    print("[Python BLE Central] Scanning for target device...")
    device = await BleakScanner.find_device_by_filter(
        lambda d, adv: adv.local_name == DEVICE_TARGET_NAME if adv.local_name else False
    )

    if not device:
        print(f"[BLE Central Monitor] Can not find any devices named: {DEVICE_TARGET_NAME}")
        return

    print(f"[BLE Central Monitor] Device found ! MAC add: {device.address}. Connecting...")

    async with BleakClient(device, disconnected_callback=lambda c: print("Disconnected !")) as client:

        # Thu vien bleak se tu dong toi uu hoa MTU cao nhat dua tren OS
        print(f"[BLE Central Monitor] Connected to Peripheral ! Now Central is Client (MTU: {client.mtu_size} bytes)")

        print(f"[BLE Central Monitor] Activating subcribe CCCD to get Notify...")
        await client.start_notify(CHARACTERISTIC_UUID, notification_handler)

        print("[BLE Central Monitor] System OK ! Receiving data...")

        # Ghim moc thoi gian goc ngay truoc khi kich hoat luong in
        last_report_time = time.perf_counter() 

        # Khoi chay luong in bao cao hieu nang 1s song song
        asyncio.create_task(monitor_performance())

        # Giu luong chinh ngu dong de duy tri link layer
        while True:
            await asyncio.sleep(3600)

if __name__ == "__main__":
    try:
        asyncio.run(main())
    except KeyboardInterrupt:
        print("\nStopped.")