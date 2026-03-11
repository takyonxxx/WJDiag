# WJDiag — Jeep WJ 2.7 CRD Diagnostic App

Qt6 C++ mobile diagnostic application for the Jeep Grand Cherokee WJ (2001-2004) with the 2.7 CRD diesel engine (Mercedes OM612) and NAG1 722.6 automatic transmission.

## Features

- **Multi-module diagnostics** across K-Line (ISO 9141-2) and J1850 VPW buses
- **Live data** from engine ECU and transmission with real-time display
- **DTC read/clear** for engine, transmission, ABS, airbag, HVAC, and more
- **Security access** for both ECU and TCM protected data
- **BLE and WiFi** ELM327 adapter support (Android/iOS)

## Supported Modules

### K-Line (ISO 9141-2, ATSP5)

| Module | Address | Security | Status |
|--------|---------|----------|--------|
| Engine ECU (Bosch EDC15C2) | 0x15 | ArvutaKoodi (lookup table) | Full live data + protected blocks |
| TCM (NAG1 722.6 EGS52) | 0x20 | EGS52 swap+XOR+MUL | Full live data + DTC |

### J1850 VPW (SAE J1850, ATSP2)

| Module | Address | SID | Status |
|--------|---------|-----|--------|
| ABS / ESP | 0x40 | 0x20/0x24 | DTC read/clear, live PIDs |
| Airbag (ORC) | 0x60 | 0x28 | NRC 0x22 on SID 0x22; try mode 0xA0/0xA3 |
| HVAC | 0x68 | 0x28 | PIDs 0x00-0x03 confirmed; try modes 0x31/0x33 |

Note: BCM (0x80), Cluster (0x90), MemSeat (0x98), SKIM (0x62), Overhead Console (0x28), VTSS (0xC0), Radio (0x87), Liftgate (0xA0) currently return NO DATA on our test vehicle (engine running, BLE ELM327 adapter). These modules are confirmed present and accessible via other diagnostic tools. Investigation ongoing.

### J1850 VPW Header Format

The J1850 VPW header byte structure is `ATSH 24 XX YY` where:
- `24` = priority byte (J1850 VPW standard)
- `XX` = target module address
- `YY` = mode byte (determines which service the module responds to)

Known mode bytes per module (discovered through reverse engineering):

| Module | Addr | Read(22) | Func(10) | Clear(11) | Sec(27) | Rtn(31) | Rtn2(33) | Alt(A0) | Alt2(A3) | DL(14) | DL2(20) | IO(30) | Spe(B4) |
|--------|------|----------|----------|-----------|---------|---------|----------|---------|----------|--------|---------|--------|---------|
| OHC | 0x28 | 22 | 10 | 11 | - | - | - | A0 | A3 | 14 | 20 | 30 | - |
| ABS | 0x40 | 22 | - | 11 | - | - | - | A0 | - | - | - | - | - |
| Airbag | 0x60 | 22 | - | 11 | 27 | 31 | - | A0 | A3 | - | - | - | - |
| SKIM | 0x62 | 22 | - | - | - | - | - | - | - | - | - | - | - |
| HVAC | 0x68 | 22 | - | 11 | - | 31 | 33 | - | - | - | - | - | - |
| BCM | 0x80 | 22 | 10 | 11 | - | - | - | - | - | - | 20 | - | - |
| Radio | 0x87 | 22 | - | - | - | - | - | - | - | - | - | - | - |
| Cluster | 0x90 | 22 | 10 | - | - | - | - | - | - | - | - | - | - |
| MemSeat | 0x98 | 22 | - | - | - | - | - | - | - | - | - | - | - |
| Liftgate| 0xA0 | 22 | - | - | - | - | - | - | - | - | - | - | - |
| 0xA1 | 0xA1 | 22 | - | - | - | 31 | 33 | - | - | - | - | - | - |
| VTSS | 0xC0 | 22 | - | - | 27 | - | - | - | - | - | - | - | B4 |

