# WJdiagPro v28 — COMPLETE Relay Command Map
## Extracted from libnative-lib.so (x86_64)

**Source:** WJdiag-Pro.apk (non-split, with embedded .so)
**All hex commands verified from binary offsets 0x83c00-0x842ff**

---

## DriverDoor Module — APK Address 0x40

**Session:** `ATSH244011` → `01 01 00`
**IOControl:** `ATSH24402F`
**Monitor:** `ATSH2440B4` → `28 07` (motor current), `28 3F 00` (position)

| # | Actuator (APK name) | ON Command | OFF Command |
|---|---|---|---|
| 1 | DdDriverFrontWindowDownRelay | `38 08 01` | `38 08 00` |
| 2 | DdDriverFrontWindowUpRelay | `38 07 01` | `38 07 00` |
| 3 | DdDriverLockRelay | `38 06 02` | `38 06 00` |
| 4 | DdDriverMirrorDown | `38 02 01` | `38 02 00` |
| 5 | DdDriverMirrorHeater | `38 06 08` | `38 06 00` |
| 6 | DdDriverMirrorLeft | `38 06 10` | `38 06 00` |
| 7 | DdDriverMirrorRight | `38 06 20` | `38 06 00` |
| 8 | DdDriverMirrorUp | `38 0C 02` | `38 0C 00` |
| 9 | DdDriverRearWindowDownRelay | `38 08 02` | `38 08 00` |
| 10 | DdDriverRearWindowUpRelay | `38 06 04` | `38 06 00` |
| 11 | DdDriverSwitchIllumination | `38 0D 01` | `38 0D 00` |
| 12 | DdDriverUnlockRelay | `3A 02 FF` | — |
| 13 | DdCourtesyLamp | (in code logic) | |
| 14 | DdDriverFoldawayIn | (in code logic) | |
| 15 | DdDriverFoldawayOut | (in code logic) | |
| 16 | DdSetMemoryLED | (in code logic) | |

**NOTE:** Register `06` is shared bitmask: Lock=02, MirrorHeat=08, MirrorL=10, MirrorR=20, RearWinUp=04.

---

## PassengerDoor Module — APK Address 0xA0

**IOControl:** `ATSH24A02F`

| # | Actuator (APK name) | ON Command | OFF Command |
|---|---|---|---|
| 0 | PdDriverFrontWindowDownRelay | `38 00 12` | `38 00 00` |
| 1 | PdDriverFrontWindowUpRelay | `38 01 12` | `38 01 00` |
| 2 | PdDriverLockRelay | `38 02 12` | `38 02 00` |
| 3 | PdDriverMirrorDown | `38 03 12` | `38 03 00` |
| 4 | PdDriverMirrorHeater | `38 04 12` | `38 04 00` |
| 5 | PdDriverMirrorLeft | `38 05 12` | `38 05 00` |
| 6 | PdDriverMirrorRight | `38 06 12` | `38 06 00` |
| 7 | PdDriverMirrorUp | `38 07 12` | `38 07 00` |
| 8 | PdDriverRearWindowDownRelay | `38 08 12` | `38 08 00` |
| 9 | PdDriverRearWindowUpRelay | `38 09 12` | `38 09 00` |
| 10 | PdDriverSwitchIllumination | `38 0A 12` | `38 0A 00` |
| 11 | PdDriverUnlockRelay | `38 0B 12` | `38 0B 00` |
| 12 | PdDriverFoldawayIn | `38 0C 12` | `38 0C 00` |
| 13 | PdDriverFoldawayOut | `38 0D 12` | `38 0D 00` |
| 14 | (Unknown) | `38 0E 12` | `38 0E 00` |
| 15 | (Unknown) | `38 0F 12` | `38 0F 00` |

**Pattern:** Sequential index 0x00-0x0F with fixed value `0x12`.

---

## Liftgate Module — APK Address 0xA1

**IOControl:** `ATSH24A12F`

| # | Actuator | ON Command | OFF Command |
|---|---|---|---|
| 1 | | `38 01 12` | `38 01 00` |
| 2 | | `38 02 12` | `38 02 00` |
| 3 | | `38 03 12` | `38 03 00` |
| 4 | | `38 04 12` | `38 04 00` |
| 5 | | `38 05 12` | `38 05 00` |
| 6 | | `38 06 12` | `38 06 00` |
| 7 | | `38 07 12` | `38 07 00` |
| 8 | | `38 08 12` | `38 08 00` |
| 9 | | `38 09 12` | `38 09 00` |
| 10 | | `38 0A 12` | `38 0A 00` |
| 11 | | `38 0B 12` | `38 0B 00` |

---

## BCM (Body Computer) — APK Address 0x80

### Mode 0x2F — Direct IOControl

**IOControl:** `ATSH24802F`

| # | Actuator (APK name) | ON Command | OFF Command | Notes |
|---|---|---|---|---|
| 1 | BodyHazardflashers | `38 01 00` | `38 01 01` | INVERTED! |
| 2 | BodyHiBeamrelay | `38 00 FF` | `38 00 00` | |
| 3 | BodyHornrelay | `38 00 CC` | `38 00 00` | |
| 4 | BodyLowbeamrelay | `38 02 05` | `38 02 00` | |
| 5 | BodyParklamprelay | `38 09 00` | `38 09 01` | INVERTED! |

### Mode 0xB4 — Extended Actuators

