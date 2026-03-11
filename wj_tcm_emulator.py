#!/usr/bin/env python3
"""
WJ Diag - ELM327 Emulator v13 (real vehicle verified)
==========================================================
Jeep Grand Cherokee WJ 2.7 CRD — full J1850 + K-Line emulation.

K-Line: ECU(0x15) + TCM(0x20)
  ECU security: ArvutaKoodi lookup-table algorithm (dynamic seed)
  TCM security: EGS52 swap+XOR+MUL (static seed 0x6824)
J1850:  ABS(0x40)  SID 0x20/0x24  | Airbag(0x60) SID 0x28
        SKIM(0x62) SID 0x38/0x3A  | HVAC(0x68)   SID 0x28
        BCM(0x80)  SID 0x2E       | Radio(0x87)  SID 0x2F
        Cluster(0x90) SID 0x32    | MemSeat(0x98) SID 0x38
        Liftgate(0xA0)            | Overhead(0x28) SID 0x2A
        VTSS(0xC0)                | 0xA1

python wj_tcm_emulator.py [--host 0.0.0.0] [--port 35000]
"""

import argparse, asyncio, logging, math, random, time
from dataclasses import dataclass, field

logging.basicConfig(level=logging.INFO, format="%(asctime)s [%(levelname)s] %(message)s", datefmt="%H:%M:%S")
log = logging.getLogger("WJ-EMU")


@dataclass
class VehicleState:
    # TCM
    turbine_rpm: float = 49.0
    output_rpm: float = 0.0
    trans_temp: int = 77
    gear_selector: int = 8
    engage_status: int = 0x1E
    tcc_slip_actual: int = -31
    tcc_slip_desired: int = -31
    line_pressure: int = 221
    solenoid_mode: int = 0x96
    # Engine
    engine_rpm: float = 750.0
    coolant_temp: float = 20.0
    iat: float = 18.0
    tps: float = 0.0
    map_actual: int = 920
    rail_actual: float = 290.0
    aap: int = 928
    maf_actual: float = 420.0
    maf_spec: float = 360.0
    injection_qty: float = 9.5
    battery_voltage: float = 14.5
    vehicle_speed: float = 0.0
    # DTCs
    tcm_dtc_list: list = field(default_factory=lambda: [
        (0x07, 0x10, 0x20), (0x26, 0x02, 0x01)])
    ecu_dtc_list: list = field(default_factory=lambda: [
        (0x05, 0x20, 0x20), (0x05, 0x79, 0x20),
        (0x03, 0x40, 0x20), (0x12, 0x42, 0xA0)])
    abs_dtcs_cleared: bool = False
    airbag_dtcs_cleared: bool = False
    # State
    active_target: int = 0x20
    tcm_security_unlocked: bool = False
    _t0: float = 0.0

    def __post_init__(self):
        self._t0 = time.time()

    def tick(self):
        t = time.time() - self._t0
        self.engine_rpm = 750 + 30 * math.sin(t * 0.5) + random.uniform(-10, 10)
        cycle_t = t % 90
        gear_ratios = [3.59, 2.19, 1.41, 1.00, 0.83]
        if cycle_t < 15:
            self.gear_selector = 8
            self.turbine_rpm = 20 + random.uniform(-5, 15)
            self.output_rpm = 0; self.vehicle_speed = 0
            self.engage_status = 0x1E; self.line_pressure = 221
            self.solenoid_mode = 0x96
            self.tcc_slip_actual = random.randint(-35, -25)
            self.tcc_slip_desired = self.tcc_slip_actual + random.randint(-3, 3)
        elif cycle_t < 20:
            self.gear_selector = 7
            self.turbine_rpm = 700 + random.uniform(-30, 30)
            self.output_rpm = 200 + random.uniform(-20, 20)
            self.vehicle_speed = self.output_rpm * 0.0385
            self.engage_status = 0x3C; self.line_pressure = 400
            self.solenoid_mode = 0x00
            self.tcc_slip_actual = int(self.turbine_rpm - self.output_rpm)
            self.tcc_slip_desired = self.tcc_slip_actual + random.randint(-5, 5)
        elif cycle_t < 25:
            self.gear_selector = 6
            self.turbine_rpm = 15 + random.uniform(-3, 10)
            self.output_rpm = 0; self.vehicle_speed = 0
            self.engage_status = 0x1E; self.line_pressure = 0
            self.solenoid_mode = 0x96
            self.tcc_slip_actual = random.randint(-15, -5)
            self.tcc_slip_desired = self.tcc_slip_actual
        else:
            self.gear_selector = 5
            drive_t = cycle_t - 25
            if drive_t < 8: gi = 0; self.engine_rpm = 1200 + 800*(drive_t/8) + random.uniform(-20,20)
            elif drive_t < 16: gi = 1; self.engine_rpm = 1400 + 600*((drive_t-8)/8) + random.uniform(-20,20)
            elif drive_t < 24: gi = 2; self.engine_rpm = 1600 + 400*((drive_t-16)/8) + random.uniform(-15,15)
            elif drive_t < 35: gi = 3; self.engine_rpm = 1800 + 200*math.sin((drive_t-24)*0.3) + random.uniform(-15,15)
            elif drive_t < 50: gi = 4; self.engine_rpm = 2000 + 100*math.sin((drive_t-35)*0.2) + random.uniform(-10,10)
            else:
                dp = (drive_t-50)/15.0
                if dp<0.3: gi=3
                elif dp<0.6: gi=2
                elif dp<0.85: gi=1
                else: gi=0
                self.engine_rpm = max(750, 2000 - 1200*dp + random.uniform(-10,10))
            ratio = gear_ratios[gi]
            self.turbine_rpm = self.engine_rpm * 0.97 + random.uniform(-15, 15)
            self.output_rpm = max(0, self.turbine_rpm / ratio + random.uniform(-10, 10))
            self.vehicle_speed = self.output_rpm * 0.0385
            self.engage_status = 0x34 + random.randint(-2, 2)
            self.line_pressure = 600 + int(200*(gi/4.0)) + random.randint(-30, 30)
            self.solenoid_mode = 0x08
            self.tcc_slip_actual = int(self.turbine_rpm - self.output_rpm * ratio)
            self.tcc_slip_desired = self.tcc_slip_actual + random.randint(-5, 10)
        self.trans_temp = min(130, 77 + int(t * 0.02))
        self.coolant_temp = min(95, 20 + t * 0.05)
        self.iat = 18 + 2 * math.sin(t * 0.01)
        self.battery_voltage = 14.5 + random.uniform(-0.2, 0.2)
        self.maf_actual = 420 + 20 * math.sin(t * 0.3)
        self.maf_spec = 360 + 10 * math.sin(t * 0.2)
        self.rail_actual = 290 + 10 * math.sin(t * 0.2)
        self.injection_qty = 12.5 + (self.engine_rpm - 750) * 0.012 + random.uniform(-0.5, 0.5)


