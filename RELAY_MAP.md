# WJ 2.7 CRD — J1850 Actuator Command Reference

ATRA is mandatory. Init: ATSP2 → ATIFR0 → ATH1 → ATSH24xx22 → ATRAxx → ATSH24xx2F → command

## Driver Door 0xA0 — Sequential (`38 PID 12` ON / `38 PID 00` OFF)

Header: `ATSH24A02F` | Filter: `ATRAA0`

| PID | Actuator | ON | OFF |
|-----|----------|-----|-----|
| 0x00 | Front Window Down | `38 00 12` | `38 00 00` |
| 0x01 | Front Window Up | `38 01 12` | `38 01 00` |
| 0x02 | Lock | `38 02 12` | `38 02 00` |
| 0x03 | Mirror Down | `38 03 12` | `38 03 00` |
| 0x04 | Mirror Heater | `38 04 12` | `38 04 00` |
| 0x05 | Mirror Left | `38 05 12` | `38 05 00` |
| 0x06 | Mirror Right | `38 06 12` | `38 06 00` |
| 0x07 | Mirror Up | `38 07 12` | `38 07 00` |
| 0x08 | Rear Window Down | `38 08 12` | `38 08 00` |
| 0x09 | Rear Window Up | `38 09 12` | `38 09 00` |
| 0x0A | Switch Illumination | `38 0A 12` | `38 0A 00` |
| 0x0B | Unlock | `38 0B 12` | `38 0B 00` |
| 0x0C | Mirror Fold In | `38 0C 12` | `38 0C 00` |
| 0x0D | Mirror Fold Out | `38 0D 12` | `38 0D 00` |

## Passenger Door 0xA1 — Sequential (SAME pattern as 0xA0)

Header: `ATSH24A12F` | Filter: `ATRAA1`

Same PID table as Driver Door. Both doors use `38 PID 12` ON / `38 PID 00` OFF.
All PIDs 0x01-0x0E confirmed working.

## Body Computer 0x40 — Relay

Header: `ATSH24402F` | Filter: `ATRA40`

| Actuator | ON | OFF | Notes |
|----------|-----|-----|-------|
| Horn | `38 00 CC` | `38 00 00` | Hold to honk |
| Hazard | `38 01 00` | `38 01 01` | INVERTED |
| Hi Beam | `38 00 FF` | `38 00 00` | |
| Low Beam | `38 02 05` | `38 02 00` | |
| Park Lamp | `38 09 00` | `38 09 01` | INVERTED |
| Mirror Right | `38 06 20` | `38 06 00` | Bitmask |
| Mirror Left | `38 06 10` | `38 06 00` | Bitmask |
| Mirror Heater | `38 06 08` | `38 06 00` | Bitmask |
| Lock | `38 06 02` | `38 06 00` | Bitmask |
| Rear Win Up | `38 06 04` | `38 06 00` | Bitmask |
| Illumination | `38 0D 01` | `38 0D 00` | |
| Window Down | `38 08 01` | `38 08 00` | |
| Window Up | `38 07 01` | `38 07 00` | |
| Release All | `3A 02 FF` | — | |

## Instrument Cluster 0x61 — SID 0x3A Gauge Test

Header: `ATSH246122` | Filter: `ATRA61`

| Command | Function | OFF |
|---------|----------|-----|
| `3A 00 80` | Speedo | `3A 00 00` |
| `3A 00 40` | Tacho | `3A 00 00` |
| `3A 00 08` | Fuel gauge | `3A 00 00` |
| `3A 00 04` | Temp gauge | `3A 00 00` |
| `3A 01 01` | Oil lamp | `3A 01 00` |
| `3A 01 04` | CE lamp | `3A 01 00` |

## DTC Clear — Mode 0x14

| Module | Header | Command | Response |
|--------|--------|---------|----------|
| Body 0x40 | `ATSH244014` + `ATRA40` | `FF 00 00` | `26 40 54 FF 00 00 39` |
| ABS 0x28 | `ATSH242814` + `ATRA28` | `FF 00 00` | `26 28 54 FF 00 00 10` |
| Rain 0xA7 | `ATSH24A714` + `ATRAA7` | `FF 00 00` | `26 A7 54 FF 00 00 4F` |

## Dead Modules

- 0x80: NO DATA (not Body Computer — use 0x40 instead)
- 0x2A: Overhead Console — NO DATA on EU vehicle
- 0x58: ESP DTC clear fails (NO DATA for mode 0x14)
