#include "elm327_emu.h"

// Hex helpers
static String hex8(uint8_t v) {
    char b[4]; snprintf(b, sizeof(b), "%02X", v); return b;
}
static int parseHexByte(const char *s) {
    int v = 0;
    for (int i = 0; i < 2 && s[i]; i++) {
        v <<= 4;
        char c = toupper(s[i]);
        if (c >= '0' && c <= '9') v |= c - '0';
        else if (c >= 'A' && c <= 'F') v |= c - 'A' + 10;
        else return -1;
    }
    return v;
}

void ELM327Emu::reset() {
    targetModule = 0x20; headerMode = 0x22;
    protocol = 0; headers = false; echo = true;
    ecuUnlocked = false; tcmUnlocked = false; ecuDtcCleared = false;
    memset(j1850DtcCleared, 0, sizeof(j1850DtcCleared));
    lastHeader = ""; cmdCount = 0; _t0 = millis();
}

void ELM327Emu::tick() {
    float t = (millis() - _t0) / 1000.0f;
    engineRpm = 750 + 30 * sin(t * 0.5f) + random(-10, 10);
    coolantTemp = min(95.0f, 20.0f + t * 0.05f);
    transTemp = min(130.0f, 77.0f + t * 0.02f);
}

// ==================== AT Commands ====================

String ELM327Emu::handleAT(const String &cmd) {
    String c = cmd;
    c.toUpperCase(); c.trim();

    if (c.startsWith("ATZ")) { echo = true; reset(); return "OBDII  v1.5"; }
    if (c == "ATI")  return "OBDII  v1.5";
    if (c == "AT@1") return "OBDII to RS232 Interpreter";
    if (c == "AT@2") return "?";
    if (c == "ATRV") {
        char buf[16]; float v = 14.2f + random(-20,20)/100.0f;
        snprintf(buf, sizeof(buf), "%.1fV", v); return buf;
    }
    if (c.startsWith("ATE")) { echo = (c.indexOf('1') >= 0); return "OK"; }
    if (c.startsWith("ATSP")) { protocol = c.charAt(4) - '0'; return "OK"; }
    if (c.startsWith("ATSH")) {
        String hdr = c.substring(4); hdr.trim(); lastHeader = hdr;
        if (hdr.length() >= 6 && hdr.startsWith("24")) {
            targetModule = parseHexByte(hdr.c_str() + 2);
            headerMode   = parseHexByte(hdr.c_str() + 4);
        } else if (hdr.length() >= 6 && hdr.startsWith("81")) {
            klTarget = parseHexByte(hdr.c_str() + 2);
            targetModule = klTarget;
        } else if (hdr.length() >= 4) {
            targetModule = parseHexByte(hdr.c_str() + 2);
        }
        return "OK";
    }
    if (c.startsWith("ATH")) { headers = (c.indexOf('1') >= 0); return "OK"; }
    if (c.startsWith("ATRA") || c.startsWith("ATAR")) return "OK";
    if (c.startsWith("ATWM")) return "OK";
    if (c.startsWith("ATFI")) return "OK"; // framing handled in main.cpp
    if (c.startsWith("ATIFR") || c.startsWith("ATL") || c.startsWith("ATM") ||
        c.startsWith("ATAT") || c.startsWith("ATST") || c.startsWith("ATS")) return "OK";
    if (c.startsWith("ATDP")) return protocol == 2 ? "SAE J1850 VPW" : "ISO 14230-4";
    if (c.startsWith("AT")) return "OK";
    return "?";
}

// ==================== Main Process ====================

String ELM327Emu::processCommand(const String &rawCmd) {
    String cmd = rawCmd; cmd.trim();
    if (cmd.length() == 0) return "";
    cmdCount++;
    if (cmd.startsWith("AT") || cmd.startsWith("at")) return handleAT(cmd);

    // Parse hex bytes
    uint8_t bytes[32]; int nBytes = 0;
    String clean = cmd; clean.replace(" ", ""); clean.toUpperCase();
    for (int i = 0; i + 1 < (int)clean.length() && nBytes < 32; i += 2) {
        int v = parseHexByte(clean.c_str() + i);
        if (v < 0) return "?";
        bytes[nBytes++] = (uint8_t)v;
    }
    if (nBytes == 0) return "?";
    tick();

    if (protocol == 2) {
        // J1850: try PCAP lookup first, then generic fallback
        String cmdUpper = cmd; cmdUpper.toUpperCase(); cmdUpper.trim();
        String pcap = j1850_pcap_lookup(targetModule, headerMode, cmdUpper);
        if (pcap.length() > 0) return pcap;
        return j1850_generic(targetModule, headerMode, bytes[0], bytes + 1, nBytes - 1);
    }
    return kwpProcess(bytes[0], bytes + 1, nBytes - 1);
}

// ==================== J1850 Generic Fallback ====================

