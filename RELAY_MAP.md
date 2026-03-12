# WJ 2.7 CRD — Complete J1850 Actuator Relay Map
## Verified from real vehicle PCAP captures (2026-03-12)

**All responses verified from real vehicle WiFi ELM327 PCAP captures.**

## Module Map — EU WJ 2.7 CRD 2003

| Addr | Module | J1850 Read | Relay (0x2F) | Status |
|------|--------|-----------|--------------|--------|
| 0x28 | TCM (J1850) | YES | - | Read OK, some PIDs NRC |
| 0x2A | EVIC/Overhead | **NO DATA** | - | Not on J1850 bus |
| 0x40 | ABS/DriverDoor | YES | YES (bitmask) | Full relay control verified |
| 0x58 | ESP/Traction | YES | - | NEW! 61 PIDs discovered |
| 0x60 | Airbag | NRC 0x22 | - | conditionsNotCorrect always |
| 0x61 | Compass/Traveler | YES | - | NEW! Has DTCs |
| 0x68 | HVAC | YES | - | 13 PIDs verified |
| 0x80 | BCM | **NO DATA** | untested | Not on J1850 read bus |
| 0x98 | Memory Seat | YES (DTC only) | - | Only 24 00 00 responds |
| 0xA0 | Door (LEFT/Driver) | YES | YES (sequential 0x12) | All 16 PIDs verified |
| 0xA1 | Liftgate/HandsFree | YES | YES (intermittent) | Some NO DATA on retries |
| 0xA7 | Siren/Security | YES | - | NEW! Has DTCs |
| 0xC0 | VTSS/Park Assist | YES | - | 4 PIDs verified |

## DriverDoor Module 0x40 — Mode 0x2F (Bitmask)

Session: `ATSH244022` -> read, `ATSH24402F` -> relay control
No DiagSession needed — relay works directly.

| Actuator | ON Command | Response | Notes |
|----------|-----------|----------|-------|
| MirrorRight | `38 06 20` | `26 40 6F 38 06 20 68` | Reg 06 bitmask |
| FrontWinDown | `38 08 01` | `26 40 6F 38 08 01 1D` | |
| FrontWinUp | `38 07 01` | `26 40 6F 38 07 01 BE` | |
| Lock | `38 06 02` | `26 40 6F 38 06 02 D5` | Reg 06 bitmask |
| MirrorDown | `38 02 01` | `26 40 6F 38 02 01 DF` | |
| MirrorHeater | `38 06 08` | `26 40 6F 38 06 08 07` | Reg 06 bitmask |
| MirrorLeft | `38 06 10` | `26 40 6F 38 06 10 22` | Reg 06 bitmask |
| RearWinDown | `38 08 02` | `26 40 6F 38 08 02 3A` | |
| RearWinUp | `38 06 04` | `26 40 6F 38 06 04 9B` | Reg 06 bitmask |
| Illumination | `38 0D 01` | `26 40 6F 38 0D 01 7C` | |
| Release All | `3A 02 FF` | `26 40 6F 3A 02 FF 05` | |

Register 0x06 is shared bitmask: Lock=02, RearWinUp=04, MirrorHeat=08, MirrorL=10, MirrorR=20

## PassengerDoor Module 0xA0 — Mode 0x2F (Sequential 0x12)

Session: `ATSH24A022` -> read, `ATSH24A02F` -> relay control
All 16 PIDs (0x00-0x0F) verified with real CRC from vehicle.

| PID | Actuator | ON | OFF | Real CRC |
|-----|----------|----|----|----------|
| 0x00 | FrontWinDown | `38 00 12` | `38 00 00` | D0 / 27 |
| 0x01 | FrontWinUp | `38 01 12` | `38 01 00` | 9C / 6B |
| 0x02 | Lock | `38 02 12` | `38 02 00` | 48 / BF |
| 0x03 | MirrorDown | `38 03 12` | `38 03 00` | 04 / F3 |
| 0x04 | MirrorHeater | `38 04 12` | `38 04 00` | FD / 0A |
| 0x05 | MirrorLeft | `38 05 12` | `38 05 00` | B1 / 46 |
| 0x06 | MirrorRight | `38 06 12` | `38 06 00` | 65 / 92 |
| 0x07 | MirrorUp | `38 07 12` | `38 07 00` | 29 / DE |
| 0x08 | RearWinDown | `38 08 12` | `38 08 00` | 8A / 7D |
| 0x09 | RearWinUp | `38 09 12` | `38 09 00` | C6 / 31 |
| 0x0A | SwitchIllum | `38 0A 12` | `38 0A 00` | 12 / E5 |
| 0x0B | Unlock | `38 0B 12` | `38 0B 00` | 5E / A9 |
| 0x0C | FoldawayIn | `38 0C 12` | `38 0C 00` | A7 / 50 |
| 0x0D | FoldawayOut | `38 0D 12` | `38 0D 00` | EB / 1C |
| 0x0E | Unknown | `38 0E 12` | `38 0E 00` | 3F / C8 |
| 0x0F | Unknown | `38 0F 12` | `38 0F 00` | 73 / 84 |

## Liftgate Module 0xA1 — Mode 0x2F (Sequential 0x12, Intermittent)

Some PIDs return NO DATA on first attempt — retries needed.

| PID | Actuator | Response (when OK) | Notes |
|-----|----------|--------------------|-------|
| 0x01 | ? | `26 A1 6F 38 01 12 F6` | OK |
| 0x02 | ? | sometimes NO DATA | Intermittent |
| 0x04 | ? | sometimes NO DATA | Intermittent |
| 0x05 | ? | `26 A1 6F 38 05 12 DB` | OK |
| 0x06-0x0E | ? | positive | Most respond |

## ECU Actuator Control (K-Line SID 0x30)

Format: `30 PID 07 VALUE_HI VALUE_LO` -> `70 PID 07 VALUE_HI VALUE_LO`
Security unlock NOT required (seed=0000 when already in session).

| PID | ON Value | OFF Value | Description |
|-----|----------|-----------|-------------|
| 0x11 | 13 88 (5000) | 00 00 | RPM limiter? |
| 0x1C | 27 10 (10000) | 00 00 | Unknown |
| 0x16 | 27 10 | 00 00 | Unknown |
| 0x17 | 27 10 | 00 00 | Unknown |
| 0x14 | 27 10 | 00 00 | Unknown |
| 0x1A | 13 88 | 00 00 | Unknown |
| 0x12 | 00 10 | 00 00 | Unknown |
| 0x18 | 08 34 / 21 34 | 00 00 | Unknown |

## Critical Notes

- **0xA0 = LEFT/DRIVER door** (labeled "Passenger Door" in US-spec tools)
- **0x40 = ABS module with door relay capability** (labeled "Driver Door" in US-spec tools)
- **Right door has NO J1850 module** — hardwired via BCM/master switch
- **BCM (0x80) = NO DATA** confirmed from real vehicle PCAP
- **EVIC (0x2A) = NO DATA** confirmed from real vehicle PCAP
- **Airbag (0x60) = NRC 0x22** always — needs special conditions (ignition off?)
- **ECU block 0x62 = LIVE DATA** (EB ED vs 8A 79 between sessions) not static calibration
