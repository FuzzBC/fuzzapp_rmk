#include "Usage.h"
#include "Globals.h"
#include "Log.h"
#include <EEPROM.h>
#include "../_rmk_Diffuser_DEF.h"

namespace USAGE {

/** CRC16-CCITT (poly 0x1021, init 0xFFFF) over the EEPROM usage block,
    offsets [0, EE_ADDR_CRC16) - new vs the original firmware (00_PLAN.md
    SS4.3), closes the "no checksum on any persisted structure" gap. */
static uint16_t crc16(uint16_t crc, uint8_t b) {
    crc ^= (uint16_t)b << 8;
    for (uint8_t i = 0; i < 8; i++)
        crc = (crc & 0x8000) ? (uint16_t)((crc << 1) ^ 0x1021) : (uint16_t)(crc << 1);
    return crc;
}

static uint16_t computeEepromCrc() {
    uint16_t crc = 0xFFFF;
    for (int addr = 0; addr < EE_ADDR_CRC16; addr++) crc = crc16(crc, EEPROM.read(addr));
    return crc;
}

uint32_t averageRefillCycleSec() {
    if (g_refillHistoryCount == 0) return 0;
    uint64_t sum = 0;
    for (uint8_t i = 0; i < g_refillHistoryCount; i++) sum += g_refillHistory[i];
    return (uint32_t)(sum / g_refillHistoryCount);
}

float dutyCycleForMode(uint8_t mode) {
    if (mode < 1 || mode > MODE_MAX) return 0.0f;
    return MODE_DUTY_CYCLE[mode - 1];
}

/** A cycle shorter than USAGE_MIN_CYCLE_SEC is NOT pushed into the history -
    almost certainly a still-dry retry attempt rather than a genuine full-
    tank cycle, which would otherwise pollute the rolling average with a
    near-zero outlier. The accumulator resets either way. */
void finalizeRefillCycle() {
    if (g_usageAccumSec >= USAGE_MIN_CYCLE_SEC) {
        g_refillHistory[g_refillHistoryNext] = g_usageAccumSec;
        g_refillHistoryNext = (g_refillHistoryNext + 1) % REFILL_HISTORY_SIZE;
        if (g_refillHistoryCount < REFILL_HISTORY_SIZE) g_refillHistoryCount++;
        g_totalRefillCount++;
        LOG::logMsg("USAGE", "refill cycle finalized: %lus (history %u/%u, avg=%lus, total=%lu)",
                  g_usageAccumSec, g_refillHistoryCount, REFILL_HISTORY_SIZE,
                  averageRefillCycleSec(), g_totalRefillCount);
    } else {
        vlogMsg("USAGE", "cycle too short (%lus < %lus) - not counted as a refill",
                  g_usageAccumSec, (unsigned long)USAGE_MIN_CYCLE_SEC);
    }
    g_usageAccumSec = 0;
    g_usageFracSec  = 0.0f;
    saveUsageToEeprom(true);
}

/** Remove exactly one entry from the refill-history ring buffer and shift
    everything after it down by one. The only history-editing command on
    purpose - deliberately no bulk "clear everything" (too easy to fire by
    mistake and wipe the whole average in one click).
    @param oneBasedIndex 1..g_refillHistoryCount, oldest-first (matches the
           history-query reply's numbering, entry 1 = oldest). */
bool removeHistoryEntryAt(uint8_t oneBasedIndex) {
    if (oneBasedIndex < 1 || oneBasedIndex > g_refillHistoryCount) return false;

    uint32_t ordered[REFILL_HISTORY_SIZE] = {0};
    for (uint8_t i = 0; i < g_refillHistoryCount; i++) {
        uint8_t srcIdx = (g_refillHistoryCount < REFILL_HISTORY_SIZE)
                       ? i
                       : (uint8_t)((g_refillHistoryNext + i) % REFILL_HISTORY_SIZE);
        ordered[i] = g_refillHistory[srcIdx];
    }

    uint8_t removeAt = oneBasedIndex - 1;
    LOG::logMsg("USAGE", "refill history entry %u/%u removed (%lus) - rest shift down (lifetime count [%lu] and live cycle unaffected)",
              oneBasedIndex, g_refillHistoryCount, ordered[removeAt], g_totalRefillCount);
    for (uint8_t i = removeAt; i < g_refillHistoryCount - 1; i++) ordered[i] = ordered[i + 1];

    g_refillHistoryCount--;
    for (uint8_t i = 0; i < REFILL_HISTORY_SIZE; i++) g_refillHistory[i] = (i < g_refillHistoryCount) ? ordered[i] : 0;
    g_refillHistoryNext = g_refillHistoryCount % REFILL_HISTORY_SIZE;
    saveUsageToEeprom(true);
    return true;
}

void loadUsageFromEeprom() {
#ifdef DIF_TEST_MODE
    g_usageAccumSec          = 0;
    g_refillHistoryCount     = 0;
    g_refillHistoryNext      = 0;
    g_totalRefillCount       = 0;
    for (uint8_t i = 0; i < REFILL_HISTORY_SIZE; i++) g_refillHistory[i] = 0;
    g_usageLastSavedAccumSec = 0;
    LOG::logMsg("USAGE", "test mode - EEPROM skipped, starting from zero");
    return;
#endif
    uint8_t schemaVer = EEPROM.read(EE_ADDR_SCHEMA_VER);
    uint16_t storedCrc; EEPROM.get(EE_ADDR_CRC16, storedCrc);
    bool crcOk = (computeEepromCrc() == storedCrc);

    if (schemaVer != EE_SCHEMA_VERSION || !crcOk) {
        // Virgin flash (0xFF), a corrupted block, or a schema this build
        // doesn't know how to migrate - start clean rather than trust
        // garbage. A real migration path belongs here once EE_SCHEMA_VERSION
        // is ever bumped past 1; there's nothing to migrate FROM yet.
        LOG::logMsg("USAGE", "EEPROM %s (schema=%u expected=%u, crc %s) - starting from zero",
                  (schemaVer == 0xFF) ? "virgin" : "unreadable",
                  schemaVer, (unsigned)EE_SCHEMA_VERSION, crcOk ? "ok" : "MISMATCH");
        g_usageAccumSec = 0;
        g_refillHistoryCount = 0;
        g_refillHistoryNext = 0;
        g_totalRefillCount = 0;
        for (uint8_t i = 0; i < REFILL_HISTORY_SIZE; i++) g_refillHistory[i] = 0;
        g_usageLastSavedAccumSec = 0;
        saveUsageToEeprom(true);   // persist a known-good block + fresh CRC immediately
        return;
    }

    EEPROM.get(EE_ADDR_USAGE_ACCUM,    g_usageAccumSec);
    EEPROM.get(EE_ADDR_REFILL_COUNT,   g_refillHistoryCount);
    EEPROM.get(EE_ADDR_REFILL_NEXT,    g_refillHistoryNext);
    EEPROM.get(EE_ADDR_TOTAL_REFILLS,  g_totalRefillCount);
    for (uint8_t i = 0; i < REFILL_HISTORY_SIZE; i++) {
        EEPROM.get(EE_ADDR_REFILL_HISTORY + i * 4, g_refillHistory[i]);
    }

    if (g_refillHistoryCount > REFILL_HISTORY_SIZE) g_refillHistoryCount = 0;
    if (g_refillHistoryNext  >= REFILL_HISTORY_SIZE) g_refillHistoryNext = 0;
    if (g_refillHistoryCount == 0) {
        g_refillHistoryNext = 0;
        for (uint8_t i = 0; i < REFILL_HISTORY_SIZE; i++) g_refillHistory[i] = 0;
    }

    g_usageLastSavedAccumSec = g_usageAccumSec;

    LOG::logMsg("USAGE", "loaded: accum=%lus history=%u/%u total=%lu avg=%lus",
              g_usageAccumSec, g_refillHistoryCount, REFILL_HISTORY_SIZE,
              g_totalRefillCount, averageRefillCycleSec());
}

/** @param includeHistory false = only the live accumulator (periodic
    checkpoint); true = everything, used right after a refill cycle
    finalizes. The schema version + CRC16 are (re)written every save. */
void saveUsageToEeprom(bool includeHistory) {
    g_usageLastSaveMs        = millis();
    g_usageLastSavedAccumSec = g_usageAccumSec;
#ifdef DIF_TEST_MODE
    (void)includeHistory;
    return;
#endif
    EEPROM.write(EE_ADDR_SCHEMA_VER, EE_SCHEMA_VERSION);
    EEPROM.put(EE_ADDR_USAGE_ACCUM, g_usageAccumSec);
    if (includeHistory) {
        EEPROM.put(EE_ADDR_REFILL_COUNT,  g_refillHistoryCount);
        EEPROM.put(EE_ADDR_REFILL_NEXT,   g_refillHistoryNext);
        EEPROM.put(EE_ADDR_TOTAL_REFILLS, g_totalRefillCount);
        for (uint8_t i = 0; i < REFILL_HISTORY_SIZE; i++) {
            EEPROM.put(EE_ADDR_REFILL_HISTORY + i * 4, g_refillHistory[i]);
        }
    }
    uint16_t crc = computeEepromCrc();
    EEPROM.put(EE_ADDR_CRC16, crc);
    EEPROM.commit();
}

/** Sub-second remainder (e.g. M2's 0.5x duty) is carried in g_usageFracSec
    across calls rather than rounded off each time - rounding per-tick would
    silently inflate M2's weighted total back up to full duty. */
void tickUsageAccum() {
    unsigned long now     = millis();
    unsigned long elapsed = now - g_usageLastTickMs;
    g_usageLastTickMs     = now;

    if (g_powered && !g_outOfWater) {
        g_usageFracSec += (elapsed / 1000.0f) * dutyCycleForMode(g_mode);
        uint32_t wholeSec = (uint32_t)g_usageFracSec;
        if (wholeSec > 0) {
            g_usageAccumSec += wholeSec;
            g_usageFracSec  -= wholeSec;
        }
    }

    if (now - g_usageLastCheckMs >= USAGE_EEPROM_CHECKPOINT_MS) {
        g_usageLastCheckMs = now;
        if (g_usageAccumSec != g_usageLastSavedAccumSec) {
            saveUsageToEeprom(false);
        } else {
            vlogMsg("USAGE", "checkpoint skipped - accum unchanged (%lus) since last save", g_usageAccumSec);
        }
    }
}

} // namespace USAGE