String ELM327Emu::j1850_generic(uint8_t t, uint8_t m, uint8_t sid, const uint8_t *data, int dlen) {
    uint8_t pid = (dlen > 0) ? data[0] : 0;
    uint8_t val = (dlen > 1) ? data[1] : 0;

    // Mode 0x14 DTC Clear
    if (m == 0x14) {
        if (t == 0x58) return "NO DATA";  // ESP: real vehicle returns NO DATA
        j1850DtcCleared[t] = true;
        return "26 " + hex8(t) + " 54 FF 00 00 DD";
    }
    // Mode 0x18 DTC Read
    if (m == 0x18) {
        if (j1850DtcCleared[t]) {
            return "26 " + hex8(t) + " 58 00";
        }
        // Per-module DTC data
        if (t == 0x40) return "26 40 58 01 08 20 20 DD";    // Body: 1 DTC
        if (t == 0x28) return "26 28 58 01 22 10 20 DD";    // ABS: 1 DTC
        if (t == 0xA7) return "26 A7 58 01 05 11 20 DD";    // Rain Sensor: 1 DTC
        if (t == 0x58) return "26 58 58 01 50 10 20 DD";    // ESP: 1 DTC
        if (t == 0x60) return "26 60 58 02 60 10 20 60 15 20 DD"; // Airbag: 2 DTCs
        if (t == 0x61) return "26 61 58 01 61 30 20 DD";    // Cluster: 1 DTC
        if (t == 0x68) return "26 68 58 01 68 20 20 DD";    // HVAC: 1 DTC
        if (t == 0x98) return "26 98 58 01 98 10 20 DD";    // Mem Seat: 1 DTC
        if (t == 0xA0) return "26 A0 58 01 A0 11 20 DD";    // Driver Door: 1 DTC
        if (t == 0xA1) return "26 A1 58 01 A1 11 20 DD";    // Passenger Door: 1 DTC
        if (t == 0xC0) return "26 C0 58 01 C0 10 20 DD";    // Park Assist: 1 DTC
        return "26 " + hex8(t) + " 58 00";                   // No DTCs
    }

    // Mode 0x10 ECUReset
    if (m == 0x10) return "26 " + hex8(t) + " 50 " + hex8(sid);
    // Mode 0x11 DiagSession
    if (m == 0x11) {
        if (sid == 0x01) return "26 " + hex8(t) + " 51 01";
        return "26 " + hex8(t) + " 7F 11 12 00 DD";
    }
    // Mode 0x2F IOControl
    if (m == 0x2F) {
        if (sid == 0x38) return "26 " + hex8(t) + " 6F 38 " + hex8(pid) + " " + hex8(val) + " DD";
        if (sid == 0x3A) return "26 " + hex8(t) + " 6F 3A " + hex8(pid) + " " + hex8(val) + " DD";
        return "26 " + hex8(t) + " 7F 2F 12 00 DD";
    }
    // Mode 0xB4
    if (m == 0xB4) {
        if (sid == 0x28) return "26 " + hex8(t) + " E8 " + hex8(pid) + " 00 00 DD";
        if (sid == 0x38) return "26 " + hex8(t) + " 6F 38 " + hex8(pid) + " " + hex8(val) + " DD";
        return "26 " + hex8(t) + " 7F B4 12 00 DD";
    }
    // BCM (0x80) and EVIC (0x2A) = NO DATA on real vehicle
    if (t == 0x80 || t == 0x2A) return "NO DATA";
    // Electro Mech Cluster (0x60) = ALL NRC on real vehicle (PCAP confirmed)
    if (t == 0x60) return "26 60 7F 22 22 00 44";
    // Generic positive for read modes
    return "26 " + hex8(t) + " 62 " + hex8(pid) + " 00 00 DD";
}
// J1850 response database from real vehicle PCAP captures
// WJ 2.7 CRD 2003 EU, WiFi ELM327, 2026-03-12
String ELM327Emu::j1850_pcap_lookup(uint8_t t, uint8_t m, const String &cmdStr) {
    // Lookup exact match from real vehicle captures
    if (t == 0x28) {
        if (m == 0x22) {
            if (cmdStr == "20 00 00") return "26 28 62 56 04 00 48";  // real vehicle
            if (cmdStr == "20 01 00") return "26 28 62 18 21 00 35";
            if (cmdStr == "20 02 00") return "26 28 62 41 41 00 51";  // real vehicle
            if (cmdStr == "20 07 00") return "26 28 62 0B 00 00 98";
            if (cmdStr == "20 08 00") return "26 28 62 03 00 00 94";
            if (cmdStr == "24 00 00") return "26 28 62 08 02 00 8C";
            if (cmdStr == "28 01 00") return "26 28 62 20 02 00 B0";
            if (cmdStr == "2E 10 00") return "26 28 62 00 00 FF DC";
            if (cmdStr == "2E 11 00") return "26 28 62 00 FF FF 9D";
            if (cmdStr == "2E 12 00") return "26 28 62 00 00 00 18";
            if (cmdStr == "2E 13 00") return "26 28 62 00 FF FF 9D";
            if (cmdStr == "2E 14 00") return "26 28 62 00 00 00 18";
            if (cmdStr == "2E 15 00") return "26 28 62 00 00 00 18";
            if (cmdStr == "2E 16 00") return "26 28 7F 22 21 00 78";
            if (cmdStr == "2E 17 00") return "26 28 62 00 FF FF 9D";
            if (cmdStr == "2E 20 00") return "26 28 7F 22 21 00 78";
            if (cmdStr == "2E 21 00") return "26 28 62 00 8F 00 72";
            if (cmdStr == "2E 22 00") return "26 28 62 00 00 00 18";
            if (cmdStr == "2E 23 00") return "26 28 62 00 8E 99 20";
            if (cmdStr == "2E 24 00") return "26 28 62 00 FF FF 9D";
            if (cmdStr == "2E 25 00") return "26 28 62 00 00 00 18";
            if (cmdStr == "2E 26 00") return "26 28 62 00 FF FF 9D";
            if (cmdStr == "2E 27 00") return "26 28 7F 22 21 00 78";
            if (cmdStr == "2E 30 00") return "26 28 62 00 FF FF 9D";
        }
    }
    if (t == 0x2A) {
        if (m == 0x22) {
            if (cmdStr == "20 00 00") return "NO DATA";
        }
    }
    if (t == 0x40) {
        if (m == 0x22) {
            if (cmdStr == "20 00 00") return "26 40 62 05 08 00 E2";
            if (cmdStr == "20 01 00") return "26 40 62 38 92 00 F0";
            if (cmdStr == "20 02 00") return "26 40 62 41 43 00 E0";
            if (cmdStr == "20 06 00") return "26 40 62 02 00 00 32";
            if (cmdStr == "24 00 00") return "26 40 62 43 00 00 DD";
            if (cmdStr == "28 00 00") return "26 40 62 0D 00 00 B4";
            if (cmdStr == "28 02 00") return "26 40 62 20 03 00 D5";
            if (cmdStr == "28 04 00") return "26 40 62 00 00 00 31";
            if (cmdStr == "28 07 00") return "26 40 62 14 00 00 2F";
            if (cmdStr == "2E 0D 00") return "26 40 62 00 40 00 DB";
            if (cmdStr == "32 02 00") return "26 40 62 A7 00 00 4B";
            if (cmdStr == "32 04 00") return "26 40 62 AE 00 00 C8";
            if (cmdStr == "32 05 00") return "26 40 62 E2 00 00 A2";
            if (cmdStr == "32 14 00") return "26 40 62 E3 00 00 2D";
            if (cmdStr == "32 15 00") return "26 40 62 1A 00 00 26";
            if (cmdStr == "32 16 00") return "26 40 62 1A 00 00 26";
            if (cmdStr == "32 17 00") return "26 40 62 C3 00 00 1D";
            if (cmdStr == "32 18 00") return "26 40 62 C3 00 00 1D";
            if (cmdStr == "32 21 00") return "26 40 62 43 00 00 DD";
            if (cmdStr == "32 26 00") return "26 40 62 65 00 00 E8";
            if (cmdStr == "32 27 00") return "26 40 62 FF 00 00 3F";
            if (cmdStr == "32 28 00") return "26 40 62 E5 00 00 28";
            if (cmdStr == "36 00 00") return "26 40 62 7F 00 00 FF";
            if (cmdStr == "36 03 00") return "26 40 62 00 00 00 31";
            if (cmdStr == "36 0E 00") return "26 40 62 00 00 00 31";
        }
        if (m == 0x2F) {
            if (cmdStr == "38 02 01") return "26 40 6F 38 02 01 DF";
            if (cmdStr == "38 02 00") return "26 40 6F 38 02 00 C2";
            if (cmdStr == "38 06 02") return "26 40 6F 38 06 02 D5";
            if (cmdStr == "38 06 00") return "26 40 6F 38 06 00 EF";
            if (cmdStr == "38 06 04") return "26 40 6F 38 06 04 9B";
            if (cmdStr == "38 06 08") return "26 40 6F 38 06 08 07";
            if (cmdStr == "38 06 10") return "26 40 6F 38 06 10 22";
            if (cmdStr == "38 06 20") return "26 40 6F 38 06 20 68";
            if (cmdStr == "38 07 01") return "26 40 6F 38 07 01 BE";
            if (cmdStr == "38 07 00") return "26 40 6F 38 07 00 A3";
            if (cmdStr == "38 08 01") return "26 40 6F 38 08 01 1D";
            if (cmdStr == "38 08 00") return "26 40 6F 38 08 00 00";
            if (cmdStr == "38 08 02") return "26 40 6F 38 08 02 3A";
            if (cmdStr == "38 0C 02") return "26 40 6F 38 0C 02 17";
            if (cmdStr == "38 0C 00") return "26 40 6F 38 0C 00 2D";
            if (cmdStr == "38 0D 01") return "26 40 6F 38 0D 01 7C";
            if (cmdStr == "38 0D 00") return "26 40 6F 38 0D 00 61";
            if (cmdStr == "3A 02 FF") return "26 40 6F 3A 02 FF 05";
            // Body Computer hazard/horn/lights (from body_computer.pcap)
            if (cmdStr == "38 00 CC") return "26 40 6F 38 00 CC 5F";
            if (cmdStr == "38 00 FF") return "26 40 6F 38 00 FF 74";
            if (cmdStr == "38 01 00") return "26 40 6F 38 01 00 84";
            if (cmdStr == "38 01 01") return "26 40 6F 38 01 01 21";
            if (cmdStr == "38 02 05") return "26 40 6F 38 02 05 3E";
            if (cmdStr == "38 09 00") return "26 40 6F 38 09 00 75";
            if (cmdStr == "38 09 01") return "26 40 6F 38 09 01 D0";
        }
    }
    if (t == 0x58) {
        if (m == 0x22) {
            if (cmdStr == "20 00 00") return "26 58 62 56 03 00 01";
            if (cmdStr == "20 01 00") return "26 58 62 87 92 00 07";
            if (cmdStr == "20 02 00") return "26 58 62 41 41 00 E1";
            if (cmdStr == "20 07 00") return "26 58 62 58 00 00 DC";
            if (cmdStr == "24 00 00") return "26 58 62 05 00 00 21";
            if (cmdStr == "28 00 00") return "26 58 62 0D 00 00 2D";
            if (cmdStr == "2E 10 00") return "26 58 62 00 00 00 A8";
            if (cmdStr == "2E 13 00") return "26 58 62 00 00 00 A8";
            if (cmdStr == "2E 16 00") return "26 58 62 00 00 00 A8";
            if (cmdStr == "2E 19 00") return "26 58 62 00 00 00 A8";
            if (cmdStr == "2E 1C 00") return "26 58 62 00 00 00 A8";
            if (cmdStr == "2E 1F 00") return "26 58 62 00 00 00 A8";
            if (cmdStr == "2E 22 00") return "26 58 62 00 00 00 A8";
            if (cmdStr == "2E 25 00") return "26 58 62 00 00 00 A8";
            if (cmdStr == "2E 28 00") return "26 58 62 00 00 00 A8";
            if (cmdStr == "2E 2B 00") return "26 58 62 00 00 00 A8";
            if (cmdStr == "2E 2E 00") return "26 58 62 00 00 00 A8";
            if (cmdStr == "2E 37 00") return "26 58 62 00 00 00 A8";
            if (cmdStr == "2E 3A 00") return "26 58 62 00 00 00 A8";
            if (cmdStr == "2E 3D 00") return "26 58 62 00 00 00 A8";
            if (cmdStr == "2E 43 00") return "26 58 62 00 00 00 A8";
            if (cmdStr == "2E 49 00") return "26 58 62 00 00 00 A8";
            if (cmdStr == "2E 4C 00") return "26 58 62 00 00 00 A8";
            if (cmdStr == "2E 4F 00") return "26 58 62 00 00 00 A8";
            if (cmdStr == "2E 52 00") return "26 58 62 00 00 00 A8";
            if (cmdStr == "2E 70 00") return "26 58 62 00 00 00 A8";
            if (cmdStr == "2E 73 00") return "26 58 62 00 00 00 A8";
            if (cmdStr == "2E 76 00") return "26 58 62 00 00 00 A8";
            if (cmdStr == "2E 79 00") return "26 58 62 00 00 00 A8";
            if (cmdStr == "2E 7C 00") return "26 58 62 00 00 00 A8";
            if (cmdStr == "2E 7F 00") return "26 58 62 00 00 00 A8";
            if (cmdStr == "2E 82 00") return "26 58 62 00 00 00 A8";
            if (cmdStr == "2E 85 00") return "26 58 62 00 00 00 A8";
            if (cmdStr == "2E 88 00") return "26 58 62 00 00 00 A8";
            if (cmdStr == "2E 8B 00") return "26 58 62 00 00 00 A8";
            if (cmdStr == "2E 8E 00") return "26 58 62 00 00 00 A8";
            if (cmdStr == "2E 91 00") return "26 58 62 00 00 00 A8";
            if (cmdStr == "2E 94 00") return "26 58 62 00 00 00 A8";
            if (cmdStr == "2E 97 00") return "26 58 62 00 00 00 A8";
            if (cmdStr == "2E DC 00") return "26 58 62 00 00 00 A8";
            if (cmdStr == "2E DF 00") return "26 58 62 00 00 00 A8";
            if (cmdStr == "2E E2 00") return "26 58 62 00 00 00 A8";
            if (cmdStr == "2E E5 00") return "26 58 62 00 00 00 A8";
            if (cmdStr == "2E FA 00") return "26 58 62 00 00 00 A8";
            if (cmdStr == "2E FD 00") return "26 58 62 00 00 00 A8";
            if (cmdStr == "2F 00 00") return "26 58 62 00 00 00 A8";
            if (cmdStr == "2F 03 00") return "26 58 62 00 00 00 A8";
            if (cmdStr == "2F 06 00") return "26 58 62 00 00 00 A8";
            if (cmdStr == "2F 18 00") return "26 58 62 00 00 00 A8";
            if (cmdStr == "2F 1B 00") return "26 58 62 00 00 00 A8";
            if (cmdStr == "2F 1E 00") return "26 58 62 00 00 00 A8";
            if (cmdStr == "2F 21 00") return "26 58 62 00 00 00 A8";
            if (cmdStr == "2F 24 00") return "26 58 62 00 00 00 A8";
            if (cmdStr == "2F 27 00") return "26 58 62 00 00 00 A8";
            if (cmdStr == "2F 2A 00") return "26 58 62 00 00 00 A8";
            if (cmdStr == "2F 2D 00") return "26 58 62 00 00 00 A8";
            if (cmdStr == "2F 30 00") return "26 58 62 00 00 00 A8";
            if (cmdStr == "2F 33 00") return "26 58 62 00 00 00 A8";
            if (cmdStr == "2F 36 00") return "26 58 62 00 00 00 A8";
            if (cmdStr == "2F 39 00") return "26 58 62 00 00 00 A8";
            if (cmdStr == "2F 3C 00") return "26 58 62 00 00 00 A8";
        }
    }
    // 0x60: ALL NRC on real vehicle (PCAP confirmed, pcap_lookup)
    if (t == 0x60) return "26 60 7F 22 22 00 44";
    if (t == 0x61) {
        if (m == 0x22) {
            if (cmdStr == "20 00 00") return "26 61 62 52 10 00 55";
            if (cmdStr == "20 01 00") return "26 61 62 44 67 00 9A";
            if (cmdStr == "20 02 00") return "26 61 62 41 47 00 66";
            if (cmdStr == "24 00 00") return "26 61 62 02 03 01 50";
            if (cmdStr == "28 00 00") return "26 61 62 0D 00 00 1F";
            if (cmdStr == "28 01 00") return "26 61 62 20 02 00 32";
            // Cluster gauge test (SID 0x3A)
            if (cmdStr == "3A 00 80") return "26 61 62 3A 00 80 9B";
            if (cmdStr == "3A 00 40") return "26 61 62 3A 00 40 AE";
            if (cmdStr == "3A 00 20") return "26 61 62 3A 00 20 3A";
            if (cmdStr == "3A 00 10") return "26 61 62 3A 00 10 DD";
            if (cmdStr == "3A 00 08") return "26 61 62 3A 00 08 55";
            if (cmdStr == "3A 00 04") return "26 61 62 3A 00 04 C9";
            if (cmdStr == "3A 00 02") return "26 61 62 3A 00 02 87";
            if (cmdStr == "3A 00 01") return "26 61 62 3A 00 01 A0";
            if (cmdStr == "3A 00 00") return "26 61 62 3A 00 00 DD";
            if (cmdStr == "3A 01 01") return "26 61 62 3A 01 01 EC";
            if (cmdStr == "3A 01 04") return "26 61 62 3A 01 04 85";
            if (cmdStr == "3A 01 02") return "26 61 62 3A 01 02 CB";
            if (cmdStr == "3A 01 00") return "26 61 62 3A 01 00 DD";
        }
    }
    if (t == 0x68) {
        if (m == 0x22) {
            if (cmdStr == "20 00 00") return "26 68 62 56 00 00 FA";
            if (cmdStr == "20 01 00") return "26 68 62 04 00 00 81";
            if (cmdStr == "20 02 00") return "26 68 62 27 00 00 3D";
            if (cmdStr == "20 03 00") return "26 68 62 54 00 00 F9";
            if (cmdStr == "20 04 00") return "26 68 62 01 00 00 08";
            if (cmdStr == "20 05 00") return "26 68 62 08 00 00 8B";
            if (cmdStr == "20 06 00") return "26 68 62 01 00 00 08";
            if (cmdStr == "24 00 00") return "26 68 62 35 00 00 26";
            if (cmdStr == "28 00 00") return "26 68 62 47 00 00 6D";
            if (cmdStr == "28 10 00") return "26 68 62 0A 00 00 88";
            if (cmdStr == "2E 02 00") return "26 68 62 00 00 00 87";
            if (cmdStr == "2E 05 00") return "26 68 62 00 00 00 87";
            if (cmdStr == "2E 08 00") return "26 68 62 00 00 00 87";
        }
    }
    if (t == 0x80) {
        if (m == 0x22) {
            if (cmdStr == "20 00 00") return "NO DATA";
        }
    }
    if (t == 0x98) {
        if (m == 0x22) {
            if (cmdStr == "24 00 00") return "26 98 62 11 04 00 AE";
        }
    }
    if (t == 0xA0) {
        if (m == 0x22) {
            if (cmdStr == "20 00 00") return "26 A0 62 63 44 00 57";
            if (cmdStr == "20 01 00") return "26 A0 62 58 39 00 EF";
            if (cmdStr == "20 02 00") return "26 A0 62 41 43 00 9D";
            if (cmdStr == "24 00 00") return "26 A0 62 34 00 00 62";
            if (cmdStr == "28 00 00") return "26 A0 62 00 00 00 4C";
            if (cmdStr == "32 00 00") return "26 A0 62 00 00 00 4C";
            if (cmdStr == "32 01 00") return "26 A0 62 00 00 00 4C";
            if (cmdStr == "32 02 00") return "26 A0 62 94 00 00 92";
            if (cmdStr == "32 03 00") return "26 A0 62 FD 00 00 41";
            if (cmdStr == "32 04 00") return "26 A0 62 FF 00 00 42";
        }
        if (m == 0x2F) {
            if (cmdStr == "38 00 12") return "26 A0 6F 38 00 12 D0";
            if (cmdStr == "38 00 00") return "26 A0 6F 38 00 00 27";
            if (cmdStr == "38 01 12") return "26 A0 6F 38 01 12 9C";
            if (cmdStr == "38 01 00") return "26 A0 6F 38 01 00 6B";
            if (cmdStr == "38 02 12") return "26 A0 6F 38 02 12 48";
            if (cmdStr == "38 02 00") return "26 A0 6F 38 02 00 BF";
            if (cmdStr == "38 03 12") return "26 A0 6F 38 03 12 04";
            if (cmdStr == "38 03 00") return "26 A0 6F 38 03 00 F3";
            if (cmdStr == "38 04 12") return "26 A0 6F 38 04 12 FD";
            if (cmdStr == "38 04 00") return "26 A0 6F 38 04 00 0A";
            if (cmdStr == "38 05 12") return "26 A0 6F 38 05 12 B1";
            if (cmdStr == "38 05 00") return "26 A0 6F 38 05 00 46";
            if (cmdStr == "38 06 12") return "26 A0 6F 38 06 12 65";
            if (cmdStr == "38 06 00") return "26 A0 6F 38 06 00 92";
            if (cmdStr == "38 07 12") return "26 A0 6F 38 07 12 29";
            if (cmdStr == "38 07 00") return "26 A0 6F 38 07 00 DE";
            if (cmdStr == "38 08 12") return "26 A0 6F 38 08 12 8A";
            if (cmdStr == "38 08 00") return "26 A0 6F 38 08 00 7D";
            if (cmdStr == "38 09 12") return "26 A0 6F 38 09 12 C6";
            if (cmdStr == "38 09 00") return "26 A0 6F 38 09 00 31";
            if (cmdStr == "38 0A 12") return "26 A0 6F 38 0A 12 12";
            if (cmdStr == "38 0A 00") return "26 A0 6F 38 0A 00 E5";
            if (cmdStr == "38 0B 12") return "26 A0 6F 38 0B 12 5E";
            if (cmdStr == "38 0B 00") return "26 A0 6F 38 0B 00 A9";
            if (cmdStr == "38 0C 12") return "26 A0 6F 38 0C 12 A7";
            if (cmdStr == "38 0C 00") return "26 A0 6F 38 0C 00 50";
            if (cmdStr == "38 0D 12") return "26 A0 6F 38 0D 12 EB";
            if (cmdStr == "38 0D 00") return "26 A0 6F 38 0D 00 1C";
            if (cmdStr == "38 0E 12") return "26 A0 6F 38 0E 12 3F";
            if (cmdStr == "38 0E 00") return "26 A0 6F 38 0E 00 C8";
            if (cmdStr == "38 0F 12") return "26 A0 6F 38 0F 12 73";
            if (cmdStr == "38 0F 00") return "26 A0 6F 38 0F 00 84";
            if (cmdStr == "3A 02 FF") return "26 A0 7F 2F 12 00 BC";
        }
    }
    if (t == 0xA1) {
        if (m == 0x2F) {
            // Passenger Door relay (sequential, same as 0xA0 pattern)
            if (cmdStr == "38 00 12") return "26 A1 6F 38 00 12 BA";
            if (cmdStr == "38 00 00") return "26 A1 6F 38 00 00 4D";
            if (cmdStr == "38 01 12") return "26 A1 6F 38 01 12 F6";
            if (cmdStr == "38 01 00") return "26 A1 6F 38 01 00 01";
            if (cmdStr == "38 02 12") return "26 A1 6F 38 02 12 22";
            if (cmdStr == "38 02 00") return "26 A1 6F 38 02 00 D5";
            if (cmdStr == "38 03 12") return "26 A1 6F 38 03 12 6E";
            if (cmdStr == "38 03 00") return "26 A1 6F 38 03 00 99";
            if (cmdStr == "38 04 12") return "26 A1 6F 38 04 12 97";
            if (cmdStr == "38 04 00") return "26 A1 6F 38 04 00 60";
            if (cmdStr == "38 05 12") return "26 A1 6F 38 05 12 DB";
            if (cmdStr == "38 05 00") return "26 A1 6F 38 05 00 2C";
            if (cmdStr == "38 06 12") return "26 A1 6F 38 06 12 0F";
            if (cmdStr == "38 06 00") return "26 A1 6F 38 06 00 F8";
            if (cmdStr == "38 07 12") return "26 A1 6F 38 07 12 43";
            if (cmdStr == "38 07 00") return "26 A1 6F 38 07 00 B4";
            if (cmdStr == "38 08 12") return "26 A1 6F 38 08 12 E0";
            if (cmdStr == "38 08 00") return "26 A1 6F 38 08 00 17";
            if (cmdStr == "38 09 12") return "26 A1 6F 38 09 12 AC";
            if (cmdStr == "38 09 00") return "26 A1 6F 38 09 00 5B";
            if (cmdStr == "38 0A 12") return "26 A1 6F 38 0A 12 78";
            if (cmdStr == "38 0B 12") return "26 A1 6F 38 0B 12 34";
            if (cmdStr == "38 0B 00") return "26 A1 6F 38 0B 00 C3";
            if (cmdStr == "38 0C 12") return "26 A1 6F 38 0C 12 CD";
            if (cmdStr == "38 0D 12") return "26 A1 6F 38 0D 12 81";
            if (cmdStr == "38 0E 12") return "26 A1 6F 38 0E 12 55";
        }
        if (m == 0x22) {
            if (cmdStr == "20 00 00") return "26 A1 62 64 44 00 B7";
            if (cmdStr == "20 01 00") return "26 A1 62 58 39 00 85";
            if (cmdStr == "20 02 00") return "26 A1 62 41 00 00 C9";
            if (cmdStr == "24 00 00") return "26 A1 62 34 00 00 08";
            if (cmdStr == "28 00 00") return "26 A1 62 00 00 00 26";
            if (cmdStr == "32 00 00") return "26 A1 62 00 00 00 26";
            if (cmdStr == "32 01 00") return "26 A1 62 00 00 00 26";
            if (cmdStr == "32 02 00") return "26 A1 62 FF 00 00 28";
            if (cmdStr == "32 03 00") return "26 A1 62 FF 00 00 28";
        }
        if (m == 0x2F) {
            if (cmdStr == "38 01 12") return "26 A1 6F 38 01 12 F6";
            if (cmdStr == "38 02 12") return "NO DATA";
            if (cmdStr == "38 04 12") return "NO DATA";
            if (cmdStr == "38 05 12") return "26 A1 6F 38 05 12 DB";
            if (cmdStr == "38 06 12") return "26 A1 6F 38 06 12 0F";
            if (cmdStr == "38 07 12") return "26 A1 6F 38 07 12 43";
            if (cmdStr == "38 08 12") return "26 A1 6F 38 08 12 E0";
            if (cmdStr == "38 09 12") return "26 A1 6F 38 09 12 AC";
            if (cmdStr == "38 0A 12") return "26 A1 6F 38 0A 12 78";
            if (cmdStr == "38 0B 12") return "26 A1 6F 38 0B 12 34";
            if (cmdStr == "38 0C 12") return "26 A1 6F 38 0C 12 CD";
            if (cmdStr == "38 0D 12") return "26 A1 6F 38 0D 12 81";
            if (cmdStr == "38 0E 12") return "26 A1 6F 38 0E 12 55";
        }
    }
    if (t == 0xA7) {
        if (m == 0x22) {
            if (cmdStr == "20 00 00") return "26 A7 62 56 04 00 17";
            if (cmdStr == "20 01 00") return "26 A7 62 27 59 00 B5";
            if (cmdStr == "20 02 00") return "26 A7 62 41 46 00 F7";
            if (cmdStr == "24 00 00") return "26 A7 62 45 A5 00 73";
            if (cmdStr == "28 01 00") return "26 A7 62 20 02 00 EF";
            if (cmdStr == "2E 10 00") return "26 A7 62 00 00 00 47";
        }
    }
    if (t == 0xC0) {
        if (m == 0x22) {
            if (cmdStr == "20 00 00") return "26 C0 62 04 68 00 D1";
            if (cmdStr == "20 01 00") return "26 C0 62 66 65 00 B9";
            if (cmdStr == "20 02 00") return "26 C0 62 41 43 00 C3";
            if (cmdStr == "24 00 00") return "26 C0 62 D1 00 00 25";
        }
    }
    return ""; // not found in PCAP database
}
// ==================== KWP2000 (K-Line) ====================
static const uint8_t AK_T1[] = {0xC0,0xD0,0xE0,0xF0,0x00,0x10,0x20,0x30,0x40,0x50,0x60,0x70,0x80,0x90,0xA0,0xB0};
static const uint8_t AK_T2[] = {0x02,0x03,0x00,0x01,0x06,0x07,0x04,0x05,0x0A,0x0B,0x08,0x09,0x0E,0x0F,0x0C,0x0D};
static const uint8_t AK_T3[] = {0x90,0x80,0xF0,0xE0,0xD0,0xC0,0x30,0x20,0x10,0x00,0x70,0x60,0x50,0x40,0xB0,0xA0};
static const uint8_t AK_T4[] = {0x0D,0x0C,0x0F,0x0E,0x09,0x08,0x0B,0x0A,0x05,0x04,0x07,0x06,0x01,0x00,0x03,0x02};

