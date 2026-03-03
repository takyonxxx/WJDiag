#!/usr/bin/env python3
"""
WJ Diag - ELM327 + NAG1 722.6 TCM Emulator
============================================
Jeep Grand Cherokee 2.7 CRD / NAG1 TCM emülatörü.
WJ Diag uygulamasını gerçek donanım olmadan test etmek için.

Protokol: WiFi ELM327 TCP -> ISO 9141-2 K-Line -> KWP2000
TCM Adresi: 0x10 (NAG1/EGS 722.6)
Tester: 0xF1

Kullanım:
    python wj_tcm_emulator.py [--host 0.0.0.0] [--port 35000]

WJ Diag'da IP: 127.0.0.1, Port: 35000 olarak bağlanın.
"""

import argparse
import asyncio
import logging
import math
import random
import struct
import time
from dataclasses import dataclass, field
from typing import Optional

logging.basicConfig(
    level=logging.INFO,
    format="%(asctime)s [%(levelname)s] %(message)s",
    datefmt="%H:%M:%S"
)
log = logging.getLogger("WJ-TCM-EMU")

# ─────────────────────────────────────────────
# Simulated TCM State
# ─────────────────────────────────────────────

@dataclass
class TCMState:
    """NAG1 722.6 TCM canlı veri durumu."""
    current_gear: int = 0        # 0=P, 1=R, 2=N, 3=D1..7=D5
    target_gear: int = 0
    selector_pos: int = 0        # Vites kolu
    turbine_rpm: float = 0.0
    output_rpm: float = 0.0
    engine_rpm: float = 750.0
    vehicle_speed: float = 0.0

    trans_temp: int = 80         # Raw (gercek = raw - 40), 80 => 40°C
    solenoid_voltage: int = 138  # Raw (gercek = raw * 0.1), 138 => 13.8V
    battery_voltage: int = 140   # Raw, 140 => 14.0V

    throttle_pos: int = 0        # Raw (gercek = raw * 0.39)
    engine_torque: int = 100     # Nm (2 byte, signed)
    tc_slip: int = 0             # Tork konvertörü kayması (2 byte, signed)

    line_pressure: int = 50      # Raw (gercek = raw * 0.1)
    tcc_pressure: int = 0
    shift_pressure: int = 60

    mod_pwm: int = 128           # PWM %
    tcc_pwm: int = 0
    spc_pwm: int = 150

    tcc_status: int = 0          # 0=off, 1=slip, 2=locked
    adaptation_value: int = 128
    limp_mode: int = 0
    # ECU motor params
    coolant_temp: int = 130     # raw, gercek=(raw-40)=90C
    turbo_boost: int = 120      # raw 2byte, gercek=raw*0.01=1.20 bar
    maf_sensor: int = 250       # raw 2byte, gercek=raw*0.1=25.0 g/s
    map_sensor: int = 150       # raw 2byte, gercek=raw*1.0=150 kPa

    # Motor sensörleri (CAN üzerinden)
    turbo_boost: int = 1200      # Raw (gercek = raw * 0.01), 1200 => 12.00 bar (1.2 bar boost)
    maf_sensor: int = 250        # Raw (gercek = raw * 0.1), 250 => 25.0 g/s
    map_sensor: int = 150        # Raw (gercek = raw * 1.0), 150 => 150 kPa

    # TCM DTC listesi: [(high_byte, low_byte, status), ...]
    tcm_dtc_list: list = field(default_factory=lambda: [
        (0x26, 0x02, 0x01),  # P2602 Selenoid voltaj (aktif)
        (0x07, 0x15, 0x28),  # P0715 Turbin hiz sensoru (kayitli)
        (0x07, 0x00, 0x18),  # P0700 Sanziman kontrol arizasi
    ])
    ecu_dtc_list: list = field(default_factory=lambda: [
        (0x03, 0x80, 0x01),  # P0380 Isitma devresi (aktif)
        (0x11, 0x30, 0x18),  # P1130 Yakit basinci kacak (kayitli)
        (0x01, 0x10, 0x28),  # P0110 Emis havasi sicaklik (kayitli)
    ])
    active_target: int = 0x10  # 0x10=TCM, 0x15=ECU (Bosch EDC15C2)

    # I/O durumları (bit mask)
    io_states: bytes = b'\x00\x00\x00\x00'

    # Session
    session_active: bool = False
    _t0: float = 0.0

    def __post_init__(self):
        self._t0 = time.time()

    def tick(self):
        """Her okumada canlı veri simülasyonu güncelle."""
        t = time.time() - self._t0
        # Motor devir - hafif titreşim
        self.engine_rpm = 750 + 30 * math.sin(t * 0.5) + random.uniform(-10, 10)
        # Türbin = motor rpm (park/nötr'de düşük)
        if self.current_gear <= 2:
            self.turbine_rpm = self.engine_rpm * 0.1
            self.output_rpm = 0
            self.vehicle_speed = 0
        else:
            self.turbine_rpm = self.engine_rpm * 0.95
            self.output_rpm = self.turbine_rpm * 0.7
            self.vehicle_speed = self.output_rpm * 0.03

        # Sıcaklık yavaşça artsın
        self.trans_temp = min(130, 80 + int(t * 0.05))  # 40°C'den yavaşça yüksel

        # Selenoid voltajı - hafif dalgalanma (P2602 testi için bazen düşür)
        base_v = 138  # 13.8V
        if t % 60 > 50:  # Her dakikanın son 10 saniyesinde voltaj düşsün
            base_v = 85  # 8.5V - P2602 tetikler
        self.solenoid_voltage = base_v + random.randint(-3, 3)

        # Akü voltajı
        self.battery_voltage = 140 + random.randint(-2, 2)

        # Gaz pedalı - sinüs dalga simülasyonu
        self.throttle_pos = int(abs(math.sin(t * 0.3)) * 200)

        # Tork
        self.engine_torque = int(100 + 50 * math.sin(t * 0.2))

        # PWM'ler
        self.mod_pwm = 128 + random.randint(-5, 5)
        self.spc_pwm = 150 + random.randint(-5, 5)

        # I/O: Park + Fren aktif
        self.io_states = bytes([
            0x00, 0x00, 0x41, 0x00,
        ])

        # ECU motor simulasyonu
        base_cool = 130 + int(5 * math.sin(t * 0.02))
        self.coolant_temp = max(80, min(160, base_cool))
        self.turbo_boost = 50 + int(self.throttle_pos * 1.5) + random.randint(-5, 5)
        self.maf_sensor = max(0, int(self.engine_rpm * 0.05) + random.randint(-10, 10))
        self.map_sensor = max(0, 100 + int(self.turbo_boost * 0.3) + random.randint(-5, 5))

