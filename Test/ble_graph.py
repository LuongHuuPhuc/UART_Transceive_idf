# Chuong trinh ve do thi 3 kenh PCG (Audio Packet) + PPG - ECG (Bio Packet) tu BLE

import asyncio
import struct
import collections
import matplotlib.pyplot as plt
import matplotlib.animation as animation
import threading
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

# Bo dem vong (Dequeue) luu tru mau du lieu de ve do thi (gioi han toi da 1000 mau tren man hinh)
MAX_SAMPLES_DISPLAY = 1000
pcg_queue = collections.deque(maxlen=MAX_SAMPLES_DISPLAY)
ppg_queue = collections.deque(maxlen=MAX_SAMPLES_DISPLAY)
ecg_queue = collections.deque(maxlen=MAX_SAMPLES_DISPLAY)

# Khoi tao mang trong ban dau de do thi khong bi loi khi chua co data 
pcg_queue.extend([0] * MAX_SAMPLES_DISPLAY)
ppg_queue.extend([0] * MAX_SAMPLES_DISPLAY)
ecg_queue.extend([0] * MAX_SAMPLES_DISPLAY)

# Bien dem debug so packet block
audio_counter = 0
bio_counter = 0
last_report_time = time.perf_counter()

def notification_handler(sender: int, data: bytearray):
    """
    Ham callback xu ly bat dong bo khi bat duoc 1 goi byte tu Anten phat sang
    Cau hinh packet: Header (6 bytes) + Payload + Footer (3 bytes)
    """
    global audio_counter, bio_counter

    if len(data) < 9: # Goi tin toi thieu phai chua Header (6B) va Footer (3B)
        return
    
    # Boc tach Header 6 bytes -> Cu phap "<BBBBH" doc Little Endian
    header, version, pkt_type, seq, payload_len = struct.unpack_from("<BBBBH", data, 0)
    
    # Footer 3 bytes nam cuoi cua packet
    footer_chk = data[-1]

    # CHOT khang nhieu bien mang 3 lop 
    if header != PKT_HEADER or footer_chk != PKT_FOOTER:
        print(f"Packet offset is incorrect ! Header: {hex(header)}, Footer: {hex(footer)}, Len: {len(data)}")
        return 

    if version != PKT_VERSION:
        print(f"Packet version incorrect ! Got: {hex(version)}, Expected: {PKT_VERSION}")
        return
    
    # Offset dich chuyen bat dau tu byte 6 de bat dau vao Payload cua packet
    offset_start = 6

    ### ----------- AUDIO PARSER ----------- 
    if pkt_type == PKT_TYPE_AUDIO: 
        audio_counter += 1

        # Goi tin nang 137 bytes = 6B Header + 128 payload + 3 footer
        # Moi sample PCG 4 bytes (Ky hieu struct: 'i' - signed int 4 bytes)
        dynamic_audio_samples = payload_len // 4

        try:
            # Ep cau truc format boc dung so luong mau thuc te (Co the co 31 chu khong phai luc nao cung 32)
            samples_pcg = struct.unpack_from(f"<{dynamic_audio_samples}i", data, offset_start)
            pcg_queue.extend(samples_pcg)
        except Exception as e:
            print(f"Audio Packet parse failed: {e} | Payload_len: {payload_len}")

    # -----------  BIO PARSER ----------- 
    elif pkt_type == PKT_TYPE_BIO:
        bio_counter += 1

        # Goi tin nang 201 bytes = 6B Header + 192 payload + 3 footer
        # PPG IR (4 bytes/sample) dung truoc, ECG (2 bytes/sample) dung sau
        SAMPLE_BIO_SIZE = 6 # 6 bytes (4B PPG + 2B ECG)
        dynamic_bio_samples = payload_len // SAMPLE_BIO_SIZE

        # Tinh toan moc ranh gioi nhay Offset trong RAM
        ir_bytes_len = dynamic_bio_samples * 4 # Chieu dai khoi IR 
        offset_ecg_start = offset_start + ir_bytes_len # Vi tri bat dau parse ECG

        try:
            # 1. Boc toan bo khoi mang IR (Ky hieu 'I' - unsigned int 4 bytes)
            samples_ir = struct.unpack_from(f"<{dynamic_bio_samples}I", data, offset_start)
            ppg_queue.extend(samples_ir)

            # 2. Boc toan bo khoi mang ECG dung ngay sau (Ky hieu: 'h' - signed short 2 bytes)
            samples_ecg = struct.unpack_from(f"<{dynamic_bio_samples}h", data, offset_ecg_start)
            ecg_queue.extend(samples_ecg)
        
        except Exception as e:
            print(f"Bio Packet parse failed: {e} | Payload_len: {payload_len}")

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

async def run_ble_client():
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

def start_async_background_loop(loop):
    asyncio.set_event_loop(loop)
    loop.run_until_complete(run_ble_client())

# ======= Khoi tao khung do thi de ve do thi cho 3 kenh =======
fig, (ax1, ax2, ax3) = plt.subplots(3, 1, figsize=(12, 8))
fig.suptitle("Real-time Cardiovascular Waveform Monitoring", fontsize=14, fontweight='bold')

# Kenh 1: PCG - Rut tu goi Audio
line_pcg, = ax1.plot(range(MAX_SAMPLES_DISPLAY), pcg_queue, color='teal', lw=1.0)
ax1.set_title("PCG (Phonocardiogram - Heart Sound) [From Audio Packet]")
ax1.set_xlim(0, MAX_SAMPLES_DISPLAY)
ax1.set_ylim(-45000, 45000)
ax1.grid(True, linestyle="--", alpha=0.5)

# Kenh 2: PPG - Rut tu goi Bio
line_ppg, = ax2.plot(range(MAX_SAMPLES_DISPLAY), ppg_queue, color='darkorange', lw=1.2)
ax2.set_title("PPG (Photoplethysmogram - Pulse Wave) [From Bio Packet]")
ax2.set_xlim(0, MAX_SAMPLES_DISPLAY)
ax2.set_ylim(200, 40000) # Khoat chat vao bien do cua MAX30102 co the den
ax2.grid(True, linestyle="--", alpha=0.5)

# Kenh 3: ECG - Rut tu goi Bio
line_ecg, = ax3.plot(range(MAX_SAMPLES_DISPLAY), ecg_queue, color='crimson', lw=1.5)
ax3.set_title("ECG (Electrocardiogram) [From Bio Packet]")
ax3.set_xlim(0, MAX_SAMPLES_DISPLAY)
ax3.set_ylim(0, 2800)
ax3.grid(True, linestyle="--", alpha=0.5)

def update_graph(frame):
    """ Cap nhat giao dien do hoa cuon cho do thi """
    list_pcg = list(pcg_queue)
    list_ppg = list(ppg_queue)
    list_ecg = list(ecg_queue)

    line_pcg.set_ydata(list_pcg)
    line_ppg.set_ydata(list_ppg)
    line_ecg.set_ydata(list_ecg)

    # Kiem tra bien dong mang PPG hien tai de tu dong nan khit truc Y theo nhip mach dap 
    p_min, p_max = min(list_ppg), max(list_ppg)
    if p_max > p_min:
        ax2.set_ylim(p_min - 10, p_max + 10)
        
    return line_pcg, line_ppg, line_ecg

if __name__ == "__main__":
    bg_loop = asyncio.new_event_loop()
    t = threading.Thread(target=start_async_background_loop, args=(bg_loop,), daemon=True)
    t.start()

    # Tan suat lat khung man hinh 20ms
    ani = animation.FuncAnimation(fig, update_graph, interval=20, blit=True)
    plt.tight_layout()
    plt.show()
