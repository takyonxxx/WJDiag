# Jeep Grand Cherokee WJ 2.7 CRD - Module Init Sequence Analysis

## Architecture Overview

The Jeep WJ 2.7 CRD uses **two separate communication buses**:

| Bus | Protocol | ELM327 Command | Modules |
|-----|----------|----------------|---------|
| **K-Line** | ISO 14230-4 (KWP2000) | ATSP5 | Engine ECU (0x15), TCM/Gearbox (0x20) |
| **J1850 VPW** | SAE J1850 VPW | ATSP2 | TCM (0x28), ABS (0x40), Airbag (0x60), and 10+ modules |

The Engine ECU and Transmission are accessible over **both K-Line and J1850**. K-Line provides deep diagnostics (block reads, security access), while J1850 is used for basic PID reading.

---

## 1. ELM327 Connection Init (Common)

```
ATZ            -> ELM327 reset
ATE1           -> Echo ON
ATH1           -> Headers ON
ATIFR0         -> Disable IFR (In-Frame Response) for J1850 VPW
ATSP2          -> Select J1850 VPW as default protocol
```

These 5 commands run once at connection. No ATAT, ATST, ATRV, or ATI commands are sent during init (APK confirmed). What follows depends on the target module's bus type.

---

## 2. Engine ECU (Bosch EDC15C2 OM612) - K-Line Init

**Bus:** K-Line (ISO 14230-4)
**Address:** 0x15
**Init Sequence:**

```
ATZ                -> Full reset
ATE1               -> Echo ON
ATH1               -> Headers ON
ATWM8115F13E       -> Wakeup message (target 0x15, source 0xF1, pattern 0x3E)
ATSH8115F1         -> Set header (ECU addr 0x15, tester addr 0xF1)
ATSP5              -> Select ISO 14230-4 fast init protocol
ATFI               -> Fast init trigger (bus init)
81                 -> StartCommunication (KWP2000 SID 0x81)
27 01              -> SecurityAccess - request seed
27 02 [KEY]        -> SecurityAccess - send key (calculated from seed)
```

**Note:** ATWM and ATSH are sent **before** ATSP5. The ELM327 stores the wakeup/header configuration first, then the protocol is selected and fast init is triggered. Ignition must be ON (ACC or RUN) for ATFI to succeed.

**Security Access:** The key must be calculated from the seed — a static key does not work. NRC 0x35 (invalidKey) is returned if the key is wrong. Blocks 21 12, 21 28, 21 20, 21 22 work without security; blocks 21 62, 21 B0-B2 require valid security access (NRC 0x33 = securityAccessDenied).

**Post-init commands (ReadLocalData SID 0x21):**

| Command | Description | Security |
|---------|-------------|----------|
| `21 12` | Coolant, IAT, TPS, MAP, Rail Pressure, AAP | No |
| `21 28` | RPM, Injection Qty, Corrections | No |
| `21 20` | MAF sensor values | No |
| `21 22` | Rail pressure spec, MAP spec | No |
| `21 62` | EGR, Wastegate, Glow Plugs, MAF, Alternator | **Yes** |
| `21 B0` | Injector corrections, oil pressure | **Yes** |
| `21 B1` | Boost/idle adaptation | **Yes** |
| `21 B2` | Fuel adaptation | **Yes** |

**Other K-Line commands:**

| Command | Description |
|---------|-------------|
| `1A 86` | ReadEcuId - Manufacturer |
| `1A 90` | ReadEcuId - VIN |
| `1A 91` | ReadEcuId - Hardware version |
| `18 02 00 00` | ReadDTCsByStatus - Read fault codes |
| `14 00 00` | ClearDTCs - Clear fault codes |

---

## 3. Transmission/TCM (NAG1 722.6) - K-Line Init

**Bus:** K-Line (ISO 14230-4)
**Address:** 0x20
**Init Sequence:**

```
ATZ                -> Full reset (shares physical bus with Engine ECU, switch required)
ATE1               -> Echo ON
ATH1               -> Headers ON
ATWM8120F13E       -> Wakeup message (target 0x20, source 0xF1, pattern 0x3E)
ATSH8120F1         -> Set header (TCM addr 0x20, tester addr 0xF1)
ATSP5              -> ISO 14230-4 fast init
ATFI               -> Fast init trigger
81                 -> StartCommunication
27 01 / 27 02      -> SecurityAccess (if required)
```

