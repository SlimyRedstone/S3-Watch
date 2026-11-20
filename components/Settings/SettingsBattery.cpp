/*
 * Settings > Battery: the rows' texts (C getters called by ui/screens.c), kept
 * current while the menu is open, and the statistics behind the battery-life
 * rows.
 *
 * The AXP2101 measures no current (no current ADC, no coulomb counter). The
 * mean current is estimated from its fuel gauge: % lost over time x
 * BATTERY_CAPACITY_MAH. 1 % is 4 mAh: one step every 2.4 min at 100 mA, every
 * 24 min at 10 mA. Life rows show "measuring..." until a few steps were seen.
 *
 * Measuring (on battery only):
 *   - awake: gauge read every SAMPLE_US by the status timer.
 *   - sleeping: ClockApp tells when. Light sleep (or its BLE/Wi-Fi stand-in):
 *     batterySleepBegin() / batterySleepEnd(), gauge read at both ends. Deep
 *     sleep / power off: batteryCommit() saves the gauge % and the wall-clock
 *     time; the next boot compares (after the RTC sync).
 * No ULP: it could only read the same gauge %, which the wake-up read gives.
 *
 * Awake and sleep rates are separate sums (time, % lost) with exponential
 * forgetting (*_WINDOW_MS), so they follow the use of the watch. Saved in NVS
 * ("settings"/"bat_stats"): a deep sleep is a reboot.
 */
#include "Settings.hpp"

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <mutex>

#include "esp_timer.h"
#include "nvs.h"

#ifdef ESP_UTILS_LOG_TAG
#   undef ESP_UTILS_LOG_TAG
#endif
#define ESP_UTILS_LOG_TAG "Settings"
#include "esp_lib_utils.h"

using esp_brookesia::apps::Settings;

