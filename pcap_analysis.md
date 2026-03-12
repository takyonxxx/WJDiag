
# PCAP ANALYSIS — Real Vehicle WJ 2.7 CRD 2003 EU
# Date: 2026-03-12, WiFi ELM327 (OBDII v1.5)
# ================================================================

## CRITICAL DISCOVERIES FROM REAL VEHICLE

### 1. NEW MODULE: 0x58 — UNKNOWN MODULE (ESP/Traction Control?)
   - Address: 0x58, Header: ATSH245822, Filter: ATRA58
   - RESPONDS to SID 0x20 (data read), 0x24 (DTC), 0x28, 0x2E
   - Periodic unsolicited: B8 58 02 2B (heartbeat?) and 2D 58 00 40
   - Has MANY 2E PIDs (0x10,0x13,0x16,0x19,0x1C,0x1F,...,0xFD)
   - READ DATA: 20 00 00 -> 26 58 62 56 03 00 01
   - NOT in our enum! Need to add Module 0x58

### 2. NEW MODULE: 0x61 — UNKNOWN (Compass? Traveler?)
   - Address: 0x61, Header: ATSH246122, Filter: ATRA61
   - RESPONDS to SID 0x20, 0x24, 0x28
   - 20 00 00 -> 26 61 62 52 10 00 55
   - 24 00 00 -> 26 61 62 02 03 01 50 (2 DTCs!)
   - NOT in our enum! Need to add Module 0x61

### 3. NEW MODULE: 0xA7 — UNKNOWN (Siren/Security?)
   - Address: 0xA7, Header: ATSH24A722, Filter: ATRAA7
   - RESPONDS to SID 0x20, 0x24, 0x28, 0x2E
   - 20 00 00 -> 26 A7 62 56 04 00 17
   - 24 00 00 -> 26 A7 62 45 A5 00 73 (DTCs!)
   - NOT in our enum!

### 4. MODULE 0x2A (EVIC) = NO DATA on real vehicle!
   - ATSH242A22 + ATRA2A -> 20 00 00 -> NO DATA (8 retries)
   - Our emulator was responding - should return NO DATA

### 5. MODULE 0x80 (BCM) = NO DATA on real vehicle!
   - ATSH248022 + ATRA80 -> 20 00 00 -> NO DATA (8 retries)
   - CONFIRMED: BCM has NO J1850 read capability
   - BCM relay control (mode 0x2F) was from binary only, NOT verified on real vehicle

### 6. MODULE 0x60 (Airbag) = NRC 0x22 (conditionsNotCorrect) for ALL
   - Every SID returns 26 60 7F 22 22 00 44
   - SID 0x20 pid 0x0B: NRC (not 0x00 as init check)
   - SID 0x24: NRC
   - SID 0x20 pid 0x00, 0x01, 0x02: all NRC
   - Need ignition OFF or special conditions

### 7. ECU BLOCK 0x62 = DIFFERENT VALUES than previous tests!
   - Previous: 8A 79 8D 84 (old test)
   - Now: EB ED 8E 84 (new PCAP)
   - First 2 bytes CHANGE! Not static calibration as we assumed
   - EB ED = EGR duty? Wastegate? These ARE live data!

### 8. ECU SECURITY SEED 00 00 = ALREADY UNLOCKED
   - Second ECU session: 27 01 -> 67 01 00 00 (seed=0000 = already unlocked)
   - Then tries 27 02 with wrong key -> 7F 27 12 (subFuncNotSupported, no key data)
   - When seed=0x0000, ECU is already authenticated, skip key send

### 9. ECU IOControl (SID 0x30) — REAL ACTUATOR COMMANDS!
   - 30 11 07 13 88 -> 70 11 07 13 88 (positive! RPM=0x1388=5000 limiter?)
   - 30 1C 07 27 10 -> 70 1C 07 27 10 (positive!)
   - 30 16 07 27 10, 30 17 07 27 10, 30 14 07 27 10 (all positive!)
   - 30 1A 07 13 88, 30 12 07 00 10, 30 18 07 08 34/21 34
   - Then OFF: same PID with 00 00 -> positive
   - Format: 30 PID 07 VALUE_HI VALUE_LO (SID=0x30, sub=PID, mode=07, 16bit value)

