/*
 * Settings > Upgrade: firmware update check and download.
 *
 * "Check for updates" (BUTTON_CHECK_UPDATES) -> Settings::checkForUpdates():
 *   1. Wi-Fi: requestWifi() (Settings connects to the saved networks, see
 *      SettingsWifi.cpp), then wait for the connection. Wi-Fi disabled or no
 *      usable network saved: the user is told why, nothing else happens.
 *   2. GET cfg.upgrade.src_url, expected answer:
 *        {"last":{"t":"v1.2.0","n":[1,2,0]}, "url":"https://.../v1.2.0.bin"}
 *   3. Installed version (Settings::getVersion()) >= latest: "up to date".
 *   4. Older: the firmware is downloaded to the SD card, /sdcard/.temp/<name>.
 *      No SD card: the user is told it is mandatory, nothing is downloaded.
 * Installing the downloaded image is not implemented yet. The partition table
 * (partitions.csv) already has otadata + ota_0 + ota_1 for it.
 *
 * Network and SD work runs on a short-lived pthread (stack in PSRAM: it never
 * touches the SPI flash), so the LVGL task stays responsive. Message boxes are
 * created under the LVGL lock from that thread.
 */
#include "Settings.hpp"

#include <atomic>
#include <cerrno>
#include <cstdio>
#include <cstring>
#include <pthread.h>
#include <sys/stat.h>

#include "bsp/esp-bsp.h"
#include "cJSON.h"
#include "esp_crt_bundle.h"
#include "esp_heap_caps.h"
#include "esp_http_client.h"
#include "esp_pthread.h"
#include "esp_timer.h"

#ifdef ESP_UTILS_LOG_TAG
#   undef ESP_UTILS_LOG_TAG
#endif
#define ESP_UTILS_LOG_TAG "Upgrade"
#include "esp_lib_utils.h"

