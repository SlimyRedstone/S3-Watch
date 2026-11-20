/*
 * Settings > Wi-Fi: the station, owned by Settings, used by every app.
 *
 * Saved networks: settings.wifi.ssid / settings.wifi.pass, two arrays of the
 * same length, tried in order. A "pass" entry is the password, or false for
 * an open network:
 *     "ssid": ["Home", "Phone hotspot", "Cafe"],
 *     "pass": ["password", "other password", false]
 * Arrays of different lengths (or a bad entry) reject the whole list: logged,
 * Wi-Fi status FAILED with the reason, and the arrays are saved back as they
 * were written so the user can fix them.
 *
 * One task ("wifi_mgr", core 0) makes every esp_wifi call, so apps never race
 * each other on the driver. It is created the first time Wi-Fi is needed:
 * with Wi-Fi disabled, or on standby (disabled_on_boot), it costs no RAM.
 * Apps only touch counters and flags, then post an EVAL message:
 *   requestWifi()  -> users + 1, retry after a failed pass, EVAL
 *   releaseWifi()  -> users - 1, EVAL
 *   applyWifi()    -> EVAL (boot, "Enable WiFi" switch)
 * EVAL starts the radio when it is wanted (disabled_on_boot false, or a user
 * holds a request) and stops it otherwise (at once when Wi-Fi is disabled,
 * WIFI_IDLE_STOP_MS after the last release on standby).
 *
 * Connecting: WIFI_EVENT_STA_START -> network 0; each failure (disconnect
 * event, or no IP after WIFI_ATTEMPT_TIMEOUT_MS) -> next network; an IP ->
 * CONNECTED. After the last network: FAILED, radio off until the next request
 * or switch toggle. A connection lost later -> one new pass over the list.
 * The radio is fully de-initialised when off: its RAM goes back to BLE & co.
 */
#include "Settings.hpp"

#include <algorithm>
#include <atomic>
#include <cstdarg>
#include <cstdio>
#include <cstring>
#include <mutex>

#include "cJSON.h"
#include "esp_attr.h"
#include "esp_event.h"
#include "esp_heap_caps.h"
#include "esp_netif.h"
#include "esp_wifi.h"
#include "freertos/FreeRTOS.h"
#include "freertos/event_groups.h"
#include "freertos/queue.h"
#include "freertos/task.h"

#ifdef ESP_UTILS_LOG_TAG
#   undef ESP_UTILS_LOG_TAG
#endif
#define ESP_UTILS_LOG_TAG "WiFi"
#include "esp_lib_utils.h"