class KWP2000Responder:
    def __init__(self, s: VehicleState):
        self.s = s

    def process(self, sid, data):
        h = {0x14:self._clear_dtc, 0x17:self._read_dtc, 0x18:self._read_dtc,
             0x1A:self._read_ecu_id, 0x21:self._read_local, 0x22:self._read_common,
             0x27:self._security, 0x30:self._io_ctrl, 0x31:self._routine,
             0x3B:self._write_local, 0x3E:self._tester, 0x81:self._startcomm}.get(sid)
        if h:
            p = h(data)
            if p is not None: return self._wrap(p)
        return self._wrap(bytes([0x7F, sid, 0x11]))

    def _wrap(self, p):
        t = self.s.active_target
        f = 0x80 | (len(p) & 0x3F) if len(p) <= 63 else 0x80
        if len(p) <= 63:
            frame = bytes([f, 0xF1, t]) + p
        else:
            frame = bytes([0x80, 0xF1, t, len(p) & 0xFF]) + p
        return frame + bytes([sum(frame) & 0xFF])

    def _startcomm(self, d): return bytes([0xC1, 0xEF, 0x8F])
    def _tester(self, d): return bytes([0x7E, d[0] if d else 0x01])

    def _security(self, d):
        if not d: return bytes([0x7F, 0x27, 0x12])
        if d[0] == 0x01:
            if self.s.active_target == 0x20:
                return bytes([0x67, 0x01, 0x68, 0x24, 0x89])
            else:
                # ECU: dynamic seed
                seed = random.randint(0x0100, 0xFFFE)
                self.s._ecu_seed = seed
                return bytes([0x67, 0x01, (seed >> 8) & 0xFF, seed & 0xFF])
        if d[0] == 0x02:
            if self.s.active_target == 0x20:
                if len(d) >= 3 and d[1] == 0xCC and d[2] == 0x21:
                    self.s.tcm_security_unlocked = True
                    return bytes([0x67, 0x02, 0x34])
                return bytes([0x7F, 0x27, 0x35])
            else:
                # ECU: verify ArvutaKoodi
                if len(d) < 3: return bytes([0x7F, 0x27, 0x12])
                seed = getattr(self.s, '_ecu_seed', 0)
                s0 = (seed >> 8) & 0xFF
                s1 = seed & 0xFF
                expected = self._arvuta_koodi(s0, s1)
                if d[1] == expected[0] and d[2] == expected[1]:
                    self.s.ecu_security_unlocked = True
                    return bytes([0x67, 0x02, 0x34])
                # Track attempts for lockout
                self.s._ecu_bad_attempts = getattr(self.s, '_ecu_bad_attempts', 0) + 1
                if self.s._ecu_bad_attempts >= 2:
                    self.s._ecu_bad_attempts = 0
                    return bytes([0x7F, 0x27, 0x36])  # exceededNumberOfAttempts
                return bytes([0x7F, 0x27, 0x35])  # invalidKey
        return bytes([0x7F, 0x27, 0x12])

    @staticmethod
    def _arvuta_koodi(s0, s1):
        """Lookup-table seed-key for Chrysler EDC15C2 (OM612)."""
        T1 = [0xC0,0xD0,0xE0,0xF0,0x00,0x10,0x20,0x30,0x40,0x50,0x60,0x70,0x80,0x90,0xA0,0xB0]
        T2 = [0x02,0x03,0x00,0x01,0x06,0x07,0x04,0x05,0x0A,0x0B,0x08,0x09,0x0E,0x0F,0x0C,0x0D]
        T3 = [0x90,0x80,0xF0,0xE0,0xD0,0xC0,0x30,0x20,0x10,0x00,0x70,0x60,0x50,0x40,0xB0,0xA0]
        T4 = [0x0D,0x0C,0x0F,0x0E,0x09,0x08,0x0B,0x0A,0x05,0x04,0x07,0x06,0x01,0x00,0x03,0x02]
        v1 = (s1 + 0x0B) & 0xFF
        key_lo = T1[(v1 >> 4) & 0xF] | T2[v1 & 0xF]
        cond = 1 if s1 > 0x34 else 0
        v2 = (s0 + cond + 1) & 0xFF
        key_hi = T3[(v2 >> 4) & 0xF] | T4[v2 & 0xF]
        return (key_hi, key_lo)

    def _read_dtc(self, d):
        self.s.tick()
        dtcs = self.s.ecu_dtc_list if self.s.active_target == 0x15 else self.s.tcm_dtc_list
        r = bytes([0x58, len(dtcs)])
        for h, l, s in dtcs: r += bytes([h, l, s])
        return r

    def _clear_dtc(self, d):
        if self.s.active_target == 0x15: self.s.ecu_dtc_list.clear()
        else: self.s.tcm_dtc_list.clear()
        return bytes([0x54, 0x00, 0x00])

    def _read_ecu_id(self, d):
        o = d[0] if d else 0x86
        if self.s.active_target == 0x20:
            if o == 0x86: return bytes([0x5A,0x86,0x56,0x04,0x19,0x06,0xBC,0x03,0x01,0x02,0x03,0x08,0x02,0x00,0x00,0x03,0x03,0x11])
            if o == 0x87: return bytes([0x5A,0x87,0x02,0x08,0x02,0x00,0x00,0x03,0x01]) + b'56041906BC'
            if o == 0x90: return bytes([0x5A,0x90]) + b'00000000000000000'
            return bytes([0x7F, 0x1A, 0x12])
        else:
            if o == 0x86: return bytes([0x5A,0x86,0xCA,0x65,0x34,0x40,0x65,0x30,0x99,0x05,0xF2,0x03,0x14,0x14,0x14,0xFF,0xFF,0xFF])
            if o == 0x90:
                if getattr(self.s, 'ecu_security_unlocked', False):
                    return bytes([0x5A,0x90]) + b'1J8GW68J42C123456'
                return bytes([0x7F, 0x1A, 0x33])
            if o == 0x91: return bytes([0x5A,0x91,0x05,0x10,0x06,0x05,0x02,0x0C,0x1F,0x0C,0x0A,0x10,0x16,0x06]) + b'  WCAAA '
            return bytes([0x7F, 0x1A, 0x12])

    def _read_common(self, d): return bytes([0x7F, 0x22, 0x11 if self.s.active_target==0x20 else 0x12])

    def _read_local(self, d):
        if not d: return bytes([0x7F, 0x21, 0x12])
        self.s.tick()
        if self.s.active_target == 0x15: return self._ecu_block(d[0])
        return self._tcm_block(d[0])

    def _tcm_block(self, b):
        t = self.s
        if b == 0x30:
            rpm = max(0, int(t.turbine_rpm)) & 0xFFFF
            eng = t.engage_status & 0xFFFF; out = max(0, int(t.output_rpm)) & 0xFFFF
            lp = t.line_pressure & 0xFFFF; sa = t.tcc_slip_actual & 0xFFFF; sd = t.tcc_slip_desired & 0xFFFF
            return bytes([0x61,0x30,(rpm>>8),rpm&0xFF,(eng>>8),eng&0xFF,(out>>8),out&0xFF,
                0x00,t.gear_selector&0xFF,0x04,(lp>>8)&0xFF,lp&0xFF,t.trans_temp&0xFF,
                (sa>>8)&0xFF,sa&0xFF,(sd>>8)&0xFF,sd&0xFF,0x05,0xBF,t.solenoid_mode&0xFF,0x08,0x10,0x00,0x08])
        if b == 0x31: return bytes([0x61,0x31,0x01,0xBE,0x00,0x00,0x02,0xDD,0x02,0xEE]+[0]*14)
        if b == 0x32: return bytes([0x61,0x32]) + bytes(16)
        if b == 0x33: return bytes([0x61,0x33,0x00,0x24,0x07,0x85,0x05,0xDC,0x02,0xAE,0x02,0xAE,0x02,0xD9,0x02,0xD9,0x00,0x00])
        if b == 0x34: return bytes([0x61,0x34,0x02,0x17,0x03,0xFF,0x03,0x32,0x02,0x15,0x08,0x07,0x00,0x01,0x00,0x28])
        if b == 0x35: return bytes([0x61,0x35,0x00,0x03])
        if b == 0x38: return bytes([0x61,0x38,0x01,0xFF,0xFF])
        if b == 0x50: return bytes([0x61,0x50]) + bytes(51)
        if b == 0x60: return bytes([0x61,0x60,0x00,0x01])
        if b == 0x70:
            d = bytes([0x61,0x70,0x00,0x27,0x00,0xF4])
            for i in range(40): d += bytes([0x19+(i%6),0x00,0x2E+i,0xFE,0x0C,0xB3+(i%10)])
            return d
        if b == 0x80: return bytes([0x61,0x80,0x03,0x00,0x00,0x03,0x38,0x02,0x02,0x00,0x0A,0x00,0x00,0x02,0x18,0x08,0x08,0x00,0x0A,0x00,0x00,0x04,0x18,0x08,0x08,0x00,0x0A])
        if b == 0xB0: return bytes([0x61,0xB0]) + b'je0a'
        if b == 0xC0: return bytes([0x61,0xC0,0x00,0xF0,0x64,0x00,0x19,0x1E,0x23,0x28,0x23,0x24,0x2D,0x32,0x29,0x29,0x33,0x3C,0x2D,0x2D,0x35,0x46,0x45,0x4A,0x4D,0x64,0x71,0x71,0x73,0x96,0xB9,0xBE,0xC3,0xC8])
        if b == 0xE0: return bytes([0x61,0xE0]) + b'aafwspsp'
        if b == 0xE1: return bytes([0x61,0xE1,0x01,0x61,0x90,0x46])
        return bytes([0x7F, 0x21, 0x12])

    def _ecu_block(self, b):
        t = self.s
        if b == 0x12:
            # Real: A2 F1 15 61 12 [34 data bytes]
            # Bytes: [0-1]=coolant, [2-3]=IAT, [4-7]=0x08B7x2, [8-9]=00 00,
            #   [10-11]=TPS*100, [12-13]=00 00, [14-15]=MAP, [16-17]=0x038E,
            #   [18-19]=rail*10, [20-21]=0x01DD, [22-23]=0x012A, [24-25]=???,
            #   [26-27]=???, [28-29]=AAP, [30-31]=0x03A0, [32-33]=00 00
            cr=int((t.coolant_temp+273.1)*10); ir=int((t.iat+273.1)*10)
            r = bytes([0x61,0x12])+cr.to_bytes(2,'big')+ir.to_bytes(2,'big')
            r += bytes([0x08,0xB7,0x08,0xB7,0x00,0x00])
            r += int(t.tps*100).to_bytes(2,'big')+bytes([0x00,0x00])
            r += int(t.map_actual).to_bytes(2,'big')+bytes([0x03,0x8E])
            r += int(t.rail_actual*10).to_bytes(2,'big')
            r += bytes([0x01,0xB1,0x01,0x2A,0x00,0x3D])
            r += int(t.aap).to_bytes(2,'big')+bytes([0x03,0xA0,0x00,0x00])
            return r
        if b == 0x28:
            # Real: 9E F1 15 61 28 [30 data bytes]
            # [0-1]=RPM, [2-3]=injQty*100, rest=zeroes
            r = bytes([0x61,0x28])+int(t.engine_rpm).to_bytes(2,'big')
            r += int(t.injection_qty*100).to_bytes(2,'big')+bytes(26)
            return r
        if b == 0x20:
            # Real: A0 F1 15 61 20 [32 data bytes]
            # [0-1]=MAF actual, [2-3]=MAF spec, rest=static from real vehicle
            r = bytes([0x61,0x20])+int(t.maf_actual).to_bytes(2,'big')+int(t.maf_spec).to_bytes(2,'big')
            r += bytes([0x03,0xFE,0x00,0x00,0x00,0x94,0x00,0x49,0x01,0xC1,0x01,0x6A,0x01,0x0A,0x02,0xED,0x02,0x5A,0x00,0x89,0x00,0x7E,0x03,0xA0,0x02,0xA2])
            return r
        if b == 0x22:
            # Real: A2 F1 15 61 22 [34 data bytes]
            # [0-1]=coolant, [2-3]=IAT, [4-7]=0x08B7x2, [8-9]=00 00, [10-11]=00 00,
            # [12-13]=???, [14-15]=MAP, [16-17]=rail*10,
            # rest=static from real vehicle
            cr=int((t.coolant_temp+273.1)*10); ir=int((t.iat+273.1)*10)
            r = bytes([0x61,0x22])+cr.to_bytes(2,'big')+ir.to_bytes(2,'big')
            r += bytes([0x08,0xB7,0x08,0xB7,0x00,0x00,0x00,0x00])
            r += int(t.map_actual).to_bytes(2,'big')  # byte 12-13 was 0x0242=578 at idle
            r += int(t.map_actual).to_bytes(2,'big')   # MAP spec
            r += int(t.rail_actual*10).to_bytes(2,'big')
            r += bytes([0x03,0x9E,0x02,0x5C,0x02,0xE4,0x02,0xE4,0x02,0xA2,0x08,0xB7,0x00,0x00])
            return r
        if b == 0x10: return bytes([0x61,0x10,0x00,0x3F,0x00,0x60,0x00,0x5C,0x0B,0xB8,0x00,0x00,0x00,0x48,0x05,0xAC,0x00,0x00])
        if b == 0x14: return bytes([0x61,0x14,0x7D,0xFE,0x82,0x41,0x82,0x41,0xFF,0x82,0xE6,0x07,0xFF,0xE6])
        if b == 0x16: return bytes([0x61,0x16]+[0x01,0x2C,0x21,0x34,0x01,0x2C]+[0]*10+[0x01,0xF4,0x27,0x10,0x27,0x10,0x07,0x94]+[0]*8+[0x01,0xF4,0x00,0x00,0x00,0x00])
        if b == 0x18: return bytes([0x61,0x18]) + bytes(30)
        if b == 0x24: return bytes([0x61,0x24,0x06,0x03,0x00,0x06,0x00,0x00,0x00,0x01]+[0]*6+[0x00,0x00,0x07,0x46,0x07,0xB9,0x09,0x4E,0x07,0x46,0x00,0x00])
        if b == 0x26: return bytes([0x61,0x26]+[0]*6+[0x5C,0x7A,0x7F,0xFF,0x00,0x00,0x2F,0xA0,0x00,0x29,0x00,0x29,0x00,0x29,0x00,0x29,0x00,0x48,0x80,0x23,0xFF,0x80,0x0C,0xDB])
        if b == 0x30:
            rpm = int(t.engine_rpm)
            return bytes([0x61,0x30]) + rpm.to_bytes(2,'big') + bytes([0x00,0x00,0x00,0x00,0x0A,0xD9,0x0A,0xD9,0x03,0xEA]+[0]*6+[0x0B,0x91,0x00,0x00,0x00,0x00])
        if b == 0x32: return bytes([0x61,0x32,0x03,0x16,0x09,0x30]+[0]*4+[0x0C,0xE4,0x03,0x16,0x03,0x16,0x01,0x99,0x01,0x82,0x00,0xBF,0x00,0x3F,0x02,0x65,0x02,0x84]+[0]*6)
        if b == 0x34: return bytes([0x61,0x34,0x00,0x08,0x10,0x00]+[0]*4+[0x04]+[0]*9+[0x03,0xFF,0x00,0x03]+[0]*5+[0x03]+[0]*5+[0x04,0x00,0x00])
        if b == 0x38: return bytes([0x61,0x38]) + bytes(54)
        if b == 0x40: return bytes([0x61,0x40]) + bytes(50)
        if b == 0x42: return bytes([0x61,0x42]+[0]*5+[0x00,0x80]+[0]*19)
        if b == 0x44: return bytes([0x61,0x44]+[0]*25+[0x56,0x00,0x00])
        if b == 0x48: return bytes([0x61,0x48,0x00,0x00,0x00,0x07,0x06,0xC8,0x00,0x06,0x00,0x00,0x00,0x00])
        if b in (0x62,0xB0,0xB1,0xB2):
            if getattr(t, 'ecu_security_unlocked', False):
                # Real vehicle data format (verified 2025-03-11):
                # 0x62: 4 data bytes — 86 F1 15 61 62 18 05 8C 84 [cs]
                if b == 0x62: return bytes([0x61,0x62,0x18,0x05,0x8C,0x84])
                # 0xB0: 2 data bytes — 84 F1 15 61 B0 37 0F [cs]
                if b == 0xB0: return bytes([0x61,0xB0,0x37,0x0F])
                # 0xB1: 2 data bytes — 84 F1 15 61 B1 D2 15 [cs]
                if b == 0xB1: return bytes([0x61,0xB1,0xD2,0x15])
                # 0xB2: 2 data bytes — 84 F1 15 61 B2 E0 4B [cs]
                if b == 0xB2: return bytes([0x61,0xB2,0xE0,0x4B])
            return bytes([0x7F, 0x21, 0x33])
        if b == 0x60: return bytes([0x7F, 0x21, 0x33])
        return bytes([0x7F, 0x21, 0x12])

    def _io_ctrl(self, d): return bytes([0x7F, 0x30, 0x22])
    def _routine(self, d): return bytes([0x71, d[0]]) if d else bytes([0x7F, 0x31, 0x12])
    def _write_local(self, d): return bytes([0x7F, 0x3B, 0x79])


