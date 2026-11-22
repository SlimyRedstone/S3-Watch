/*
 * Settings > Apps: which registered apps main.cpp creates at boot.
 *
 * settings.apps.unloaded is the list of app names NOT loaded:
 *     "apps": { "unloaded": ["Sonometer", "RemoteNow"] }
 * Missing = every app is loaded. An unloaded app is never created nor
 * installed: no launcher icon, its init() does not run (BLE Mouse unloaded =
 * no Bluetooth handler, the Bluetooth switch then stays off). Changes apply at
 * the next boot.
 *
 * App tags (SETTINGS_APP_TAGS() in the app's .cpp, static init):
 *   APP_TAG_HIDDEN         not in get_applications() (the page)
 *   APP_TAG_ALWAYS_LOADED  set_application(name, false) refused, JSON entry ignored
 *   APP_TAG_SYSTEM         both: Clock (sleep, wake-up, auto-start), Settings (this list)
 *
 * The page (ui/screens.c) is a create_list() of get_applications(), one
 * checkbox per app: checked = loaded on boot. A row click toggles the box and
 * calls action_application_clicked(); syncAppList() sets the boxes from cfg
 * after each menu build.
 */
#include "Settings.hpp"

#include <cstring>
#include <string>
#include <utility>
#include <vector>

#include "cJSON.h"

#ifdef ESP_UTILS_LOG_TAG
#   undef ESP_UTILS_LOG_TAG
#endif
#define ESP_UTILS_LOG_TAG "Settings"
#include "esp_lib_utils.h"

using esp_brookesia::apps::Settings;
using AppRegistry = esp_brookesia::systems::base::App::Registry;

namespace {

// name -> tags. Function-local static: filled by static constructors of other
// translation units, whose order is not defined.
std::vector<std::pair<std::string, uint32_t>> &tag_table()
{
    static std::vector<std::pair<std::string, uint32_t>> table;
    return table;
}

// Registered names (no app created). Built once: the registry is filled by
// static constructors before app_main.
const std::vector<std::string> &registered_names()
{
    static const std::vector<std::string> names = AppRegistry::listRegisteredNames();
    return names;
}

bool is_registered(const char *name)
{
    for (const auto &n : registered_names()) {
        if (n == name) return true;
    }
    return false;
}

// Registered names minus APP_TAG_HIDDEN, NULL-terminated (get_applications()).
const char **listed_names()
{
    static std::vector<const char *> ptrs;
    if (ptrs.empty()) {
        for (const auto &n : registered_names()) {
            if ((Settings::appTags(n.c_str()) & Settings::APP_TAG_HIDDEN) == 0) ptrs.push_back(n.c_str());
        }
        ptrs.push_back(nullptr);
    }
    return ptrs.data();
}

int unloaded_index(const char *name)
{
    const auto &a = Settings::cfg.apps;
    for (int i = 0; i < a.unloaded_count; i++) {
        if (strcmp(a.unloaded[i], name) == 0) return i;
    }
    return -1;
}

void set_checked(lv_obj_t *obj, bool on)
{
    if (on) lv_obj_add_state(obj, LV_STATE_CHECKED);
    else    lv_obj_remove_state(obj, LV_STATE_CHECKED);
}

} // namespace

// ---------------------------------------------------------------------------
// C++ API
// ---------------------------------------------------------------------------

void Settings::tagApp(const char *name, uint32_t tags)
{
    if (name == nullptr) return;
    for (auto &entry : tag_table()) {
        if (entry.first == name) { entry.second |= tags; return; }
    }
    tag_table().emplace_back(name, tags);
}

uint32_t Settings::appTags(const char *name)
{
    if (name == nullptr) return 0;
    for (const auto &entry : tag_table()) {
        if (entry.first == name) return entry.second;
    }
    return 0;
}

bool Settings::isAppAlwaysLoaded(const char *name)
{
    return (appTags(name) & APP_TAG_ALWAYS_LOADED) != 0;
}

bool Settings::isAppLoadedOnBoot(const char *name)
{
    if (name == nullptr) return false;
    return isAppAlwaysLoaded(name) || unloaded_index(name) < 0;
}

bool Settings::setAppLoadedOnBoot(const char *name, bool load)
{
    if (name == nullptr || name[0] == '\0') return false;
    auto &a = cfg.apps;
    const int idx = unloaded_index(name);
    if (load) {
        if (idx >= 0) {                                  // drop it, keep the order of the others
            for (int i = idx; i + 1 < a.unloaded_count; i++) strcpy(a.unloaded[i], a.unloaded[i + 1]);
            a.unloaded_count--;
            a.unloaded[a.unloaded_count][0] = '\0';
        }
        return true;
    }
    if (isAppAlwaysLoaded(name)) {
        ESP_UTILS_LOGW("App '%s' is always loaded", name);
        return false;
    }
    if (idx >= 0) return true;
    if (a.unloaded_count >= APPS_MAX) {
        ESP_UTILS_LOGW("App '%s' not unloaded: list full (%d)", name, APPS_MAX);
        return false;
    }
    if (strlen(name) >= (size_t)APP_NAME_MAX) {
        ESP_UTILS_LOGW("App '%s' not unloaded: name longer than %d bytes", name, APP_NAME_MAX - 1);
        return false;
    }
    strcpy(a.unloaded[a.unloaded_count++], name);
    return true;
}

