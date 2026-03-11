# WJ 2.7 CRD — Complete J1850 Actuator Relay Map

All commands extracted from WJdiag Pro APK v28 native lib (x86_64 + ARM32).
Every module requires DiagnosticSession activation before IOControl commands work.

## Activation Sequence (REQUIRED)

```
ATSP2                        (set J1850 VPW protocol)
ATSH24TT11                   (DiagSessionControl header, TT=target)
ATRArr                       (receive filter, rr=target)
01 01 00                     (activate diagnostic session)
ATSH24TTMM                   (switch to IOControl/read mode, MM=mode)
38 PID VAL                   (relay command)
```

Without the DiagSession step, relay commands return positive response
but do NOT physically activate — this is a safety feature.

---

## DriverDoor 0x40

Session: `ATSH244011` → `ATRA40` → `01 01 00`
IOControl: `ATSH24402F`
Monitor: `ATSH2440B4` → `28 07` (motor current), `28 3F 00` (position)

| # | Actuator | ON | OFF |
|---|----------|----|----|
| 1 | Front Window Down | `38 08 01` | `38 08 00` |
| 2 | Front Window Up | `38 07 01` | `38 07 00` |
| 3 | Door Lock | `38 06 02` | `38 06 00` |
| 4 | Mirror Down | `38 02 01` | `38 02 00` |
| 5 | Mirror Heater | `38 06 08` | `38 06 00` |
| 6 | Mirror Left | `38 06 10` | `38 06 00` |
| 7 | Mirror Right | `38 06 20` | `38 06 00` |
| 8 | Mirror Up | `38 0C 02` | `38 0C 00` |
| 9 | Rear Window Down | `38 08 02` | `38 08 00` |
| 10 | Rear Window Up | `38 06 04` | `38 06 00` |
| 11 | Switch Illumination | `38 0D 01` | `38 0D 00` |
| 12 | Unlock (release all) | `3A 02 FF` | — |
| 13 | Courtesy Lamp | (separate function) | |
| 14 | Mirror Foldaway In | (separate function) | |
| 15 | Mirror Foldaway Out | (separate function) | |
| 16 | Set Memory LED | (separate function) | |

Note: Register `06` is shared — Lock(02), MirrorHeat(08), MirrorL(10),
MirrorR(20), RearWinUp(04) all use `38 06 xx`. OFF `38 06 00` clears all.

---

## PassengerDoor 0xA0

Session: `ATSH24A011` → `ATRAA0` → `01 01 00`
IOControl: `ATSH24A02F`

| # | Actuator | ON | OFF |
|---|----------|----|----|
| 1 | Front Window Down | `38 00 12` | `38 00 00` |
| 2 | Front Window Up | `38 01 12` | `38 01 00` |
| 3 | Door Lock | `38 02 12` | `38 02 00` |
| 4 | Mirror Down | `38 03 12` | `38 03 00` |
| 5 | Mirror Heater | `38 04 12` | `38 04 00` |
| 6 | Mirror Left | `38 05 12` | `38 05 00` |
| 7 | Mirror Right | `38 06 12` | `38 06 00` |
| 8 | Mirror Up | `38 07 12` | `38 07 00` |
| 9 | Rear Window Down | `38 08 12` | `38 08 00` |
| 10 | Rear Window Up | `38 09 12` | `38 09 00` |
| 11 | Switch Illumination | `38 0A 12` | `38 0A 00` |
| 12 | Unlock | `38 0B 12` | `38 0B 00` |
| 13 | Mirror Foldaway In | `38 0C 12` | `38 0C 00` |
| 14 | Mirror Foldaway Out | `38 0D 12` | `38 0D 00` |
| 15 | Unknown (0x0E) | `38 0E 12` | `38 0E 00` |
| 16 | Unknown (0x0F) | `38 0F 12` | `38 0F 00` |

---

## BCM (Body Computer) 0x80

### Mode 0x2F — Direct IOControl

Session: `ATSH248011` → `ATRA80` → `01 01 00`
IOControl: `ATSH24802F`

| # | Actuator | ON | OFF |
|---|----------|----|----|
| 1 | Hazard Flashers | `38 01 00` | `38 01 01` |
| 2 | Hi-Beam Relay | `38 00 FF` | `38 00 00` |
| 3 | Horn | `38 00 CC` | `38 00 00` |
| 4 | Low-Beam Relay | `38 02 05` | `38 02 00` |
| 5 | Park Lamp Relay | `38 09 00` | `38 09 01` |

Note: Hazard and Park Lamp have INVERTED ON/OFF values.

### Mode 0xB4 — Extended Actuators (requires read session first)

Pre-req: `ATSH248022` → `28 0D 00`, then `ATSH2480B4` → `28 0D 01`

