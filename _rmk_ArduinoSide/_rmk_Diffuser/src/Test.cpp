#include "Test.h"
#ifdef DIF_TEST_MODE
#include "Globals.h"
#include "Log.h"
#include "ModeButton.h"
#include "Buzzer.h"
#include "Strip.h"
#include "Parfum.h"
#include "Telnet.h"
#include "ProtoLink.h"
#include "../_rmk_Diffuser_DEF.h"
#include <ArduinoOTA.h>

namespace TEST {

static bool modeSettled() {
    return !g_modeTargetActive && !g_modePending && !g_shutdownCmdPending;
}

/** Keeps buzzer/mode/parfum/strip ticking for up to ms milliseconds (or
    until settled() says done) - runs the same tick functions loop() does,
    so a real buzzer confirmation during a test step still registers. */
static void pump(unsigned long ms, bool (*settled)() = nullptr) {
    unsigned long start      = millis();
    unsigned long lastBuzzMs = start;
    while (millis() - start < ms) {
        unsigned long now = millis();
        if (now - lastBuzzMs >= BUZZER_SAMPLE_MS) { lastBuzzMs = now; BUZZ::tickBuzzer(); }
        MODE::tickPinRelease();
        MODE::tickModeTimeout();
        PARFUM::tickParfum();
        STRIP::tickStripEffect();
        STRIP::tickStripFade();
        STRIP::tickStripOowChase();
        STRIP::tickStripParfumBreathe();
        ArduinoOTA.handle();
        if (settled && settled()) return;
        delay(1);
    }
}

static void step(const char *label, const char *cmd, unsigned long settleMs, bool (*settled)() = nullptr) {
    LOG::dbgPrintf("\n[TEST] %-30s console \"%s\"\n", label, cmd);
    TELNET::handleCmd(String(cmd));
    pump(settleMs, settled);
}

/** Same dispatch path a real UDP frame hits - see PROTOLINK::dispatchForTest(). */
static void stepOpcode(const char *label, uint8_t opcode, const uint8_t *payload, size_t len,
                        unsigned long settleMs, bool (*settled)() = nullptr) {
    LOG::dbgPrintf("\n[TEST] %-30s opcode 0x%02X\n", label, opcode);
    PROTOLINK::dispatchForTest(opcode, payload, len);
    pump(settleMs, settled);
}

void runSelfTest() {
    const unsigned long MODE_CEIL_MS = MODE_CONFIRM_MS * MODE_MAX + 1000UL;

    LOG::dbgPrintln("\n════════════════════════════════════════════════");
    LOG::dbgPrintln("  SELF-TEST - sweeping every command (WiFi/OTA excluded)");
    LOG::dbgPrintln("  A \"TIMED OUT\" line under a mode step is expected");
    LOG::dbgPrintln("  if no diffuser is wired to MODE_PIN/BUZZER_PIN yet.");
    LOG::dbgPrintln("════════════════════════════════════════════════");

    step("quick status",               "S",        300);
    step("debug dump",                 "D",        300);

    step("effect cycle",               "E",        500);
    step("effect cycle",               "E",        500);
    step("effect cycle",               "E",        500);

    step("solid colour (red)",         "CFF0000",  500);
    step("solid colour (green)",       "C00FF00",  500);
    step("solid colour (blue)",        "C0000FF",  500);
    step("bad colour - reject",        "CGG0000",  300);

    step("parfum start (1 min)",       "P1",       1500);
    step("parfum cancel",              "P0",       500);
    step("bad parfum minutes - reject","P9999",    300);

    LOG::dbgPrintln("\n[TEST] -- mode sweep (physically drives the diffuser) --");
    step("mode -> CONT",               "M1", MODE_CEIL_MS, modeSettled);
    step("mode -> 10 SEC",             "M2", MODE_CEIL_MS, modeSettled);
    step("mode -> 2H AFTER SLEEP",     "M3", MODE_CEIL_MS, modeSettled);
    step("mode -> 4H AFTER SLEEP",     "M4", MODE_CEIL_MS, modeSettled);
    step("mode -> shutdown",           "M0", MODE_CEIL_MS, modeSettled);

    step("unknown command - reject",   "X",        300);

    LOG::dbgPrintln("\n[TEST] -- Link A opcode equivalents (same PROTOLINK dispatch a real UDP frame hits) --");
    stepOpcode("status query",         (uint8_t)Proto::Opcode::DIFFUSER_STATUS_QUERY, (const uint8_t[]){1}, 1, 300);
    stepOpcode("status poll (silent)", (uint8_t)Proto::Opcode::DIFFUSER_STATUS_QUERY, (const uint8_t[]){0}, 1, 300);
    stepOpcode("refill history",       (uint8_t)Proto::Opcode::DIFFUSER_HISTORY_QUERY, nullptr, 0, 300);
    stepOpcode("manual refill",        (uint8_t)Proto::Opcode::DIFFUSER_MANUAL_REFILL, nullptr, 0, 300);
    {
        const uint8_t badTurnOn[] = { 9, 0, 0xFF, 0, 0, 40, 9, 0x1A };  // bad mode (9) - reject
        stepOpcode("turn-on, bad mode - reject", (uint8_t)Proto::Opcode::DIFFUSER_TURN_ON, badTurnOn, sizeof(badTurnOn), 300);
    }
    {
        const uint8_t turnOn[] = { 1, 0, 0xFF, 0, 0, 255, 0, 0x1A };  // CONT, solid red
        stepOpcode("turn-on, valid (CONT, red)", (uint8_t)Proto::Opcode::DIFFUSER_TURN_ON, turnOn, sizeof(turnOn), MODE_CEIL_MS, modeSettled);
    }
    {
        const uint8_t parfumBad[] = { 0, 1, 0 };   // 1 minute, mode 0 - invalid mode
        stepOpcode("parfum, bad mode - reject", (uint8_t)Proto::Opcode::DIFFUSER_PARFUM_START, parfumBad, sizeof(parfumBad), 300);
    }
    {
        const uint8_t parfumOk[] = { 0, 1, 1 };    // 1 minute, mode 1 (CONT)
        stepOpcode("parfum start (1 min, CONT)", (uint8_t)Proto::Opcode::DIFFUSER_PARFUM_START, parfumOk, sizeof(parfumOk), 1000);
    }
    stepOpcode("parfum cancel",        (uint8_t)Proto::Opcode::DIFFUSER_PARFUM_CANCEL, nullptr, 0, 500);
    stepOpcode("shutdown",             (uint8_t)Proto::Opcode::DIFFUSER_SHUTDOWN, nullptr, 0, MODE_CEIL_MS, modeSettled);
    stepOpcode("unrecognized opcode - reject", 0x7F, nullptr, 0, 300);

    LOG::dbgPrintln("\n════════════════════════════════════════════════");
    LOG::dbgPrintln("  SELF-TEST COMPLETE");
    LOG::dbgPrintln("════════════════════════════════════════════════\n");
}

} // namespace TEST
#endif // DIF_TEST_MODE