# ─────────────────────────────────────────────
# KWP2000 Response Builder
# ─────────────────────────────────────────────

class KWP2000Responder:
    """KWP2000 servis isteklerini simüle eder."""

    # Header: 83 F1 10 => yanıtta 80+len target(F1) source(10)
    # WJ Diag ATH1 modunda çalışıyor, header dahil yanıt bekler

    def __init__(self, tcm: TCMState):
        self.tcm = tcm

    def process(self, service_id: int, data: bytes) -> Optional[bytes]:
        """KWP2000 isteğini işle, yanıt baytlarını döndür (header dahil)."""
        handler = {
            0x10: self._start_diag_session,
            0x14: self._clear_dtc,
            0x17: self._read_dtc_alt,      # Chrysler alternatif
            0x18: self._read_dtc,
            0x1A: self._read_ecu_id,
            0x21: self._read_local_data,
            0x22: self._read_common_data,
            0x27: self._security_access,
            0x30: self._io_control,
            0x3E: self._tester_present,
        }.get(service_id)

        if handler:
            payload = handler(data)
            if payload is not None:
                return self._wrap_response(payload)

        # Desteklenmeyen servis -> Negative Response
        return self._wrap_response(bytes([0x7F, service_id, 0x11]))

    def _wrap_response(self, payload: bytes) -> bytes:
        """ISO 9141-2 header + payload + checksum."""
        # Format: 80+len, target=F1, source=10, payload..., checksum
        fmt_byte = 0x80 | (len(payload) & 0x3F)
        frame = bytes([fmt_byte, 0xF1, 0x10]) + payload
        checksum = sum(frame) & 0xFF
        return frame + bytes([checksum])

    # --- Service Handlers ---

    def _start_diag_session(self, data: bytes) -> bytes:
        session_type = data[0] if data else 0x01
        self.tcm.session_active = True
        log.info("Diagnostik oturum baslatildi (type=0x%02X)", session_type)
        return bytes([0x50, session_type])

    def _tester_present(self, data: bytes) -> bytes:
        return bytes([0x7E, data[0] if data else 0x01])

    def _read_dtc(self, data: bytes) -> bytes:
        """ReadDTCByStatus (SID 0x18)."""
        self.tcm.tick()
        dtcs = self.tcm.ecu_dtc_list if self.tcm.active_target == 0x15 else self.tcm.tcm_dtc_list
        # Positive: 0x58, count, [high, low, status]...
        resp = bytes([0x58, len(dtcs)])
        for h, l, s in dtcs:
            resp += bytes([h, l, s])
        log.info("DTC okuma: %d adet", len(dtcs))
        return resp

    def _read_dtc_alt(self, data: bytes) -> bytes:
        """Chrysler alternatif DTC okuma (SID 0x17)."""
        return self._read_dtc(data)

    def _clear_dtc(self, data: bytes) -> bytes:
        """ClearDiagnosticInformation (SID 0x14)."""
        if self.tcm.active_target == 0x15:
            self.tcm.ecu_dtc_list.clear()
            log.info("ECU DTC temizlendi")
        else:
            self.tcm.tcm_dtc_list.clear()
            log.info("TCM DTC temizlendi")
        log.info("DTC'ler temizlendi")
        return bytes([0x54, 0xFF, 0xFF])

    def _read_ecu_id(self, data: bytes) -> bytes:
        """ReadECUIdentification (SID 0x1A)."""
        option = data[0] if data else 0x01
        resp = bytes([0x5A, option])

        if option == 0x01:
            # Part number
            resp += b'A 630 270 10 00'
        elif option == 0x02:
            # Software version
            resp += b'NAG1-WJ-SW-V04.12'
        elif option == 0x03:
            # Hardware version
            resp += b'NAG1-722.6-HW-R03'
        else:
            resp += b'N/A'

        log.info("ECU ID okuma (option=0x%02X)", option)
        return resp

    def _read_local_data(self, data: bytes) -> bytes:
        """ReadDataByLocalID (SID 0x21)."""
        if not data:
            return bytes([0x7F, 0x21, 0x12])

        local_id = data[0]
        self.tcm.tick()

        # ECU Motor (0x15) - Bosch EDC15C2
        if self.tcm.active_target == 0x15:
            return self._read_ecu_local_data(local_id)

        # Positive response: 0x61, localID, value_bytes...
        resp = bytes([0x61, local_id])

        value_map_1byte = {
            0x01: self.tcm.current_gear,
            0x02: self.tcm.target_gear,
            0x03: self.tcm.selector_pos,
            0x08: self.tcm.trans_temp,
            0x09: self.tcm.solenoid_voltage,
            0x0A: self.tcm.battery_voltage,
            0x0B: self.tcm.throttle_pos,
            0x0E: self.tcm.line_pressure,
            0x0F: self.tcm.tcc_pressure,
            0x10: self.tcm.shift_pressure,
            0x11: self.tcm.mod_pwm,
            0x12: self.tcm.tcc_pwm,
            0x13: self.tcm.spc_pwm,
            0x14: self.tcm.tcc_status,
            0x15: self.tcm.adaptation_value,
            0x16: self.tcm.limp_mode,
            0x20: self.tcm.coolant_temp,
        }

        value_map_2byte = {
            0x04: int(self.tcm.turbine_rpm),
            0x05: int(self.tcm.output_rpm),
            0x06: int(self.tcm.engine_rpm),
            0x07: int(self.tcm.vehicle_speed),
            0x0C: self.tcm.engine_torque,
            0x0D: self.tcm.tc_slip,
            0x21: self.tcm.turbo_boost,
            0x22: self.tcm.maf_sensor,
            0x23: self.tcm.map_sensor,
        }

        if local_id in value_map_1byte:
            val = value_map_1byte[local_id] & 0xFF
            resp += bytes([val])
        elif local_id in value_map_2byte:
            val = value_map_2byte[local_id] & 0xFFFF
            resp += bytes([(val >> 8) & 0xFF, val & 0xFF])
        elif local_id == 0x30:
            # I/O states (toplu)
            resp += self.tcm.io_states
        else:
            return bytes([0x7F, 0x21, 0x31])  # requestOutOfRange

        return resp

    def _read_ecu_local_data(self, local_id):
        """Motor ECU (Bosch EDC15C2) local data. Based on real WJ 2.7 CRD test vectors."""
        t = self.tcm
        resp = bytes([0x61, local_id])
        if local_id == 0x12:
            craw = int((t.coolant_temp - 40 + 273.1) * 10)
            iraw = int((25 + 273.1) * 10)
            traw = int(t.throttle_pos * 100)
            mraw = t.map_sensor
            resp += craw.to_bytes(2,'big') + iraw.to_bytes(2,'big')
            resp += bytes([0x08,0xB7,0x08,0xB7,0x00,0x00,0x02,0xFD,0x0B,0xA1])
            resp += traw.to_bytes(2,'big') + mraw.to_bytes(2,'big')
            resp += bytes([0x0B,0xBB,0x01,0x32,0x01,0x2B,0x00,0x6F,0x09,0x7F])
            resp += (1013).to_bytes(2,'big') + bytes([0x00,0x00])
        elif local_id == 0x20:
            maf_a = int(t.maf_sensor)
            maf_s = int(maf_a * 0.85)
            resp += bytes([0x03,0x5A,0x03,0x3C,0x03,0xFE,0x00,0x01,0x00,0x94,0x00,0x4A])
            resp += maf_a.to_bytes(2,'big') + maf_s.to_bytes(2,'big')
            resp += bytes([0x01,0x0C,0x03,0x02,0x02,0x66,0x00,0x84,0x00,0x5C,0x03,0xA0,0x03,0x03])
        elif local_id == 0x22:
            rspec = int(t.turbo_boost * 25)
            mspec = t.map_sensor - 1
            resp += bytes([0x0B,0x43,0x0A,0xDD,0x08,0xB7,0x08,0xB7,0x00,0x00,0x00,0x00,0x02,0x43])
            resp += mspec.to_bytes(2,'big') + rspec.to_bytes(2,'big')
            resp += bytes([0x03,0xB8,0x02,0x67,0x02,0xE4,0x02,0xE4,0x02,0x85,0x08,0xB7,0x00,0x0C])
        elif local_id == 0x28:
            rpm = int(t.engine_rpm)
            iq = int(t.throttle_pos * 12)
            resp += rpm.to_bytes(2,'big') + iq.to_bytes(2,'big')
            resp += rpm.to_bytes(2,'big') * 3
            resp += (rpm-2).to_bytes(2,'big') + rpm.to_bytes(2,'big')
            resp += bytes([0x00,0x00,0x00,0xCC,0xFF,0x3C,0x00,0x68,0xFF,0xFB,0xFF,0x94,0x00,0x00,0xF6])
        elif local_id == 0x26:
            resp += bytes(14) + bytes([0x5B,0x37,0x7F,0xFF,0x00,0x00,0x2F,0xA0])
            resp += bytes([0x00,0x29]*4) + bytes([0x00,0x48,0x00,0x23,0x00,0x00,0x0B,0x41])
        else:
            return bytes([0x7F, 0x21, 0x31])
        log.info("ECU 21 %02X: %d bytes", local_id, len(resp))
        return resp

    def _read_common_data(self, data: bytes) -> bytes:
        """ReadDataByCommonID (SID 0x22)."""
        if len(data) < 2:
            return bytes([0x7F, 0x22, 0x12])
        common_id = (data[0] << 8) | data[1]
        # Basit yanıt
        return bytes([0x62, data[0], data[1], 0x00])

    def _security_access(self, data: bytes) -> bytes:
        """SecurityAccess (SID 0x27)."""
        if not data:
            return bytes([0x7F, 0x27, 0x12])

        sub = data[0]
        if sub == 0x01:
            # requestSeed - sabit seed
            log.info("Security seed gonderildi")
            return bytes([0x67, 0x01, 0xDE, 0xAD, 0xBE, 0xEF])
        elif sub == 0x02:
            # sendKey - her zaman kabul et
            log.info("Security key kabul edildi")
            return bytes([0x67, 0x02])
        else:
            return bytes([0x7F, 0x27, 0x12])

    def _io_control(self, data: bytes) -> bytes:
        """IOControlByLocalID (SID 0x30)."""
        if len(data) < 2:
            return bytes([0x7F, 0x30, 0x12])
        local_id = data[0]
        control = data[1]
        log.info("I/O kontrol: ID=0x%02X, ctrl=0x%02X", local_id, control)
        return bytes([0x70, local_id, control])