| # | Actuator | ON | OFF |
|---|----------|----|----|
| 1 | Rear Defog Relay | `38 02 02` | `38 02 00` |
| 2 | Rear Fog Lamp | `38 09 01` | `38 09 00` |
| 3 | VTSS Lamp | `38 04 01` | `38 04 00` |
| 4 | HI/LOW Wiper Relay | `38 04 02` | `38 04 00` |
| 5 | Front Fog Lamps | `38 02 04` | `38 02 00` |
| 6 | Wiper Relay (single) | `38 04 03` | `38 04 00` |
| 7 | Chime | `38 02 03` | `38 02 00` |
| 8 | EU Lights (tuled) | `38 04 04` | `38 04 00` |

### Mode 0xA3 — Alternative (Free APK only)

Header: `ATSH2480A3`

| # | Command | Function |
|---|---------|----------|
| 1 | `38 04 02` | Wiper |
| 2 | `32 03 00` | Read |
| 3 | `36 0E 00` | Read |
| 4 | `2A 0C 00` | Read |

Note: BCM 0x80 returned NO DATA on EU-spec WJ in all previous tests.
DiagSession activation (mode 0x11) may fix this — pending verification.
BodyReset and BodyChangeCountryCode are special functions (not relay toggle).

---

## Cluster (Electro Mech Cluster) 0x90

### Mode 0x2F — Gauge Motor Test

Session: `ATSH249011` (session) then `ATSH24902F`

| # | Actuator | Command |
|---|----------|---------|
| 1 | Gauge Test 1 | `38 01 01` |
| 2 | Gauge Test 2 | `38 01 02` |

### Mode 0x22 — Gauge Self-Test (individual)

Header: `ATSH249022`

| # | Gauge/Lamp | ON | OFF |
|---|-----------|----|----|
| 1 | Speedometer | `3A 00 80` | `3A 00 00` |
| 2 | Tachometer | `3A 00 40` | `3A 00 00` |
| 3 | Fuel Gauge | `3A 00 20` | `3A 00 00` |
| 4 | Temp Gauge | `3A 00 10` | `3A 00 00` |
| 5 | Volts Gauge | `3A 00 08` | `3A 00 00` |
| 6 | Unknown (0x04) | `3A 00 04` | `3A 00 00` |
| 7 | Unknown (0x02) | `3A 00 02` | `3A 00 00` |
| 8 | Unknown (0x01) | `3A 00 01` | `3A 00 00` |
| 9 | Oil Lamp | `3A 01 01` | `3A 01 00` |
| 10 | ABS Lamp | `3A 01 02` | `3A 01 00` |
| 11 | Check Engine Lamp | `3A 01 04` | `3A 01 00` |

Note: `3A 00 xx` = gauge bitmask (multiple can be active), `3A 01 xx` = lamp bitmask.

---

## Radio 0x87

### Mode 0x2F — IOControl

Header: `ATSH24872F`

| # | Function | ON | OFF |
|---|----------|----|----|
| 1 | Mute | `38 18 01` | `38 18 00` |
| 2 | Volume Up | `38 0D 02` | `38 0D 00` |
| 3 | Volume Down | `38 0D 03` | `38 0D 00` |
| 4 | Bass Test | `38 0A 01` | `38 0A 00` |

Additional Radio functions (from JNI names — commands in native code):
- RadioDisplayTest / RadioDisplayTestEnd
- RadioClockDisplay / RadioClockDisplayEnable / RadioClockDisplayDisable
- RadioFMSeek / RadioFMTuneDown
- RadioCDFWD / RadioCDRWD / RadioCDEject / RadioCDSearchFWD
- RadioCDChangerFWD / RadioCDChangerRWD / RadioCDChangerSearchFWD
- RadioCassetteFWD / RadioCassetteRWD / RadioCassetteEject
- RadioCassetteDirection / RadioCassetteSearchFWD
- RadioAutomaticLoudnessEnable
- RadioEndHrz / RadioReset

---

## Memory Seat 0x98

### Mode 0x30 — Save Memory Positions

Header: `ATSH249830`

| # | Command | Function |
|---|---------|----------|
| 1 | `01 FF FF` | Save Position 1 |
| 2 | `02 FF FF` | Save Position 2 |
| 3 | `03 FF FF` | Save Position 3 |

### Mode 0x2F — Motor Control

Session: `ATSH249811` then `ATSH24982F`

| # | Motor | Activate | Stop |
|---|-------|----------|------|
| 1 | Tilt Forward | `38 03 00` | release |
| 2 | Tilt Back | `38 04 00` | release |
| 3 | Slide Forward | `38 07 00` | release |
| 4 | Slide Back | `38 08 00` | release |
| 5 | Recline Forward | `38 0B 00` | release |
| 6 | Recline Back | `38 0C 00` | release |
| 7 | Unknown (0x0F) | `38 0F 00` | release |
| 8 | Unknown (0x10) | `38 10 00` | release |

