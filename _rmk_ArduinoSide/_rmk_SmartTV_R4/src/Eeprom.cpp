#include "Eeprom.h"
#include "Globals.h"
#include "Led.h"
#include "AppLink.h"
#include "Debug.h"
#include <EEPROM.h>
#include <string.h>

namespace EE {

// True if any w() call during the current chunked write cycle failed its
// readback verification. Reset at the start of each cycle in WriteTime(),
// checked at completion in Write() so a real EEPROM failure is reported to
// the app via Notify_Saved() instead of always claiming success.
static bool g_writeHadError = false;

static const char* const EE_GROUPS[] PROGMEM = { "TV_", "MOTION_", "UDPRAW_", "HB_", "OTHER_", "TASK_", "DIF_" };
static const uint8_t EE_GROUP_COUNT = sizeof(EE_GROUPS) / sizeof(EE_GROUPS[0]);

/** CRC16-CCITT (poly 0x1021, init 0xFFFF) over the whole persisted region
    [EE_ADDR_SET, EE_ADDR_MAX) - new vs the original firmware (00_PLAN.md
    SS4.3), closes the "no checksum on any persisted structure" gap. Same
    algorithm as the Diffuser's Usage.cpp port. */
static uint16_t crc16(uint16_t crc, uint8_t b) {
    crc ^= (uint16_t)b << 8;
    for (uint8_t i = 0; i < 8; i++)
        crc = (crc & 0x8000) ? (uint16_t)((crc << 1) ^ 0x1021) : (uint16_t)(crc << 1);
    return crc;
}

static uint16_t computeEepromCrc() {
    uint16_t crc = 0xFFFF;
    for (int addr = EE_ADDR_SET; addr < EE_ADDR_MAX; addr++) crc = crc16(crc, EEPROM.read(addr));
    return crc;
}

/** Persist the schema version + a fresh CRC16 over the just-written region.
    Called once at the end of every completed Write() cycle. */
static void writeSchemaAndCrc() {
    EEPROM.update(EE_ADDR_SCHEMA_VER, EE_SCHEMA_VERSION);
    uint16_t crc = computeEepromCrc();
    EEPROM.put(EE_ADDR_CRC16, crc);
}

/** Schedule (or re-schedule) the chunked EEPROM write after EE_SAVE_TIME ms.
    Resets EE::State.Index to EE_START_READ_INDEX, kills any existing Write
    task, and registers a new one. Call after any change to EE_SET[], LED
    colours, or motion colour that should be persisted across power cycles. */
void WriteTime() {
    bool hasChanges = false;

    for (unsigned int i = 0; i < sizeof(EE_Changed); i++) {
        if (EE_Changed[i] != 0) {
            hasChanges = true;
            break;
        }
    }

    if (!hasChanges) {
        for (int i = 0; i < LED_NUM; i++) {
            if (BIT_TEST(EE_ColorChanged, i) || BIT_TEST(EE_AmbientChanged, i)) {
                hasChanges = true;
                break;
            }
        }
    }

    if (!hasChanges) {
        hasChanges = EE_UdpChanged || EE_MotionChanged;
    }

    if (!hasChanges) {
        return;
    }

    if (EE::State.tID != SCHED::TASK_ID_NONE) {
        SCHED::KillId(EE::State.tID);
    }

    EE::State.Index = EE_START_READ_INDEX;
    g_writeHadError = false;

    DLOG_EE("changes pending - scheduling chunked write in [%d] ms", EE_SAVE_TIME);
    EE::State.tID = SCHED::AddTask("Write", "Write", Write, SCHED::Unit::MS, EE_SAVE_DELAY_BETWEEN_CHUNKS, EE_SAVE_TIME, true);
}

/** Chunked EEPROM write task - delta mode: writes only changed data per
    invocation (settings/LED colours/ambient/UDPRAW/motion), 1 byte per
    call, EE_SAVE_DELAY_BETWEEN_CHUNKS ms apart. Self-terminates when done,
    persisting the schema version + CRC16 over the region. Do not call
    directly - scheduled by WriteTime(). */
void Write(SCHED::TaskId taskId) {
    (void)taskId;

    if (EE::State.Index == EE_START_READ_INDEX) {
        bool hasChanges = false;

        for (unsigned int i = 0; i < sizeof(EE_Changed); i++) {
            if (EE_Changed[i] != 0) {
                hasChanges = true;
                break;
            }
        }

        if (!hasChanges) {
            for (int i = 0; i < LED_NUM; i++) {
                if (BIT_TEST(EE_ColorChanged, i) || BIT_TEST(EE_AmbientChanged, i)) {
                    hasChanges = true;
                    break;
                }
            }
        }

        if (!hasChanges) {
            hasChanges = EE_UdpChanged || EE_MotionChanged;
        }

        if (!hasChanges) {
            SCHED::KillId(EE::State.tID);
            return;
        }
    }

    const int idx = EE::State.Index;

    const int ADDR_SET      = EE_START_READ_INDEX;
    const int ADDR_COLOR    = ADDR_SET   + EE_MEM_X;
    const int ADDR_AMBIENT  = ADDR_COLOR + (LED_NUM << 2);
    const int ADDR_UDP      = ADDR_AMBIENT + (LED_NUM << 2);
    const int ADDR_MOTION   = ADDR_UDP   + 4;
    const int ADDR_MAX      = ADDR_MOTION + 3;

    // 1. Settings - delta: write only changed
    if (idx < ADDR_COLOR) {
        int offset = idx - ADDR_SET;

        if (!IsChanged(offset)) {
            EE::State.Index++;
            return;
        }

        if (!w(idx, EE_SET[offset])) g_writeHadError = true;

        EE::State.Index++;
        return;
    }

    // 2. LED colour data - delta: write only changed LEDs
    if (idx < ADDR_AMBIENT) {
        int offset    = idx - ADDR_COLOR;
        int n         = offset >> 2;
        int component = offset % 4;

        if (!BIT_TEST(EE_ColorChanged, n)) {
            EE::State.Index += 4 - component;
            return;
        }

        uint8_t value;
        if (component == 0) {
            value = LED::State.StoredColor[n].r;
        } else if (component == 1) {
            value = LED::State.StoredColor[n].g;
        } else if (component == 2) {
            value = LED::State.StoredColor[n].b;
        } else {
            value = LED::State.StoredBrightness[n];
        }
        if (!w(idx, value)) g_writeHadError = true;

        EE::State.Index++;
        return;
    }

    // 3. Ambient data - delta: write only changed ambient
    if (idx < ADDR_UDP) {
        int offset    = idx - ADDR_AMBIENT;
        int n         = offset >> 2;
        int component = offset % 4;

        if (!BIT_TEST(EE_AmbientChanged, n)) {
            EE::State.Index += 4 - component;
            return;
        }

        uint8_t value;
        if (component == 0) {
            value = LED::State.AmbientBackgroundColor[n].r;
        } else if (component == 1) {
            value = LED::State.AmbientBackgroundColor[n].g;
        } else if (component == 2) {
            value = LED::State.AmbientBackgroundColor[n].b;
        } else {
            value = LED::State.AmbientBackgroundBrightness[n];
        }
        if (!w(idx, value)) g_writeHadError = true;

        EE::State.Index++;
        return;
    }

    // 4. Ambilight (UDPRAW - 4 bytes) - delta: write only if changed
    if (idx < ADDR_MOTION) {
        if (!EE_UdpChanged) {
            EE::State.Index = ADDR_MOTION;
            return;
        }

        int component = idx - ADDR_UDP;
        uint8_t value;
        if (component == 0) {
            value = LED::State.StreamColor.r;
        } else if (component == 1) {
            value = LED::State.StreamColor.g;
        } else if (component == 2) {
            value = LED::State.StreamColor.b;
        } else {
            value = LED::State.StreamBrightness;
        }
        if (!w(idx, value)) g_writeHadError = true;

        EE::State.Index++;
        return;
    }

    // 5. Motion colour (3 bytes) - delta: write only if changed
    if (idx < ADDR_MAX) {
        if (!EE_MotionChanged) {
            EE::State.Index = ADDR_MAX;
            return;
        }

        int component = idx - ADDR_MOTION;
        uint8_t colorByte = (component == 0) ? MOTION::State.Color.r :
                            (component == 1) ? MOTION::State.Color.g : MOTION::State.Color.b;
        if (!w(idx, colorByte)) g_writeHadError = true;

        EE::State.Index++;
        return;
    }

    // --- DONE ---
    SCHED::KillId(EE::State.tID);

    // Clear all change flags - always, even on a partial failure, so a bad
    // byte doesn't get retried forever and starve the rest of the settings.
    ClearAllChanges();
    writeSchemaAndCrc();
    APP::Notify_Saved(g_writeHadError ? 1 : 0);
}

/* ======================================================================= */
/* SETTINGS TABLE & HELPER FUNCTIONS                                       */
/* ======================================================================= */

const EE_SettingDef EE_SETTINGS_TABLE[EE_MEM_X] PROGMEM = {
    { EE_TV_ON_EFF, "TV_ON_EFF", EE_TYPE_NUMERIC, 0 },
    { EE_TV_OFF_EFF, "TV_OFF_EFF", EE_TYPE_NUMERIC, 1 },
    { EE_TV_OFF_TIME, "TV_OFF_TIME", EE_TYPE_NUMERIC, 2 },
    { EE_TV_BR_COM, "TV_BR_COM", EE_TYPE_NUMERIC, 3 },
    { EE_TV_BR_UCOM, "TV_BR_UCOM", EE_TYPE_NUMERIC, 4 },
    { EE_TV_BR_BED, "TV_BR_BED", EE_TYPE_NUMERIC, 5 },
    { EE_TV_BR_LAMP, "TV_BR_LAMP", EE_TYPE_NUMERIC, 6 },
    { EE_MOTION_BRIGHTNESS, "MOTION_BRIGHTNESS", EE_TYPE_NUMERIC, 7 },
    { EE_MOTION_ON_TIME, "MOTION_ON_TIME", EE_TYPE_NUMERIC, 8 },
    { EE_UDPRAW_AMBILIGHT_BRIGHTNESS_MAX, "UDPRAW_AMBILIGHT_BR_MAX", EE_TYPE_NUMERIC, 9 },
    { EE_MOTION_RANDOM_COLOR, "MOTION_RANDOM_COLOR", EE_TYPE_ONOFF, 10 },
    { EE_OTHER_BR_CL_DEL, "OTHER_BR_CL_DEL", EE_TYPE_NUMERIC, 11 },
    { EE_OTHER_BR_CL_INC, "OTHER_BR_CL_INC", EE_TYPE_NUMERIC, 12 },
    { EE_OTHER_BRIGHTNESS_AUTO, "OTHER_BRIGHTNESS_AUTO", EE_TYPE_ONOFF, 13 },
    { EE_OTHER_TO_OFF_TIME, "OTHER_TO_OFF_TIME", EE_TYPE_NUMERIC, 14 },
    { EE_TV_RANDOM_COLOR_START, "TV_RANDOM_COLOR_START", EE_TYPE_NUMERIC, 15 },
    { EE_MOTION_DIVIDE_BRIGHTNESS, "MOTION_DIVIDE_BRIGHTNESS", EE_TYPE_ONOFF, 16 },
    { EE_TV_BR_TV, "TV_BR_TV", EE_TYPE_NUMERIC, 17 },
    { EE_MOTION_RENEW_COLOR_TIME, "MOTION_RENEW_COLOR_TIME", EE_TYPE_NUMERIC, 18 },
    { EE_MOTION_AUTO_OFF_TIME, "MOTION_AUTO_OFF_TIME", EE_TYPE_NUMERIC, 19 },
    { EE_MOTION_ON_EFF, "MOTION_ON_EFFECT", EE_TYPE_NUMERIC, 20 },
    { EE_OTHER_AMBIENT_MODE_TIME, "OTHER_AMBIENT_MODE_TIME", EE_TYPE_NUMERIC, 21 },
    { EE_OTHER_LED_FPS, "OTHER_LED_FPS", EE_TYPE_NUMERIC, 22 },
    { EE_TV_ON_BR_CL_DEL, "TV_ON_BR_CL_DEL", EE_TYPE_NUMERIC, 23 },
    { EE_TV_ON_BR_CL_INC, "TV_ON_BR_CL_INC", EE_TYPE_NUMERIC, 24 },
    { EE_TV_OFF_BR_CL_DEL, "TV_OFF_BR_CL_DEL", EE_TYPE_NUMERIC, 25 },
    { EE_TV_OFF_BR_CL_INC, "TV_OFF_BR_CL_INC", EE_TYPE_NUMERIC, 26 },
    { EE_MOTION_BR_CL_DEL, "MOTION_BR_CL_DEL", EE_TYPE_NUMERIC, 27 },
    { EE_MOTION_BR_CL_INC, "MOTION_BR_CL_INC", EE_TYPE_NUMERIC, 28 },
    { EE_UDPRAW_BR_CL_DEL, "UDPRAW_BR_CL_DEL", EE_TYPE_NUMERIC, 29 },
    { EE_UDPRAW_BR_CL_INC, "UDPRAW_BR_CL_INC", EE_TYPE_NUMERIC, 30 },
    { EE_HB_DUAL_COLOR, "HB_DUAL_COLOR", EE_TYPE_ONOFF, 31 },
    { EE_HB_EFFECT, "HB_EFFECT", EE_TYPE_NUMERIC, 32 },
    { EE_HB_EFFECT_SPEED, "HB_EFFECT_SPEED", EE_TYPE_NUMERIC, 33 },
    { EE_TV_ON_HB_EFF, "TV_ON_HB_EFFECT", EE_TYPE_NUMERIC, 34 },
    { EE_DIF_EFFECT,      "DIF_EFFECT",      EE_TYPE_NUMERIC, 35 },
    { EE_DIF_MODE_TV,      "DIF_MODE_TV",      EE_TYPE_NUMERIC, 36 },
    { EE_DIF_MODE_MOTION,  "DIF_MODE_MOTION",  EE_TYPE_NUMERIC, 37 },
    { EE_DIF_MODE_UDPRAW,  "DIF_MODE_UDPRAW",  EE_TYPE_NUMERIC, 38 },
    { EE_DIF_MODE_AMBIENT, "DIF_MODE_AMBIENT", EE_TYPE_NUMERIC, 39 },
    /* -- expansion block 40-49 -- */
    { EE_DIF_IDLE_WAIT_MIN, "DIF_IDLE_WAIT_MIN", EE_TYPE_NUMERIC, 40 },
    { EE_DIF_IDLE_ON_MIN,   "DIF_IDLE_ON_MIN",   EE_TYPE_NUMERIC, 41 },
    { EE_DIF_IDLE_MODE,     "DIF_IDLE_MODE",     EE_TYPE_NUMERIC, 42 },
    { EE_DIF_BRIGHTNESS, "DIF_BRIGHTNESS", EE_TYPE_NUMERIC, 43 },
    { EE_DIF_SPEED,      "DIF_SPEED",      EE_TYPE_NUMERIC, 44 },
    { 45, "RESERVED_45", EE_TYPE_NUMERIC, 45 },
    { 46, "RESERVED_46", EE_TYPE_NUMERIC, 46 },
    { 47, "RESERVED_47", EE_TYPE_NUMERIC, 47 },
    { 48, "RESERVED_48", EE_TYPE_NUMERIC, 48 },
    { 49, "RESERVED_49", EE_TYPE_NUMERIC, 49 }
};

/** Clear all EEPROM change tracking after a successful write. */
void ClearAllChanges() {
    memset(EE_Changed, 0, sizeof(EE_Changed));
    BIT_CLEAR_ALL(EE_ColorChanged, sizeof(EE_ColorChanged));
    BIT_CLEAR_ALL(EE_AmbientChanged, sizeof(EE_AmbientChanged));
    EE_UdpChanged = false;
    EE_MotionChanged = false;
}

/** Get setting value with type checking - returns 0 for an out-of-range id
    (no defaults table exists to fall back to, see Read()'s header note). */
uint8_t Get(uint8_t settingId) {
    if (settingId >= EE_MEM_X) {
        return 0;
    }
    return EE_SET[settingId];
}

/** @return Number of settings marked as changed. */
int getChangedCount() {
    int count = 0;
    for (int i = 0; i < EE_MEM_X; i++) {
        if (IsChanged(i)) count++;
    }
    return count;
}

/** @return Pointer to EE_SettingDef, or nullptr if not found. O(EE_MEM_X). */
const EE_SettingDef* getDef(uint8_t settingId) {
    for (int i = 0; i < EE_MEM_X; i++) {
        const EE_SettingDef* def = &EE_SETTINGS_TABLE[i];
        if (def->id == settingId) {
            return def;
        }
    }
    return nullptr;
}

/** @return Pointer to the EE_SettingDef at the given array index (0-49). */
const EE_SettingDef* getDefByIndex(uint8_t index) {
    if (index >= EE_MEM_X) return nullptr;
    return &EE_SETTINGS_TABLE[index];
}

/** @return Setting name string (in PROGMEM), or nullptr if not found. */
const char* getName(uint8_t settingId) {
    const EE_SettingDef* def = getDef(settingId);
    return def ? def->name : nullptr;
}

/** True when a setting name starts with one of EE_GROUPS. */
bool InAnyGroup(const char* name) {
    for (uint8_t g = 0; g < EE_GROUP_COUNT; g++) {
        const char* groupPrefix = (const char*)pgm_read_ptr(&EE_GROUPS[g]);
        if (strncmp(name, groupPrefix, strlen(groupPrefix)) == 0) return true;
    }
    return false;
}

/** @return true if the setting at this array index (0-49) has been marked
    as changed since the last successful write. */
bool IsChanged(uint8_t index) {
    if (index >= EE_MEM_X) {
        return false;
    }
    return BIT_TEST(EE_Changed, index);
}

/** Print one EEPROM setting with its type-appropriate value. */
void LogSetting(const EE_SettingDef* def) {
    if (!def) return;
    uint8_t value = EE::Get(def->id);
    if (def->type == EE_TYPE_ONOFF) {
        APP::termMsgLog(APP_LOG_INF, APP_SRC_EE, "EE", "LogSetting", "%s [%s]", def->name, (value) ? "ON" : "OFF");
    } else {
        APP::termMsgLog(APP_LOG_INF, APP_SRC_EE, "EE", "LogSetting", "%s [%d]", def->name, value);
    }
}

void MarkAmbientChanged(uint8_t ledIndex) {
    if (ledIndex < LED_NUM) {
        BIT_SET(EE_AmbientChanged, ledIndex);
    }
}

void MarkChanged(uint8_t index) {
    if (index < EE_MEM_X) {
        BIT_SET(EE_Changed, index);
    }
}

void MarkColorChanged(uint8_t ledIndex) {
    if (ledIndex < LED_NUM) {
        BIT_SET(EE_ColorChanged, ledIndex);
    }
}

void MarkMotionChanged() {
    EE_MotionChanged = true;
}

void MarkUdpChanged() {
    EE_UdpChanged = true;
}

/* ------------------------------------------------------------------------ */
/* EE                                                                        */
/* EEPROM persistence - settings table read/write, LED/ambient/motion colour */
/* ------------------------------------------------------------------------ */

/** Read all persisted data from EEPROM into live structs on startup.
    Reads settings -> LED colours/brightness -> ambient -> UDPRAW -> motion
    colour, in that fixed order.

    Schema-version + CRC16 gate (new vs. the original, 00_PLAN.md SS4.2/
    SS4.3): if the stored schema doesn't match or the CRC over the region
    doesn't verify (virgin flash, a corrupted write, or an unmigratable
    older schema), the read is skipped entirely and every RAM-side struct
    is left at its already-zero-initialized state rather than loading
    garbage bytes - R15 (00_PLAN.md SS9) is still an open ruling on whether
    a real per-setting factory-default table should exist; until it does,
    "zero" is the only safe fallback this rewrite can produce on its own,
    same call the Diffuser's Usage.cpp port made for its own EEPROM block.
    A fresh valid block is persisted immediately after, same as that port.

    Call once in setup() after LED::Setup() and before the main loop. Does
    not write to hardware LEDs - call LED::Setup() first so arrays are
    sized. */
void Read() {
    uint8_t schemaVer = EEPROM.read(EE_ADDR_SCHEMA_VER);
    uint16_t storedCrc; EEPROM.get(EE_ADDR_CRC16, storedCrc);
    bool crcOk = (computeEepromCrc() == storedCrc);

    if (schemaVer != EE_SCHEMA_VERSION || !crcOk) {
        memset(EE_SET, 0, sizeof(EE_SET));
        memset(LED::State.StoredColor, 0, LED_NUM * sizeof(CRGB));
        memset(LED::State.StoredBrightness, 0, LED_NUM * sizeof(LED::State.StoredBrightness[0]));
        memset(LED::State.AmbientBackgroundColor, 0, LED_NUM * sizeof(CRGB));
        memset(LED::State.AmbientBackgroundBrightness, 0, LED_NUM * sizeof(LED::State.AmbientBackgroundBrightness[0]));
        LED::State.StreamColor = CRGB();
        LED::State.StreamBrightness = 0;
        MOTION::State.Color = CRGB();

        writeSchemaAndCrc();
        return;
    }

    int sEntry = EE_START_READ_INDEX;
    int tEntry = sEntry + EE_MEM_X;

    // 1. Settings
    for (int i = sEntry; i < tEntry; i++) {
        EE_SET[i - sEntry] = EEPROM.read(i);
    }

    // 2. Colour data (4-byte chunks)
    sEntry = tEntry - 1;
    tEntry = sEntry + (LED_NUM << 2);

    for (int i = sEntry, n = 0; i < tEntry; i += 4, n++) {
        LED::State.StoredColor[n].r = EEPROM.read(i + 1);
        LED::State.StoredColor[n].g = EEPROM.read(i + 2);
        LED::State.StoredColor[n].b = EEPROM.read(i + 3);
        LED::State.StoredBrightness[n] = EEPROM.read(i + 4);
    }

    // 3. Ambient data (4-byte chunks)
    sEntry = tEntry;
    tEntry = sEntry + (LED_NUM << 2);

    for (int i = sEntry, n = 0; i < tEntry; i += 4, n++) {
        LED::State.AmbientBackgroundColor[n].r = EEPROM.read(i + 1);
        LED::State.AmbientBackgroundColor[n].g = EEPROM.read(i + 2);
        LED::State.AmbientBackgroundColor[n].b = EEPROM.read(i + 3);
        LED::State.AmbientBackgroundBrightness[n] = EEPROM.read(i + 4);
    }

    // 4. Ambilight (UDPRAW)
    sEntry = tEntry;

    for (int i = 0; i < 4; i++) {
        if (i == 0) LED::State.StreamColor.r = EEPROM.read(sEntry + i + 1);
        else if (i == 1) LED::State.StreamColor.g = EEPROM.read(sEntry + i + 1);
        else if (i == 2) LED::State.StreamColor.b = EEPROM.read(sEntry + i + 1);
        else LED::State.StreamBrightness = EEPROM.read(sEntry + i + 1);
    }

    // 5. Motion colour
    sEntry += 4;

    uint8_t colorComponents[3];
    for (int i = 0; i < 3; i++) {
        colorComponents[i] = EEPROM.read(sEntry + i + 1);
    }
    MOTION::State.Color.r = colorComponents[0];
    MOTION::State.Color.g = colorComponents[1];
    MOTION::State.Color.b = colorComponents[2];
}

/** Cross-boot EEPROM persistence self-test. Verifies the pattern written on
    the PREVIOUS boot actually survived to this boot (real flash
    persistence, not just an in-RAM echo) before writing a fresh pattern
    for the NEXT boot to check. Always prints its result, unconditionally -
    a diagnostic, not a debug-gated log line. Call once in setup(), after
    EE::Read(). */
void SelfTest() {
    const int addr = EEPROM.length() - MQTTCRED_BLOCK_SIZE - EE_TEST_BLOCK_SIZE;

    uint8_t bootNum;
    bool ok = true;
    if (EEPROM.read(addr) == EE_TEST_MAGIC) {
        bootNum = EEPROM.read(addr + 1);
        const uint8_t expected[4] = {
            (uint8_t)(bootNum ^ 0x55), (uint8_t)(bootNum + 1),
            (uint8_t)~bootNum,         (uint8_t)(bootNum * 3)
        };
        for (int i = 0; i < 4; i++) {
            uint8_t actual = EEPROM.read(addr + 2 + i);
            if (actual != expected[i]) {
                ok = false;
                APP::termMsgLog(APP_LOG_ERR, APP_SRC_EE, "EE", "SelfTest", "byte[%d] boot#%d expected[%d] got[%d]", i, bootNum, expected[i], actual);
            }
        }
        APP::termMsgLog(ok ? APP_LOG_INF : APP_LOG_ERR, APP_SRC_EE, "EE", "SelfTest", "boot #%d pattern %s across reboot", bootNum, ok ? "PASS - survived" : "FAILED - lost");
        bootNum++;
    } else {
        APP::termMsgLog(APP_LOG_INF, APP_SRC_EE, "EE", "SelfTest", "first boot ever - no previous pattern to verify");
        bootNum = 0;
    }

    // Each w() call must always run regardless of any earlier one's result -
    // && short-circuits, which would otherwise skip later writes entirely
    // the moment one call falsely reported failure.
    bool writeOk = true;
    if (!w(addr,     EE_TEST_MAGIC))          writeOk = false;
    if (!w(addr + 1, bootNum))                writeOk = false;
    if (!w(addr + 2, bootNum ^ 0x55))          writeOk = false;
    if (!w(addr + 3, bootNum + 1))             writeOk = false;
    if (!w(addr + 4, (uint8_t)~bootNum))       writeOk = false;
    if (!w(addr + 5, bootNum * 3))             writeOk = false;
    if (!writeOk) {
        APP::termMsgLog(APP_LOG_ERR, APP_SRC_EE, "EE", "SelfTest", "failed writing boot #%d pattern for next-boot check", bootNum);
    }
}

/** Single-shot raw EEPROM dump - every used byte, read directly from flash
    (not the RAM copies EE::Read() loaded). One line per record:
      SET <index> <value>                        x EE_MEM_X
      LEDCOLOR <n> <r> <g> <b> <brightness>       x LED_NUM
      AMBIENT <n> <r> <g> <b> <brightness>        x LED_NUM
      UDPRAW <r> <g> <b> <brightness>
      MOTION <r> <g> <b>
      MQTTCRED <configured|not-configured>        (never prints credentials)
    A real backup/restore reference, not a status snapshot - reads flash
    directly rather than whatever's live in RAM. Triggered via the debug
    interface's EEPROM-backup command. */
void DumpBackup() {
    APP::termMsgLog(APP_LOG_INF, APP_SRC_EE, "EE", "DumpBackup", "===== EEPROM_BACKUP_BEGIN =====");

    for (int i = 0; i < EE_MEM_X; i++) {
        APP::termMsgLog(APP_LOG_INF, APP_SRC_EE, "EE", "DumpBackup", "SET %d %d", i, EEPROM.read(EE_ADDR_SET + i));
    }
    for (int n = 0; n < LED_NUM; n++) {
        const int base = EE_ADDR_COLOR + (n << 2);
        APP::termMsgLog(APP_LOG_INF, APP_SRC_EE, "EE", "DumpBackup", "LEDCOLOR %d %d %d %d %d", n,
            EEPROM.read(base), EEPROM.read(base + 1), EEPROM.read(base + 2), EEPROM.read(base + 3));
    }
    for (int n = 0; n < LED_NUM; n++) {
        const int base = EE_ADDR_AMBIENT + (n << 2);
        APP::termMsgLog(APP_LOG_INF, APP_SRC_EE, "EE", "DumpBackup", "AMBIENT %d %d %d %d %d", n,
            EEPROM.read(base), EEPROM.read(base + 1), EEPROM.read(base + 2), EEPROM.read(base + 3));
    }
    APP::termMsgLog(APP_LOG_INF, APP_SRC_EE, "EE", "DumpBackup", "UDPRAW %d %d %d %d",
        EEPROM.read(EE_ADDR_UDP), EEPROM.read(EE_ADDR_UDP + 1), EEPROM.read(EE_ADDR_UDP + 2), EEPROM.read(EE_ADDR_UDP + 3));
    APP::termMsgLog(APP_LOG_INF, APP_SRC_EE, "EE", "DumpBackup", "MOTION %d %d %d",
        EEPROM.read(EE_ADDR_MOTION), EEPROM.read(EE_ADDR_MOTION + 1), EEPROM.read(EE_ADDR_MOTION + 2));

    const int mqttBase = EEPROM.length() - MQTTCRED_BLOCK_SIZE;
    APP::termMsgLog(APP_LOG_INF, APP_SRC_EE, "EE", "DumpBackup", "MQTTCRED %s",
        (EEPROM.read(mqttBase) == MQTTCRED_MAGIC) ? "configured" : "not-configured");

    APP::termMsgLog(APP_LOG_INF, APP_SRC_EE, "EE", "DumpBackup", "===== EEPROM_BACKUP_END =====");
}

/** Set setting value and mark as changed for delta sync. No bounds
    validation (caller's responsibility). Call WriteTime() to persist.
    @return true if the setting id exists, false if out of range. */
bool Set(uint8_t settingId, uint8_t value) {
    if (settingId >= EE_MEM_X) {
        return false;
    }
    EE_SET[settingId] = value;
    MarkChanged(settingId);
    return true;
}

/** Write one byte to EEPROM with readback verification. Uses
    EEPROM.update() (only writes if the value changed) then reads back to
    confirm the write succeeded.
    @return true if the readback matches, false on write failure. */
bool w(int index, int value) {
    DLOGV_EE("write index [%d] = [%d]", index, value);
    EEPROM.update(index, value);

    // EEPROM.update() narrows value to a uint8_t on write, so the
    // comparison must too - a caller passing a computed value outside
    // 0-255 (e.g. ~someUint8, which promotes to a negative int) would
    // otherwise always mismatch here even though the byte itself was
    // written correctly.
    const uint8_t expected = (uint8_t)value;
    const uint8_t readback = EEPROM.read(index);
    if (readback != expected) {
        // Unconditional (not gated behind ENABLE_LOG_EEPROM in the
        // original) - a real write failure the user should always see.
        APP::termMsgLog(APP_LOG_ERR, APP_SRC_EE, "EE", "w", "error at index [%d], wanted [%d], readback [%d]", index, expected, readback);
        return false;
    }
    return true;
}

} // namespace EE
