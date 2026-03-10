# Jeep Grand Cherokee WJ 2.7 CRD - Diagnostic Protocol Analysis

**Last updated: 2025-03-10** (test v10 + WJDiag-Pro APK reverse engineered)

## Architecture

| Bus | Protocol | ELM327 | Modules |
|-----|----------|--------|---------|
| **K-Line** | ISO 14230-4 (KWP2000) | ATSP5 | Engine ECU (0x15), TCM (0x20) |
| **J1850 VPW** | SAE J1850 VPW | ATSP2 | ABS, Airbag, BCM, HVAC, Cluster, MemSeat, Radio, SKIM, Overhead, VTSS, Liftgate |

## J1850 Module Map (from WJDiag-Pro APK)

Each module has a unique target address. Header format: `ATSH24<target><mode>`
- mode `22` = data read, `11` = ECUReset/DTC clear, `2F` = IOControl
- mode `27` = security access, `30` = routine, `31` = start routine
- mode `A0/A3/B4` = download/upload/transfer

**CRITICAL: Each module uses a different SID (Service ID) for data read!**

| Module | Addr | ATSH Read | ATSH Clear | ATRA | Data SID | DTC Read | DTC Clear |
|--------|------|-----------|------------|------|----------|----------|-----------|
| ABS | 0x40 | ATSH244022 | ATSH244011 | ATRA40 | 0x20/0x24 | `24 00 00` | `01 01 00` |
| Airbag | 0x60 | ATSH246022 | ATSH246011 | ATRA60 | 0x28 | `28 37 00`/`28 37 01` | `0D 10 00` |
| SKIM | 0x62 | ATSH246222 | ATSH246211 | - | 0x38/0x3A | ? | ? |
| HVAC | 0x68 | ATSH246822 | ATSH246811 | - | 0x28 | `28 37 00`? | ? |
| **BCM** | 0x80 | ATSH248022 | ATSH248011(?) | ATRA80 | **0x2E** | `2E xx 00`? | `01 01 00`? |
| Radio | 0x87 | ATSH248722 | - | - | 0x2F? | ? | ? |
| **Cluster** | 0x90 | ATSH249022 | ATSH249011 | - | **0x32** | `32 xx 00`? | `01 01 00`? |
| **MemSeat** | 0x98 | ATSH249822 | ATSH249811 | - | **0x38** | ? | ? |
| Liftgate | 0xA0 | ATSH24A022 | - | - | ? | ? | ? |
| ??? | 0xA1 | ATSH24A122 | - | - | ? | ? | ? |
| **Overhead** | 0x28 | ATSH242822 | ATSH242811 | - | **0x2A** | ? | ? |
| EVIC | 0x2A | ATSH242A22 | - | - | ? | ? | ? |
| **VTSS** | 0xC0 | ATSH24C022 | ATSH24C011 | - | ? | ? | ? |

### Airbag Special Modes (from APK)
- `ATSH246027` — Security access mode
- `ATSH246031` — Routine control
- `ATSH2460A0/A3/B4` — Download/transfer

### BCM PIDs (SID 0x2E, 35+ PIDs)
`2E 00..2E 0A`, `2E 0D`, `2E 10..2E 17`, `2E 20..2E 27`, `2E 30`, `2E 50..2E 54`

### Cluster PIDs (SID 0x32, 20+ PIDs)
`32 00..32 05`, `32 10..32 18`, `32 21`, `32 25..32 28`

### Overhead PIDs (SID 0x2A)
`2A 03..2A 0C`, `2A 1F`

### MemSeat PIDs (SID 0x38, extensive)
`38 00..38 0F`, `38 10`, `38 18` (many sub-functions each)

---

## TCM / NAG1 722.6 (0x20) - K-Line

### Security: seed=6824(static), key=CC21 ✓

### Block Map (ALL verified on real vehicle)