All testing was performed with engine running. The `ATRA` receive filter command is required for each module (`ATRAXX` where XX is the module address).

### Possible causes for NO DATA on some modules

1. **EU-spec WJ 2.7 CRD may not have all US-spec J1850 modules** — the PCI/CCD bus architecture differs between markets
2. **BLE ELM327 adapter timing** — some modules may need longer response timeout than the BLE adapter provides
3. **Mode byte mismatch** — some modules only respond on specific mode bytes (e.g., Overhead Console primarily uses mode 0xA0, not 0x22)

## ECU Security Access

The Bosch EDC15C2 in the WJ 2.7 CRD uses a **lookup-table based** seed-key algorithm — not the ProcessKey5 shift-XOR algorithm used by European EDC15 variants. This was discovered through extensive trial-and-error testing.

### Algorithm: ArvutaKoodi

The key is computed from the 2-byte seed using four 16-entry lookup tables:

```
Input: seed = [s0, s1]  (s0 = high byte, s1 = low byte)

Step 1: v1 = (s1 + 0x0B) & 0xFF
        keyLo = TABLE1[v1 >> 4] | TABLE2[v1 & 0xF]

Step 2: cond = 1 if s1 > 0x34, else 0
        v2 = (s0 + cond + 1) & 0xFF
        keyHi = TABLE3[v2 >> 4] | TABLE4[v2 & 0xF]

Output: key = [keyHi, keyLo]
```

Lookup tables:

```
TABLE1 (high nibble): C0 D0 E0 F0 00 10 20 30 40 50 60 70 80 90 A0 B0
TABLE2 (low nibble):  02 03 00 01 06 07 04 05 0A 0B 08 09 0E 0F 0C 0D
TABLE3 (high nibble): 90 80 F0 E0 D0 C0 30 20 10 00 70 60 50 40 B0 A0
TABLE4 (low nibble):  0D 0C 0F 0E 09 08 0B 0A 05 04 07 06 01 00 03 02
```

### ECU characteristics

- **Seed**: Dynamic (changes every session)
- **Lockout**: 2 wrong attempts triggers NRC 0x36 (exceededNumberOfAttempts), requires full bus re-init + 3s wait
- **Protected blocks** (NRC 0x33 without security, verified 2025-03-11):
  - 0x62: 4 data bytes — EGR duty, wastegate duty, + 2 unknown bytes
  - 0xB0: 2 data bytes — meaning TBD (0x37, 0x0F observed at idle)
  - 0xB1: 2 data bytes — boost adaptation (s16/10 = mbar)
  - 0xB2: 2 data bytes — fuel quantity adaptation (s16/100 = mg/stroke)
  - 1A 90: VIN (17 ASCII chars)
  - 1A 91: ECU identification (date, variant code)
  - 1A 86: ECU hardware/calibration data

### Verified ECU security session (real vehicle log)

```
→ 27 01
← 84 F1 15 67 01 DF A0 71        (seed = DF A0)
  ArvutaKoodi: key = BC 69
→ 27 02 BC 69
← 83 F1 15 67 02 34 26            (positive response)
→ 21 62
← 86 F1 15 61 62 18 05 8C 84 7C  (4 data bytes)
→ 21 B0
← 84 F1 15 61 B0 37 0F E1        (2 data bytes)
→ 1A 90
← 93 F1 15 5A 90 31 4A 38 47 57 45 38 32 58 32 59 31 32 32 30 30 36 91
  VIN: 1J8GWE82X2Y122006
```

### TCM Security (EGS52)

- **Seed**: Static `0x6824` (never changes)
- **Algorithm**: `key = ((swap_bytes(seed) ^ 0x5AA5) * 0x5AA5) & 0xFFFF` = `0xCC21`

## TCM Live Data — Block 0x30 Byte Map