---

## VTSS (Vehicle Theft Security) 0xC0

Session: `ATSH24C011` then `ATSH24C02F`

| # | Command | Function |
|---|---------|----------|
| 1 | `38 00 01` | Arm/Disarm |

Monitor (mode 0xB4): `ATSH24C0B4` → `28 03 00`
Read PIDs: `28 11 00` through `28 20 00` (16 PIDs)

---

## EVIC 0x2A

### Mode 0x2F — IOControl

Header: `ATSH242A2F`

| # | Command | Function |
|---|---------|----------|
| 1 | `38 00 04` | EVIC Test 1 |
| 2 | `38 00 14` | EVIC Test 2 |

### Mode 0xB7 — Write

Header: `ATSH242AB7`

| # | Command | Function |
|---|---------|----------|
| 1 | `2A 0A FF 00 00` | Set value |
| 2 | `2A 0A 00 00 00` | Clear value |

---

## SKIM (Sentry Key) 0x62

Session: `ATSH246211`

Functions: SkimLamp, SkimProg, SkimReset, SkimView

---

## Airbag 0x60

### Mode 0x22 — Read

Header: `ATSH246022` + `ATRA60`
Command: `28 37 00`

### Mode 0xB4 — Extended Read

Header: `ATSH2460B4`
Command: `28 37 01`

### Mode 0x27 — Security

Header: `ATSH246027`
Commands: `7F 00 00`, `80 08 09`

### Mode 0xA3 — DTC Clear

Header: `ATSH2460A3`
Command: `0D 10 00`

### Mode 0x31 — Reset

Header: `ATSH246031`

---

## Overhead Console 0x28

### Mode 0x30 — Compass/Settings Write

Header: `ATSH242830`

| # | Command | Function |
|---|---------|----------|
| 1 | `01 FE FF` | Setting 1 |
| 2 | `01 FB FF` | Setting 2 |
| 3 | `01 EF FF` | Setting 3 |
| 4 | `01 BF FF` | Setting 4 |
| 5 | `01 FD FF` | Setting 5 |
| 6 | `01 F7 FF` | Setting 6 |
| 7 | `01 DF FF` | Setting 7 |
| 8 | `01 7F FF` | Setting 8 |
| 9 | `01 FF BF` | Setting 9 |
| 10 | `01 55 40` | Compass Cal Start |
| 11 | `01 FF 40` | Compass Cal Mid |
| 12 | `01 FF 00` | Compass Cal End |
| 13 | `36 00 00` | Read setting |
| 14 | `2E 0D 00` | Read config |

### Other OHC modes

- `ATSH242811` → `01 02 00` (DiagSession)
- `ATSH242810` → `02 00 00` (ECUReset)
- `ATSH2428A3` → `00 BD 00` (special read)
- `ATSH242820` → `00 00 00` (status read)
- `ATSH2428A0` → `00 BD 02`, `00 BE`, `00 64 00` (extended read)
- `ATSH242814` → SID 0x32/0x36 reads (live data)

---

## Liftgate 0xA1

Header: `ATSH24A131` → `00 00` (reset)
Read: `ATSH24A122`, `ATSH24A133`

---

## Satellite Audio Receiver

Functions: SarToneOn, SarToneOff

---

## Hands Free Module

Read only: HandsFreeModuleKoodi/Namesi/Paljuv/TagasiPci

---

## Park Assist

Functions: ParkAssistReset
Read: ParkAssistModuleKoodi/Namesi/Paljuv/TagasiPci

---

## ABS 0x40

### Mode 0x22 — Comprehensive Read

Header: `ATSH244022` + `ATRA40`

**SID 0x20** (Status): `20 00-08, 0B, 10, 12-13, 15`
**SID 0x24** (DTC): `24 00 00`, `24 0B 00`
**SID 0x28** (Extended): `28 00-02, 04, 07, 0A, 10`
**SID 0x2E** (Data): `2E 00-27, 30, 50-54`
**SID 0x2F** (IOControl): `2F 01-08, 0A, 0C, 12-29`
**SID 0x2A**: `2A 1F 00`
**Special**: `01 00 00` (reset), `FF 00 00` (clear)

Pinion Factor: `ATSH244022` → `2E 07 00` (read) / `2E 0A 00` (write)

---

## HVAC (Automatic Temp Control) 0x68

Headers: `ATSH246822` (read), `ATSH246831` (routine), `ATSH246833` (IO), `ATSH246811` (session)

v28 APK added 52 new PIDs: SID 0x28 (26 PIDs: 0x00-0x0F, 0x41-0x44, 0x46-0x48, 0x50-0x52)
and SID 0x29 (26 PIDs: 0x00-0x04, 0x10-0x13, 0x20-0x24, 0x30-0x34, 0x40-0x42, 0x51-0x53, 0x55)