class ELM327Emulator:
    VER = "ELM327 v1.5"

    def __init__(self):
        self.state = VehicleState()
        self.kwp = KWP2000Responder(self.state)
        self.echo = True; self.headers = True; self.protocol = 2
        self.header_bytes = bytes([0x81, 0x20, 0xF1])
        self.header_mode = 0x22

    def process_command(self, raw):
        c = raw.strip().upper().replace(" ", "")
        if not c: return ""
        if c.startswith("AT"): return self._at(c[2:])
        return self._obd(c)

    def _at(self, a):
        if a == "Z":
            self.echo=True; self.headers=True; self.protocol=2; self.header_mode=0x22
            return self.VER
        if a == "I": return self.VER
        if a in ("E0","E1"): self.echo=(a=="E1"); return "OK"
        if a in ("H0","H1"): self.headers=(a=="H1"); return "OK"
        if a.startswith("IFR"): return "OK"
        if a == "RV": return "%.1fV" % self.state.battery_voltage
        if a.startswith("SP"): p=a[2:]; self.protocol=int(p) if p.isdigit() else 2; return "OK"
        if a.startswith("SH"):
            try:
                self.header_bytes = bytes.fromhex(a[2:])
                if len(self.header_bytes) >= 2: self.state.active_target = self.header_bytes[1]
                self.header_mode = self.header_bytes[2] if len(self.header_bytes) >= 3 else 0x22
                log.info("Header -> target=0x%02X mode=0x%02X", self.state.active_target, self.header_mode)
            except: pass
            return "OK"
        if a.startswith("WM"): return "OK"
        if a == "FI": return "BUS INIT: OK" if self.protocol == 5 else "BUS INIT: ...ERROR"
        if a.startswith("RA"): return "OK"
        return "OK"

    def _obd(self, h):
        try: cb = bytes.fromhex(h)
        except: return "?"
        if not cb: return "?"
        if self.protocol == 2: return self._j1850(cb[0], cb[1:])
        r = self.kwp.process(cb[0], cb[1:])
        return self._fmt(r) if r else "NO DATA"

    def _j1850(self, sid, data):
        t = self.state.active_target
        mode = self.header_mode
        self.state.tick()

        # ECUReset mode (0x11) — DTC clear
        if mode == 0x11:
            if t == 0x40 and sid == 0x01:
                self.state.abs_dtcs_cleared = True; return "26 40 51 01"
            if t == 0x60 and sid == 0x0D:
                self.state.airbag_dtcs_cleared = True; return "26 60 51 0D"
            if t in (0x80,0x90,0x98,0x68) and sid == 0x01:
                return f"26 {t:02X} 51 01"
            return "NO DATA"

        # ABS (0x40) — SID 0x20 data, 0x24 DTC
        if t == 0x40:
            if sid == 0x24:
                if self.state.abs_dtcs_cleared: return "26 40 62 00 DD"
                return "26 40 62 02 40 31 01 40 45 20 DD"
            if sid == 0x20: return self._abs_pid(data[0] if data else 0)
            return "26 40 7F 22 12 00 44"

        # Airbag (0x60) — SID 0x28
        if t == 0x60:
            if sid == 0x28:
                p = data[0] if data else 0
                if p == 0x37:
                    if self.state.airbag_dtcs_cleared: return "26 60 68 00 DD"
                    return "26 60 68 02 90 00 01 90 10 20 DD"
                if p in (0x00, 0x01, 0xFF): return "26 60 7F 22 22 00 44"
                return "26 60 7F 22 12 00 85"
            return "26 60 7F 22 12 00 85"

        # SKIM (0x62) — SID 0x38/0x3A
        if t == 0x62:
            if sid == 0x38: return f"26 62 78 {data[0]:02X} 01 00 DD" if data else "NO DATA"
            if sid == 0x3A: return f"26 62 7A {data[0]:02X} {data[1]:02X} DD" if len(data)>=2 else "NO DATA"
            if sid == 0x1A: return "A4 62 81"
            return "NO DATA"

        # HVAC (0x68) — SID 0x28
        if t == 0x68:
            if sid == 0x28:
                p = data[0] if data else 0xFF
                m = {0x00:"26 68 62 47 00 00 6D", 0x01:"26 68 62 FF 00 00 89",
                     0x02:"26 68 62 7F 00 00 49", 0x03:"26 68 62 01 00 00 08",
                     0x04:"26 68 62 20 00 00 DD", 0x05:"26 68 62 1A 00 00 DD",
                     0x06:"26 68 62 00 00 00 DD", 0x07:"26 68 62 45 00 00 DD",
                     0x08:"26 68 62 03 00 00 DD", 0x10:"26 68 62 80 00 00 DD"}
                return m.get(p, "26 68 7F 22 12 00 F2")
            return "NO DATA"

        # BCM (0x80) — SID 0x2E
        if t == 0x80:
            if sid == 0x2E:
                p = data[0] if data else 0
                # Simulate various BCM data
                bcm = {0x00:"26 80 6E 04 20 00 DD", 0x01:"26 80 6E 00 00 00 DD",
                       0x02:"26 80 6E FF 00 00 DD", 0x03:"26 80 6E 01 40 00 DD",
                       0x04:"26 80 6E 00 80 00 DD", 0x05:"26 80 6E 22 00 00 DD",
                       0x10:"26 80 6E 08 00 00 DD", 0x11:"26 80 6E 10 00 00 DD",
                       0x20:"26 80 6E A0 00 00 DD", 0x21:"26 80 6E 00 FF 00 DD",
                       0x30:"26 80 6E 02 00 00 DD", 0x50:"26 80 6E 56 04 19 DD",
                       0x51:"26 80 6E 06 BC 00 DD", 0x52:"26 80 6E 03 01 02 DD",
                       0x53:"26 80 6E 41 00 00 DD", 0x54:"26 80 6E 08 02 00 DD"}
                return bcm.get(p, "26 80 7F 22 12 00 DD")
            if sid == 0x1A: return "26 80 5A 87 56 30 34 31 39 30 36 42 41 DD"
            if sid == 0x24: return "26 80 62 00 00 00 DD"
            return "26 80 7F 22 12 00 DD"

        # Radio (0x87) — SID 0x2F
        if t == 0x87:
            if sid == 0x2F: return f"26 87 6F {data[0]:02X} 00 00 DD" if data else "NO DATA"
            if sid == 0x1A: return "26 87 5A 87 52 41 5A DD"
            return "NO DATA"

        # Cluster (0x90) — SID 0x32
        if t == 0x90:
            if sid == 0x32:
                p = data[0] if data else 0
                cl = {0x00:"26 90 72 01 40 00 DD", 0x01:"26 90 72 00 00 00 DD",
                      0x02:"26 90 72 FF 00 00 DD", 0x03:"26 90 72 3C 00 00 DD",
                      0x04:"26 90 72 00 C8 00 DD", 0x05:"26 90 72 50 00 00 DD",
                      0x10:"26 90 72 02 EE 00 DD", 0x11:"26 90 72 03 98 00 DD",
                      0x15:"26 90 72 00 3C 00 DD", 0x21:"26 90 72 41 42 43 DD",
                      0x25:"26 90 72 04 01 02 DD"}
                return cl.get(p, "26 90 7F 22 12 00 DD")
            if sid == 0x1A: return "26 90 5A 87 50 30 35 36 30 32 37 44 DD"
            return "26 90 7F 22 12 00 DD"

        # MemSeat (0x98) — SID 0x38
        if t == 0x98:
            if sid == 0x38:
                p = data[0] if data else 0
                return f"26 98 78 {p:02X} 01 00 DD"
            if sid == 0x1A: return "NO DATA"
            return "NO DATA"

        # Overhead Console (0x28) — SID 0x2A
        if t == 0x28:
            if sid == 0x2A:
                p = data[0] if data else 0
                ohc = {0x03:"26 28 6A 19 00 00 DD", 0x04:"26 28 6A 1E 00 00 DD",
                       0x05:"26 28 6A 42 00 00 DD", 0x06:"26 28 6A 00 50 00 DD",
                       0x07:"26 28 6A 0C 80 00 DD", 0x08:"26 28 6A FF 00 00 DD",
                       0x0A:"26 28 6A 00 00 01 DD", 0x1F:"26 28 6A 30 30 45 DD"}
                return ohc.get(p, "26 28 7F 22 12 00 DD")
            return "NO DATA"

        # EVIC (0x2A) — SID 0x2A
        if t == 0x2A:
            if sid == 0x2A: return f"26 2A 6A {data[0]:02X} 00 00 DD" if data else "NO DATA"
            return "NO DATA"

        # Liftgate (0xA0)
        if t == 0xA0:
            if sid == 0x2E: return f"26 A0 6E {data[0]:02X} 00 DD" if data else "NO DATA"
            return "NO DATA"

        # VTSS (0xC0)
        if t == 0xC0:
            if sid in (0x2E,0x20,0x38): return f"26 C0 {sid+0x40:02X} 01 00 DD"
            if sid == 0x1A: return "26 C0 5A 87 56 54 53 53 DD"
            return "NO DATA"

        # Module 0xA1
        if t == 0xA1:
            if sid == 0x2E: return f"26 A1 6E {data[0]:02X} 00 DD" if data else "NO DATA"
            return "NO DATA"

        return "NO DATA"

    def _abs_pid(self, pid):
        v = {0x00:"26 40 62 43 00 00 DD", 0x01:"26 40 62 38 92 00 F0",
             0x02:"26 40 62 41 43 00 E0", 0x03:"26 40 62 01 00 00 BE",
             0x04:"26 40 62 1A 65 00 D8", 0x05:"26 40 62 41 00 00 DE",
             0x06:"26 40 62 02 00 00 32", 0x10:"26 40 62 08 00 00 3D",
             0x11:"26 40 62 92 00 00 EA", 0x20:"26 40 62 30 30 45 A2",
             0x30:"26 40 62 72 00 00 7A"}
        return v.get(pid, "26 40 7F 22 12 00 44")

    def _fmt(self, raw):
        if self.headers: return " ".join(f"{b:02X}" for b in raw)
        p = raw[3:-1] if len(raw) > 4 else raw
        return " ".join(f"{b:02X}" for b in p)