**Note:** K-Line TCM (0x20) shares the same physical wire as the Engine ECU. Switching between them requires a full ATZ reset.

---

## 4. TCM/Transmission (NAG1) - J1850 VPW Init

**Bus:** J1850 VPW
**Address:** 0x28
**Init Sequence (APK verified):**

```
ATSP2              -> Select J1850 VPW protocol
ATSH242810         -> Functional header (SID 0x10 = DiagSession)
02 00 00           -> DiagnosticSessionControl subFunc=0x02 (extended diagnostic)
                      Wait for response containing "50" (positive)
ATSH242822         -> Switch to ReadDataByID header (SID 0x22)
```

**CRITICAL:** The TCM requires a DiagnosticSessionControl command before data read. Without the `ATSH242810` + `02 00 00` sequence, all `2E xx 00` data read commands return NO DATA. The session command uses the functional header with SID 0x10 embedded, and the `02 00 00` data bytes represent subFunction=0x02 (extended/programming diagnostic session) with zero padding.

**Live Data Reading (APK verified):**

```
ATSH242822         -> ReadDataByID header (set once after session)
2E PID 00          -> Read PID (e.g. "2E 10 00" for Turbine RPM)
Response: 62 PID DATA
```

The command format is `2E [PID] 00` — the `2E` prefix is the TCM's LocalID identifier, `00` is padding for 3-byte minimum. This is different from standard OBD-II `22 PID` format.

**TCM J1850 Live Data PIDs (APK verified, 33 PIDs):**

| PID | Description | Size |
|-----|-------------|------|
| 0x00 | Status/Unknown | 1B |
| 0x01 | Actual Gear | 1B |
| 0x02 | Selected Gear | 1B |
| 0x03 | Max Gear | 1B |
| 0x04 | Shift Selector Position | 1B |
| 0x05 | Unknown | 1B |
| 0x06 | Unknown | 1B |
| 0x07 | Unknown | 1B |
| 0x08 | Unknown | 1B |
| 0x09 | Unknown | 1B |
| 0x0D | Unknown | 1B |
| 0x10 | Turbine RPM | 2B |
| 0x11 | Input RPM (N2) | 2B |
| 0x12 | Input RPM (N3) | 2B |
| 0x13 | Output RPM | 2B |
| 0x14 | Transmission Temp (+40 offset) | 1B |
| 0x15 | TCC Pressure (*0.1) | 1B |
| 0x16 | Solenoid Supply Voltage (*0.1V) | 1B |
| 0x17 | TCC Clutch State | 1B |
| 0x18 | Actual TCC Slip (signed) | 2B |
| 0x19 | Desired TCC Slip (signed) | 2B |
| 0x1A-0x1F | Solenoid act/set values (*0.39%) | 1B each |
| 0x20 | Vehicle Speed | 2B |
| 0x21 | Front Vehicle Speed | 2B |
| 0x22 | Rear Vehicle Speed | 2B |
| 0x23 | Shift PSI | 2B |
| 0x24 | Modulation PSI | 2B |
| 0x25 | Park Lockout Solenoid | 1B |
| 0x26 | Park/Neutral Switch | 1B |
| 0x27 | Brake Light Switch | 1B |
| 0x28-0x2D | Various switches and solenoids | 1B each |
| 0x30 | Calculated Gear | 1B |
| 0x50-0x54 | Adaptation Values (APK extra) | 2B each |

**TCM J1850 Headers (all SIDs from APK):**