namespace {

constexpr int64_t  SAMPLE_US        = 10LL * 1000 * 1000;          // gauge read while awake
constexpr int64_t  SAVE_PERIOD_US   = 15LL * 60 * 1000 * 1000;     // statistics -> NVS while awake (crash, forced off)
constexpr int64_t  INFO_MAX_AGE_US  = 500 * 1000;                  // row getters share one PMU read
constexpr int      JUMP_PCT         = 3;                           // bigger step in one sample / light sleep: gauge re-calibration, ignored
constexpr float    HOUR_MS          = 3600.0f * 1000.0f;
constexpr float    AWAKE_WINDOW_MS  = 2.0f * HOUR_MS;              // forgetting: about the last 2 h awake
constexpr float    SLEEP_WINDOW_MS  = 48.0f * HOUR_MS;             // about the last 48 h asleep
constexpr float    AWAKE_MIN_MS     = 5.0f * 60.0f * 1000.0f;      // shown after this much time ...
constexpr float    SLEEP_MIN_MS     = 1.0f * HOUR_MS;
constexpr float    MIN_STEPS_PCT    = 2.0f;                        // ... and this many gauge steps
constexpr float    BOUND_MS         = 6.0f * HOUR_MS;              // fewer steps after this long: lower bound ("> 12d04h")
constexpr int64_t  OFF_MAX_S        = 30LL * 24 * 3600;            // longer deep sleep / power off: ignored
constexpr time_t   TIME_VALID_AFTER = 1704067200;                  // 2024-01-01: wall clock set (RTC synced)
constexpr const char *NVS_KEY       = "bat_stats";
constexpr uint32_t STATS_MAGIC      = 0xBA750001;                  // layout version

// AXP2101 STATUS2[2:0]
constexpr uint8_t CHG_TRICKLE = 0, CHG_PRE = 1, CHG_CC = 2, CHG_CV = 3, CHG_DONE = 4;

// Row titles: the getters' prefixes, found again by them to refresh the rows.
constexpr const char *ROW_STATUS     = "Status: ";
constexpr const char *ROW_USB        = "USB connected: ";
constexpr const char *ROW_VBUS       = "USB voltage: ";
constexpr const char *ROW_VBAT       = "Voltage: ";
constexpr const char *ROW_LIFE      = "Est. life: ";
constexpr const char *ROW_SLEEP_LIFE = "Est. life, while sleeping: ";
constexpr const char *MEASURING      = "measuring...";

struct Rate {                 // time spent in one state, and the gauge % lost in it
    float ms;
    float pct;                // signed: the gauge can step back up
};

struct Stats {                // NVS blob (no padding: 4 + 8 + 8 + 4 + 8)
    uint32_t magic;
    Rate     awake;
    Rate     sleep;
    int32_t  off_soc;         // gauge % at the last deep sleep / power off, -1 = none pending
    int64_t  off_time;        // wall clock then (s)
};

struct Mark {                 // one gauge reading
    bool    valid;
    bool    vbus;
    int     soc;
    int64_t us;               // esp_timer (runs through light sleep)
};

std::mutex s_mtx;             // LVGL task (tick) vs ClockApp's sleep task (begin / end / commit)
Stats      s_stats        = {STATS_MAGIC, {0, 0}, {0, 0}, -1, 0};
bool       s_dirty        = false;
Mark       s_base        = {};              // last awake reading
Mark       s_sleep0       = {};              // reading at sleep start
bool       s_sleeping     = false;
int64_t    s_last_sample  = 0;
int64_t    s_last_save    = 0;

void add(Rate &r, float ms, float pct, float window_ms)
{
    if (ms <= 0) return;
    r.ms  += ms;
    r.pct += pct;
    if (r.ms > window_ms) {                    // keep the last window_ms, older data fades out
        const float k = window_ms / r.ms;
        r.ms  *= k;
        r.pct *= k;
    }
}

Mark mark_of(const Settings::BatteryInfo &bi, int64_t now)
{
    return {bi.battery && bi.soc >= 0, bi.vbus, bi.soc, now};
}

// Awake reading: on battery, the % lost since the previous one goes to the
// awake sum. On USB (or plugged / unplugged in between) nothing. s_mtx held.
void awake_step(const Settings::BatteryInfo &bi, int64_t now)
{
    const Mark m    = mark_of(bi, now);
    const Mark prev = s_base;
    s_base = m;
    if (!m.valid || !prev.valid || prev.vbus || m.vbus) return;
    const float ms = (float)(now - prev.us) / 1000.0f;
    const int   d  = prev.soc - m.soc;             // % lost
    if (std::abs(d) > JUMP_PCT) return;
    add(s_stats.awake, ms, (float)d, AWAKE_WINDOW_MS);
    s_dirty = true;
}

void stats_save()
{
    Stats copy;
    {
        std::lock_guard<std::mutex> lk(s_mtx);
        if (!s_dirty) return;
        copy    = s_stats;
        s_dirty = false;
    }
    nvs_handle_t h;
    esp_err_t e = nvs_open(Settings::CFG_NVS_NAMESPACE, NVS_READWRITE, &h);
    if (e == ESP_OK) {
        e = nvs_set_blob(h, NVS_KEY, &copy, sizeof(copy));
        if (e == ESP_OK) e = nvs_commit(h);
        nvs_close(h);
    }
    if (e != ESP_OK) {
        ESP_UTILS_LOGW("Battery statistics not saved (%s)", esp_err_to_name(e));
        std::lock_guard<std::mutex> lk(s_mtx);
        s_dirty = true;
    }
    s_last_save = esp_timer_get_time();
}

// Deep sleep / power off since the last commit -> sleep sum. Waits for a set
// wall clock (ClockApp syncs it from the RTC chip at boot). s_mtx held.
// Returns true when the pending record was used up.
bool consume_off_record(const Settings::BatteryInfo &bi)
{
    if (s_stats.off_soc < 0) return false;
    const time_t now = time(nullptr);
    if (now < TIME_VALID_AFTER) return false;
    const int64_t dt = (int64_t)now - s_stats.off_time;
    const int     d  = s_stats.off_soc - bi.soc;
    // USB now, or the gauge went up: charged meanwhile, unknown share. Clock
    // reset or very long: unknown time.
    if (bi.battery && bi.soc >= 0 && !bi.vbus && dt > 0 && dt <= OFF_MAX_S && d >= -1) {
        add(s_stats.sleep, (float)dt * 1000.0f, (float)d, SLEEP_WINDOW_MS);
        ESP_UTILS_LOGI("Battery: %d %% in %lld min of deep sleep / power off", d, (long long)(dt / 60));
    }
    s_stats.off_soc = -1;
    s_dirty         = true;
    return true;
}

// ---- Estimates ----

enum class Est : uint8_t { NONE, BOUND, OK };

// Mean current (mA) of a rate: pct/100 x capacity over ms/3.6e6 h. BOUND: few
// steps in a long time, the current is at most this (taken as >= 1 step).
Est estimate(const Rate &r, float min_ms, float &ma)
{
    if (r.ms >= min_ms && r.pct >= MIN_STEPS_PCT) {
        ma = r.pct * (float)Settings::BATTERY_CAPACITY_MAH * 36000.0f / r.ms;
        return Est::OK;
    }
    if (r.ms >= BOUND_MS) {
        const float pct = (r.pct < 1.0f) ? 1.0f : r.pct;
        ma = pct * (float)Settings::BATTERY_CAPACITY_MAH * 36000.0f / r.ms;
        return Est::BOUND;
    }
    return Est::NONE;
}

// "1h35", "12d04h" from 100 h up.
void format_duration(char *out, size_t len, float hours)
{
    if (!(hours > 0)) hours = 0;
    if (hours > 999.0f * 24.0f) hours = 999.0f * 24.0f;
    const long mins = lroundf(hours * 60.0f);
    if (mins < 100L * 60) {
        snprintf(out, len, "%ldh%02ld", mins / 60, mins % 60);
    } else {
        const long h = mins / 60;
        snprintf(out, len, "%ldd%02ldh", h / 24, h % 24);
    }
}

// Shared PMU read for the row getters (LVGL task only).
const Settings::BatteryInfo &info()
{
    static Settings::BatteryInfo bi = {};
    static int64_t               at = 0;
    const int64_t now = esp_timer_get_time();
    if (at == 0 || now - at >= INFO_MAX_AGE_US) {
        Settings::readBatteryInfo(bi);
        at = now;
    }
    return bi;
}

const char *charge_state_text(const Settings::BatteryInfo &bi)
{
    if (!bi.pmu)     return "unknown";
    if (!bi.battery) return "NO BATTERY";
    switch (bi.charge_state) {
        case CHG_TRICKLE:
        case CHG_PRE:  return "PRE-CHARGING";          // deeply discharged cell
        case CHG_CC:   return "CHARGING";
        case CHG_CV:   return "CHARGING (TOP-OFF)";    // constant voltage, current tapering
        case CHG_DONE: return "CHARGED";
        default:       return bi.vbus ? "NOT CHARGING" : "DISCHARGING";
    }
}

bool is_charging(const Settings::BatteryInfo &bi)
{
    return bi.vbus && bi.charge_state <= CHG_CV;
}

// Battery life rows: remaining capacity over the given rate.
const char *life_text(char *out, size_t len, const char *prefix, bool sleep)
{
    const auto &bi = info();
    if (!bi.pmu || !bi.battery || bi.soc < 0) { snprintf(out, len, "%s-", prefix); return out; }
    if (bi.vbus) { snprintf(out, len, "%s%s", prefix, is_charging(bi) ? "charging" : "on USB power"); return out; }

    Rate r;
    {
        std::lock_guard<std::mutex> lk(s_mtx);
        r = sleep ? s_stats.sleep : s_stats.awake;
    }
    float ma = 0;
    const Est e = estimate(r, sleep ? SLEEP_MIN_MS : AWAKE_MIN_MS, ma);
    if (e == Est::NONE || ma <= 0) { snprintf(out, len, "%s%s", prefix, MEASURING); return out; }

    const float remaining_mah = (float)bi.soc / 100.0f * (float)Settings::BATTERY_CAPACITY_MAH;
    char dur[16];
    format_duration(dur, sizeof(dur), remaining_mah / ma);
    snprintf(out, len, "%s%s%s", prefix, (e == Est::BOUND) ? "> " : "", dur);
    return out;
}

} // namespace

