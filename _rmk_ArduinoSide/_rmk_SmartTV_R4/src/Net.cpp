#include "Net.h"
#include "Globals.h"
#include "Udpraw.h"
#include "AppLink.h"
#include "DifLink.h"
#include "Debug.h"

namespace NET {

/** Pause for a short interval while allowing background tasks to run
    (yield() in a tight loop) - used for network retry/DHCP polling delays. */
static void WaitWithYield(uint32_t ms) {
    unsigned long start = millis();
    while ((millis() - start) < ms) {
        yield();
    }
}

/** Initialises the network stack: starts the WiFi handshake, blocks up to
    NET_CONNECT_TIMEOUT s for WL_CONNECTED, and on success stamps the
    connect time and triggers the first RTC_Begin(). On failure, schedules
    a one-shot Reconnect after NET_RETRY_DELAY s. Registers Check() as a
    locked repeating health-check task regardless of outcome, so the
    system self-heals after setup. Call once in setup(). */
void Setup() {
    Connect();

    uint8_t ticks = (uint8_t)(NET_CONNECT_TIMEOUT * 2);
    uint8_t elapsed = 0;

    while (!IsConnected() && ticks > 0) {
        WaitWithYield(500);
        ticks--;
        elapsed++;
    }
    (void)elapsed;

    if (IsConnected()) {
        NET_WifiSt = netWifiOK;
        IPAddress ip = getIP();
        APP::termMsgLog(APP_LOG_INF, APP_SRC_NET, "NET", "Setup", "WiFi connected, IP [%d.%d.%d.%d] RSSI [%d] dBm",
            ip[0], ip[1], ip[2], ip[3], (int)WiFi.RSSI());

        RTC_Begin();
        setConnectTime(false);
    } else {
        NET_WifiSt = netWifiRetrying;
        APP::termMsgLog(APP_LOG_WRN, APP_SRC_NET, "NET", "Setup", "WiFi connect FAILED after [%d] s - retrying in background", NET_CONNECT_TIMEOUT);

        SCHED::AddTask("NET_Setup", "Reconnect", Reconnect, SCHED::Unit::S, NET_RETRY_DELAY, NET_RETRY_DELAY, false);
    }

    SCHED::AddTask("NET_Setup", "NET_Check", Check, SCHED::Unit::S, NET_CHECK_TIME, 0, true);
}

/** Periodic background health check, locked (survives KillAllUnlocked()).
    Skips entirely while UDPRAW is streaming. On disconnect: issues an
    immediate Connect(), clears the connect stamp, and schedules a one-shot
    unlocked Reconnect. On regain (ConnectTime[_YY] == 0 sentinel):
    restores UDPRAW/APP/DIF ports, re-arms NTP if it was retrying, and
    saves the connect stamp. */
void Check(SCHED::TaskId taskId) {
    (void)taskId;
    DLOGV_NET("tick, WifiSt=[%d] connected=[%d]", (int)NET_WifiSt, (int)IsConnected());
    if (UDPRAW::State.Status) return;

    if (!IsConnected()) {
        if (NET_WifiSt != netWifiRetrying) {
            DLOG_NET("WiFi drop detected - issuing Connect() and scheduling Reconnect in [%d] s", NET_RETRY_DELAY);
            NET_WifiSt = netWifiRetrying;
            Connect();
            setConnectTime(true);

            SCHED::AddTask("NET_Check", "Reconnect", Reconnect, SCHED::Unit::S, NET_RETRY_DELAY, NET_RETRY_DELAY, false);
        }
    } else {
        if (NET::Wifi.ConnectTime[_YY] == 0) {
            DLOG_NET("WiFi regained - restoring UDP ports%s", (RTC_Status == rtcRETRY) ? " and re-arming NTP" : "");
            NET_WifiSt = netWifiOK;
            UDPRAW::UdpSet();
            APP::UdpSet();
            DIF::UdpSet();

            if (RTC_Status == rtcRETRY) {
                RTC_Begin();
            }

            setConnectTime(false);
        }
    }
}

/** Disconnects then re-issues WiFi.begin(). Non-blocking. */
void Connect() {
    WiFi.disconnect();
    char ssidBuf[64], passBuf[64];
    strcpy_P(ssidBuf, WIFI_SSID);
    strcpy_P(passBuf, WIFI_PASS);
    DLOG_NET("WiFi.begin(\"%s\")", ssidBuf);
    WiFi.begin(ssidBuf, passBuf);
}

/** Shared WiFi status cache, refreshed at most once per NET_CHECK_INTERVAL
    ms - avoids APP::Loop()/DIF::Loop()/UDPRAW::Loop() each polling
    IsConnected() independently (up to 3 calls/cycle instead of 1). */
bool Connected_Cached() {
    if (TimeNow - NET_LastCheck >= NET_CHECK_INTERVAL) {
        NET_Connected = IsConnected();
        NET_LastCheck = TimeNow;
    }
    return NET_Connected;
}

/** Polls WiFi.localIP() until a valid (non-zero) address is assigned - the
    DHCP lease may take a second or two after WL_CONNECTED. Polls every
    500ms for up to NET_IP_TIMEOUT seconds.
    @return Assigned IPAddress, or (0,0,0,0) on timeout. */
IPAddress getIP() {
    uint8_t ticks = (uint8_t)(NET_IP_TIMEOUT * 2);
    IPAddress ip  = WiFi.localIP();

    while (ip == IPAddress(0, 0, 0, 0) && ticks > 0) {
        WaitWithYield(500);
        ticks--;
        ip = WiFi.localIP();
    }

    return ip;
}

/** WiFi connectivity test - use in all modules. */
bool IsConnected() { return WiFi.status() == WL_CONNECTED; }

/** Fixed per-deployment id (MQTT_DEVICE_ID_VALUE, WiFiCredentials.h), cached
    after the first call. Used to build this board's MQTT topic path (see
    Globals.h) - see Net.h's DeviceId() comment for why this isn't derived
    from the WiFi MAC. */
const char* DeviceId() {
    static char id[MQTT_DEVICE_ID_MAX];
    static bool have = false;
    if (!have) {
        strcpy_P(id, MQTT_DEVICE_ID);
        have = true;
    }
    return id;
}

/** One-shot retry task: self-kills immediately, then blocks up to
    NET_CONNECT_TIMEOUT s (or just 1 tick if UDPRAW is streaming, so an
    active ambilight stream isn't starved - WaitWithYield() never services
    UDPRAW::Loop()) for reconnect. On success, restores every UDP port and
    re-syncs NTP; on failure, re-arms itself after NET_RETRY_DELAY s. */
void Reconnect(SCHED::TaskId taskId) {
    SCHED::KillId(taskId);

    Connect();

    uint8_t ticks = (uint8_t)(NET_CONNECT_TIMEOUT * 2);
    if (UDPRAW::State.Status) ticks = 1;
    while (!IsConnected() && ticks > 0) {
        WaitWithYield(500);
        ticks--;
    }

    if (IsConnected()) {
        DLOG_NET("reconnected after retry");
        NET_WifiSt = netWifiOK;
        getIP();

        RTC_Begin();
        setConnectTime(false);
        UDPRAW::UdpSet();
        APP::UdpSet();
        DIF::UdpSet();
    } else {
        DLOG_NET("retry timed out - re-arming in [%d] s", NET_RETRY_DELAY);
        SCHED::AddTask("Reconnect", "Reconnect", Reconnect, SCHED::Unit::S, NET_RETRY_DELAY, NET_RETRY_DELAY, false);
    }
}

/** Performs a blocking NTP sync (NET_CONNECT_TIMEOUT s ceiling, or 1 tick
    while UDPRAW is streaming) and arms the parse/resync tasks. Guarded
    against re-entrancy and skipped entirely if the time is already set
    (NTPClient throttles updates at 60s anyway). On success: applies the
    timezone offset, parses immediately, registers a 1s RTC_Parse ticker
    and a 6h RTC_Resync, both locked. On failure: schedules RTC_RetryTask
    after NET_RTC_RETRY s. */
void RTC_Begin() {
    static bool syncing = false;
    if (syncing) return;

    if (NET::Date.time[_YY] > 0 && RTC_Status == rtcOK) {
        return;
    }

    if (!IsConnected()) {
        RTC_Status = rtcRETRY;
        SCHED::AddTask("NET_RTC_Begin", "RTC_RetryTask", RTC_RetryTask, SCHED::Unit::S, NET_RTC_RETRY, NET_RTC_RETRY, false);
        return;
    }

    syncing = true;

    RTC_TimeClient.begin();

    bool ok      = false;
    uint8_t ticks = (uint8_t)(NET_CONNECT_TIMEOUT * 2);
    if (UDPRAW::State.Status) ticks = 1;

    while (ticks-- > 0) {
        if (RTC_TimeClient.update() &&
            RTC_TimeClient.getEpochTime() > 100000UL) {
            ok = true;
            break;
        }
        WaitWithYield(500);
    }

    if (!ok) {
        NET::Date.time[_HH] = NET::Date.time[_MI] = NET::Date.time[_SS] = 0;
        NET::Date.time[_DD] = NET::Date.time[_MM] = NET::Date.time[_YY] = 0;

        RTC_Status = rtcRETRY;
        syncing    = false;
        APP::termMsgLog(APP_LOG_WRN, APP_SRC_RTC, "NET", "RTC_Begin", "NTP sync FAILED - retrying in [%d] s", NET_RTC_RETRY);
        SCHED::AddTask("NET_RTC_Begin", "RTC_RetryTask", RTC_RetryTask, SCHED::Unit::S, NET_RTC_RETRY, NET_RTC_RETRY, false);
        return;
    }

    RTC_Status = rtcOK;
    RTC_TimeClient.setTimeOffset(NET_TIMEZONE * 3600L);
    RTC_Parse(SCHED::TASK_ID_NONE);

    if (NET::Date.time[_YY] > 0) {
        SCHED::KillAllUnlocked();
        SCHED::AddTask("NET_RTC_Begin", "RTC_Parse", RTC_Parse, SCHED::Unit::S, 1UL, 0, true);
        SCHED::AddTask("NET_RTC_Begin", "RTC_Resync", RTC_Resync, SCHED::Unit::S, NET_RTC_RESYNC, NET_RTC_RESYNC, true);

        setConnectTime(false);
        APP::termMsgLog(APP_LOG_INF, APP_SRC_RTC, "NET", "RTC_Begin", "NTP synced, date [%02d/%02d/%d] time [%02d:%02d:%02d]",
            NET::Date.time[_DD], NET::Date.time[_MM], NET::Date.time[_YY],
            NET::Date.time[_HH], NET::Date.time[_MI], NET::Date.time[_SS]);
    }

    syncing = false;
}

/** True UTC epoch, local offset removed. Kept for the fault-detection NTP
    check.
    @return Seconds since the Unix epoch in UTC, or 0 when never synced. */
uint32_t RTC_EpochUTC() {
    const uint32_t local = RTC_TimeClient.getEpochTime();
    if (local < (uint32_t)RTC_EPOCH_SANE) return 0;
    return local - (uint32_t)(NET_TIMEZONE * 3600L);
}

/** 1-second repeating task: unpacks the NTP epoch into NET::Date.time[]. */
void RTC_Parse(SCHED::TaskId taskId) {
    (void)taskId;
    const time_t raw = RTC_TimeClient.getEpochTime();

    NET::Date.time[_HH] = hour(raw);
    NET::Date.time[_MI] = minute(raw);
    NET::Date.time[_SS] = second(raw);
    NET::Date.time[_DD] = day(raw);
    NET::Date.time[_MM] = month(raw);
    NET::Date.time[_YY] = (uint8_t)(year(raw) - 2000);
}

/** 6-hour repeating task: forces an NTP re-sync to prevent drift, then
    refreshes the WiFi connect timestamp. */
void RTC_Resync(SCHED::TaskId taskId) {
    (void)taskId;
    if (!IsConnected()) return;

    RTC_TimeClient.forceUpdate();
    setConnectTime(false);
}

/** One-shot retry task that re-attempts NTP sync. */
void RTC_RetryTask(SCHED::TaskId taskId) {
    SCHED::KillId(taskId);
    RTC_Status = rtcOK;
    RTC_Begin();
}

/** Saves or clears the WiFi connect timestamp in NET::Wifi.ConnectTime[].
    @param reset true zeroes every field; false copies NET::Date.time[] only
    if the RTC is synced (NET::Date.time[_YY] > 0), otherwise also zeroes -
    ConnectTime[_YY] == 0 is the sentinel Check() uses to detect a fresh
    reconnect requiring port re-init. */
void setConnectTime(bool reset) {
    const bool synced = (NET::Date.time[_YY] > 0);
    const bool set    = (!reset && synced);

    NET::Wifi.ConnectTime[_HH] = set ? NET::Date.time[_HH] : 0;
    NET::Wifi.ConnectTime[_MI] = set ? NET::Date.time[_MI] : 0;
    NET::Wifi.ConnectTime[_SS] = set ? NET::Date.time[_SS] : 0;
    NET::Wifi.ConnectTime[_DD] = set ? NET::Date.time[_DD] : 0;
    NET::Wifi.ConnectTime[_MM] = set ? NET::Date.time[_MM] : 0;
    NET::Wifi.ConnectTime[_YY] = set ? NET::Date.time[_YY] : 0;
}

} // namespace NET