| Header | SID | Description | Command |
|--------|-----|-------------|---------|
| `ATSH242810` | 0x10 | DiagnosticSessionControl | `02 00 00` → response `50` |
| `ATSH242811` | 0x11 | ECUReset | `01 02 00` → response `51` |
| `ATSH242814` | 0x14 | ClearDTCs | DTC clear operations |
| `ATSH242820` | 0x20 | ReturnToNormal | `00 00 00` → response `60` |
| `ATSH242822` | 0x22 | ReadDataByID | `2E PID 00` → response `62` |
| `ATSH242830` | 0x30 | InputOutputControl | Bitmask solenoid commands |
| `ATSH2428A0` | 0xA0 | WriteMemoryByAddress | `00 BD 02`, `00 BE`, `00 64 00` |
| `ATSH2428A3` | 0xA3 | ReadMemoryByAddress | `00 BD 00` → response `E3` |

**TCM I/O Control (ATSH242830, APK verified):**

| Command | Description |
|---------|-------------|
| `01 FE FF` | Solenoid output 1 |
| `01 FB FF` | Solenoid output 2 |
| `01 EF FF` | Solenoid output 3 |
| `01 BF FF` | Solenoid output 4 |
| `01 FD FF` | Solenoid output 5 |
| `01 F7 FF` | Solenoid output 6 |
| `01 DF FF` | Solenoid output 7 |
| `01 7F FF` | Solenoid output 8 |
| `01 FF BF` | Solenoid output 9 |
| `36 00 00` | TransferData |
| `01 55 40` | Multi-solenoid pattern |
| `01 FF 40` | All solenoids + pattern |
| `01 FF 00` | All solenoids off |

**DTC Reading (APK verified):**
```
ATSH242810         -> Functional header (SID 0x10)
02 00 00           -> DiagSession (needed before DTC read too)
ATSH242814         -> ClearDTC header (SID 0x14)
18 02 FF 00        -> ReadDTCsByStatus (via functional header)
```

---

## 5. ABS (Anti-lock Braking System) - J1850 VPW Init

**Bus:** J1850 VPW
**Address:** 0x40
**Init Sequence:**

```
ATSP2              -> Select J1850 VPW protocol
ATSH244022         -> ABS ReadDataByID header
ATRA40             -> Filter/listen for ABS address responses
```

**No DiagSession needed** — ABS responds to data read commands directly.

**Live Data Reading (APK verified, real device confirmed):**

```
20 PID 00          -> Read PID (e.g. "20 01 00" for LF Wheel Speed)
Response: 26 40 62 DATA (with headers on)
```

The ABS module uses `20` prefix (its own LocalID) followed by PID and `00` padding.

**ABS PIDs (real device verified):**

| PID | Response | Status |
|-----|----------|--------|
| `20 00 00` | `26 40 62 05 08 00 E2` | Data OK |
| `20 01 00` | `26 40 62 38 92 00 F0` | Data OK |
| `20 02 00` | `26 40 62 41 43 00 E0` | Data OK |
| `20 03 00` | `26 40 62 01 00 00 BE` | Data OK |
| `20 04 00` | `26 40 62 1A 65 00 D8` | Data OK |
| `20 05 00` | `26 40 62 41 00 00 DE` | Data OK |
| `20 06 00` | `26 40 62 02 00 00 32` | Data OK |
| `20 07 00` | `26 40 7F 22 12` | NRC: subFunctionNotSupported |
| `20 08 00` | `26 40 7F 22 12` | NRC: subFunctionNotSupported |
| `20 09 00` | `26 40 7F 22 12` | NRC: subFunctionNotSupported |

PIDs 0x00-0x06 return valid data; 0x07-0x09 are not supported on this vehicle.

**ABS Headers (from APK):**

| Header | SID | Description |
|--------|-----|-------------|
| `ATSH244011` | 0x11 | ECUReset |
| `ATSH244022` | 0x22 | ReadDataByID |
| `ATSH24402F` | 0x2F | IOControl (actuator test) |
| `ATSH2440B4` | 0xB4 | ReadMemoryByAddress |

**Note:** ABS has no SecurityAccess (0x27) header — no security required.

---

## 6. Airbag/ORC (AOSIM) - J1850 VPW Init

**Bus:** J1850 VPW
**Address:** 0x60
**Init Sequence:**

```
ATSP2              -> Select J1850 VPW protocol
ATSH246022         -> Airbag ReadDataByID header
ATRA60             -> Listen for Airbag messages
```

**No DiagSession needed** — Airbag responds directly (though with NRC in some PIDs).