| Block | Bytes | Content |
|-------|-------|---------|
| **0x30** | 22 | Live data: N2 RPM, engage, output RPM, gear, line press, temp, TCC slip, solenoid |
| 0x31 | 24 | `01 BE 00 00 02 DD 02 EE...` shift params |
| 0x32 | 16 | All zero at idle |
| **0x33** | 16 | `00 24 0785 05DC 02AE 02AE 02D9 02D9 0000` |
| **0x34** | 14 | `0217 03FF 0332 0215 0807 0001 0028` |
| **0x35** | 2 | `00 03` |
| **0x38** | 3 | `01 FF FF` |
| 0x50 | 51 | NRC 0x78 pending then zeros |
| 0x60 | 2 | `00 01` |
| 0x70 | ~244 | Shift calibration table |
| 0x80 | 27 | DTC/shift config |
| 0xB0 | 4 | `je0a` |
| 0xC0 | 34 | Lookup table |
| 0xE0 | 8 | `aafwspsp` |
| 0xE1 | 4 | `01 61 90 46` |

---

## Engine ECU (0x15) - K-Line

### Security: Level 01, 2-byte, constants UNKNOWN (NRC 0x35)

### Block Map (ALL verified)

| Block | Bytes | Key Data |
|-------|-------|----------|
| **0x10** | 18 | Idle params, maxRPM=3000 |
| **0x12** | 34 | Coolant, IAT, TPS, MAP, Rail, AAP |
| **0x14** | 12 | `7DFE 8241 8241 FF82 E607 FFE6` |
| **0x16** | 40 | Idle/fuel limits |
| **0x18** | 30 | Zero at idle |
| **0x20** | 32 | MAF actual/spec |
| **0x22** | 34 | Rail spec, MAP spec |
| **0x24** | 26 | RPM thresholds |
| **0x26** | 32 | Sensor raw, injector trims |
| **0x28** | 30 | RPM, injection qty |
| **0x30** | 26 | RPM setpoints |
| **0x32** | 34 | `0316(790) 0930(2352)` thresholds |
| **0x34** | 36 | `0008 1000...03FF` timing? |
| **0x38** | 54 | Zero at idle (driving data?) |
| **0x40** | 52 | Zero at idle |
| **0x42** | 26 | byte7=0x80 |
| **0x44** | 30 | byte27=0x56 |
| **0x48** | 12 | `06C8`(1736) threshold |
| 0x62,0xB0-B2 | - | NRC 0x33 (security locked) |

---

## ABS (0x40) - J1850 VPW — VERIFIED

### DTC Format
```
24 00 00 → 62 43 00 00 DD  (0 DTCs, status=0x43)
```
Only SID 0x24 works for DTC read. All other SIDs return NRC 0x12.

### PIDs (SID 0x20)
| PID | Response | Content |
|-----|----------|---------|
| 0x01-0x04 | 62 xx xx 00 | Wheel speeds (LF,RF,LR,RR) |
| 0x05-0x06 | 62 xx 00 00 | Unknown |
| 0x10 | 62 08 00 00 | Vehicle speed |
| 0x11 | 62 92 00 00 | NEW |
| 0x20 | 62 30 30 45 | NEW (ASCII "00E") |
| 0x30 | 62 72 00 00 | NEW |

---

## Airbag (0x60) - J1850 VPW — NRC 0x22 / 0x12

PID 0x00/01/FF return NRC 0x22 (conditionsNotCorrect).
PID 0x02-0x37 return NRC 0x12.
APK also uses `28 37 01` (not tested yet!).
APK uses security mode `ATSH246027` before some operations.

---

## HVAC (0x68) - PIDs 0x00-0x03 verified, 0x04-0x20 not tested

## SKIM (0x62), BCM (0x80), Cluster (0x90) — NO DATA on our vehicle

**But APK confirms these modules exist on WJ.** Our vehicle may have different configuration, or modules need different initialization.

### Next Steps
1. Test `28 37 01` for Airbag DTC (different sub-function)
2. Test BCM with SID 0x2E: `ATSH248022` → `2E 00 00`
3. Test Cluster with SID 0x32: `ATSH249022` → `32 00 00`
4. Test Overhead with SID 0x2A: `ATSH242822` → `2A 03 00`
5. Test VTSS (0xC0): `ATSH24C022` → various
6. Test Airbag security: `ATSH246027` → security sequence
7. HVAC PID 0x04-0x20 scan