# ─────────────────────────────────────────────
# ELM327 Emulator (AT command layer)
# ─────────────────────────────────────────────

class ELM327Emulator:
    """
    WiFi ELM327 TCP emülatörü.
    AT komutlarını işler, OBD hex komutlarını KWP2000'e yönlendirir.
    """

    ELM_VERSION = "ELM327 v2.1 (WJ-TCM-EMU)"
    BATTERY_VOLTAGE = "13.8V"

    def __init__(self):
        self.tcm = TCMState()
        self.kwp = KWP2000Responder(self.tcm)

        self.echo = True
        self.headers = False
        self.spaces = True
        self.linefeed = True
        self.protocol = 3  # ISO 9141-2
        self.header_bytes = bytes([0x81, 0x10, 0xF1])
        self.tcm_state = self.kwp.tcm

    def process_command(self, raw: str) -> str:
        """Bir ELM327 komutunu işle, yanıt döndür."""
        cmd = raw.strip().upper().replace(" ", "")

        if not cmd:
            return ""

        # AT komutu mu?
        if cmd.startswith("AT"):
            return self._handle_at(cmd[2:])

        # OBD hex komutu (KWP2000)
        return self._handle_obd(cmd)

    def _handle_at(self, at_cmd: str) -> str:
        """AT komutlarını işle."""
        at = at_cmd.strip()

        # ATZ - Reset
        if at == "Z":
            self.echo = True
            self.headers = False
            self.spaces = True
            log.info("ELM327 reset")
            return self.ELM_VERSION

        # ATI - Version
        if at == "I":
            return self.ELM_VERSION

        # ATE0/ATE1 - Echo
        if at == "E0":
            self.echo = False
            return "OK"
        if at == "E1":
            self.echo = True
            return "OK"

        # ATL0/ATL1 - Linefeed
        if at == "L0":
            self.linefeed = False
            return "OK"
        if at == "L1":
            self.linefeed = True
            return "OK"

        # ATS0/ATS1 - Spaces
        if at == "S0":
            self.spaces = False
            return "OK"
        if at == "S1":
            self.spaces = True
            return "OK"

        # ATH0/ATH1 - Headers
        if at == "H0":
            self.headers = False
            return "OK"
        if at == "H1":
            self.headers = True
            return "OK"

        # ATAT0/1/2 - Adaptive timing
        if at.startswith("AT"):
            return "OK"

        # ATRV - Battery voltage
        if at == "RV":
            return self.BATTERY_VOLTAGE

        # ATSP - Set protocol
        if at.startswith("SP"):
            proto = at[2:]
            self.protocol = int(proto) if proto.isdigit() else 3
            log.info("Protokol ayarlandi: %s", proto)
            return "OK"

        # ATSH - Set header
        if at.startswith("SH"):
            hex_str = at[2:]
            try:
                self.header_bytes = bytes.fromhex(hex_str)
                target = self.header_bytes[1] if len(self.header_bytes) >= 2 else 0x10
                self.tcm_state.active_target = target
                log.info("Header: %s -> target=0x%02X", hex_str, target)
            except ValueError:
                pass
            return "OK"

        # ATIIA - Init address
        if at.startswith("IIA"):
            log.info("Init adresi: %s", at[3:])
            return "OK"

        # ATDP - Describe protocol
        if at == "DP":
            if self.protocol == 5:
                return "ISO 14230-4 (KWP 5BAUD)"
            elif self.protocol == 3:
                return "ISO 9141-2"
            return "AUTO, ISO 9141-2"

        # ATSI - Slow init (5-baud)
        if at == "SI":
            return "OK"

        # ATFI - Fast init (genuine ELM327 feature)
        if at == "FI":
            log.info("Fast init (ATFI) - genuine ELM emulated")
            return "OK"

        # ATWM - Wakeup message (genuine ELM327 feature)
        if at.startswith("WM"):
            wm_hex = at[2:]
            log.info("Wakeup message set: %s", wm_hex)
            return "OK"

        # ATST - Set timeout
        if at.startswith("ST"):
            log.info("Timeout set: %s", at[2:])
            return "OK"

        # Bilinmeyen AT -> OK
        log.debug("Bilinmeyen AT komutu: AT%s -> OK", at)
        return "OK"

    def _handle_obd(self, hex_cmd: str) -> str:
        """OBD/KWP2000 hex komutunu işle."""
        try:
            cmd_bytes = bytes.fromhex(hex_cmd)
        except ValueError:
            return "?"

        if not cmd_bytes:
            return "?"

        service_id = cmd_bytes[0]
        data = cmd_bytes[1:]

        resp = self.kwp.process(service_id, data)
        if resp is None:
            return "NO DATA"

        return self._format_response(resp)

    def _format_response(self, raw_bytes: bytes) -> str:
        """Yanıt baytlarını ELM327 formatına dönüştür."""
        if self.headers:
            # Header dahil tüm frame'i gönder
            hex_list = [f"{b:02X}" for b in raw_bytes]
        else:
            # Header ve checksum'ı atla, sadece payload
            # Header: 3 byte (format, target, source), son byte checksum
            if len(raw_bytes) > 4:
                payload = raw_bytes[3:-1]
            else:
                payload = raw_bytes
            hex_list = [f"{b:02X}" for b in payload]

        if self.spaces:
            return " ".join(hex_list)
        else:
            return "".join(hex_list)