```
[0-1]   N2 Sensor RPM (turbine input)
[2-3]   Engage status (P/N: 0x1E, R/D: 0x3C)
[4-5]   Output Shaft RPM
[6]     Unknown
[7]     Selector: P=8, R=7, N=6, D=5
[8]     Config (usually 4)
[9-10]  Line pressure
[11]    Trans temp raw (raw - 40 = degC)
[12-13] TCC slip actual (signed)
[14-15] TCC slip desired (signed)
[16-17] Unknown counter
[18]    Solenoid mode bitmask
[19]    Status flags
[20]    Shift flags
[21]    Always 0x08
```

NAG1 722.6 gear ratios: 1st=3.59, 2nd=2.19, 3rd=1.41, 4th=1.00, 5th=0.83.

## ECU Live Data Blocks (verified from real vehicle)

| Block | Data Bytes | Key Parameters |
|-------|-----------|----------------|
| 0x12 | 34 | Coolant temp, IAT, TPS, MAP, rail pressure, AAP |
| 0x20 | 32 | MAF actual/spec |
| 0x22 | 34 | Rail spec, MAP spec |
| 0x28 | 30 | RPM, injection quantity (mg/stroke) |
| 0x10 | 16 | Idle params, max RPM |
| 0x62 | 4 | EGR, wastegate, + 2 unknown (security required) |
| 0xB0 | 2 | Unknown — 0x37, 0x0F at idle (security required) |
| 0xB1 | 2 | Unknown — 0xD2, 0x15 at idle (security required) |
| 0xB2 | 2 | Unknown — 0xE0, 0x4B at idle (security required) |

### Fuel consumption calculation

Instantaneous fuel flow is calculated from block 0x28 data:

```
RPM       = block 0x28 [0-1] (unsigned 16-bit)
InjQty    = block 0x28 [2-3] / 100.0 (mg/stroke)
Cylinders = 5 (OM612)
Density   = 832 g/L (diesel)

fuelFlow_gs = RPM * InjQty * 5 / (2 * 1000 * 60)     [g/s]
fuelFlow_lh = fuelFlow_gs * 3600 / 832                 [L/h]
```

Verified values from real vehicle:
- Idle (750 RPM, 12.5 mg/str): **1.69 L/h**
- Acceleration (2233 RPM, 28.0 mg/str): **11.27 L/h**
- Deceleration (1339 RPM, 5.6 mg/str): **1.35 L/h**

### Known issue: K-Line module switching

When switching between ECU (0x15) and TCM (0x20) on the K-Line bus, the ELM327 must perform a full bus reinit (ATZ → ATWM → ATSH → ATSP5 → ATFI → 81 → 27). If the previous session has timed out (no communication for 3-5 seconds), the `3E` (TesterPresent) heartbeat will detect this and trigger a reinit. Response source address is validated: TCM responses contain `F1 20`, ECU responses contain `F1 15`.

## Architecture

```
ELM327Connection (WiFi TCP + BLE GATT)
  -> KWP2000Handler (K-Line framing, SID routing)
      -> WJDiagnostics (module management, live data decode)
          -> LiveDataManager (polling, gear detection)
              -> MainWindow (Qt QML UI)
```

## Building

Requires Qt 6.5+ with QtConnectivity (Bluetooth) module.

```bash
mkdir build && cd build
cmake .. -DCMAKE_PREFIX_PATH=/path/to/qt6
cmake --build .
```

For Android: Use Qt Creator with Android SDK/NDK configured.

## Emulator

`wj_tcm_emulator.py` provides a TCP server that emulates the ELM327 + vehicle bus for development without a real vehicle. Supports all modules, realistic timing, gear shift simulation, and both ECU (ArvutaKoodi) and TCM (EGS52) security access.

```bash
python3 wj_tcm_emulator.py --port 35000
```

## File Structure

```
src/
  mainwindow.cpp      UI, test procedures
  wjdiagnostics.cpp   Module management, security, live data decode
  kwp2000handler.cpp  KWP2000 protocol framing
  livedata.cpp        Live data polling and display
  elm327connection.cpp ELM327 communication (WiFi/BLE)
  main.cpp            Application entry point
include/
  *.h                 Headers for all source files
wj_tcm_emulator.py   Development emulator
README.md             This file
```