### 10. ECU SID 0x32 — ReadMemory/Routine read
   - 32 25 -> 72 25 (positive response, after 27 01 -> seed 00 00)

### 11. TCM IOControl = NRC!
   - 30 10 07 00 02 -> 7F 30 22 (conditionsNotCorrect)
   - 30 10 07 00 00 -> 7F 30 33 (securityAccessDenied)
   - TCM actuator control needs security unlock first

### 12. Driver Door 0x40 — REAL RESPONSE DATA FROM PCAP 2
   Mode 0x22 init reads:
     20 00 00 -> 26 40 62 05 08 00 E2
     20 01 00 -> 26 40 62 38 92 00 F0
     20 02 00 -> 26 40 62 41 43 00 E0
     24 00 00 -> 26 40 62 43 00 00 DD (DTC count)
     28 00 00 -> 26 40 62 0D 00 00 B4
     28 02 00 -> 26 40 62 20 03 00 D5
     28 07 00 -> 26 40 62 14 00 00 2F
     20 06 00 -> 26 40 62 02 00 00 32
   
   Mode 0x2F relay commands (ALL verified with real responses):
     38 06 20 -> 26 40 6F 38 06 20 68  (MirrorRight)
     38 08 01 -> 26 40 6F 38 08 01 1D  (FrontWinDown)
     38 07 01 -> 26 40 6F 38 07 01 BE  (FrontWinUp)
     38 06 02 -> 26 40 6F 38 06 02 D5  (Lock)
     38 02 01 -> 26 40 6F 38 02 01 DF  (MirrorDown)
     38 06 08 -> 26 40 6F 38 06 08 07  (MirrorHeater)
     38 06 10 -> 26 40 6F 38 06 10 22  (MirrorLeft)
     38 06 20 -> 26 40 6F 38 06 20 68  (MirrorRight)
     38 08 02 -> 26 40 6F 38 08 02 3A  (RearWinDown)
     38 06 04 -> 26 40 6F 38 06 04 9B  (RearWinUp)
     38 0D 01 -> 26 40 6F 38 0D 01 7C  (Illumination)
     3A 02 FF -> 26 40 6F 3A 02 FF 05  (Release All)
   
   After relay: back to mode 0x22 for reads:
     28 04 00 -> 26 40 62 00 00 00 31
     2E 0D 00 -> 26 40 62 00 40 00 DB

### 13. Door 0xA0 — Live data reads
   Mode 0x22:
     20 00 00 -> 26 A0 62 63 44 00 57
     20 01 00 -> 26 A0 62 58 39 00 EF  
     20 02 00 -> 26 A0 62 41 43 00 9D  ("AC" in ASCII)
     24 00 00 -> 26 A0 62 34 00 00 62
     28 00 00 -> 26 A0 62 00 00 00 4C
   
   Mode 0x2F relay: all 16 PIDs (0x00-0x0F) confirmed with real CRC!
   Mode 0x22 SID 0x32 reads:
     32 00 00 -> 26 A0 62 00 00 00 4C
     32 01 00 -> 26 A0 62 00 00 00 4C
     32 02 00 -> 26 A0 62 94 00 00 92
     32 03 00 -> 26 A0 62 FD 00 00 41
     32 04 00 -> 26 A0 62 FF 00 00 42

### 14. Liftgate 0xA1 — INTERMITTENT!
   Mode 0x2F: some PIDs return NO DATA intermittently
   38 01 12: sometimes NO DATA, sometimes positive
   38 02 12: sometimes NO DATA, retries needed
   This is REAL bus behavior - module is slower/less reliable

### 15. MODULE 0x28 (TCM J1850) — real response format
   First read sometimes gets stale/multiline:
     20 00 00 -> first try: 2D 28 02 51 (stale) + 26 28 7F 22 21 00 78 (NRC)
     20 00 00 -> second try: 26 28 62 56 04 00 48 (positive)
   Unsolicited: 2D 28 02 51 appears periodically

### 16. REAL ATZ RESPONSE FORMAT
   ATZ -> echo "ATZ" + \xFC (unknown byte) + delay ~800ms + "OBDII  v1.5"
   Note: "OBDII  v1.5" NOT "ELM327 v1.5"! Two spaces!