// ---------------------------------------------------------------------------
// Row getters (ui/screens.c builds each row once from these, the status timer
// rewrites them). Static buffers: lv_label_set_text() copies.
// ---------------------------------------------------------------------------
extern "C" {
    #include "settings_ui_bridge.h"

    const char *get_battery_charge_status_label(void) {
        static char txt[48];
        snprintf(txt, sizeof(txt), "%s%s", ROW_STATUS, charge_state_text(info()));
        return txt;
    }

    const char *get_battery_usb_status_label(void) {
        static char txt[32];
        snprintf(txt, sizeof(txt), "%s%s", ROW_USB, info().vbus ? "true" : "false");
        return txt;
    }

    const char *get_battery_voltage_label(void) {
        static char txt[40];
        const auto &bi = info();
        if (bi.battery && bi.vbat_mv > 0) snprintf(txt, sizeof(txt), "%s%dmV", ROW_VBAT, bi.vbat_mv);
        else                              snprintf(txt, sizeof(txt), "%s-", ROW_VBAT);
        return txt;
    }

    const char *get_battery_ac_voltage_label(void) {
        static char txt[32];
        snprintf(txt, sizeof(txt), "%s%dmV", ROW_VBUS, info().vbus_mv);
        return txt;
    }

    const char *get_battery_estimated_life_label(void) {
        static char txt[64];
        return life_text(txt, sizeof(txt), ROW_LIFE, false);
    }

    const char *get_battery_estimated_sleep_life_label(void) {
        static char txt[72];
        return life_text(txt, sizeof(txt), ROW_SLEEP_LIFE, true);
    }
}