String ELM327Emu::kwpWrap(const uint8_t *p, int plen) {
    // KWP frame: LEN F1 TARGET PAYLOAD CHECKSUM
    uint8_t frame[128];
    int flen;
    if (plen <= 63) {
        frame[0] = 0x80 | (plen & 0x3F);
        frame[1] = 0xF1;
        frame[2] = klTarget;
        memcpy(frame + 3, p, plen);
        flen = 3 + plen;
    } else {
        frame[0] = 0x80;
        frame[1] = 0xF1;
        frame[2] = klTarget;
        frame[3] = plen & 0xFF;
        memcpy(frame + 4, p, plen);
        flen = 4 + plen;
    }
    uint8_t cs = 0;
    for (int i = 0; i < flen; i++) cs += frame[i];
    frame[flen] = cs;
    flen++;

    if (headers) {
        String r;
        for (int i = 0; i < flen; i++) {
            if (i > 0) r += " ";
            r += hex8(frame[i]);
        }
        return r;
    }
    // No headers: just payload
    String r;
    int start = (frame[0] == 0x80) ? 4 : 3;
    for (int i = start; i < flen - 1; i++) {
        if (i > start) r += " ";
        r += hex8(frame[i]);
    }
    return r;
}

String ELM327Emu::kwpProcess(uint8_t sid, const uint8_t *data, int dlen) {
    // StartCommunication
    if (sid == 0x81) {
        uint8_t r[] = {0xC1, 0xEF, 0x8F};
        return kwpWrap(r, 3);
    }
    // TesterPresent
    if (sid == 0x3E) {
        uint8_t r[] = {0x7E, (dlen > 0) ? data[0] : (uint8_t)0x01};
        return kwpWrap(r, 2);
    }
    // Security
    if (sid == 0x27) {
        if (dlen > 0 && data[0] == 0x01) {
            if (klTarget == 0x20) {
                // TCM: static seed
                uint8_t r[] = {0x67, 0x01, 0x68, 0x24, 0x89};
                return kwpWrap(r, 5);
            } else {
                // ECU: dynamic seed
                ecuSeed = random(0x0100, 0xFFFE);
                uint8_t r[] = {0x67, 0x01, (uint8_t)(ecuSeed >> 8), (uint8_t)(ecuSeed & 0xFF)};
                return kwpWrap(r, 4);
            }
        }
        if (dlen >= 3 && data[0] == 0x02) {
            if (klTarget == 0x20) {
                if (data[1] == 0xCC && data[2] == 0x21) {
                    tcmUnlocked = true;
                    uint8_t r[] = {0x67, 0x02, 0x34};
                    return kwpWrap(r, 3);
                }
            } else {
                // Verify ArvutaKoodi
                uint8_t s0 = (ecuSeed >> 8) & 0xFF;
                uint8_t s1 = ecuSeed & 0xFF;
                uint8_t v1 = (s1 + 0x0B) & 0xFF;
                uint8_t keyLo = AK_T1[(v1>>4)&0xF] | AK_T2[v1&0xF];
                uint8_t cond = (s1 > 0x34) ? 1 : 0;
                uint8_t v2 = (s0 + cond + 1) & 0xFF;
                uint8_t keyHi = AK_T3[(v2>>4)&0xF] | AK_T4[v2&0xF];
                if (data[1] == keyHi && data[2] == keyLo) {
                    ecuUnlocked = true;
                    uint8_t r[] = {0x67, 0x02, 0x34};
                    return kwpWrap(r, 3);
                }
            }
            uint8_t r[] = {0x7F, 0x27, 0x35};
            return kwpWrap(r, 3);
        }
        uint8_t r[] = {0x7F, 0x27, 0x12};
        return kwpWrap(r, 3);
    }
    // ReadLocalData
    if (sid == 0x21) {
        uint8_t blk = (dlen > 0) ? data[0] : 0;
        if (klTarget == 0x20) {
            // TCM block 0x30
            if (blk == 0x30) {
                tick();
                uint16_t turbRpm = (uint16_t)max(0.0f, engineRpm * 0.97f);
                uint8_t traw = (uint8_t)(transTemp + 40);
                uint16_t tccSlip = 25;    // Des TCC slip
                uint16_t tccActual = 30;  // Actual TCC slip
                uint8_t r[] = {0x61, 0x30,
                    (uint8_t)(turbRpm>>8), (uint8_t)(turbRpm&0xFF), // Turbine RPM
                    (uint8_t)(tccSlip>>8), (uint8_t)(tccSlip&0xFF), // Des TCC slip
                    (uint8_t)(tccActual>>8), (uint8_t)(tccActual&0xFF), // Actual TCC slip
                    0x00, gearSel, 0x04,
                    0x00, 0xDD, traw,     // Fluid temp raw
                    0xFF, 0xF7, 0xFF, 0xF7,
                    0x00, 0x00, 0x96, 0x18, 0x00, 0x08};
                return kwpWrap(r, 24);
            }
            if (blk == 0x31) {
                uint16_t batV = (uint16_t)(14.2f * 100);   // Battery voltage
                uint16_t sensV = (uint16_t)(5.0f * 100);   // Sensor Supply
                uint16_t solV = (uint16_t)(12.5f * 100);   // Solenoid Supply
                uint16_t turbRpm = (uint16_t)max(0.0f, engineRpm * 0.97f);
                uint8_t r[] = {0x61,0x31,
                    (uint8_t)(batV>>8),(uint8_t)(batV&0xFF),   // Battery
                    (uint8_t)(sensV>>8),(uint8_t)(sensV&0xFF), // Sensor Supply
                    (uint8_t)(solV>>8),(uint8_t)(solV&0xFF),   // Solenoid Supply
                    (uint8_t)(turbRpm>>8),(uint8_t)(turbRpm&0xFF), // turbine RPM
                    0x00,0x00, 0x00,0x00, 0x00,0x00,
                    0x00,0x00, 0x00,0x00, 0x00,0x00};
                return kwpWrap(r, 22);
            }
            if (blk == 0x34) {
                tick();
                uint16_t fluidT = (uint16_t)((transTemp + 40.0f) * 10); // Fluid temp
                uint16_t tccP = (uint16_t)(55.0f * 10);    // TCC pressure PSI
                uint16_t shiftP = (uint16_t)(80.0f * 10);  // Shift PSI
                uint16_t modP = (uint16_t)(65.0f * 10);    // Modulation PSI
                float tps = (engineRpm > 850.0f) ? (engineRpm - 850.0f) * 0.05f : 0.0f;
                uint16_t tpsPct = (uint16_t)(tps * 10);    // TPS percent
                uint16_t uphill = (uint16_t)(0.0f * 10);   // Uphill Grad
                uint8_t r[] = {0x61,0x34,
                    (uint8_t)(fluidT>>8),(uint8_t)(fluidT&0xFF),
                    (uint8_t)(tccP>>8),(uint8_t)(tccP&0xFF),
                    (uint8_t)(shiftP>>8),(uint8_t)(shiftP&0xFF),
                    (uint8_t)(modP>>8),(uint8_t)(modP&0xFF),
                    (uint8_t)(tpsPct>>8),(uint8_t)(tpsPct&0xFF),
                    (uint8_t)(uphill>>8),(uint8_t)(uphill&0xFF),
                    0x00,0x28};
                return kwpWrap(r, 16);
            }
            if (blk == 0x33) {
                // Wheel speeds (all 0 when stopped)
                uint16_t lfW = 0, rfW = 0, lrW = 0, rrW = 0;
                uint16_t rearV = 0, frontV = 0;
                uint8_t r[] = {0x61,0x33,
                    (uint8_t)(lfW>>8),(uint8_t)(lfW&0xFF),     // LF wheel
                    (uint8_t)(rfW>>8),(uint8_t)(rfW&0xFF),     // RF wheel
                    (uint8_t)(lrW>>8),(uint8_t)(lrW&0xFF),     // LR wheel
                    (uint8_t)(rrW>>8),(uint8_t)(rrW&0xFF),     // RR wheel
                    (uint8_t)(rearV>>8),(uint8_t)(rearV&0xFF), // Rear Vehicle speed
                    (uint8_t)(frontV>>8),(uint8_t)(frontV&0xFF),// Front Vehicle speed
                    0x00,0x00, 0x00,0x00};
                return kwpWrap(r, 18);
            }
            if (blk == 0x32) {
                uint8_t r[] = {0x61,0x32,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00};
                return kwpWrap(r, 15);
            }
            // Other TCM blocks: return generic
            uint8_t r[18] = {0x61, blk};
            return kwpWrap(r, 18);
        }
        // ECU blocks — PCAP-verified format (2026-03-12)
        if (blk == 0x12) {
            // PCAP: 61 12 [34 bytes] — coolant, IAT, TPS, MAP, rail, AAP
            tick();
            uint16_t cr = (uint16_t)((coolantTemp + 273.1f) * 10);
            uint16_t ir = (uint16_t)((18.0f + 273.1f) * 10);
            float tps = (engineRpm > 850.0f) ? (engineRpm - 850.0f) * 0.05f : 0.0f;
            uint16_t tpsRaw = (uint16_t)(tps * 100);
            uint16_t mapVal = 920 + (uint16_t)(tps * 2.5f);
            uint16_t railVal = (uint16_t)((290.0f + tps * 5.0f) * 10);
            uint8_t r[36] = {0x61, 0x12,
                (uint8_t)(cr>>8), (uint8_t)(cr&0xFF),
                (uint8_t)(ir>>8), (uint8_t)(ir&0xFF),
                0x08, 0xB7, 0x08, 0xB7, 0x00, 0x00,
                0x04, 0x03,
                (uint8_t)(tpsRaw>>8), (uint8_t)(tpsRaw&0xFF),
                (uint8_t)(mapVal>>8), (uint8_t)(mapVal&0xFF),
                0x03, 0x91,
                (uint8_t)(railVal>>8), (uint8_t)(railVal&0xFF),
                0x02, 0x24, 0x01, 0x2A, 0x00, 0x68,
                0x03, 0xA0, 0x03, 0xA0, 0x00, 0x0B};
            return kwpWrap(r, 34);
        }
        if (blk == 0x20) {
            // PCAP: 61 20 [16 bytes] — MAF actual, MAF spec, rest static
            tick();
            float tps = (engineRpm > 850.0f) ? (engineRpm - 850.0f) * 0.05f : 0.0f;
            uint16_t mafAct = 420 + (uint16_t)(tps * 3.0f);
            uint16_t mafSpec = 360 + (uint16_t)(tps * 1.5f);
            uint8_t r[18] = {0x61, 0x20,
                (uint8_t)(mafAct>>8), (uint8_t)(mafAct&0xFF),
                (uint8_t)(mafSpec>>8), (uint8_t)(mafSpec&0xFF),
                0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
                0x00, 0x00, 0x00, 0x00};
            return kwpWrap(r, 16);
        }
        if (blk == 0x22) {
            // Barometric pressure, outside air temp, coolant/IAT voltages
            uint16_t barP = (uint16_t)(1013.0f);             // Barometric Pressure mbar
            uint16_t barPV = (uint16_t)(4.0f * 1000);        // Barometric Voltage mV
            uint16_t oat = (uint16_t)((22.0f + 40.0f) * 10); // Outside Air Temp (offset +40)
            uint16_t coolV = (uint16_t)(1.5f * 1000);        // Coolant Sensor Voltage
            uint16_t iatV = (uint16_t)(2.8f * 1000);         // Air Intake Volts
            uint16_t mafV = (uint16_t)(1.2f * 1000);         // Mass Air Flow Voltage
            uint8_t r[] = {0x61,0x22,
                (uint8_t)(barP>>8),(uint8_t)(barP&0xFF),
                (uint8_t)(barPV>>8),(uint8_t)(barPV&0xFF),
                (uint8_t)(oat>>8),(uint8_t)(oat&0xFF),
                (uint8_t)(coolV>>8),(uint8_t)(coolV&0xFF),
                (uint8_t)(iatV>>8),(uint8_t)(iatV&0xFF),
                (uint8_t)(mafV>>8),(uint8_t)(mafV&0xFF),
                0x00,0x00};
            return kwpWrap(r, 16);
        }
        if (blk == 0x28) {
            // PCAP: 61 28 [30 bytes] — RPM, injQty, injector data
            tick();
            uint16_t rpm = (uint16_t)engineRpm;
            float injQty = (engineRpm > 850.0f) ? 9.5f + (engineRpm - 850.0f) * 0.02f : 9.5f;
            uint16_t iq = (uint16_t)(injQty * 100);
            uint8_t r[32] = {0x61, 0x28,
                (uint8_t)(rpm>>8), (uint8_t)(rpm&0xFF),
                (uint8_t)(iq>>8), (uint8_t)(iq&0xFF),
                // Remaining 26 bytes: injector balance + other data
                0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
                0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
                0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
                0x00, 0x00, 0x00, 0x00, 0x00, 0x00};
            return kwpWrap(r, 32);
        }
        if (blk == 0x62) {
            if (!ecuUnlocked) { uint8_t r[]={0x7F,0x21,0x33}; return kwpWrap(r,3); }
            uint8_t r[] = {0x61, 0x62, 0x50, 0x52, 0x8F, 0x84};  // real PCAP
            return kwpWrap(r, 6);
        }
        if (blk == 0xB0) {
            if (!ecuUnlocked) { uint8_t r[]={0x7F,0x21,0x33}; return kwpWrap(r,3); }
            uint8_t r[] = {0x61, 0xB0, 0x37, 0x0F};
            return kwpWrap(r, 4);
        }
        if (blk == 0xB1) {
            if (!ecuUnlocked) { uint8_t r[]={0x7F,0x21,0x33}; return kwpWrap(r,3); }
            uint8_t r[] = {0x61, 0xB1, 0xD2, 0x15};
            return kwpWrap(r, 4);
        }
        if (blk == 0xB2) {
            if (!ecuUnlocked) { uint8_t r[]={0x7F,0x21,0x33}; return kwpWrap(r,3); }
            uint8_t r[] = {0x61, 0xB2, 0xE0, 0x4B};
            return kwpWrap(r, 4);
        }
        // Block 0x30: RPM setpoints, idle control
        if (blk == 0x30) {
            tick();
            uint16_t rpm = (uint16_t)engineRpm;
            uint16_t idleSet = 750;
            uint8_t r[] = {0x61,0x30,
                (uint8_t)(rpm>>8),(uint8_t)(rpm&0xFF),      // Engine RPM
                (uint8_t)(idleSet>>8),(uint8_t)(idleSet&0xFF), // Low Idle Setpoint
                0x00,0x00, 0x00,0x00, 0x00,0x00,
                0x03,0x20, // idle governor
                0x00,0x00, 0x00,0x00};
            return kwpWrap(r, 18);
        }
        // Block 0x23: Boost/turbo control
        if (blk == 0x23) {
            tick();
            float tps = (engineRpm > 850.0f) ? (engineRpm - 850.0f) * 0.05f : 0.0f;
            uint16_t boostAct = 980 + (uint16_t)(tps * 3.0f);  // mbar
            uint16_t boostSet = 970 + (uint16_t)(tps * 2.8f);
            uint16_t boostV = (uint16_t)(1.8f * 1000);         // voltage mV
            uint8_t r[] = {0x61,0x23,
                (uint8_t)(boostAct>>8),(uint8_t)(boostAct&0xFF), // Boost Pressure Sensor
                (uint8_t)(boostV>>8),(uint8_t)(boostV&0xFF),     // Boost Pressure Voltage
                (uint8_t)(boostSet>>8),(uint8_t)(boostSet&0xFF), // Boost Pressure Setpoint
                0x00,0x00, 0x00,0x00, 0x00,0x00,
                0x00,0x00, 0x00,0x00};
            return kwpWrap(r, 18);
        }
        // Block 0x21: Fuel demand / desired quantities
        if (blk == 0x21) {
            tick();
            float tps = (engineRpm > 850.0f) ? (engineRpm - 850.0f) * 0.05f : 0.0f;
            uint16_t fuelDem = (uint16_t)((9.5f + tps * 0.5f) * 100);
            uint16_t fuelDrv = (uint16_t)((8.0f + tps * 0.4f) * 100);
            uint16_t fuelAct = (uint16_t)((9.2f + tps * 0.48f) * 100);
            uint16_t fuelStart = 1200;
            uint16_t fuelLim = 4500;
            uint16_t fuelTrq = (uint16_t)((9.0f + tps * 0.3f) * 100);
            uint16_t fuelIdle = (uint16_t)(9.5f * 100);
            uint8_t r[] = {0x61,0x21,
                (uint8_t)(fuelDem>>8),(uint8_t)(fuelDem&0xFF),   // Desired Fuel QTY Demand
                (uint8_t)(fuelDrv>>8),(uint8_t)(fuelDrv&0xFF),   // Desired Fuel QTY Driver
                (uint8_t)(fuelAct>>8),(uint8_t)(fuelAct&0xFF),   // Actual Fuel Quantity
                (uint8_t)(fuelStart>>8),(uint8_t)(fuelStart&0xFF), // Fuel QTY Start Setpoint
                (uint8_t)(fuelLim>>8),(uint8_t)(fuelLim&0xFF),   // Fuel QTY Limit
                (uint8_t)(fuelTrq>>8),(uint8_t)(fuelTrq&0xFF),   // Fuel QTY Torque
                (uint8_t)(fuelIdle>>8),(uint8_t)(fuelIdle&0xFF),  // Fuel QTY Low-Idle Gov
                0x00,0x00};
            return kwpWrap(r, 18);
        }
        // Block 0x16: Battery/alternator
        if (blk == 0x16) {
            uint16_t batV = (uint16_t)(14.2f * 100);    // Battery Voltage
            uint16_t batT = (uint16_t)(28.0f * 10);     // Battery Temp
            uint16_t batTV = (uint16_t)(2.5f * 1000);   // Battery Temp Voltage
            uint16_t altField = (uint16_t)(3.2f * 100);  // Alternator Field
            uint16_t altDuty = (uint16_t)(45.0f * 10);   // Alternator Duty Cycle
            uint8_t r[] = {0x61,0x16,
                (uint8_t)(batV>>8),(uint8_t)(batV&0xFF),
                (uint8_t)(batT>>8),(uint8_t)(batT&0xFF),
                (uint8_t)(batTV>>8),(uint8_t)(batTV&0xFF),
                (uint8_t)(altField>>8),(uint8_t)(altField&0xFF),
                (uint8_t)(altDuty>>8),(uint8_t)(altDuty&0xFF),
                0x00,0x00, 0x00,0x00, 0x00,0x00};
            return kwpWrap(r, 18);
        }
        // Block 0x32: Vehicle speed
        if (blk == 0x32) {
            uint16_t vspd = 0;     // Vehicle Speed (stopped)
            uint16_t vspdSet = 0;  // Speed Setpoint
            uint16_t cruiseV = (uint16_t)(0.5f * 1000);
            uint8_t r[] = {0x61,0x32,
                (uint8_t)(vspd>>8),(uint8_t)(vspd&0xFF),
                (uint8_t)(vspdSet>>8),(uint8_t)(vspdSet&0xFF),
                (uint8_t)(cruiseV>>8),(uint8_t)(cruiseV&0xFF),
                0x00,0x00, 0x00,0x00, 0x00,0x00,
                0x00,0x00, 0x00,0x00};
            return kwpWrap(r, 18);
        }
        // Block 0x37: EGR/wastegate
        if (blk == 0x37) {
            uint16_t mafEgr = (uint16_t)(350.0f);       // MAF for EGR Setpoint
            uint16_t wastegate = (uint16_t)(25.0f * 10); // Wastegate Solenoid %
            uint8_t r[] = {0x61,0x37,
                (uint8_t)(mafEgr>>8),(uint8_t)(mafEgr&0xFF),
                (uint8_t)(wastegate>>8),(uint8_t)(wastegate&0xFF),
                0x00,0x00, 0x00,0x00, 0x00,0x00,
                0x00,0x00, 0x00,0x00, 0x00,0x00};
            return kwpWrap(r, 18);
        }
        // Block 0x13: AC system / oil pressure
        if (blk == 0x13) {
            uint16_t oilP = (uint16_t)(3.5f * 100);     // Oil Pressure Sensor bar
            uint16_t oilPV = (uint16_t)(2.1f * 1000);   // Oil Pressure Voltage mV
            uint16_t acP = (uint16_t)(12.0f * 100);      // AC System Pressure
            uint16_t acPV = (uint16_t)(1.8f * 1000);     // AC System Pressure Voltage
            uint8_t r[] = {0x61,0x13,
                (uint8_t)(oilP>>8),(uint8_t)(oilP&0xFF),
                (uint8_t)(oilPV>>8),(uint8_t)(oilPV&0xFF),
                (uint8_t)(acP>>8),(uint8_t)(acP&0xFF),
                (uint8_t)(acPV>>8),(uint8_t)(acPV&0xFF),
                0x00,0x00, 0x00,0x00,
                0x00,0x00, 0x00,0x00, 0x00,0x00};
            return kwpWrap(r, 18);
        }
        // Block 0x36: Pedal position sensors
        if (blk == 0x36) {
            tick();
            float tps = (engineRpm > 850.0f) ? (engineRpm - 850.0f) * 0.05f : 0.0f;
            uint16_t ped1 = (uint16_t)(tps * 100);          // Accel Pedal 1 %
            uint16_t ped2 = (uint16_t)(tps * 50);           // Accel Pedal 2 %
            uint16_t ped1V = (uint16_t)((0.8f + tps * 0.04f) * 1000); // Voltage mV
            uint16_t ped2V = (uint16_t)((0.4f + tps * 0.02f) * 1000);
            uint16_t fuelPed = (uint16_t)((8.0f + tps * 0.3f) * 100);
            uint16_t fuelCru = 0;
            uint8_t r[] = {0x61,0x36,
                (uint8_t)(ped1>>8),(uint8_t)(ped1&0xFF),
                (uint8_t)(ped2>>8),(uint8_t)(ped2&0xFF),
                (uint8_t)(ped1V>>8),(uint8_t)(ped1V&0xFF),
                (uint8_t)(ped2V>>8),(uint8_t)(ped2V&0xFF),
                (uint8_t)(fuelPed>>8),(uint8_t)(fuelPed&0xFF),
                (uint8_t)(fuelCru>>8),(uint8_t)(fuelCru&0xFF),
                0x00,0x00, 0x00,0x00};
            return kwpWrap(r, 18);
        }
        // Block 0x26: Fuel level / pressure regulator
        if (blk == 0x26) {
            uint16_t fuelLvl = (uint16_t)(65.0f * 10);      // Fuel Level %
            uint16_t fuelLvlV = (uint16_t)(3.2f * 1000);    // Fuel Level Sensor Voltage
            uint16_t fuelRegOut = (uint16_t)(50.0f * 10);    // Fuel Pressure Regulator Output
            uint16_t fuelRailV = (uint16_t)(2.8f * 1000);    // Fuel Pressure Volts
            uint16_t fuelPSet = (uint16_t)(290.0f * 10);     // Fuel Pressure Setpoint
            uint8_t r[] = {0x61,0x26,
                (uint8_t)(fuelLvl>>8),(uint8_t)(fuelLvl&0xFF),
                (uint8_t)(fuelLvlV>>8),(uint8_t)(fuelLvlV&0xFF),
                (uint8_t)(fuelRegOut>>8),(uint8_t)(fuelRegOut&0xFF),
                (uint8_t)(fuelRailV>>8),(uint8_t)(fuelRailV&0xFF),
                (uint8_t)(fuelPSet>>8),(uint8_t)(fuelPSet&0xFF),
                0x00,0x00, 0x00,0x00, 0x00,0x00};
            return kwpWrap(r, 18);
        }
        // Block 0x34: Transfer case, cam sync, injector bank
        if (blk == 0x34) {
            uint16_t tcaseV = (uint16_t)(2.5f * 1000);      // Transfer Case Position Voltage
            uint8_t camSync = 1;                              // Cam/Crank Sync (1=OK)
            uint16_t injCap = (uint16_t)(85.0f * 10);        // Injector Bank 1 Capacitor V
            uint8_t r[] = {0x61,0x34,
                (uint8_t)(tcaseV>>8),(uint8_t)(tcaseV&0xFF),
                camSync, 0x00,
                (uint8_t)(injCap>>8),(uint8_t)(injCap&0xFF),
                0x00,0x00, 0x00,0x00, 0x00,0x00,
                0x00,0x00, 0x00,0x00};
            return kwpWrap(r, 18);
        }
        // Generic ECU block
        uint8_t r[18] = {0x61, blk};
        return kwpWrap(r, 18);
    }
    // ReadECUID
    if (sid == 0x1A) {
        uint8_t opt = (dlen > 0) ? data[0] : 0x86;
        if (klTarget == 0x20) {
            if (opt == 0x90) {
                // TCM VIN: all zeros (from PCAP)
                uint8_t r[] = {0x5A,0x90,'0','0','0','0','0','0','0','0','0','0','0','0','0','0','0','0','0'};
                return kwpWrap(r, 19);
            }
            if (opt == 0x86) {
                uint8_t r[] = {0x5A,0x86,0x56,0x04,0x19,0x06,0xBC,0x03,0x01,0x02,0x03,0x08,0x02,0x00,0x00,0x03,0x03,0x11};
                return kwpWrap(r, 18);
            }
            if (opt == 0x87) {
                uint8_t r[] = {0x5A,0x87,0x02,0x08,0x02,0x00,0x00,0x03,0x01};
                return kwpWrap(r, 9);
            }
            if (opt == 0x91) {
                uint8_t r[] = {0x5A,0x91,0x56,0x04,0x19,0x06};
                return kwpWrap(r, 6);
            }
        } else {
            if (opt == 0x86) {
                uint8_t r[] = {0x5A,0x86,0xCA,0x65,0x34,0x40,0x65,0x30,0x99,0x05,0xF2,0x03,0x14,0x14,0x14,0xFF,0xFF,0xFF};
                return kwpWrap(r, 18);  // full real PCAP
            }
            if (opt == 0x90) {
                if (ecuUnlocked) {
                    uint8_t r[] = {0x5A,0x90,'1','J','8','G','W','E','8','2','X','2','Y','1','2','2','0','0','6'};
                    return kwpWrap(r, 19);
                }
                uint8_t r[] = {0x7F, 0x1A, 0x33};
                return kwpWrap(r, 3);
            }
            if (opt == 0x91) {
                uint8_t r[] = {0x5A,0x91,0x05,0x10,0x06,0x05,0x02,0x0C,0x1F,0x0C,0x0A,0x10,0x16,0x06,0x20,0x20,0x57,0x43,0x41,0x41,0x41,0x20};
                return kwpWrap(r, 22);  // full real PCAP
            }
        }
        uint8_t r[] = {0x7F, 0x1A, 0x12};
        return kwpWrap(r, 3);
    }
    // ReadDTC
    if (sid == 0x17 || sid == 0x18) {
        if (klTarget == 0x20) {
            // TCM DTC
            if (ecuDtcCleared) { uint8_t r[] = {0x58, 0x00}; return kwpWrap(r, 2); }
            uint8_t r[] = {0x58, 0x01, 0x22, 0x10, 0x20};
            return kwpWrap(r, 5);
        } else {
            // ECU DTC
            if (ecuDtcCleared) { uint8_t r[] = {0x58, 0x00}; return kwpWrap(r, 2); }
            uint8_t r[] = {0x58, 0x01, 0x07, 0x02, 0xE0};  // real PCAP: 1 DTC
            return kwpWrap(r, 5);
        }
    }
    // ClearDTC
    if (sid == 0x14) {
        ecuDtcCleared = true;
        uint8_t r[] = {0x54, 0x00, 0x00};
        return kwpWrap(r, 3);
    }
    // SID 0x30 IOControl (ECU actuators) — positive response = echo SID+PID
    if (sid == 0x30) {
        if (dlen >= 2) {
            uint8_t r[] = {0x70, data[0], data[1]};
            return kwpWrap(r, 3);
        }
        uint8_t r[] = {0x70, 0x00};
        return kwpWrap(r, 2);
    }
    // SID 0x31 RoutineControl (TCM reset/store adaptives)
    if (sid == 0x31) {
        if (dlen >= 1) {
            uint8_t r[] = {0x71, data[0]};
            return kwpWrap(r, 2);
        }
        uint8_t r[] = {0x71, 0x00};
        return kwpWrap(r, 2);
    }
    // NRC
    uint8_t r[] = {0x7F, sid, 0x11};
    return kwpWrap(r, 3);
}
