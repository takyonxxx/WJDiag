# WJDiag — Jeep Grand Cherokee WJ 2.7 CRD Diagnostic Tool

## Vehicle: 2003 EU-spec WJ 2.7 CRD (OM612 / NAG1)

Qt6 cross-platform diagnostic application + ESP32-S2 ELM327 emulator.
All commands and responses verified from real vehicle PCAP captures (11 PCAPs, 2025-03-11/12).

## Complete Module Address Map (Scan Order, PCAP-Verified)

| # | Addr | Bus | Module | Real Vehicle Response |
|---|------|-----|--------|----------------------|
| 1 | 0x15 | K-Line | Engine ECU (Bosch EDC15C2 OM612) | OK — 9 actuators + 14-block live data |
| 2 | 0x20 | K-Line | Transmission (NAG1 722.6) | OK — 4 tests + 5-block live data |
| 3 | 0x28 | J1850 | ABS | OK — read + 12 valve tests + DTC |
| 4 | 0x58 | J1850 | ESP / Traction Control | OK — read + 50 live PIDs. DTC clear: NO DATA |
| 5 | 0x61 | J1850 | Instrument Cluster | OK — 11 LED + gauge tests (SID 0x3A) |
| 6 | 0xC0 | J1850 | SKIM / Immobilizer | OK — reset + VIN + key program |
| 7 | 0x40 | J1850 | Body Computer | OK — 14 relays + mode 0xB4 config |
| 8 | 0x98 | J1850 | HVAC / ATC / Memory Seat | OK — 10 motor tests |
| 9 | 0xA0 | J1850 | Driver Door (left windows) | OK — 16 actuators |
| 10 | 0xA1 | J1850 | Passenger Door (right windows) | OK — 15 actuators + RKE |
| 11 | 0x60 | J1850 | Electro Mech Cluster | NRC 7F 22 22 on all commands |
| 12 | 0x68 | J1850 | Overhead Console | OK — self test + reset |
| 13 | 0x6D | J1850 | Navigation | `62 00 00 00` on all reads |
| 14 | 0x80 | J1850 | Radio | NO DATA |
| 15 | 0x81 | J1850 | CD Changer | `62 00 00 00` on all reads |
| 16 | 0x62 | J1850 | Park Assist | `62 00 00 00` on all reads |
| 17 | 0xA7 | J1850 | Rain Sensor | OK — read + DTC clear |
| 18 | 0x2A | J1850 | Adjustable Pedal | NO DATA |
| 19 | 0x87 | J1850 | Satellite Audio | `62 00 00 00` on all reads |
| 20 | 0x90 | J1850 | Hands Free / Uconnect | `62 00 00 00` on all reads |

20 modules total. All connectable.

## Real Vehicle PCAP Findings

- ABS 0x28 PID `20 00 00`: Valid response `62 56 04 00`, NOT NRC
- ESP 0x58: Background bus traffic (`B8 58 02 2B`, `2D 58 00 40`, `A3 58 00 89`)
- ESP 0x58 DTC Clear (mode 0x14): Returns NO DATA
- ECU Block 0x62: Values vary between sessions
- ECU DTC: 1 DTC (0x0702)
- TCM DTC Clear: First attempt returns `7F 14 78`, retry needed
- 0x60: ALL commands return NRC `7F 22 22`

## Controls Tab

See [RELAY_MAP.md](RELAY_MAP.md) for full command reference.

### Windows
Both doors: `38 PID 12` ON, `38 PID 00` OFF.
PID 0x01=Front Up, 0x02=Front Down, 0x03=Rear Up, 0x04=Rear Down.

### Body Computer 0x40
Hazard: `38 06 20`, Horn: `38 0D 01`, Hi Beam: `38 06 08`, Park: `38 06 04`

### Cluster 0x61 Gauge Test
SID 0x3A: `3A 00 80`=Speedo, `3A 00 40`=Tacho, `3A 00 08`=Fuel, `3A 00 04`=Temp

## ECU Security — ArvutaKoodi

4-table lookup: T1-T4 (16 bytes each). See RELAY_MAP.md for algorithm.
TCM: Static seed `68 24 89` -> Key `CC 21`

## Live Data

ECU: 14 blocks + 4 security + ATRV per cycle
TCM: 5 blocks + ATRV per cycle
See RELAY_MAP.md for byte offset/formula tables.

## DTC

K-Line: `18 02 00 00` read, `14 00 00` clear
J1850: `ATSH24xx18` + `FF 00 00` read, `ATSH24xx14` + `FF 00 00` clear
ESP 0x58 DTC clear: NO DATA

## ESP32-S2 Emulator

WiFi AP "WiFi_OBDII", IP 192.168.0.10, TCP 35000. All responses matched to real vehicle PCAPs.

## J1850 DTC Read — PID Scan Method (NEW)

WJDiag Pro and this app read J1850 DTCs via **PID scanning**, not mode 0x18.
Mode 0x18 returns NRC on all WJ J1850 modules (ABS, ESP, Body, etc).

**How it works:**
1. Switch to module's read header: `ATSH24xx22` + `ATRAxx`
2. Send each DTC PID: `2E xx 00` (or `2F xx 00` for ESP extended range)
3. Parse response: `26 <src> 62 <D0> <D1> <D2> <CRC>`
4. If D0:D1 != 0x0000 and != 0xFFFF → DTC is active at that PID slot
5. DTC code is mapped from the PID number via a lookup table

**Response patterns:**
- `00 00 00` or `00 00 FF` → No fault (cleared)
- `00 FF FF` → Unlearned (slot never had a fault)
- `00 8F 00` → Active fault, occurrence count = 0x8F
- `7F 22 21` → NRC (PID not supported)

**DTC PID counts per module (PCAP verified):**
- ABS 0x28: 17 PIDs (2E 10~2E 30)
- ESP 0x58: 55 PIDs (2E 10~2F 3C, 3-step increments) — dtc.pcap verified all 55
- Body 0x40: 7 PIDs (2E 00~2E 12)
- HVAC 0x98: 4 PIDs (2E 03~2E 06)
- Overhead 0x68: 3 PIDs (2E 02~2E 08)
- Rain 0xA7: 1 PID (2E 10)
- SKIM 0xC0: 1 PID (2E 00)

See [RELAY_MAP.md](RELAY_MAP.md) for full PID→DTC code mapping tables.

**DTC clear** uses mode 0x14:
- ESP 0x58: `ATSH245814` + `01 00 00` (retry up to 7x, NO DATA is normal before success)
- Others: `ATSH24xx14` + `FF 00 00`

## Raw Data Test Button

Log tab "Raw Data" button runs a comprehensive vehicle scan including:
- K-Line ECU/TCM: identification, security, live data blocks, actuators, DTC
- J1850 all 20 modules: identification, live data, actuators, DTC
- **DTC PID Scan**: 113 PID scan steps across 10 modules (7 known + 3 discovery)
- Discovery scan for DriverDoor, PassengerDoor, Cluster (unknown DTC PIDs)

Use **COPY LOG** after test to capture all results for analysis.