// ---------------------------------------------------------------------------
// Rows on screen: found by their prefix after each menu build, rewritten by
// the status timer when the text changes. The DELETE event (menu closed)
// drops the pointer.
// ---------------------------------------------------------------------------
namespace {

struct Row {
    const char *prefix;
    const char *(*text)(void);
    lv_obj_t   *label;
};

Row s_rows[] = {
    {ROW_STATUS,     get_battery_charge_status_label,        nullptr},
    {ROW_USB,        get_battery_usb_status_label,           nullptr},
    {ROW_VBAT,       get_battery_voltage_label,              nullptr},
    {ROW_VBUS,       get_battery_ac_voltage_label,           nullptr},
    {ROW_LIFE,      get_battery_estimated_life_label,       nullptr},
    {ROW_SLEEP_LIFE, get_battery_estimated_sleep_life_label, nullptr},
};

void row_label_deleted_cb(lv_event_t *e)
{
    lv_obj_t *obj = lv_event_get_target_obj(e);
    for (auto &row : s_rows) {
        if (row.label == obj) row.label = nullptr;
    }
}

void bind_rows(lv_obj_t *root)
{
    if (root == nullptr) return;
    if (lv_obj_check_type(root, &lv_label_class)) {
        const char *t = lv_label_get_text(root);
        if (t == nullptr) return;
        for (auto &row : s_rows) {
            if (row.label == nullptr && strncmp(t, row.prefix, strlen(row.prefix)) == 0) {
                row.label = root;
                lv_obj_add_event_cb(root, row_label_deleted_cb, LV_EVENT_DELETE, nullptr);
                return;
            }
        }
        return;
    }
    const uint32_t n = lv_obj_get_child_count(root);
    for (uint32_t i = 0; i < n; i++) bind_rows(lv_obj_get_child(root, (int32_t)i));
}

} // namespace

void Settings::batteryBindLabels(lv_obj_t *root)
{
    for (auto &row : s_rows) row.label = nullptr;
    bind_rows(root);
}

void Settings::refreshBatteryLabels()
{
    for (auto &row : s_rows) {
        if (row.label == nullptr) continue;
        const char *now   = row.text();
        const char *shown = lv_label_get_text(row.label);
        if (shown == nullptr || strcmp(shown, now) != 0) lv_label_set_text(row.label, now);
    }
}

