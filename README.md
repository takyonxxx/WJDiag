# WJDiag — Jeep Grand Cherokee WJ 2.7 CRD Diagnostic Tool

## Vehicle: 2003 EU-spec WJ 2.7 CRD (OM612 / NAG1)

Qt6 cross-platform diagnostic application communicating via ELM327 (Bluetooth/WiFi).

## Module Map — EU WJ 2.7 CRD

| Ref App Menu | Addr | Bus | Status |
|---|---|---|---|
| Engine ECU | K-Line 0x15 | ISO 14230 | Full data + security + DTC |
| Transmission | K-Line 0x20 | ISO 14230 | Full data + security + DTC |
| ABS / TCM | J1850 0x28 | VPW | Read + DTC clear |
| Body Computer | J1850 0x40 | VPW | Read + relay (hazard/horn/lights/mirrors) |
| ESP / Traction | J1850 0x58 | VPW | Read only |
| Airbag (ORC) | J1850 0x60 | VPW | NRC 0x22 always |
| Instrument Cluster | J1850 0x61 | VPW | Gauge test via SID 0x3A |
| Climate (HVAC) | J1850 0x68 | VPW | Read OK |
| Memory Seat | J1850 0x98 | VPW | DTC read only |
| Driver Door | J1850 0xA0 | VPW | Read + relay (left windows/locks/mirrors) |
| Passenger Door | J1850 0xA1 | VPW | Read + relay (right windows/locks/mirrors) |
| Rain Sensor | J1850 0xA7 | VPW | Read + DTC clear |
| Park Assist | J1850 0xC0 | VPW | Read OK |

Dead modules: 0x80 (NO DATA), 0x2A Overhead Console (NO DATA)

## Controls Tab

- **Front Windows**: Left (0xA0 sequential) + Right (0xA1 sequential) — hold to activate
- **Rear Windows**: Left (0xA0) + Right (0xA1) — hold to activate
- **Hazard / Horn**: Body Computer 0x40 — toggle/hold
- **Cluster Gauge Test**: Speedo, Tacho, Lamps via 0x61

Both doors use same sequential pattern: `38 PID 12` ON, `38 PID 00` OFF.
ATRA (receive address filter) is mandatory for all J1850 commands.

## Live Data

ECU blocks: 0x12, 0x30, 0x22, 0x20, 0x23, 0x21, 0x16, 0x32, 0x37, 0x13, 0x36, 0x26, 0x34, 0x62, 0xB0, 0xB1, 0xB2
TCM blocks: 0x30, 0x31, 0x34, 0x33, 0x32

## DTC Management

- ECU (K-Line): `14 00 00` → `54 00 00` (instant)
- TCM (K-Line): `14 00 00` → `7F 14 78` (pending) → retry needed
- Body Computer 0x40: `ATSH244014` → `FF 00 00`
- ABS 0x28: `ATSH242814` → `FF 00 00`
- Rain Sensor 0xA7: `ATSH24A714` → `FF 00 00`

## ESP32-S2 Emulator

WiFi AP "WiFi_OBDII", IP 192.168.0.10, TCP port 35000, HTTP dashboard.

## Protocol Notes

- CRC-16/MODBUS (poly=0xA001, init=0xFFFF) — never change
- J1850 VPW: ATSP2, header 24XXYY (XX=target, YY=mode)
- K-Line ISO 14230: ATSP5, KWP2000 framing
- 0xA0 = Driver Door (LEFT), 0xA1 = Passenger Door (RIGHT)
- 0x40 = Body Computer (NOT ABS — hazard/horn/lights/mirrors are here)
- 0x61 = Instrument Cluster (gauge test via SID 0x3A)
