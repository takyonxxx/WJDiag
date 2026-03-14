# WJDiag — Jeep Grand Cherokee WJ 2.7 CRD Diagnostic Tool

## Vehicle: 2003 EU-spec WJ 2.7 CRD (OM612 / NAG1)

Qt6 cross-platform diagnostic application + ESP32-S2 ELM327 emulator.
All commands verified from real vehicle PCAP captures and simulator testing.

## Complete Module Address Map (Scan Order)

Exact order from full vehicle module scan. All addresses confirmed via PCAP.

| # | Addr | Bus | Module | Status |
|---|------|-----|--------|--------|
| 1 | 0x15 | K-Line | Engine ECU (Bosch EDC15C2 OM612) | Full + 9 actuators + 14-block live data |
| 2 | 0x20 | K-Line | Transmission (NAG1 722.6) | Full + 4 tests + 5-block live data |
| 3 | 0x28 | J1850 | ABS | 12 valve tests + DTC |
| 4 | 0x58 | J1850 | Airbag / ESP | Read + 50 live PIDs |
| 5 | 0x61 | J1850 | Instrument Cluster | 11 LED + 6 gauge tests (SID 0x3A) |
| 6 | 0xC0 | J1850 | SKIM / Immobilizer | Reset + VIN + key program |
| 7 | 0x40 | J1850 | Body Computer | 14 relays + mode 0xB4 config |
| 8 | 0x98 | J1850 | HVAC / ATC / Memory Seat | 10 motor tests + cross-read 0x40 |
| 9 | 0xA0 | J1850 | Driver Door (left windows) | 16 actuators |
| 10 | 0xA1 | J1850 | Passenger Door (right windows) | 15 actuators + RKE program |
| 11 | 0x60 | J1850 | Electro Mech Cluster | NRC always on EU vehicle |
| 12 | 0x68 | J1850 | Overhead Console | Self test + reset |
| 13 | 0x6D | J1850 | Navigation | Not present |
| 14 | 0x80 | J1850 | Radio | NO DATA |
| 15 | 0x81 | J1850 | CD Changer | Not present |
| 16 | 0x62 | J1850 | Park Assist | Not present |
| 17 | 0xA7 | J1850 | Rain Sensor | Read + DTC clear |
| 18 | 0x2A | J1850 | Adjustable Pedal | NO DATA |
| 19 | 0x87 | J1850 | Satellite Audio | Not present |
| 20 | 0x90 | J1850 | Hands Free / Uconnect | Not present |

12 active modules (1-12, 17), 7 dead/not present (13-16, 18-20).

## Controls Tab

Detailed command reference: [RELAY_MAP.md](RELAY_MAP.md)

### Windows

| PID | Function |
|-----|----------|
| 0x01 | Front Window Up |
| 0x02 | Front Window Down |
| 0x03 | Rear Window Up |
| 0x04 | Rear Window Down |

Both doors use: `38 PID 12` ON, `38 PID 00` OFF.
Driver Door: `ATSH24A02F` + `ATRAA0` | Passenger: `ATSH24A12F` + `ATRAA1`

### Body Computer 0x40

| Function | Command |
|----------|---------|
| Hazard flashers | `38 06 20` / `38 06 00` |
| Horn relay | `38 0D 01` / `38 0D 00` |
| Hi Beam | `38 06 08` / `38 06 00` |
| Park lamp | `38 06 04` / `38 06 00` |

### Cluster Gauge Test (0x61)

SID 0x3A: `3A 00 80`=Speedo, `3A 00 40`=Tacho, `3A 00 08`=Fuel, `3A 00 04`=Temp, `3A 01 01`=Oil, `3A 01 04`=CE

## ECU Security Unlock (K-Line KWP2000)

### Algorithm — "ArvutaKoodi" (Bosch EDC15C2)

SID 0x27 challenge-response with lookup-table key derivation:

```
Request:  27 01           -> Response: 67 01 SEED_HI SEED_LO
Request:  27 02 KEY_HI KEY_LO -> Response: 67 02 34 (success)
```

**Key calculation:**

```c
// 4 lookup tables (16 bytes each)
T1 = {C0,D0,E0,F0,00,10,20,30,40,50,60,70,80,90,A0,B0}
T2 = {02,03,00,01,06,07,04,05,0A,0B,08,09,0E,0F,0C,0D}
T3 = {90,80,F0,E0,D0,C0,30,20,10,00,70,60,50,40,B0,A0}
T4 = {0D,0C,0F,0E,09,08,0B,0A,05,04,07,06,01,00,03,02}

s0 = SEED >> 8, s1 = SEED & 0xFF
v1 = (s1 + 0x0B) & 0xFF
KEY_LO = T1[v1 >> 4] | T2[v1 & 0x0F]
carry = (s1 > 0x34) ? 1 : 0
v2 = (s0 + carry + 1) & 0xFF
KEY_HI = T3[v2 >> 4] | T4[v2 & 0x0F]
```

### TCM Security

Static seed: `67 01 68 24 89` -> Key: `CC 21` (fixed key)

## ECU Live Data (K-Line 0x15)

14 blocks per cycle: `0x12 -> 0x30 -> 0x22 -> 0x20 -> 0x23 -> 0x21 -> 0x16 -> 0x32 -> 0x37 -> 0x13 -> 0x36 -> 0x26 -> 0x34 -> 0x28`
Plus security blocks: `0x62 -> 0xB0 -> 0xB1 -> 0xB2` + `ATRV`

See RELAY_MAP.md for full byte offset/formula tables.

## TCM Live Data (K-Line 0x20)

5 blocks per cycle: `0x30 -> 0x31 -> 0x34 -> 0x33 -> 0x32` + `ATRV`

## DTC Management

### K-Line (ECU 0x15 / TCM 0x20)
- Read: `18 02 00 00` (ECU) or `18 02 FF 00` (TCM)
- Clear: `14 00 00` -> `54 00 00`

### J1850 Modules (all supported)
- Read: `ATSH24xx18` + `ATRAxx` -> `FF 00 00`
- Clear: `ATSH24xx14` + `ATRAxx` -> `FF 00 00`

## ESP32-S2 Emulator

WiFi AP "WiFi_OBDII", IP 192.168.0.10, TCP port 35000, HTTP dashboard at port 80.

Features: full K-Line KWP2000 with security, per-module J1850 DTC with clear flags, all ECU/TCM live data blocks with realistic values, ATZ reset clears all flags.

## Protocol Notes

- CRC-16/MODBUS (poly=0xA001, init=0xFFFF) — immutable across all boards
- J1850 VPW: ATSP2, header format 24XXYY (XX=target, YY=mode)
- K-Line ISO14230: ATSP5, ATWM81XXF13E + ATSH81XXF1
- ATRA (receive address filter) mandatory for all J1850 commands