// ---------------------------------------------------------------------------
// Statistics
// ---------------------------------------------------------------------------

void Settings::batteryInit()
{
    nvs_handle_t h;
    if (nvs_open(CFG_NVS_NAMESPACE, NVS_READONLY, &h) != ESP_OK) return;   // nothing saved yet
    Stats  st  = {};
    size_t len = sizeof(st);
    const esp_err_t e = nvs_get_blob(h, NVS_KEY, &st, &len);
    nvs_close(h);
    if (e != ESP_OK) return;
    const bool sane = len == sizeof(st) && st.magic == STATS_MAGIC &&
                      std::isfinite(st.awake.ms) && std::isfinite(st.awake.pct) && st.awake.ms >= 0 &&
                      std::isfinite(st.sleep.ms) && std::isfinite(st.sleep.pct) && st.sleep.ms >= 0;
    if (!sane) {
        ESP_UTILS_LOGW("Battery statistics in NVS unreadable: start over");
        return;
    }
    std::lock_guard<std::mutex> lk(s_mtx);
    s_stats = st;
    ESP_UTILS_LOGI("Battery statistics: awake %.1f %% / %.0f min, asleep %.1f %% / %.0f min%s",
                   st.awake.pct, st.awake.ms / 60000.0f, st.sleep.pct, st.sleep.ms / 60000.0f,
                   (st.off_soc >= 0) ? ", deep sleep pending" : "");
}

void Settings::batteryTick()
{
    const int64_t now = esp_timer_get_time();
    if (s_last_sample != 0 && now - s_last_sample < SAMPLE_US) return;
    s_last_sample = now;

    BatteryInfo bi;
    if (!readBatteryInfo(bi)) return;

    bool save_now = false;
    {
        std::lock_guard<std::mutex> lk(s_mtx);
        if (!s_sleeping) awake_step(bi, now);         // stand-in sleep: the LVGL task still runs, not awake time
        if (consume_off_record(bi)) save_now = true;  // a second boot must not count it again
        if (s_dirty && now - s_last_save >= SAVE_PERIOD_US) save_now = true;
    }
    if (save_now) stats_save();
}

void Settings::batterySleepBegin()
{
    BatteryInfo bi;
    readBatteryInfo(bi);
    const int64_t now = esp_timer_get_time();
    std::lock_guard<std::mutex> lk(s_mtx);
    awake_step(bi, now);                              // awake time up to here
    s_sleep0   = mark_of(bi, now);
    s_sleeping = true;
}

void Settings::batterySleepEnd()
{
    BatteryInfo bi;
    readBatteryInfo(bi);
    const int64_t now = esp_timer_get_time();
    const Mark    m   = mark_of(bi, now);
    std::lock_guard<std::mutex> lk(s_mtx);
    if (!s_sleeping) return;
    s_sleeping = false;
    if (s_sleep0.valid && m.valid && !s_sleep0.vbus && !m.vbus) {   // on battery the whole time
        const float ms = (float)(now - s_sleep0.us) / 1000.0f;
        const int   d  = s_sleep0.soc - m.soc;
        if (std::abs(d) <= JUMP_PCT) {
            add(s_stats.sleep, ms, (float)d, SLEEP_WINDOW_MS);
            s_dirty = true;
        }
    }
    s_base = m;                                                     // awake again from here
}

void Settings::batteryCommit()
{
    BatteryInfo bi;
    readBatteryInfo(bi);
    const int64_t now = esp_timer_get_time();
    {
        std::lock_guard<std::mutex> lk(s_mtx);
        if (!s_sleeping) awake_step(bi, now);
        const time_t wall = time(nullptr);
        // On USB the gauge would only go up while off: nothing to learn.
        if (bi.battery && bi.soc >= 0 && !bi.vbus && wall >= TIME_VALID_AFTER) {
            s_stats.off_soc  = bi.soc;
            s_stats.off_time = (int64_t)wall;
        } else {
            s_stats.off_soc = -1;
        }
        s_dirty = true;
    }
    stats_save();
}