**Live Data Reading (APK verified, real device tested):**

```
28 PID 00          -> Read PID (e.g. "28 00 00" for status)
Response: 26 60 7F 22 22 (NRC: conditionsNotCorrect for PID 0x00, 0x01)
Response: 26 60 7F 22 12 (NRC: subFunctionNotSupported for PID 0x02-0x05)
```

The Airbag module uses `28` prefix followed by PID and `00` padding. On real device test, all PIDs returned NRC — this may require ignition RUN (not just ACC) or a specific DiagSession.

**Airbag Headers (from APK):**

| Header | SID | Description |
|--------|-----|-------------|
| `ATSH246011` | 0x11 | ECUReset |
| `ATSH246022` | 0x22 | ReadDataByID |
| `ATSH246027` | 0x27 | SecurityAccess |
| `ATSH246031` | 0x31 | StartRoutine |
| `ATSH2460A0` | 0xA0 | Download/Upload |
| `ATSH2460A3` | 0xA3 | WriteDataByAddress |
| `ATSH2460B4` | 0xB4 | ReadMemoryByAddress |

---

## 7. Other J1850 Modules

| Module | Address | ATRA | Headers | Features |
|--------|---------|------|---------|----------|
| **Security/SKIM** | 0x62 | - | ATSH246211, 246222 | Immobilizer, key programming |
| **HVAC/ATC** | 0x68 | - | ATSH246811, 246822, 246831, 246833 | Climate control, StartRoutine+StopRoutine |
| **BCM (Body Computer)** | 0x80 | ATRA80 | ATSH248022, 24802F, 2480B4 | Body control, IOControl, memory read |
| **Radio/Audio** | 0x87 | - | ATSH248722, 24872F | Radio, IOControl |
| **Instrument Cluster** | 0x90 | - | ATSH249011, 249022, 24902F | Odometer, IOControl |
| **Memory Seat** | 0x98 | - | ATSH249811, 249822, 24982F, 249830 | Seat/mirror position |
| **Power Liftgate** | 0xA0 | - | ATSH24A022, 24A02F | Tailgate |
| **HandsFree/Uconnect** | 0xA1 | - | ATSH24A122, 24A12F, 24A131, 24A133 | Phone, Start/Stop Routine |
| **Park Assist** | 0xC0 | - | ATSH24C011, 24C022, 24C027, 24C02F, 24C0B4 | Parking sensor, Security, Memory |
| **Overhead Console (EVIC)** | 0x2A | - | ATSH242A22, 242A2F, 242AB7 | Compass/temperature, DynData |

---

## 8. J1850 VPW Command Format (APK Verified)

Each J1850 module uses its **own LocalID prefix** in the data portion. The header contains SID 0x22 (ReadDataByID), and the data bytes use a module-specific LocalID:

| Module | Header (ReadData) | LocalID | Data Format | Example | Response |
|--------|-------------------|---------|-------------|---------|----------|
| **TCM (0x28)** | ATSH242822 | 0x2E | `2E PID 00` | `2E 10 00` | `62 10 xx xx` |
| **ABS (0x40)** | ATSH244022 | 0x20 | `20 PID 00` | `20 01 00` | `60 01 xx xx` |
| **Airbag (0x60)** | ATSH246022 | 0x28 | `28 PID 00` | `28 00 00` | `68 00 xx` |

The `00` suffix is padding to meet the 3-byte minimum message length.

**Wire format:** `[priority=24][target_addr][SID_in_header=22][LocalID][PID][00]`

**Response format:** Positive response byte = LocalID + 0x40:
- TCM: `2E` → `6E` (but standard response uses `62` from SID 0x22)
- ABS: `20` → `60`
- Airbag: `28` → `68`

**DiagSession requirement by module:**

| Module | DiagSession Required | Command |
|--------|---------------------|---------|
| TCM (0x28) | **YES** | `ATSH242810` → `02 00 00` → response `50` |
| ABS (0x40) | No | Direct data read |
| Airbag (0x60) | No | Direct data read |

---

## 9. Bus Switching Strategy (K-Line <-> J1850)