class ELM327Server:
    def __init__(self, host="0.0.0.0", port=35000):
        self.host = host; self.port = port

    async def handle_client(self, reader, writer):
        addr = writer.get_extra_info("peername"); log.info("Connected: %s", addr)
        elm = ELM327Emulator()
        try:
            buf = ""
            while True:
                data = await reader.read(4096)
                if not data: break
                buf += data.decode("latin-1")
                while "\r" in buf:
                    line, buf = buf.split("\r", 1); line = line.strip()
                    if not line: continue
                    log.info("  <- %s", line)
                    resp = elm.process_command(line)
                    cmd = line.strip().upper()
                    if cmd.startswith("ATZ"): delay = random.uniform(0.5, 0.8)
                    elif cmd.startswith("AT"): delay = random.uniform(0.15, 0.35)
                    elif cmd == "81": delay = random.uniform(0.5, 0.7)
                    elif cmd.startswith("27"): delay = random.uniform(0.5, 0.7)
                    elif cmd.startswith("21") or cmd.startswith("18") or cmd.startswith("14"):
                        if not hasattr(elm, '_rtog'): elm._rtog = False
                        elm._rtog = not elm._rtog
                        delay = random.uniform(0.25, 0.35) if elm._rtog else random.uniform(0.60, 0.95)
                    elif cmd.startswith("1A"): delay = random.uniform(0.4, 0.6)
                    else: delay = random.uniform(0.2, 0.5)
                    await asyncio.sleep(delay)
                    log.info("  -> %s (%.0fms)", resp, delay*1000)
                    writer.write((resp + "\r\r>").encode("latin-1"))
                    await writer.drain()
        except: pass
        finally: log.info("Disconnected: %s", addr); writer.close()

    async def run(self):
        srv = await asyncio.start_server(self.handle_client, self.host, self.port)
        log.info("=" * 60)
        log.info("  WJ Diag ELM327 Emulator v13")
        log.info("  K-Line: ECU(0x15) + TCM(0x20)")
        log.info("  J1850: ABS BCM Cluster Airbag HVAC Seat OHC Radio VTSS")
        log.info("  Host: %s  Port: %d", self.host, self.port)
        # Show local IP addresses
        import socket
        try:
            for info in socket.getaddrinfo(socket.gethostname(), None, socket.AF_INET):
                ip = info[4][0]
                if not ip.startswith("127."):
                    log.info("  Connect: %s:%d", ip, self.port)
        except Exception:
            pass
        # Fallback: get default route IP
        try:
            s = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
            s.connect(("8.8.8.8", 80))
            log.info("  WiFi IP: %s:%d", s.getsockname()[0], self.port)
            s.close()
        except Exception:
            pass
        log.info("=" * 60)
        async with srv: await srv.serve_forever()


def main():
    p = argparse.ArgumentParser(); p.add_argument("--host", default="0.0.0.0"); p.add_argument("--port", type=int, default=35000)
    a = p.parse_args()
    try: asyncio.run(ELM327Server(a.host, a.port).run())
    except KeyboardInterrupt: log.info("Bye")

if __name__ == "__main__": main()