# ─────────────────────────────────────────────
# TCP Server
# ─────────────────────────────────────────────

class ELM327Server:
    """Asenkron TCP sunucusu - WiFi ELM327 gibi davranır."""

    def __init__(self, host: str = "0.0.0.0", port: int = 35000):
        self.host = host
        self.port = port

    async def handle_client(self, reader: asyncio.StreamReader,
                            writer: asyncio.StreamWriter):
        addr = writer.get_extra_info("peername")
        log.info("Baglanti: %s", addr)

        elm = ELM327Emulator()

        try:
            buf = ""
            while True:
                data = await reader.read(4096)
                if not data:
                    break

                buf += data.decode("latin-1")

                # ELM327 komutları \r ile biter
                while "\r" in buf:
                    line, buf = buf.split("\r", 1)
                    line = line.strip()
                    if not line:
                        continue

                    log.info("  <- %s", line)
                    response = elm.process_command(line)
                    log.info("  -> %s", response)

                    # Yanıtı gönder: yanıt + \r\r> (ELM327 prompt)
                    out = response + "\r\r>"
                    writer.write(out.encode("latin-1"))
                    await writer.drain()

        except (ConnectionResetError, asyncio.IncompleteReadError):
            pass
        except Exception as e:
            log.error("Hata: %s", e)
        finally:
            log.info("Baglanti kapandi: %s", addr)
            writer.close()
            try:
                await writer.wait_closed()
            except Exception:
                pass

    async def run(self):
        server = await asyncio.start_server(
            self.handle_client, self.host, self.port
        )
        addrs = ", ".join(str(s.getsockname()) for s in server.sockets)
        log.info("=" * 55)
        log.info("  WJ Diag - ELM327 + NAG1 TCM Emulator")
        log.info("  Dinleniyor: %s", addrs)
        log.info("")
        log.info("  WJ Diag'da baglan:")
        log.info("    IP:   127.0.0.1")
        log.info("    Port: %d", self.port)
        log.info("")
        log.info("  Simulasyon:")
        log.info("    - P2602 (selenoid voltaj) aktif DTC")
        log.info("    - P0715, P0562 kayitli DTC'ler")
        log.info("    - Canli veri: RPM, sicaklik, voltaj...")
        log.info("    - Her 60sn'de 10sn voltaj dususu (P2602)")
        log.info("=" * 55)

        async with server:
            await server.serve_forever()


# ─────────────────────────────────────────────
# Main
# ─────────────────────────────────────────────

def main():
    parser = argparse.ArgumentParser(
        description="WJ Diag - ELM327 + NAG1 722.6 TCM Emulator"
    )
    parser.add_argument("--host", default="0.0.0.0",
                        help="Dinleme adresi (varsayilan: 0.0.0.0)")
    parser.add_argument("--port", type=int, default=35000,
                        help="TCP port (varsayilan: 35000)")
    args = parser.parse_args()

    srv = ELM327Server(args.host, args.port)

    try:
        asyncio.run(srv.run())
    except KeyboardInterrupt:
        log.info("Kapatiliyor...")


if __name__ == "__main__":
    main()