// ---------------------------------------------------------------------------
// JSON: settings.apps.unloaded
// ---------------------------------------------------------------------------

void Settings::appsFromJson(cJSON *apps)
{
    cfg.apps = AppConfig::Apps{};
    cJSON *list = cJSON_GetObjectItem(apps, "unloaded");
    if (list == nullptr) return;
    if (!cJSON_IsArray(list)) {
        ESP_UTILS_LOGW("settings.apps.unloaded is not an array: ignored");
        return;
    }
    cJSON *item = nullptr;
    cJSON_ArrayForEach(item, list) {
        if (!cJSON_IsString(item) || item->valuestring[0] == '\0') {
            ESP_UTILS_LOGW("settings.apps.unloaded: entry not a name, ignored");
            continue;
        }
        if (isAppAlwaysLoaded(item->valuestring)) {
            ESP_UTILS_LOGW("settings.apps.unloaded: '%s' is always loaded, ignored", item->valuestring);
            continue;
        }
        if (!setAppLoadedOnBoot(item->valuestring, false)) continue;   // logged
        if (!is_registered(item->valuestring)) {
            ESP_UTILS_LOGW("settings.apps.unloaded: no app '%s' in this firmware (kept)", item->valuestring);
        }
    }
    if (cfg.apps.unloaded_count > 0) ESP_UTILS_LOGI("%d app(s) not loaded on boot", cfg.apps.unloaded_count);
}

void Settings::appsToJson(cJSON *apps)
{
    if (apps == nullptr) return;
    cJSON *list = cJSON_AddArrayToObject(apps, "unloaded");
    for (int i = 0; i < cfg.apps.unloaded_count; i++) {
        cJSON_AddItemToArray(list, cJSON_CreateString(cfg.apps.unloaded[i]));
    }
}

// ---------------------------------------------------------------------------
// Apps page: checkbox states after each menu build. A create_list() row is a
// list button { checkbox, label }. Rows of an always-loaded app are disabled.
// ---------------------------------------------------------------------------

void Settings::syncAppList(lv_obj_t *root)
{
    if (root == nullptr) return;
    if (lv_obj_check_type(root, &lv_list_button_class)) {
        lv_obj_t *label = lv_obj_get_child_by_type(root, 0, &lv_label_class);
        lv_obj_t *box   = lv_obj_get_child_by_type(root, 0, &lv_checkbox_class);
        if (label == nullptr || box == nullptr) return;
        const char *name = lv_label_get_text(label);
        if (name == nullptr || !is_registered(name)) return;
        set_checked(box, isAppLoadedOnBoot(name));
        if (isAppAlwaysLoaded(name)) {
            lv_obj_add_state(root, LV_STATE_DISABLED);
            lv_obj_add_state(box, LV_STATE_DISABLED);
        }
        return;
    }
    const uint32_t n = lv_obj_get_child_count(root);
    for (uint32_t i = 0; i < n; i++) syncAppList(lv_obj_get_child(root, (int32_t)i));
}

// ---------------------------------------------------------------------------
// C bridge (ui/screens.c)
// ---------------------------------------------------------------------------
extern "C" {
    #include "settings_ui_bridge.h"

    const char **get_applications(void) {
        return listed_names();
    }

    void set_application(const char *name, bool load_on_boot) {
        if (Settings::setAppLoadedOnBoot(name, load_on_boot)) {
            ESP_UTILS_LOGI("App '%s' %s on boot (next boot)", name, load_on_boot ? "loaded" : "not loaded");
        }
    }

    bool get_application_status(const char *name) {
        return Settings::isAppLoadedOnBoot(name);
    }

    // create_list() already toggled the row's checkbox: apply it, show the
    // state actually kept (a refusal puts the box back), save.
    void action_application_clicked(lv_obj_t *item) {
        if (item == nullptr) return;
        lv_obj_t *label = lv_obj_get_child_by_type(item, 0, &lv_label_class);
        lv_obj_t *box   = lv_obj_get_child_by_type(item, 0, &lv_checkbox_class);
        if (label == nullptr || box == nullptr) return;
        const char *name = lv_label_get_text(label);
        if (name == nullptr) return;
        set_application(name, lv_obj_has_state(box, LV_STATE_CHECKED));
        set_checked(box, get_application_status(name));
        Settings::saveLater();
    }
}