namespace esp_brookesia::apps {

volatile bool Settings::wifi_running = false;

namespace {

using WifiStatus  = Settings::WifiStatus;
using WifiNetwork = Settings::WifiNetwork;

constexpr uint32_t    TASK_STACK     = 4096;          // esp_wifi_init/start (PHY, NVS) run here: internal RAM
constexpr UBaseType_t TASK_PRIO      = 5;
constexpr BaseType_t  TASK_CORE      = 0;             // radio core (main.cpp)
constexpr uint32_t    RSSI_PERIOD_MS = 10000;         // status-bar bars
constexpr int         QUEUE_LEN      = 8;
constexpr EventBits_t BIT_CONNECTED  = BIT0;
constexpr EventBits_t BIT_FAILED     = BIT1;          // pass ended without an IP, or Wi-Fi turned off

// ---- Saved networks: written at load, read-only afterwards ----
EXT_RAM_BSS_ATTR WifiNetwork s_nets[Settings::WIFI_MAX_NETWORKS];
int    s_net_count       = 0;
char   s_list_error[160] = "";                        // non-empty: wifi.ssid / wifi.pass rejected
cJSON *s_raw_ssid        = nullptr;                   // rejected arrays, saved back untouched
cJSON *s_raw_pass        = nullptr;

// ---- Shared between the app tasks and the Wi-Fi task ----
std::atomic<WifiStatus> s_status{WifiStatus::DISABLED};
std::atomic<int>        s_users{0};
std::atomic<bool>       s_retry{false};               // a request arrived after a failed pass
std::atomic<int>        s_rssi{0};
std::atomic<uint32_t>   s_ip{0};                      // IPv4, network order, 0 = none
char                    s_fail_text[160] = "";        // written before s_status becomes FAILED

QueueHandle_t      s_queue    = nullptr;
EventGroupHandle_t s_bits     = nullptr;
std::once_flag     s_infra_once;
std::atomic<bool>  s_infra_ok{false};

// ---- Wi-Fi task only ----
esp_netif_t *s_sta           = nullptr;
bool         s_radio         = false;                 // esp_wifi initialised and started
int          s_idx           = -1;                    // network being tried / connected
bool         s_attempt_armed = false;
TickType_t   s_attempt_end   = 0;
bool         s_idle_armed    = false;
TickType_t   s_idle_end      = 0;
TickType_t   s_rssi_next     = 0;
const char  *s_last_reason   = "";

enum class Cmd : uint8_t { EVAL, STA_START, DISCONNECTED, GOT_IP };
struct Msg {
    Cmd      cmd;
    uint8_t  reason;                                  // DISCONNECTED
    uint8_t  ssid_len;                                // DISCONNECTED
    uint8_t  ssid[32];
    uint32_t ip;                                      // GOT_IP
};

bool keep_on()     { return Settings::cfg.wifi.enabled && !Settings::cfg.wifi.disabled_on_boot; }
bool list_usable() { return s_list_error[0] == '\0' && s_net_count > 0; }

bool expired(TickType_t end) { return (int32_t)(xTaskGetTickCount() - end) >= 0; }

__attribute__((format(printf, 1, 2)))
void set_failed(const char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(s_fail_text, sizeof(s_fail_text), fmt, ap);
    va_end(ap);
    s_status = WifiStatus::FAILED;
    if (s_bits != nullptr) {
        xEventGroupClearBits(s_bits, BIT_CONNECTED);
        xEventGroupSetBits(s_bits, BIT_FAILED);
    }
}

// Reason of a failed attempt, for the log and the failure text.
const char *reason_text(uint8_t r)
{
    switch (r) {
    case WIFI_REASON_NO_AP_FOUND:
        return "not in range";
    case WIFI_REASON_NO_AP_FOUND_W_COMPATIBLE_SECURITY:
    case WIFI_REASON_NO_AP_FOUND_IN_AUTHMODE_THRESHOLD:
        return "security mismatch (password given for an open network?)";
    case WIFI_REASON_AUTH_FAIL:
    case WIFI_REASON_4WAY_HANDSHAKE_TIMEOUT:
    case WIFI_REASON_HANDSHAKE_TIMEOUT:
        return "wrong password";
    case WIFI_REASON_NO_AP_FOUND_IN_RSSI_THRESHOLD:
        return "signal too weak";
    default:
        return "connection refused";
    }
}

void post(const Msg &m)
{
    if (s_queue == nullptr) return;
    if (xQueueSend(s_queue, &m, pdMS_TO_TICKS(500)) != pdTRUE) {
        ESP_UTILS_LOGW("Wi-Fi task queue full, message %d dropped", (int)m.cmd);
    }
}

void post_eval()
{
    Msg m = {};
    m.cmd = Cmd::EVAL;
    post(m);
}

// Runs in the default event loop task: forward to the Wi-Fi task.
void on_event(void * /*arg*/, esp_event_base_t base, int32_t id, void *data)
{
    Msg m = {};
    if (base == WIFI_EVENT && id == WIFI_EVENT_STA_START) {
        m.cmd = Cmd::STA_START;
    } else if (base == WIFI_EVENT && id == WIFI_EVENT_STA_DISCONNECTED) {
        const auto *d = static_cast<const wifi_event_sta_disconnected_t *>(data);
        m.cmd      = Cmd::DISCONNECTED;
        m.reason   = d->reason;
        m.ssid_len = std::min<uint8_t>(d->ssid_len, sizeof(m.ssid));
        memcpy(m.ssid, d->ssid, m.ssid_len);
    } else if (base == IP_EVENT && id == IP_EVENT_STA_GOT_IP) {
        m.cmd = Cmd::GOT_IP;
        m.ip  = static_cast<const ip_event_got_ip_t *>(data)->ip_info.ip.addr;
    } else {
        return;
    }
    if (s_queue != nullptr && xQueueSend(s_queue, &m, 0) != pdTRUE) {
        ESP_UTILS_LOGW("Wi-Fi event %d dropped (queue full)", (int)id);
    }
}

// device.name -> hostname (RFC 1123: letters, digits, '-', no '-' at the
// ends). Spaces, '_' and '.' become '-', anything else is dropped:
// "SlimyRedstone's Watch" -> "SlimyRedstones-Watch".
void hostname_from(const char *name, char *out, size_t out_len)
{
    size_t n = 0;
    for (const char *p = name; *p != '\0' && n + 1 < out_len; p++) {
        const char c = *p;
        if ((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9')) {
            out[n++] = c;
        } else if ((c == ' ' || c == '-' || c == '_' || c == '.') && n > 0 && out[n - 1] != '-') {
            out[n++] = '-';
        }
    }
    while (n > 0 && out[n - 1] == '-') n--;
    out[n] = '\0';
    if (n == 0) snprintf(out, out_len, "S3-Watch");
}

// ---------------------------------------------------------------------------
// Radio (Wi-Fi task)
// ---------------------------------------------------------------------------
void radio_stop()
{
    s_attempt_armed = false;
    s_idle_armed    = false;
    s_idx           = -1;
    if (s_radio) {
        esp_wifi_disconnect();
        esp_wifi_stop();
        esp_wifi_deinit();
        s_radio = false;
    }
    if (s_sta != nullptr) {
        esp_netif_destroy_default_wifi(s_sta);
        s_sta = nullptr;
    }
    Settings::wifi_running = false;
    s_rssi = 0;
    s_ip   = 0;
    xEventGroupClearBits(s_bits, BIT_CONNECTED);
    ESP_UTILS_LOGI("Radio off (internal RAM free %u)", (unsigned)heap_caps_get_free_size(MALLOC_CAP_INTERNAL));
}

bool radio_start()
{
    s_retry       = false;
    s_last_reason = "";
    // Same idea as BLEMouse's start check: the driver's buffers and task are
    // internal RAM, and the BT controller asserts when it runs out of it.
    const size_t free_before = heap_caps_get_free_size(MALLOC_CAP_INTERNAL);
    const size_t block       = heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL);
    if (free_before < Settings::WIFI_MIN_INTERNAL_FREE || block < Settings::WIFI_MIN_INTERNAL_BLOCK) {
        ESP_UTILS_LOGE("Wi-Fi not started: internal RAM %u free / %u max block (need %u / %u)", (unsigned)free_before,
                       (unsigned)block, (unsigned)Settings::WIFI_MIN_INTERNAL_FREE,
                       (unsigned)Settings::WIFI_MIN_INTERNAL_BLOCK);
        set_failed("Not enough memory to start Wi-Fi (%u KB free). Turn Bluetooth off and try again.",
                   (unsigned)(free_before / 1024));
        return false;
    }
    if (s_sta == nullptr) s_sta = esp_netif_create_default_wifi_sta();
    if (s_sta != nullptr) {
        // Name the router shows for the watch (DHCP option 12), before DHCP starts.
        char host[33];
        hostname_from(Settings::cfg.device.name, host, sizeof(host));
        if (esp_netif_set_hostname(s_sta, host) == ESP_OK) ESP_UTILS_LOGI("Hostname '%s'", host);
    }

    wifi_init_config_t ic = WIFI_INIT_CONFIG_DEFAULT();
    esp_err_t e = esp_wifi_init(&ic);
    if (e == ESP_OK) {
        e = esp_wifi_set_storage(WIFI_STORAGE_RAM);   // networks live in the settings, not in the driver's NVS
        if (e == ESP_OK) e = esp_wifi_set_mode(WIFI_MODE_STA);
        if (e == ESP_OK) e = esp_wifi_start();       // -> WIFI_EVENT_STA_START -> first network
        if (e != ESP_OK) esp_wifi_deinit();
    }
    if (e != ESP_OK) {
        ESP_UTILS_LOGE("Wi-Fi start failed: %s (internal RAM free %u)", esp_err_to_name(e), (unsigned)free_before);
        if (s_sta != nullptr) {
            esp_netif_destroy_default_wifi(s_sta);
            s_sta = nullptr;
        }
        if (e == ESP_ERR_NO_MEM) {
            set_failed("Not enough memory to start Wi-Fi (%u KB free). Turn Bluetooth off and try again.",
                       (unsigned)(free_before / 1024));
        } else {
            set_failed("Wi-Fi could not start (%s).", esp_err_to_name(e));
        }
        return false;
    }
    s_radio = true;
    s_idx   = -1;
    Settings::wifi_running = true;
    s_status = WifiStatus::CONNECTING;
    xEventGroupClearBits(s_bits, BIT_CONNECTED | BIT_FAILED);
    ESP_UTILS_LOGI("Radio on: internal RAM %u -> %u free, %d saved network(s)", (unsigned)free_before,
                   (unsigned)heap_caps_get_free_size(MALLOC_CAP_INTERNAL), s_net_count);
    return true;
}

// Next saved network, or FAILED once the list is exhausted.
void try_next()
{
    while (++s_idx < s_net_count) {
        const WifiNetwork &n = s_nets[s_idx];
        wifi_config_t wc = {};
        memcpy(wc.sta.ssid, n.ssid, strnlen(n.ssid, sizeof(wc.sta.ssid)));        // 32 bytes, no terminator needed
        if (n.has_pass) memcpy(wc.sta.password, n.pass, strnlen(n.pass, sizeof(wc.sta.password)));
        wc.sta.threshold.authmode = n.has_pass ? WIFI_AUTH_WEP : WIFI_AUTH_OPEN;
        wc.sta.scan_method        = WIFI_ALL_CHANNEL_SCAN;   // several APs with this name: the strongest one
        wc.sta.sort_method        = WIFI_CONNECT_AP_BY_SIGNAL;
        wc.sta.failure_retry_cnt  = 1;
        wc.sta.sae_pwe_h2e        = WPA3_SAE_PWE_BOTH;
        esp_err_t e = esp_wifi_set_config(WIFI_IF_STA, &wc);
        if (e == ESP_OK) e = esp_wifi_connect();
        if (e != ESP_OK) {
            ESP_UTILS_LOGW("'%s': %s", n.ssid, esp_err_to_name(e));
            s_last_reason = "driver error";
            continue;
        }
        ESP_UTILS_LOGI("Trying '%s' (%d/%d)", n.ssid, s_idx + 1, s_net_count);
        s_attempt_armed = true;
        s_attempt_end   = xTaskGetTickCount() + pdMS_TO_TICKS(Settings::WIFI_ATTEMPT_TIMEOUT_MS);
        return;
    }

    s_attempt_armed = false;
    ESP_UTILS_LOGE("None of the %d saved network(s) could be joined: radio off", s_net_count);
    const char *only = (s_net_count == 1) ? s_nets[0].ssid : nullptr;
    const char *why  = s_last_reason;
    radio_stop();
    if (only != nullptr) set_failed("Could not connect to Wi-Fi '%s': %s.", only, why);
    else                 set_failed("Could not connect to any of the %d saved Wi-Fi networks.", s_net_count);
}

void on_disconnected(const Msg &m)
{
    if (!s_radio || s_idx < 0 || s_idx >= s_net_count) return;          // radio restarted, or not trying
    const WifiNetwork &n = s_nets[s_idx];
    if (m.ssid_len != strlen(n.ssid) || memcmp(m.ssid, n.ssid, m.ssid_len) != 0) return;   // aborted attempt

    if (s_status == WifiStatus::CONNECTED) {
        ESP_UTILS_LOGW("Lost '%s' (reason %u): trying the saved networks again", n.ssid, (unsigned)m.reason);
        s_status = WifiStatus::CONNECTING;
        xEventGroupClearBits(s_bits, BIT_CONNECTED);
        s_rssi        = 0;
        s_ip          = 0;
        s_idx         = -1;
        s_last_reason = "";
        try_next();
        return;
    }
    s_last_reason = reason_text(m.reason);
    ESP_UTILS_LOGW("'%s': %s (reason %u)", n.ssid, s_last_reason, (unsigned)m.reason);
    s_attempt_armed = false;
    try_next();
}

void on_got_ip(const Msg &m)
{
    if (!s_radio || s_idx < 0 || s_idx >= s_net_count) return;
    s_attempt_armed = false;
    wifi_ap_record_t ap = {};
    s_rssi      = (esp_wifi_sta_get_ap_info(&ap) == ESP_OK) ? ap.rssi : -70;
    s_rssi_next = xTaskGetTickCount() + pdMS_TO_TICKS(RSSI_PERIOD_MS);
    esp_ip4_addr_t ip = {};
    ip.addr = m.ip;
    ESP_UTILS_LOGI("Connected to '%s', IP " IPSTR ", %d dBm", s_nets[s_idx].ssid, IP2STR(&ip), s_rssi.load());
    s_ip     = m.ip;
    s_status = WifiStatus::CONNECTED;
    xEventGroupClearBits(s_bits, BIT_FAILED);
    xEventGroupSetBits(s_bits, BIT_CONNECTED);
}

// Radio on or off, from cfg.wifi and the users.
void evaluate()
{
    if (!Settings::cfg.wifi.enabled) {
        if (s_radio || s_sta != nullptr) radio_stop();
        s_status = WifiStatus::DISABLED;
        xEventGroupSetBits(s_bits, BIT_FAILED);       // wake waiters: no connection coming
        return;
    }
    if (!list_usable()) {
        if (s_radio) radio_stop();
        if (s_list_error[0] != '\0') set_failed("%s", s_list_error);
        else set_failed("No Wi-Fi network saved: add one to settings.wifi.ssid / settings.wifi.pass.");
        return;
    }
    if (keep_on() || s_users > 0) {
        s_idle_armed = false;
        // After a failed pass, only a new request (s_retry) starts another one.
        if (!s_radio && (s_status != WifiStatus::FAILED || s_retry.exchange(false))) radio_start();
        return;
    }
    // Standby (disabled_on_boot) and nobody needs it: radio off after a grace
    // period, so back-to-back requests reuse the connection.
    if (s_radio) {
        if (!s_idle_armed) {
            s_idle_armed = true;
            s_idle_end   = xTaskGetTickCount() + pdMS_TO_TICKS(Settings::WIFI_IDLE_STOP_MS);
        }
    } else if (s_status != WifiStatus::FAILED) {
        s_status = WifiStatus::IDLE;
    }
}

void wifi_task(void *)
{
    for (;;) {
        TickType_t wait = portMAX_DELAY;
        auto until = [&wait](bool armed, TickType_t end) {
            if (!armed) return;
            const int32_t left = (int32_t)(end - xTaskGetTickCount());
            wait = std::min<TickType_t>(wait, (left > 0) ? (TickType_t)left : 0);
        };
        until(s_attempt_armed, s_attempt_end);
        until(s_idle_armed, s_idle_end);
        until(s_radio && s_status == WifiStatus::CONNECTED, s_rssi_next);

        Msg m;
        if (xQueueReceive(s_queue, &m, wait) == pdTRUE) {
            switch (m.cmd) {
            case Cmd::EVAL:
                evaluate();
                break;
            case Cmd::STA_START:
                if (s_radio && s_idx < 0) try_next();
                break;
            case Cmd::DISCONNECTED:
                on_disconnected(m);
                break;
            case Cmd::GOT_IP:
                on_got_ip(m);
                break;
            }
        }

        if (s_attempt_armed && expired(s_attempt_end)) {
            s_attempt_armed = false;
            s_last_reason = "no answer";
            ESP_UTILS_LOGW("'%s': no IP after %u s", s_nets[s_idx].ssid,
                           (unsigned)(Settings::WIFI_ATTEMPT_TIMEOUT_MS / 1000));
            esp_wifi_disconnect();                    // its disconnect event names this SSID: ignored
            try_next();
        }
        if (s_idle_armed && expired(s_idle_end)) {
            s_idle_armed = false;
            if (s_users == 0 && !keep_on() && s_radio) {
                ESP_UTILS_LOGI("No app needs Wi-Fi any more");
                radio_stop();
                s_status = WifiStatus::IDLE;
            }
        }
        if (s_radio && s_status == WifiStatus::CONNECTED && expired(s_rssi_next)) {
            wifi_ap_record_t ap = {};
            if (esp_wifi_sta_get_ap_info(&ap) == ESP_OK) s_rssi = ap.rssi;
            s_rssi_next = xTaskGetTickCount() + pdMS_TO_TICKS(RSSI_PERIOD_MS);
        }
    }
}

// lwIP, default event loop, handlers and the Wi-Fi task: once, on first need.
void infra_init()
{
    s_queue = xQueueCreate(QUEUE_LEN, sizeof(Msg));
    s_bits  = xEventGroupCreate();
    if (s_queue == nullptr || s_bits == nullptr) {
        ESP_UTILS_LOGE("Wi-Fi: out of memory");
        return;
    }
    esp_err_t e = esp_netif_init();
    if (e == ESP_OK) {
        e = esp_event_loop_create_default();
        if (e == ESP_ERR_INVALID_STATE) e = ESP_OK;  // already created by someone else
    }
    if (e == ESP_OK) e = esp_event_handler_instance_register(WIFI_EVENT, ESP_EVENT_ANY_ID, &on_event, nullptr, nullptr);
    if (e == ESP_OK) e = esp_event_handler_instance_register(IP_EVENT, IP_EVENT_STA_GOT_IP, &on_event, nullptr, nullptr);
    if (e != ESP_OK) {
        ESP_UTILS_LOGE("Wi-Fi: network stack init failed (%s)", esp_err_to_name(e));
        return;
    }
    if (xTaskCreatePinnedToCore(&wifi_task, "wifi_mgr", TASK_STACK, nullptr, TASK_PRIO, nullptr, TASK_CORE) != pdPASS) {
        ESP_UTILS_LOGE("Wi-Fi: task create failed");
        return;
    }
    s_infra_ok = true;
}

bool infra()
{
    std::call_once(s_infra_once, infra_init);
    if (!s_infra_ok) set_failed("Wi-Fi could not start (out of memory).");
    return s_infra_ok;
}

// A new pass over the list unless one is running or connected.
void kick()
{
    const WifiStatus st = s_status.load();
    if (st != WifiStatus::CONNECTED && st != WifiStatus::CONNECTING) {
        s_retry = true;
        xEventGroupClearBits(s_bits, BIT_FAILED);
        s_status = WifiStatus::CONNECTING;
    }
    post_eval();
}

} // namespace

// ---------------------------------------------------------------------------
// settings.wifi.ssid / settings.wifi.pass
// ---------------------------------------------------------------------------

void Settings::wifiListFromJson(cJSON *wifi)
{
    s_net_count = 0;
    s_list_error[0] = '\0';
    cJSON_Delete(s_raw_ssid);
    cJSON_Delete(s_raw_pass);
    s_raw_ssid = nullptr;
    s_raw_pass = nullptr;

    cJSON *ssid = cJSON_GetObjectItem(wifi, "ssid");
    cJSON *pass = cJSON_GetObjectItem(wifi, "pass");
    if (ssid == nullptr && pass == nullptr) return;   // nothing saved yet

    const int ns = cJSON_IsArray(ssid) ? cJSON_GetArraySize(ssid) : -1;
    const int np = cJSON_IsArray(pass) ? cJSON_GetArraySize(pass) : -1;
    char *err = s_list_error;
    const size_t err_len = sizeof(s_list_error);
    if (ns < 0 || np < 0) {
        snprintf(err, err_len, "settings.wifi.ssid and settings.wifi.pass must both be arrays.");
    } else if (ns != np) {
        snprintf(err, err_len, "settings.wifi.ssid has %d entries but settings.wifi.pass has %d: "
                 "they must have the same length (false for an open network).", ns, np);
    } else if (ns > WIFI_MAX_NETWORKS) {
        snprintf(err, err_len, "settings.wifi.ssid has %d entries, the maximum is %d.", ns, WIFI_MAX_NETWORKS);
    } else {
        for (int i = 0; i < ns && err[0] == '\0'; i++) {
            cJSON *s = cJSON_GetArrayItem(ssid, i);
            cJSON *p = cJSON_GetArrayItem(pass, i);
            WifiNetwork &n = s_nets[i];
            n = {};
            const size_t sl = cJSON_IsString(s) ? strlen(s->valuestring) : 0;
            if (sl == 0 || sl >= sizeof(n.ssid)) {
                snprintf(err, err_len, "settings.wifi.ssid[%d] must be a name of 1 to 32 characters.", i);
            } else if (cJSON_IsFalse(p)) {
                memcpy(n.ssid, s->valuestring, sl);
            } else if (cJSON_IsString(p) && strlen(p->valuestring) < sizeof(n.pass)) {
                memcpy(n.ssid, s->valuestring, sl);
                strcpy(n.pass, p->valuestring);
                n.has_pass = (n.pass[0] != '\0');
            } else {
                snprintf(err, err_len, "settings.wifi.pass[%d] must be a password of up to 64 characters, "
                         "or false for an open network.", i);
            }
        }
    }

    if (err[0] != '\0') {
        ESP_UTILS_LOGE("Wi-Fi networks ignored: %s", err);   // status FAILED with this text: applyWifi()
        if (ssid != nullptr) s_raw_ssid = cJSON_Duplicate(ssid, true);
        if (pass != nullptr) s_raw_pass = cJSON_Duplicate(pass, true);
        return;
    }
    s_net_count = ns;
}

void Settings::wifiListToJson(cJSON *wifi)
{
    if (wifi == nullptr) return;
    if (s_raw_ssid != nullptr || s_raw_pass != nullptr) {
        if (s_raw_ssid != nullptr) cJSON_AddItemToObject(wifi, "ssid", cJSON_Duplicate(s_raw_ssid, true));
        if (s_raw_pass != nullptr) cJSON_AddItemToObject(wifi, "pass", cJSON_Duplicate(s_raw_pass, true));
        return;
    }
    // Written even when empty, so the keys show up in the file.
    cJSON *ssid = cJSON_AddArrayToObject(wifi, "ssid");
    cJSON *pass = cJSON_AddArrayToObject(wifi, "pass");
    for (int i = 0; i < s_net_count; i++) {
        cJSON_AddItemToArray(ssid, cJSON_CreateString(s_nets[i].ssid));
        cJSON_AddItemToArray(pass, s_nets[i].has_pass ? cJSON_CreateString(s_nets[i].pass) : cJSON_CreateFalse());
    }
}

// ---------------------------------------------------------------------------
// Public API
// ---------------------------------------------------------------------------

void Settings::applyWifi()
{
    if (!cfg.wifi.enabled) {
        if (s_infra_ok) post_eval();                  // the task stops the radio
        else            s_status = WifiStatus::DISABLED;
        return;
    }
    if (!list_usable()) {
        if (s_infra_ok) {
            post_eval();
        } else if (s_list_error[0] != '\0') {
            set_failed("%s", s_list_error);
        } else {
            set_failed("No Wi-Fi network saved: add one to settings.wifi.ssid / settings.wifi.pass.");
        }
        return;
    }
    if (keep_on() || s_users > 0) {
        if (infra()) kick();                          // connect now
        return;
    }
    // Standby: the task (and its RAM) waits for the first requestWifi().
    if (s_infra_ok) post_eval();
    else            s_status = WifiStatus::IDLE;
}

bool Settings::requestWifi()
{
    if (!cfg.wifi.enabled) {
        s_status = WifiStatus::DISABLED;
        return false;
    }
    if (!list_usable()) {
        applyWifi();                                  // sets FAILED with the reason
        return false;
    }
    if (!infra()) return false;
    s_users++;
    kick();
    return true;
}

void Settings::releaseWifi()
{
    int n = s_users.load();
    do {
        if (n <= 0) {
            ESP_UTILS_LOGW("releaseWifi() without a matching requestWifi()");
            return;
        }
    } while (!s_users.compare_exchange_weak(n, n - 1));
    if (n == 1) post_eval();
}

bool Settings::waitWifi(uint32_t timeout_ms)
{
    if (!s_infra_ok) return false;
    if (timeout_ms == 0) {
        timeout_ms = (uint32_t)std::max(s_net_count, 1) * WIFI_ATTEMPT_TIMEOUT_MS + 10000;   // whole list + start
    }
    const EventBits_t b = xEventGroupWaitBits(s_bits, BIT_CONNECTED | BIT_FAILED, pdFALSE, pdFALSE,
                                              pdMS_TO_TICKS(timeout_ms));
    return (b & BIT_CONNECTED) != 0;
}

Settings::WifiStatus Settings::wifiStatus()
{
    return s_status.load();
}

const char *Settings::wifiStatusText()
{
    switch (s_status.load()) {
    case WifiStatus::DISABLED:
        return "Wi-Fi is off.\nEnable it in Settings > WiFi.";
    case WifiStatus::IDLE:
        return "Wi-Fi is on standby: it connects when an app needs it.";
    case WifiStatus::CONNECTING:
        return "Connecting to Wi-Fi...";
    case WifiStatus::CONNECTED:
        return "Wi-Fi connected.";
    case WifiStatus::FAILED:
    default:
        return (s_fail_text[0] != '\0') ? s_fail_text : "Wi-Fi connection failed.";
    }
}

int Settings::wifiRssi()
{
    return (s_status.load() == WifiStatus::CONNECTED) ? s_rssi.load() : 0;
}

bool Settings::wifiIpText(char *out, size_t len)
{
    if (out == nullptr || len == 0) return false;
    const uint32_t a = (s_status.load() == WifiStatus::CONNECTED) ? s_ip.load() : 0;
    if (a == 0) {
        out[0] = '\0';
        return false;
    }
    esp_ip4_addr_t ip = {};
    ip.addr = a;
    snprintf(out, len, IPSTR, IP2STR(&ip));
    return true;
}

bool Settings::wifiInUse()
{
    return s_users.load() > 0;
}

int Settings::wifiNetworkCount()
{
    return s_net_count;
}

bool Settings::isWifiConnected()
{
    return s_status.load() == WifiStatus::CONNECTED;
}

} // namespace esp_brookesia::apps