**Pre-req:** `ATSH248022` → `28 0D 00` (read), then `ATSH2480B4` → `28 0D 01` (activate)

| # | Actuator (APK name) | ON Command | OFF Command |
|---|---|---|---|
| 1 | BodyRDefogrelay | `38 02 02` | `38 02 00` |
| 2 | BodyRearfoglamprelay | `38 09 01` | `38 09 00` |
| 3 | BodyVTSSlamp | `38 04 01` | `38 04 00` |
| 4 | BodyHILOWwiperrelay | `38 04 02` | `38 04 00` |
| 5 | BodyFrontFogLamps | `38 02 04` | `38 02 00` |
| 6 | BodyViperrelay (wiper single) | `38 04 03` | `38 04 00` |
| 7 | BodyChime | `38 02 03` | `38 02 00` |
| 8 | BodyEUtuled (EU Daylights) | `38 04 04` | `38 04 00` |

### Special Functions
- **BodyReset:** `ATSH246031` (ECU Reset)
- **BodyChangeCountryCode:** Special write function via `BodyCountrycodeTagJNI`

---

## Cluster (Electro Mech Cluster) — 0x90

### Mode 0x2F — Gauge Motor Test
**IOControl:** `ATSH24902F`

| # | Command | Function |
|---|---|---|
| 1 | `38 01 01` | Gauge Test 1 |
| 2 | `38 01 02` | Gauge Test 2 |

**Session:** `ATSH249011` (for DiagSession)

### Mode 0x22 — Gauge Self-Test
**Header:** `ATSH249022`

| # | ON | OFF | Function |
|---|---|---|---|
| 1 | `3A 00 80` | `3A 00 00` | Speedometer |
| 2 | `3A 00 40` | `3A 00 00` | Tachometer |
| 3 | `3A 00 20` | `3A 00 00` | Fuel Gauge |
| 4 | `3A 00 10` | `3A 00 00` | Temp Gauge |
| 5 | `3A 00 08` | `3A 00 00` | Volts Gauge |
| 6 | `3A 00 04` | `3A 00 00` | (Unknown) |
| 7 | `3A 00 02` | `3A 00 00` | (Unknown) |
| 8 | `3A 00 01` | `3A 00 00` | (Unknown) |
| 9 | `3A 01 01` | `3A 01 00` | Oil Lamp |
| 10 | `3A 01 02` | `3A 01 00` | ABS Lamp |
| 11 | `3A 01 04` | `3A 01 00` | Check Engine Lamp |

---

## Radio — 0x87

**IOControl:** `ATSH24872F`

| # | ON | OFF | Function |
|---|---|---|---|
| 1 | `38 18 01` | `38 18 00` | Mute |
| 2 | `38 0D 02` | `38 0D 00` | Volume Up |
| 3 | `38 0D 03` | `38 0D 00` | Volume Down |
| 4 | `38 0A 01` | `38 0A 00` | Bass Test |

---

## Memory Seat — 0x98

**Session:** `ATSH249811`
**Read:** `ATSH249822`
**Save:** `ATSH249830` → `01 FF FF`, `02 FF FF`, `03 FF FF`
**IOControl:** `ATSH24982F`

| # | Command | Motor |
|---|---|---|
| 1 | `38 03 00` | Tilt Forward |
| 2 | `38 04 00` | Tilt Back |
| 3 | `38 07 00` | Slide Forward |
| 4 | `38 08 00` | Slide Back |
| 5 | `38 0B 00` | Recline Forward |
| 6 | `38 0C 00` | Recline Back |
| 7 | `38 0F 00` | (Unknown) |
| 8 | `38 10 00` | (Unknown) |

---

## VTSS (Vehicle Theft Security) — 0xC0

**Session:** `ATSH24C011`
**IOControl:** `ATSH24C02F` → `38 00 01`
**Monitor:** `ATSH24C0B4` → `28 03 00`
**Read PIDs:** `28 11 00` through `28 20 00` (16 PIDs)
**Security:** `ATSH24C027`

---

## EVIC (Overhead Console) — 0x2A

**IOControl:** `ATSH242A2F` → `38 00 04`, `38 00 14`
**Read:** `ATSH242A22` → `2A 0A 00`
**Write:** `ATSH242AB7` → `2A 0A FF 00 00` (set) / `2A 0A 00 00 00` (clear)

---

## SKIM — 0x62

**Session:** `ATSH246211`
**Functions:** SkimLamp, SkimReset

---

## Airbag — 0x60

**Read:** `ATSH246022` + `ATRA60` → `28 37 00`
**Extended:** `ATSH2460B4` → `28 37 01`
**Security:** `ATSH246027` → `7F 00 00`, `80 08 09`
**DTC Clear:** `ATSH2460A3` → `0D 10 00`
**Reset:** `ATSH246031`

---

## EU WJ 2.7 CRD Mapping Notes

On EU LHD vehicles:
- **0xA0 = LEFT/DRIVER door** (APK calls "Passenger Door")
- **0x40 = ABS module** (APK calls "Driver Door")
- **Right door has NO J1850 module** — controlled via BCM hardwire

For your Qt app Controls tab, use:
- `ATSH24A02F` with PassengerDoor commands (`38 xx 12`) for LEFT door
- DriverDoor 0x40 commands will NOT work on EU (that's ABS)
- BCM 0x80 may not respond on EU spec — test with DiagSession first