### Switching to K-Line (J1850 -> K-Line):
```
ATZ              -> Full ELM327 reset
ATE1             -> Echo ON
ATH1             -> Headers ON
ATWM81xxF13E     -> Wakeup (xx = module address: 0x15 or 0x20)
ATSH81xxF1       -> Set header
ATSP5            -> Select K-Line protocol
ATFI             -> Fast init trigger
81               -> StartCommunication
27 01 / 27 02    -> SecurityAccess
```

### Switching to J1850 (K-Line -> J1850):
```
ATZ              -> Full ELM327 reset
ATE1             -> Echo ON
ATH1             -> Headers ON
ATIFR0           -> Disable IFR
ATSP2            -> Select J1850 VPW protocol
ATSH24xxYY       -> Set header (xx = module, YY = SID)
ATRAxx           -> Set receive filter (if required: ABS=ATRA40, Airbag=ATRA60, BCM=ATRA80)
```

### TCM J1850 switch includes DiagSession:
```
ATZ → ATE1 → ATH1 → ATIFR0 → ATSP2 → ATSH242810 → "02 00 00" (DiagSession)
→ ATSH242822 (switch to data read header) → ready for "2E xx 00" commands
```

### Same-bus module change (J1850 -> J1850):
```
ATSH24xxYY       -> Change header
ATRAxx           -> Set receive filter (if required)
```

When switching to TCM from another J1850 module, the DiagSession (`ATSH242810` → `02 00 00` → `ATSH242822`) is sent even on same-bus switch.

### ATFI Failure Recovery:
If K-Line ATFI fails (e.g. ignition off), the code automatically:
1. Sends ATZ to reset
2. Restores J1850 VPW (ATE1 → ATH1 → ATSP2)
3. Restores previous J1850 module ATSH header
4. Reports error but keeps J1850 session alive

---

## 10. Timing (Real Device Verified)

| Parameter | Value | Notes |
|-----------|-------|-------|
| **Inter-command delay** | 340ms | Timer between sequential commands (APK matched) |
| **ATZ timeout** | ~840ms | ELM327 reset response time |
| **ATFI timeout** | ~470ms | K-Line bus init response time |
| **AT command response** | ~35-80ms | Typical AT command response |
| **K-Line data response** | ~280-400ms | ECU block read response |
| **J1850 data response** | ~120-220ms | Module PID response |
| **NO DATA response** | ~140-220ms | When module doesn't respond |
| **ATZ recovery delay** | 500ms | After ATZ before next command |

---

## 11. ELM327 Compatibility

- **Genuine ELM327 recommended:** ATFI, ATWM, ATSH support required for K-Line ECU access
- **Clone ELM327 (v1.5/v2.1):** J1850 VPW modules work, K-Line may fail (no ATFI support)
- **BLE OBD adapters (Viecar etc.):** Supported via Bluetooth Low Energy connection
- **WiFi OBD adapters:** Supported via TCP connection (default 192.168.0.10:35000)

---

## 12. Real Device Test Results (2025-03-04)

**Test setup:** iPhone 16 Pro + Viecar BLE (ELM327 v1.5) + Jeep WJ 2.7 CRD (engine running, idle)

| Module | Result | Notes |
|--------|--------|-------|
| **Engine ECU K-Line** | Blocks 21 12, 21 28, 21 20, 21 22 OK | 21 62/B0/B1/B2 → NRC 0x33 (security key wrong) |
| **TCM J1850** | All PIDs NO DATA | DiagSession was missing (fixed) |
| **ABS J1850** | PIDs 0x00-0x06 return data | PIDs 0x07-0x09 → NRC 0x12 |
| **Airbag J1850** | All PIDs NRC | 0x00-0x01: conditionsNotCorrect, 0x02-0x05: subFuncNotSupported |
| **Battery Voltage** | ATRV = 14.0-14.5V | Alternator charging confirmed |

**ECU data samples (engine idle):**
- Coolant: 65-66°C, IAT: 25-31°C, TPS: 0.0%
- RPM: 746-752, Injection Qty: 7.6-8.2 mg
- Rail Pressure: 280-306 bar, MAP: 910-928 mbar
- Battery: 14.0-14.5V