namespace esp_brookesia::apps {

namespace {

constexpr int      HTTP_TIMEOUT_MS   = 15000;
constexpr int      MAX_REDIRECTS     = 5;
constexpr int      VERSION_JSON_MAX  = 2048;          // the version document is a few hundred bytes
constexpr int      DOWNLOAD_CHUNK    = 4096;
constexpr uint32_t WORKER_STACK      = 12 * 1024;     // TLS handshake + cJSON + stdio
constexpr int64_t  PROGRESS_EVERY_US = 500 * 1000;    // message box refresh while downloading
constexpr const char *TEMP_DIR       = BSP_SD_MOUNT_POINT "/.temp";

// One check/download at a time. True for the whole worker (Wi-Fi wait, check,
// download, and the install step once it exists): ClockApp does not sleep
// meanwhile (Settings::isFirmwareBusy()).
std::atomic<bool> s_busy{false};

// ---------------------------------------------------------------------------
// Message box (one at a time). All access under the LVGL lock.
// ---------------------------------------------------------------------------
lv_obj_t *s_box  = nullptr;                           // nullptr once the user closed it
lv_obj_t *s_text = nullptr;                           // its text label

void box_deleted_cb(lv_event_t *e)
{
    if (lv_event_get_target_obj(e) == s_box) {
        s_box  = nullptr;
        s_text = nullptr;
    }
}

// Replaces the current message box. Callable from any task (takes the lock;
// the lvgl_port mutex is recursive, so also fine from the LVGL task).
void ui_message(const char *title, const char *text)
{
    ESP_UTILS_LOGI("%s: %s", title, text);
    if (!bsp_display_lock(2000)) {
        ESP_UTILS_LOGW("LVGL lock timeout, message not shown");
        return;
    }
    if (s_box != nullptr) {
        lv_msgbox_close(s_box);                       // LV_EVENT_DELETE clears s_box / s_text
    }
    lv_obj_t *box = lv_msgbox_create(nullptr);        // modal, on the top layer
    if (box != nullptr) {
        lv_obj_set_width(box, lv_pct(86));
        lv_msgbox_add_title(box, title);
        s_text = lv_msgbox_add_text(box, text);
        lv_msgbox_add_close_button(box);
        lv_obj_add_event_cb(box, box_deleted_cb, LV_EVENT_DELETE, nullptr);
        s_box = box;
    }
    bsp_display_unlock();
}

// Updates the text of the current box, if the user has not closed it.
void ui_text(const char *text)
{
    if (!bsp_display_lock(500)) return;
    if (s_text != nullptr) lv_label_set_text(s_text, text);
    bsp_display_unlock();
}

// ---------------------------------------------------------------------------
// Versions
// ---------------------------------------------------------------------------
int compare_versions(const Settings::FirmwareVersion &a, const Settings::FirmwareVersion &b)
{
    if (a.major_num != b.major_num) return (a.major_num < b.major_num) ? -1 : 1;
    if (a.minor_num != b.minor_num) return (a.minor_num < b.minor_num) ? -1 : 1;
    if (a.patch_num != b.patch_num) return (a.patch_num < b.patch_num) ? -1 : 1;
    return 0;
}

// "v1.2.0" / "1.2.0" -> {1, 2, 0}
bool parse_tag(const char *tag, Settings::FirmwareVersion &out)
{
    if (tag == nullptr) return false;
    if (*tag == 'v' || *tag == 'V') tag++;
    unsigned a = 0, b = 0, c = 0;
    if (sscanf(tag, "%u.%u.%u", &a, &b, &c) != 3 || a > 0xFFFF || b > 0xFFFF || c > 0xFFFF) return false;
    out = {(uint16_t)a, (uint16_t)b, (uint16_t)c};
    return true;
}

// ---------------------------------------------------------------------------
// HTTP(S)
// ---------------------------------------------------------------------------
esp_http_client_handle_t http_new(const char *url)
{
    esp_http_client_config_t c = {};
    c.url               = url;
    c.timeout_ms        = HTTP_TIMEOUT_MS;
    c.crt_bundle_attach = esp_crt_bundle_attach;      // HTTPS: Mozilla CA bundle (CONFIG_MBEDTLS_CERTIFICATE_BUNDLE)
    c.user_agent        = "S3-Watch";
    c.buffer_size       = 2048;
    c.buffer_size_tx    = 1024;
    return esp_http_client_init(&c);
}

// Sends the GET and reads the headers, following redirects (open/read mode
// does not follow them by itself). Returns the HTTP status, or -1 with err set.
int http_open(esp_http_client_handle_t client, int64_t *length, char *err, size_t err_len)
{
    for (int hop = 0; hop <= MAX_REDIRECTS; hop++) {
        esp_err_t e = esp_http_client_open(client, 0);
        if (e != ESP_OK) {
            snprintf(err, err_len, "Cannot reach the server (%s).", esp_err_to_name(e));
            return -1;
        }
        const int64_t len    = esp_http_client_fetch_headers(client);
        const int     status = esp_http_client_get_status_code(client);
        if (status == 301 || status == 302 || status == 303 || status == 307 || status == 308) {
            esp_http_client_set_redirection(client);
            esp_http_client_close(client);
            continue;
        }
        if (len < 0) {
            snprintf(err, err_len, "No answer from the server (timeout).");
            return -1;
        }
        *length = len;                                // 0 = unknown (chunked)
        return status;
    }
    snprintf(err, err_len, "Too many redirections.");
    return -1;
}

void http_free(esp_http_client_handle_t client)
{
    esp_http_client_close(client);
    esp_http_client_cleanup(client);
}

// Last path segment of the URL, safe for FAT: [A-Za-z0-9._-], else "firmware.bin".
void file_name_from_url(const char *url, char *out, size_t out_len)
{
    const char *end = url + strcspn(url, "?#");
    const char *start = end;
    while (start > url && start[-1] != '/') start--;
    size_t n = 0;
    for (const char *p = start; p < end && n + 1 < out_len; p++) {
        const char ch = *p;
        const bool ok = (ch >= 'a' && ch <= 'z') || (ch >= 'A' && ch <= 'Z') || (ch >= '0' && ch <= '9') ||
                        ch == '.' || ch == '_' || ch == '-';
        if (ok) out[n++] = ch;
    }
    out[n] = '\0';
    if (n == 0 || strcmp(out, ".") == 0 || strcmp(out, "..") == 0) snprintf(out, out_len, "firmware.bin");
}

// Downloads info.url to TEMP_DIR/<name> through a ".part" file, with progress.
bool download_firmware(const Settings::UpdateInfo &info, char *path, size_t path_len, char *err, size_t err_len)
{
    if (mkdir(TEMP_DIR, 0775) != 0 && errno != EEXIST) {
        snprintf(err, err_len, "Cannot create %s on the SD card (errno %d).", TEMP_DIR, errno);
        return false;
    }
    char name[64];
    file_name_from_url(info.url, name, sizeof(name));
    snprintf(path, path_len, "%s/%s", TEMP_DIR, name);
    char part[200];
    snprintf(part, sizeof(part), "%s.part", path);

    esp_http_client_handle_t client = http_new(info.url);
    if (client == nullptr) {
        snprintf(err, err_len, "HTTP client init failed (memory).");
        return false;
    }
    int64_t length = 0;
    const int status = http_open(client, &length, err, err_len);
    if (status != 200) {
        if (status > 0) snprintf(err, err_len, "Firmware download refused: HTTP %d.", status);
        http_free(client);
        return false;
    }

    FILE *f = fopen(part, "wb");
    if (f == nullptr) {
        snprintf(err, err_len, "Cannot write .temp/%s on the SD card (removed?).", name);
        http_free(client);
        return false;
    }
    // Internal DMA-capable buffer when available: the SD driver then writes it
    // straight away instead of bouncing every sector through a DMA copy.
    char *buf = (char *)heap_caps_malloc(DOWNLOAD_CHUNK, MALLOC_CAP_DMA | MALLOC_CAP_INTERNAL);
    if (buf == nullptr) buf = (char *)heap_caps_malloc(DOWNLOAD_CHUNK, MALLOC_CAP_8BIT);
    if (buf == nullptr) {
        snprintf(err, err_len, "Out of memory.");
        fclose(f);
        remove(part);
        http_free(client);
        return false;
    }

    bool ok = true;
    int64_t total = 0;
    int64_t last_ui = 0;
    char text[160];                                   // tag (31) + 3 numbers, worst case ~110
    for (;;) {
        const int r = esp_http_client_read(client, buf, DOWNLOAD_CHUNK);
        if (r < 0) {
            snprintf(err, err_len, "Connection lost after %lld KB.", (long long)(total / 1024));
            ok = false;
            break;
        }
        if (r == 0) break;                            // end of the body
        if (fwrite(buf, 1, (size_t)r, f) != (size_t)r) {
            snprintf(err, err_len, "SD card write failed (card full?).");
            ok = false;
            break;
        }
        total += r;
        const int64_t now = esp_timer_get_time();
        if (now - last_ui >= PROGRESS_EVERY_US) {
            last_ui = now;
            if (length > 0) {
                snprintf(text, sizeof(text), "Downloading %s\n%d %%  (%lld / %lld KB)", info.tag,
                         (int)(total * 100 / length), (long long)(total / 1024), (long long)(length / 1024));
            } else {
                snprintf(text, sizeof(text), "Downloading %s\n%lld KB", info.tag, (long long)(total / 1024));
            }
            ui_text(text);
        }
    }
    if (ok && ((length > 0 && total != length) || !esp_http_client_is_complete_data_received(client))) {
        snprintf(err, err_len, "Incomplete download (%lld of %lld bytes).", (long long)total, (long long)length);
        ok = false;
    }
    if (fclose(f) != 0 && ok) {
        snprintf(err, err_len, "SD card write failed while closing the file.");
        ok = false;
    }
    heap_caps_free(buf);
    http_free(client);

    if (!ok) {
        remove(part);
        return false;
    }
    remove(path);                                     // an older download of the same name
    if (rename(part, path) != 0) {
        snprintf(err, err_len, "Cannot rename .temp/%s.part (errno %d).", name, errno);
        remove(part);
        return false;
    }
    ESP_UTILS_LOGI("Firmware %s saved to %s (%lld bytes)", info.tag, path, (long long)total);
    return true;
}

// ---------------------------------------------------------------------------
// Worker
// ---------------------------------------------------------------------------
void run_update_check(void)
{
    char err[128] = {0};
    char text[320];
    Settings::UpdateInfo info = {};

    if (!Settings::isWifiConnected()) {
        ui_message("Firmware update", "Connecting to Wi-Fi...");
        if (!Settings::waitWifi()) {
            ui_message("Wi-Fi not connected", Settings::wifiStatusText());
            return;
        }
    }
    ui_message("Firmware update", "Checking for updates...");
    if (!Settings::fetchLatestVersion(info, err, sizeof(err))) {
        ui_message("Update check failed", err);
        return;
    }

    if (compare_versions(Settings::getVersion(), info.version) >= 0) {
        snprintf(text, sizeof(text), "Firmware %s is up to date.\nLatest release: %s",
                 Settings::getVersionText(), info.tag);
        ui_message("Up to date", text);
        return;
    }

    if (!Settings::isSdMounted()) {
        snprintf(text, sizeof(text),
                 "Firmware %s is available (installed: %s).\n\n"
                 "An SD card is mandatory to download it: insert one, restart the watch and check again.",
                 info.tag, Settings::getVersionText());
        ui_message("SD card required", text);
        return;
    }

    snprintf(text, sizeof(text), "Downloading %s...", info.tag);
    ui_message("Firmware update", text);
    char path[160];
    if (!download_firmware(info, path, sizeof(path), err, sizeof(err))) {
        ui_message("Download failed", err);
        return;
    }
    snprintf(text, sizeof(text), "Firmware %s downloaded to\n%s\n\nInstallation is not available yet.",
             info.tag, path);
    ui_message("Update downloaded", text);
}

void *update_worker(void *)
{
    run_update_check();
    Settings::releaseWifi();                          // the request made by checkForUpdates()
    s_busy = false;
    return nullptr;
}

} // namespace

// ---------------------------------------------------------------------------
// Public API
// ---------------------------------------------------------------------------

bool Settings::isFirmwareBusy()
{
    return s_busy.load();
}

const char *Settings::getVersionText()
{
    static char text[32] = {0};                       // "v65535.65535.65535" fits
    if (text[0] == '\0') {
        snprintf(text, sizeof(text), "v%u.%u.%u", (unsigned)FW_VERSION.major_num, (unsigned)FW_VERSION.minor_num,
                 (unsigned)FW_VERSION.patch_num);
    }
    return text;
}

bool Settings::fetchLatestVersion(UpdateInfo &out, char *err, size_t err_len)
{
    out = {};
    const char *url = cfg.upgrade.src_url;
    if (url[0] == '\0') {
        snprintf(err, err_len, "No update URL set (settings.upgrade.src_url).");
        return false;
    }
    esp_http_client_handle_t client = http_new(url);
    if (client == nullptr) {
        snprintf(err, err_len, "HTTP client init failed (memory).");
        return false;
    }
    char *body = (char *)heap_caps_calloc(1, VERSION_JSON_MAX + 1, MALLOC_CAP_8BIT);
    if (body == nullptr) {
        snprintf(err, err_len, "Out of memory.");
        http_free(client);
        return false;
    }

    bool ok = false;
    int64_t length = 0;
    const int status = http_open(client, &length, err, err_len);
    if (status == 200) {
        int total = 0;
        bool read_ok = true;
        while (total < VERSION_JSON_MAX) {
            const int r = esp_http_client_read(client, body + total, VERSION_JSON_MAX - total);
            if (r < 0) { read_ok = false; break; }
            if (r == 0) break;
            total += r;
        }
        if (!read_ok || total == 0) {
            snprintf(err, err_len, "Empty or broken answer from the update server.");
        } else {
            cJSON *j = cJSON_Parse(body);
            cJSON *last = cJSON_GetObjectItem(j, "last");
            cJSON *n    = cJSON_GetObjectItem(last, "n");
            cJSON *t    = cJSON_GetObjectItem(last, "t");
            cJSON *fw   = cJSON_GetObjectItem(j, "url");

            bool have_version = false;
            if (cJSON_IsArray(n) && cJSON_GetArraySize(n) >= 3) {
                cJSON *a = cJSON_GetArrayItem(n, 0), *b = cJSON_GetArrayItem(n, 1), *c = cJSON_GetArrayItem(n, 2);
                if (cJSON_IsNumber(a) && cJSON_IsNumber(b) && cJSON_IsNumber(c) &&
                    a->valueint >= 0 && b->valueint >= 0 && c->valueint >= 0 &&
                    a->valueint <= 0xFFFF && b->valueint <= 0xFFFF && c->valueint <= 0xFFFF) {
                    out.version = {(uint16_t)a->valueint, (uint16_t)b->valueint, (uint16_t)c->valueint};
                    have_version = true;
                }
            }
            if (!have_version && cJSON_IsString(t)) have_version = parse_tag(t->valuestring, out.version);

            if (j == nullptr) {
                snprintf(err, err_len, "The update server did not answer JSON.");
            } else if (!have_version) {
                snprintf(err, err_len, "No version number in the server answer.");
            } else if (!cJSON_IsString(fw) || fw->valuestring[0] == '\0') {
                snprintf(err, err_len, "No firmware URL in the server answer.");
            } else if (strlen(fw->valuestring) >= sizeof(out.url)) {
                snprintf(err, err_len, "Firmware URL too long.");
            } else {
                strcpy(out.url, fw->valuestring);
                if (cJSON_IsString(t) && strlen(t->valuestring) < sizeof(out.tag)) {
                    strcpy(out.tag, t->valuestring);
                } else {
                    snprintf(out.tag, sizeof(out.tag), "v%u.%u.%u", (unsigned)out.version.major_num,
                             (unsigned)out.version.minor_num, (unsigned)out.version.patch_num);
                }
                ok = true;
            }
            cJSON_Delete(j);
        }
    } else if (status > 0) {
        snprintf(err, err_len, "Update server answered HTTP %d.", status);
    }

    heap_caps_free(body);
    http_free(client);
    if (ok) ESP_UTILS_LOGI("Latest firmware %s at %s", out.tag, out.url);
    return ok;
}

void Settings::checkForUpdates()
{
    if (s_busy.exchange(true)) {
        ESP_UTILS_LOGW("Update check already running");
        return;
    }
    // Settings connects (or is already connected); the worker waits for it.
    if (!requestWifi()) {
        s_busy = false;
        ui_message("Wi-Fi required", wifiStatusText());   // off, no network saved, or list invalid
        return;
    }

    esp_pthread_cfg_t pc = esp_pthread_get_default_config();
    pc.stack_size       = WORKER_STACK;
    pc.prio             = 4;
    pc.pin_to_core      = 0;                          // next to lwIP / the radio
    pc.thread_name      = "fw_update";
    pc.inherit_cfg      = false;
    pc.stack_alloc_caps = MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT;   // no flash access from this thread
    esp_pthread_set_cfg(&pc);
    pthread_t thread;
    const int rc = pthread_create(&thread, nullptr, update_worker, nullptr);
    esp_pthread_cfg_t def = esp_pthread_get_default_config();
    esp_pthread_set_cfg(&def);                        // the LVGL task's later pthreads get defaults again

    if (rc != 0) {
        releaseWifi();
        s_busy = false;
        ui_message("Update check failed", "Could not start the update task (memory).");
        return;
    }
    pthread_detach(thread);
}

} // namespace esp_brookesia::apps
