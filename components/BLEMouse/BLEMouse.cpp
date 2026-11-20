#include "BLEMouse.hpp"
#include "Settings.hpp"   // status-bar Bluetooth icon
#include <cstring>
#include "esp_timer.h"
#include "esp_random.h"
#include "nvs_flash.h"

#include "nimble/nimble_port.h"
#include "nimble/nimble_port_freertos.h"
#include "host/ble_hs.h"
#include "host/util/util.h"
#include "host/ble_store.h"
#include "services/gap/ble_svc_gap.h"
#include "services/gatt/ble_svc_gatt.h"

extern "C" {
    void ble_store_config_init(void);
    int  ble_store_util_delete_peer(const ble_addr_t *peer_id_addr);
    LV_IMG_DECLARE(trackpad_icon);
}

#ifdef ESP_UTILS_LOG_TAG
#   undef ESP_UTILS_LOG_TAG
#endif
#define ESP_UTILS_LOG_TAG "BLEMouse"
#include "esp_lib_utils.h"


// Report ID 1 = mouse (4 bytes: buttons, dx, dy, wheel)
// Report ID 2 = consumer control (2 bytes: 16-bit usage code)
static const uint8_t HID_REPORT_MAP[] = {
    // ---- Mouse (Report ID 1) ----
    0x05, 0x01, 0x09, 0x02, 0xA1, 0x01, 0x85, 0x01, 0x09, 0x01,
    0xA1, 0x00, 0x05, 0x09, 0x19, 0x01, 0x29, 0x03, 0x15, 0x00,
    0x25, 0x01, 0x95, 0x03, 0x75, 0x01, 0x81, 0x02, 0x95, 0x01,
    0x75, 0x05, 0x81, 0x03, 0x05, 0x01, 0x09, 0x30, 0x09, 0x31,
    0x09, 0x38, 0x15, 0x81, 0x25, 0x7F, 0x75, 0x08, 0x95, 0x03,
    0x81, 0x06, 0xC0, 0xC0,
    // ---- Consumer Control (Report ID 2) ----
    0x05, 0x0C,                    // Usage Page (Consumer)
    0x09, 0x01,                    // Usage (Consumer Control)
    0xA1, 0x01,                    // Collection (Application)
    0x85, 0x02,                    //   Report ID (2)
    0x15, 0x00,                    //   Logical Min (0)
    0x26, 0xFF, 0x03,              //   Logical Max (0x3FF)
    0x19, 0x00,                    //   Usage Min (0)
    0x2A, 0xFF, 0x03,              //   Usage Max (0x3FF)
    0x75, 0x10,                    //   Report Size (16)
    0x95, 0x01,                    //   Report Count (1)
    0x81, 0x00,                    //   Input (Data,Array,Abs)
    0xC0,                          // End Collection
};
static const uint8_t HID_INFO[] = { 0x11, 0x01, 0x00, 0x02 };
static const uint8_t HID_BOOT_FLAGS = 0;

static uint16_t s_conn_handle  = BLE_HS_CONN_HANDLE_NONE;
static uint16_t s_input_h      = 0;
static uint16_t s_consumer_h   = 0;
static uint16_t s_boot_in_h    = 0;
static uint8_t  s_addr_type    = 0;
static volatile bool s_connected   = false;
static volatile bool s_nimble_up   = false;
static volatile bool s_stopping    = false;   // ble_set_enabled(false) in progress: no UI from gap_event()
static uint8_t  s_proto_mode  = 1;
static uint8_t  s_ctrl_pt     = 0;
static const uint8_t REPORT_ID = 1;
static const uint8_t REPORT_TYPE_INPUT = 1;

static int access_cb(uint16_t, uint16_t, struct ble_gatt_access_ctxt *ctxt, void *arg)
{
    uint16_t uuid = ble_uuid_u16(ctxt->chr->uuid);
    if (ctxt->op == BLE_GATT_ACCESS_OP_READ_CHR) {
        switch (uuid) {
        case 0x2A4A: os_mbuf_append(ctxt->om, HID_INFO, sizeof(HID_INFO)); return 0;
        case 0x2A4B: os_mbuf_append(ctxt->om, HID_REPORT_MAP, sizeof(HID_REPORT_MAP)); return 0;
        case 0x2A4E: os_mbuf_append(ctxt->om, &s_proto_mode, 1); return 0;
        case 0x2A4D: { uint8_t z[4] = {0}; os_mbuf_append(ctxt->om, z, 4); return 0; }
        case 0x2A33: { uint8_t z[3] = {0}; os_mbuf_append(ctxt->om, z, 3); return 0; }
        }
    } else if (ctxt->op == BLE_GATT_ACCESS_OP_WRITE_CHR) {
        if (uuid == 0x2A4E) { uint16_t l; ble_hs_mbuf_to_flat(ctxt->om, &s_proto_mode, 1, &l); return 0; }
        if (uuid == 0x2A4C) { uint16_t l; ble_hs_mbuf_to_flat(ctxt->om, &s_ctrl_pt, 1, &l); return 0; }
    }
    return BLE_ATT_ERR_UNLIKELY;
}

static int report_ref_mouse_cb(uint16_t, uint16_t, struct ble_gatt_access_ctxt *ctxt, void *)
{
    if (ctxt->op == BLE_GATT_ACCESS_OP_READ_DSC) {
        uint8_t v[2] = { 1 /*Report ID*/, REPORT_TYPE_INPUT };
        os_mbuf_append(ctxt->om, v, 2);
        return 0;
    }
    return BLE_ATT_ERR_UNLIKELY;
}

static int report_ref_consumer_cb(uint16_t, uint16_t, struct ble_gatt_access_ctxt *ctxt, void *)
{
    if (ctxt->op == BLE_GATT_ACCESS_OP_READ_DSC) {
        uint8_t v[2] = { 2 /*Report ID*/, REPORT_TYPE_INPUT };
        os_mbuf_append(ctxt->om, v, 2);
        return 0;
    }
    return BLE_ATT_ERR_UNLIKELY;
}

static const ble_uuid16_t UUID_HID         = BLE_UUID16_INIT(0x1812);
static const ble_uuid16_t UUID_HID_INFO    = BLE_UUID16_INIT(0x2A4A);
static const ble_uuid16_t UUID_REPORT_MAP  = BLE_UUID16_INIT(0x2A4B);
static const ble_uuid16_t UUID_REPORT      = BLE_UUID16_INIT(0x2A4D);
static const ble_uuid16_t UUID_BOOT_MOUSE  = BLE_UUID16_INIT(0x2A33);
static const ble_uuid16_t UUID_PROTO_MODE  = BLE_UUID16_INIT(0x2A4E);
static const ble_uuid16_t UUID_CTRL_PT     = BLE_UUID16_INIT(0x2A4C);
static const ble_uuid16_t UUID_REPORT_REF  = BLE_UUID16_INIT(0x2908);

static const struct ble_gatt_dsc_def report_dsc_mouse[] = {
    { .uuid = (ble_uuid_t *)&UUID_REPORT_REF, .att_flags = BLE_ATT_F_READ, .access_cb = report_ref_mouse_cb },
    { 0 },
};
static const struct ble_gatt_dsc_def report_dsc_consumer[] = {
    { .uuid = (ble_uuid_t *)&UUID_REPORT_REF, .att_flags = BLE_ATT_F_READ, .access_cb = report_ref_consumer_cb },
    { 0 },
};

static const struct ble_gatt_chr_def hid_chrs[] = {
    { .uuid = (ble_uuid_t *)&UUID_HID_INFO,    .access_cb = access_cb, .flags = BLE_GATT_CHR_F_READ | BLE_GATT_CHR_F_READ_ENC },
    { .uuid = (ble_uuid_t *)&UUID_REPORT_MAP,  .access_cb = access_cb, .flags = BLE_GATT_CHR_F_READ | BLE_GATT_CHR_F_READ_ENC },
    { .uuid = (ble_uuid_t *)&UUID_PROTO_MODE,  .access_cb = access_cb, .flags = BLE_GATT_CHR_F_READ | BLE_GATT_CHR_F_WRITE_NO_RSP },
    { .uuid = (ble_uuid_t *)&UUID_CTRL_PT,     .access_cb = access_cb, .flags = BLE_GATT_CHR_F_WRITE_NO_RSP },
    { .uuid = (ble_uuid_t *)&UUID_REPORT, .access_cb = access_cb, .descriptors = (struct ble_gatt_dsc_def *)report_dsc_mouse,    .flags = BLE_GATT_CHR_F_READ | BLE_GATT_CHR_F_READ_ENC | BLE_GATT_CHR_F_NOTIFY, .val_handle = &s_input_h },
    { .uuid = (ble_uuid_t *)&UUID_REPORT, .access_cb = access_cb, .descriptors = (struct ble_gatt_dsc_def *)report_dsc_consumer, .flags = BLE_GATT_CHR_F_READ | BLE_GATT_CHR_F_READ_ENC | BLE_GATT_CHR_F_NOTIFY, .val_handle = &s_consumer_h },
    { .uuid = (ble_uuid_t *)&UUID_BOOT_MOUSE,  .access_cb = access_cb, .flags = BLE_GATT_CHR_F_READ | BLE_GATT_CHR_F_READ_ENC | BLE_GATT_CHR_F_NOTIFY, .val_handle = &s_boot_in_h },
    { 0 }
};

static const struct ble_gatt_svc_def gatt_svcs[] = {
    {
        .type            = BLE_GATT_SVC_TYPE_PRIMARY,
        .uuid            = (ble_uuid_t *)&UUID_HID,
        .characteristics = hid_chrs,
    },
    { 0 },
};

static void start_advertising(void);

static void notify_app(bool connected);

static int gap_event(struct ble_gap_event *event, void *)
{
    switch (event->type) {
    case BLE_GAP_EVENT_CONNECT:
        if (event->connect.status == 0) {
            s_conn_handle = event->connect.conn_handle;
            s_connected = true;
            notify_app(true);
            // Some hosts (Windows) need us to start security right away.
            ble_gap_security_initiate(event->connect.conn_handle);
        } else {
            start_advertising();
        }
        return 0;
    case BLE_GAP_EVENT_DISCONNECT:
        s_conn_handle = BLE_HS_CONN_HANDLE_NONE;
        s_connected = false;
        notify_app(false);
        start_advertising();
        return 0;
    case BLE_GAP_EVENT_ADV_COMPLETE:
        start_advertising();
        return 0;
    case BLE_GAP_EVENT_ENC_CHANGE:
        // Encryption result; nothing to do, but keep connection alive.
        return 0;
    case BLE_GAP_EVENT_PASSKEY_ACTION: {
        struct ble_sm_io pk = {};
        pk.action = event->passkey.params.action;
        if (event->passkey.params.action == BLE_SM_IOACT_NUMCMP) {
            pk.numcmp_accept = 1;
            ble_sm_inject_io(event->passkey.conn_handle, &pk);
        } else if (event->passkey.params.action == BLE_SM_IOACT_DISP) {
            pk.passkey = 0;
            ble_sm_inject_io(event->passkey.conn_handle, &pk);
        }
        return 0;
    }
    case BLE_GAP_EVENT_REPEAT_PAIRING: {
        // Host has a stale bond; drop our copy and let pairing restart.
        struct ble_gap_conn_desc desc;
        if (ble_gap_conn_find(event->repeat_pairing.conn_handle, &desc) == 0) {
            ble_store_util_delete_peer(&desc.peer_id_addr);
        }
        return BLE_GAP_REPEAT_PAIRING_RETRY;
    }
    default:
        return 0;
    }
}

static void start_advertising(void)
{
    struct ble_hs_adv_fields fields = {};
    const char *name = ble_svc_gap_device_name();
    fields.flags = BLE_HS_ADV_F_DISC_GEN | BLE_HS_ADV_F_BREDR_UNSUP;
    fields.tx_pwr_lvl_is_present = 1;
    fields.tx_pwr_lvl = BLE_HS_ADV_TX_PWR_LVL_AUTO;
    fields.name = (uint8_t *)name;
    fields.name_len = strlen(name);
    fields.name_is_complete = 1;
    static const ble_uuid16_t adv_uuid = BLE_UUID16_INIT(0x1812);
    fields.uuids16 = (ble_uuid16_t *)&adv_uuid;
    fields.num_uuids16 = 1;
    fields.uuids16_is_complete = 1;
    fields.appearance = 0x03C2;
    fields.appearance_is_present = 1;
    ble_gap_adv_set_fields(&fields);

    struct ble_gap_adv_params adv_params = {};
    adv_params.conn_mode = BLE_GAP_CONN_MODE_UND;
    adv_params.disc_mode = BLE_GAP_DISC_MODE_GEN;
    ble_gap_adv_start(s_addr_type, NULL, BLE_HS_FOREVER, &adv_params, gap_event, NULL);
}

// Internal RAM free just before the stack was started, to log its real cost
// once the host is up (BLE_MIN_INTERNAL_FREE is an estimate until measured).
static size_t s_ble_free_before = 0;

static void on_sync(void)
{
    ble_hs_id_infer_auto(0, &s_addr_type);
    start_advertising();
    if (s_ble_free_before != 0) {
        const size_t now = heap_caps_get_free_size(MALLOC_CAP_INTERNAL);
        ESP_UTILS_LOGI("BLE up: internal RAM %u -> %u free, stack took %u B (min block now %u)",
                       (unsigned)s_ble_free_before, (unsigned)now,
                       (unsigned)(s_ble_free_before > now ? s_ble_free_before - now : 0),
                       (unsigned)heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL));
        s_ble_free_before = 0;
    }
}

static void on_reset(int reason) { (void)reason; }

static void host_task(void *) { nimble_port_run(); nimble_port_freertos_deinit(); }

static esp_err_t ble_mouse_init(const char *name)
{
    esp_err_t r = nvs_flash_init();
    if (r == ESP_ERR_NVS_NO_FREE_PAGES || r == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        nvs_flash_erase();
        nvs_flash_init();
    }
    nimble_port_init();

    ble_hs_cfg.sync_cb     = on_sync;
    ble_hs_cfg.reset_cb    = on_reset;
    ble_hs_cfg.sm_io_cap   = BLE_SM_IO_CAP_NO_IO;
    ble_hs_cfg.sm_bonding  = 1;
    ble_hs_cfg.sm_sc       = 1;
    ble_hs_cfg.sm_our_key_dist   = BLE_SM_PAIR_KEY_DIST_ENC | BLE_SM_PAIR_KEY_DIST_ID;
    ble_hs_cfg.sm_their_key_dist = BLE_SM_PAIR_KEY_DIST_ENC | BLE_SM_PAIR_KEY_DIST_ID;

    ble_svc_gap_init();
    ble_svc_gatt_init();

    ble_gatts_count_cfg(gatt_svcs);
    ble_gatts_add_svcs(gatt_svcs);

    ble_svc_gap_device_name_set(name);

    ble_store_config_init();

    nimble_port_freertos_init(host_task);
    return ESP_OK;
}

// Settings "Enable Bluetooth" switch (registered via Settings::setBluetoothHandler).
// Runs on the LVGL task (switch handler) or on the main task at app init.
// Stopping blocks until the NimBLE host task has exited, a few ms.
// The BLE controller allocates from internal SRAM only and the library asserts
// instead of failing when that runs out. Check first; the host heap is in
// PSRAM (CONFIG_BT_NIMBLE_MEM_ALLOC_MODE_EXTERNAL), the controller is not.
static constexpr size_t BLE_MIN_INTERNAL_FREE  = 72 * 1024;
static constexpr size_t BLE_MIN_INTERNAL_BLOCK = 24 * 1024;

static bool ble_set_enabled(bool enable)
{
    using esp_brookesia::apps::Settings;
    if (enable) {
        if (s_nimble_up) return true;
        const size_t free_b  = heap_caps_get_free_size(MALLOC_CAP_INTERNAL);
        const size_t block_b = heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL);
        if (free_b < BLE_MIN_INTERNAL_FREE || block_b < BLE_MIN_INTERNAL_BLOCK) {
            ESP_UTILS_LOGE("Bluetooth not started: internal RAM %u free / %u max block (need %u / %u)",
                           (unsigned)free_b, (unsigned)block_b,
                           (unsigned)BLE_MIN_INTERNAL_FREE, (unsigned)BLE_MIN_INTERNAL_BLOCK);
            return false;
        }
        s_ble_free_before = free_b;
        if (ble_mouse_init("S3 Watch") != ESP_OK) {
            ESP_UTILS_LOGE("Bluetooth init failed");
            return false;
        }
        s_nimble_up = true;
        Settings::bluetooth_running = true;
        ESP_UTILS_LOGI("Bluetooth on (internal RAM was %u free)", (unsigned)free_b);
        return true;
    }
    if (!s_nimble_up) return true;

    s_stopping = true;                        // gap_event() must not touch LVGL from here on
    int rc = nimble_port_stop();              // drops links, waits for the host to stop
    if (rc == 0) {
        nimble_port_deinit();                 // host + HCI + controller down (memory kept for restart)
        s_nimble_up = false;
        Settings::bluetooth_running = false;
        ESP_UTILS_LOGI("Bluetooth off");
    } else {
        ESP_UTILS_LOGW("nimble_port_stop failed: %d", rc);
    }
    s_stopping    = false;
    s_connected   = false;
    s_conn_handle = BLE_HS_CONN_HANDLE_NONE;
    s_input_h = s_consumer_h = s_boot_in_h = 0;
    notify_app(false);                        // status-bar icon + app label
    return !s_nimble_up;
}

static void send_mouse_report(uint8_t buttons, int8_t dx, int8_t dy, int8_t wheel = 0)
{
    if (!s_connected || s_input_h == 0) return;
    uint8_t r[4] = { buttons, (uint8_t)dx, (uint8_t)dy, (uint8_t)wheel };
    struct os_mbuf *om = ble_hs_mbuf_from_flat(r, sizeof(r));
    if (!om) return;
    ble_gatts_notify_custom(s_conn_handle, s_input_h, om);
}

extern "C" void ble_mouse_send_consumer(uint16_t code)
{
    if (!s_connected || s_consumer_h == 0) return;
    uint8_t r[2] = { (uint8_t)(code & 0xFF), (uint8_t)((code >> 8) & 0xFF) };
    struct os_mbuf *om = ble_hs_mbuf_from_flat(r, sizeof(r));
    if (!om) return;
    ble_gatts_notify_custom(s_conn_handle, s_consumer_h, om);
}

#include "bsp/esp-bsp.h"
#include "driver/i2c_master.h"

static i2c_master_dev_handle_t s_tp_dev = nullptr;

// Read the FocalTech touch controller directly: TD_STATUS (0x02, low nibble =
// number of fingers) followed by the first two touch records (6 bytes each,
// 0x03..0x0E). LVGL only ever sees the first point and the controller is free
// to swap which finger comes first, so for scrolling we use the mean Y of all
// fingers instead of LVGL's point: that is stable across a swap.
//
// Returns the finger count (0 on error) and, when count > 0, their mean Y.
// Shares the BSP I2C bus with the esp_lcd_touch driver. Both run on the LVGL
// task, so the two readers never interleave.
static int read_touch(int *mean_y)
{
    if (s_tp_dev == nullptr) {
        i2c_master_bus_handle_t bus = bsp_i2c_get_handle();
        if (!bus) return 0;
        i2c_device_config_t cfg = {};
        cfg.dev_addr_length = I2C_ADDR_BIT_LEN_7;
        cfg.device_address  = 0x38;   // FT5x06 family (FT3168 on this board)
        cfg.scl_speed_hz    = 400000;
        esp_err_t err = i2c_master_bus_add_device(bus, &cfg, &s_tp_dev);
        if (err != ESP_OK) {
            static bool warned = false;
            if (!warned) { warned = true; ESP_UTILS_LOGW("touch dev add failed: %s", esp_err_to_name(err)); }
            s_tp_dev = nullptr;
            return 0;
        }
    }
    uint8_t reg = 0x02;                 // TD_STATUS, then P1 (0x03..0x08), P2 (0x09..0x0E)
    uint8_t buf[1 + 2 * 6] = {0};
    esp_err_t err = i2c_master_transmit_receive(s_tp_dev, &reg, 1, buf, sizeof(buf), 50);
    if (err != ESP_OK) {
        ESP_UTILS_LOGD("touch read failed: %s", esp_err_to_name(err));
        return 0;
    }
    int n = buf[0] & 0x0F;
    if (n <= 0 || n > 5) return 0;
    int use = (n > 2) ? 2 : n;
    int sum_y = 0;
    for (int i = 0; i < use; i++) {
        const uint8_t *p = &buf[1 + i * 6];
        // Same decode as esp_lcd_touch_ft5x06: Y = (P_YH & 0x0F) << 8 | P_YL
        sum_y += (((int)p[2] & 0x0F) << 8) | p[3];
    }
    if (mean_y) *mean_y = sum_y / use;
    return n;
}

static int read_finger_count(void)
{
    return read_touch(nullptr);
}

namespace esp_brookesia::apps {

static constexpr char APP_NAME[] = "BLE Mouse";

BLEMouse *BLEMouse::_instance = nullptr;

BLEMouse *BLEMouse::requestInstance(bool use_status_bar, bool use_navigation_bar)
{
    if (_instance == nullptr) _instance = new BLEMouse(use_status_bar, use_navigation_bar);
    return _instance;
}

BLEMouse::BLEMouse(bool use_status_bar, bool use_navigation_bar)
    : App(APP_NAME, &trackpad_icon,
          true, use_status_bar, use_navigation_bar) {}

BLEMouse::~BLEMouse() { _instance = nullptr; }

bool BLEMouse::init()
{
    // App inits run in registry order, which is not defined: make sure the
    // saved config is loaded before reading the Bluetooth switch.
    Settings::ensureLoaded();
    Settings::setBluetoothHandler(&ble_set_enabled);
    if (Settings::cfg.bluetooth.enabled) {
        if (!ble_set_enabled(true)) {
            ESP_UTILS_LOGW("Bluetooth enabled in Settings but could not start");
        }
    } else {
        ESP_UTILS_LOGI("Bluetooth disabled in Settings, stack not started");
    }
    return true;
}

bool BLEMouse::deinit() { return true; }

bool BLEMouse::run()
{
    
    gpio_config_t cfg = {};
    cfg.pin_bit_mask = 1ULL << BTN_GPIO;
    cfg.mode         = GPIO_MODE_INPUT;
    cfg.pull_up_en   = GPIO_PULLUP_ENABLE;
    cfg.pull_down_en = GPIO_PULLDOWN_DISABLE;
    cfg.intr_type    = GPIO_INTR_DISABLE;
    gpio_config(&cfg);
    _btn_last = _btn_prev = gpio_get_level(BTN_GPIO);
    if (!s_nimble_up) {
        ESP_UTILS_LOGW("Bluetooth is off (Settings > Bluetooth); the pad will not send anything");
    }
    lv_obj_t *scr = lv_scr_act();
    lv_obj_set_style_bg_color(scr, lv_color_hex(0x030303), 0);
    lv_obj_set_style_bg_opa(scr, LV_OPA_COVER, 0);
    lv_obj_clear_flag(scr, LV_OBJ_FLAG_SCROLLABLE);

    _pad = lv_obj_create(scr);
    lv_obj_set_size(_pad, lv_pct(100), lv_pct(100));
    lv_obj_set_style_bg_color(_pad, lv_color_hex(0x030303), 0);
    lv_obj_set_style_border_width(_pad, 0, 0);
    lv_obj_set_style_radius(_pad, 0, 0);
    lv_obj_clear_flag(_pad, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(_pad, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(_pad, &BLEMouse::onPadPress,    LV_EVENT_PRESSED,  this);
    lv_obj_add_event_cb(_pad, &BLEMouse::onPadPressing, LV_EVENT_PRESSING, this);
    lv_obj_add_event_cb(_pad, &BLEMouse::onPadRelease,  LV_EVENT_RELEASED, this);

    // Scroll strip hint along the right edge. Not clickable, so presses on it
    // are delivered to _pad and classified in onPadPress().
    _strip = lv_obj_create(_pad);
    lv_obj_set_size(_strip, SCROLL_STRIP_PX, lv_pct(100));
    lv_obj_align(_strip, LV_ALIGN_RIGHT_MID, 0, 0);
    lv_obj_set_style_bg_color(_strip, lv_color_hex(0x141414), 0);
    lv_obj_set_style_bg_opa(_strip, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(_strip, 0, 0);
    lv_obj_set_style_radius(_strip, 0, 0);
    lv_obj_set_style_pad_all(_strip, 0, 0);
    lv_obj_clear_flag(_strip, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_clear_flag(_strip, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_t *strip_lbl = lv_label_create(_strip);
    lv_label_set_text(strip_lbl, LV_SYMBOL_UP "\n\n" LV_SYMBOL_DOWN);
    lv_obj_set_style_text_color(strip_lbl, lv_color_hex(0x3A3A3A), 0);
    lv_obj_set_style_text_align(strip_lbl, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_center(strip_lbl);

    // _lbl_status = lv_label_create(scr);
    // lv_obj_set_style_text_color(_lbl_status, lv_color_white(), 0);
    // lv_obj_set_style_text_font(_lbl_status, &lv_font_montserrat_18, 0);
    // lv_obj_align(_lbl_status, LV_ALIGN_BOTTOM_MID, 0, 0);
    // lv_label_set_text(_lbl_status, s_connected ? "Connected" : "Advertising...");

    refreshStatusIcon();

    _poll = lv_timer_create(&BLEMouse::onPollTimer, 30, this);
    Settings::sleep_locked = true;     // hold wake-lock while mouse is used
    return true;
}

bool BLEMouse::back()
{
    notifyCoreClosed();
    return true;
}

bool BLEMouse::pause()  { return true; }
bool BLEMouse::resume() { refreshStatusIcon(); return true; }

bool BLEMouse::close()
{
    _pad = nullptr;
    _strip = nullptr;
    _lbl_status = nullptr;
    _poll = nullptr;
    _has_last = false;
    return true;
}

// ---------------------------------------------------------------------------
// Pad gestures
//   1 finger drag        -> cursor move
//   quick tap (1 finger) -> left click      (< TAP_MAX_US, < TAP_MAX_MOVE_PX)
//   2 finger drag        -> wheel scroll    (slide down = scroll down)
//   edge drag            -> back to launcher (unchanged)
//
// LVGL only ever reports one pointer (the touch driver's first point), so the
// second finger is detected by reading the FT5x06 touch-count register
// directly (read_finger_count()). Once a second finger has been seen the
// whole press is treated as a scroll gesture, even if one finger lifts
// early: that avoids a cursor jump when the reported point swaps fingers.
// ---------------------------------------------------------------------------

void BLEMouse::onPadPress(lv_event_t *e)
{
    auto *self = static_cast<BLEMouse *>(lv_event_get_user_data(e));
    lv_indev_t *ind = lv_indev_active();
    if (!ind) return;
    lv_point_t p; lv_indev_get_point(ind, &p);
    self->_last_point   = p;
    self->_start_point  = p;
    self->_has_last     = true;
    self->_press_us      = esp_timer_get_time();
    self->_move_total    = 0;
    self->_scroll_accum  = 0;
    self->_scroll_last_y = -1;
    {
        int my = 0;
        self->_two_finger = (read_touch(&my) >= 2);
        if (self->_two_finger) self->_scroll_last_y = my;
    }
    // Mark as candidate "back" gesture if press starts within 30 px of left/right edge.
    int w = lv_obj_get_width(self->_pad);
    self->_edge_swipe  = (p.x < 30) || (p.x > w - 30);
    // Press in the right-edge strip = one-finger scroll gesture.
    self->_strip_scroll = (p.x >= w - SCROLL_STRIP_PX);
    self->_strip_last_y = p.y;
}

void BLEMouse::onPadPressing(lv_event_t *e)
{
    auto *self = static_cast<BLEMouse *>(lv_event_get_user_data(e));
    if (!self->_has_last) return;
    lv_indev_t *ind = lv_indev_active();
    if (!ind) return;
    lv_point_t p; lv_indev_get_point(ind, &p);

    int raw_dx = p.x - self->_last_point.x;
    int raw_dy = p.y - self->_last_point.y;

    // Finger count + mean Y straight from the controller (see read_touch()).
    int mean_y = 0;
    int fingers = read_touch(&mean_y);

    // Diagnostic: log every change of the reported finger count. If this never
    // prints "fingers=2" with two fingers on the glass, the FT3168 firmware is
    // in its 1-point + gesture mode and two-finger scroll cannot work; use the
    // right-edge strip instead.
    {
        static int s_last_fingers = -1;
        if (fingers != s_last_fingers) {
            s_last_fingers = fingers;
            ESP_UTILS_LOGI("touch: fingers=%d", fingers);
        }
    }

    // Latch scroll mode as soon as a second finger shows up.
    if (!self->_two_finger && fingers >= 2) {
        self->_two_finger    = true;
        self->_scroll_accum  = 0;
        self->_scroll_last_y = mean_y;   // start measuring from here, not from the landing jump
        self->_last_point    = p;
        ESP_UTILS_LOGD("scroll: start (fingers=%d, y=%d)", fingers, mean_y);
        return;
    }

    if (self->_two_finger) {
        self->_last_point = p;           // keep the cursor path in sync for the release handler
        if (fingers < 2) {
            // One finger lifted early: stay in scroll mode but stop measuring
            // until both are back, so the remaining finger does not scroll.
            self->_scroll_last_y = -1;
            return;
        }
        if (self->_scroll_last_y < 0) {  // first sample after a resync
            self->_scroll_last_y = mean_y;
            return;
        }
        int dy = mean_y - self->_scroll_last_y;
        self->_scroll_last_y = mean_y;
        if (dy > SCROLL_JUMP_PX || dy < -SCROLL_JUMP_PX) return;   // glitch, ignore

        self->_scroll_accum += dy;
        int notches = self->_scroll_accum / SCROLL_STEP_PX;   // truncates toward zero
        if (notches != 0) {
            self->_scroll_accum -= notches * SCROLL_STEP_PX;
            // HID wheel: +1 = scroll up. Sliding the fingers down scrolls down.
            int wheel = -notches;
            if (wheel > 127)  wheel = 127;
            if (wheel < -127) wheel = -127;
            ESP_UTILS_LOGD("scroll: dy=%d wheel=%d", dy, wheel);
            send_mouse_report(0, 0, 0, (int8_t)wheel);
        }
        return;
    }

    // Right-edge strip: one-finger vertical drag = wheel.
    if (self->_strip_scroll) {
        int dy = p.y - self->_strip_last_y;
        self->_strip_last_y = p.y;
        self->_last_point   = p;
        self->_scroll_accum += dy;
        int notches = self->_scroll_accum / SCROLL_STEP_PX;
        if (notches != 0) {
            self->_scroll_accum -= notches * SCROLL_STEP_PX;
            int wheel = -notches;            // slide down = scroll down
            if (wheel > 127)  wheel = 127;
            if (wheel < -127) wheel = -127;
            send_mouse_report(0, 0, 0, (int8_t)wheel);
        }
        return;
    }

    // Single finger: cursor move.
    int dx = (int)(raw_dx * MOVE_GAIN);
    int dy = (int)(raw_dy * MOVE_GAIN);
    if (dx > 127) dx = 127;
    if (dx < -127) dx = -127;
    if (dy > 127) dy = 127;
    if (dy < -127) dy = -127;
    if (dx != 0 || dy != 0) {
        self->_move_total += (raw_dx < 0 ? -raw_dx : raw_dx) + (raw_dy < 0 ? -raw_dy : raw_dy);
        send_mouse_report(0, (int8_t)dx, (int8_t)dy);
        self->_last_point = p;
    }
}

void BLEMouse::onPadRelease(lv_event_t *e)
{
    auto *self = static_cast<BLEMouse *>(lv_event_get_user_data(e));
    self->_has_last = false;

    const int64_t held_us = esp_timer_get_time() - self->_press_us;
    const bool strip_drag = self->_strip_scroll && self->_scroll_accum != 0;
    const bool tap = !self->_two_finger && !strip_drag &&
                     held_us < TAP_MAX_US &&
                     self->_move_total < TAP_MAX_MOVE_PX;

    // Quick, still, single-finger tap = left click (press + release).
    if (tap) {
        send_mouse_report(0x01, 0, 0);
        send_mouse_report(0x00, 0, 0);
    }

    // Per-app "back" gesture: edge-start drag, only when BLE is connected.
    // Lower threshold than the global gesture (15 px instead of 30).
    // A tap cannot also be a back swipe: it moved less than TAP_MAX_MOVE_PX.
    if (!tap && !self->_two_finger && self->_edge_swipe && s_connected) {
        lv_indev_t *ind = lv_indev_active();
        if (ind) {
            lv_point_t p; lv_indev_get_point(ind, &p);
            int dx = p.x - self->_start_point.x;
            if (dx > 15 || dx < -15) {
                self->notifyCoreClosed();
            }
        }
    }
    self->_edge_swipe   = false;
    self->_two_finger   = false;
    self->_strip_scroll = false;
}

void BLEMouse::onPollTimer(lv_timer_t *t)
{
    auto *self = static_cast<BLEMouse *>(lv_timer_get_user_data(t));
    int level = gpio_get_level(BTN_GPIO);
    if (level == self->_btn_last) {
        if (self->_btn_stable < 4) self->_btn_stable++;
    } else {
        self->_btn_stable = 0;
        self->_btn_last = level;
    }
    int stable = (self->_btn_stable >= 2) ? self->_btn_last : self->_btn_prev;

    if (self->_btn_prev == 1 && stable == 0) {
        self->_btn_press_us = esp_timer_get_time();
        self->_btn_armed = true;
    }
    if (self->_btn_armed && stable == 0 &&
        (esp_timer_get_time() - self->_btn_press_us) >= LONG_PRESS_US) {
        send_mouse_report(0x02, 0, 0);
        send_mouse_report(0x00, 0, 0);
        self->_btn_armed = false;
    }
    if (self->_btn_prev == 0 && stable == 1) {
        if (self->_btn_armed) {
            send_mouse_report(0x01, 0, 0);
            send_mouse_report(0x00, 0, 0);
        }
        self->_btn_armed = false;
    }
    self->_btn_prev = stable;
}

void BLEMouse::refreshStatusIcon()
{
    // The Wi-Fi icon used to mirror the BLE link here. The status bar now has
    // its own Bluetooth icon (Settings), and the Wi-Fi icon follows the Wi-Fi /
    // ESP-NOW switches. Caller holds the LVGL lock.
    Settings::setBluetoothConnected(s_connected);
}

void BLEMouse::onConnectionChanged(bool /*connected*/)
{
    // Reached from gap_event(), i.e. the NimBLE host task, not the LVGL task.
    // LVGL is not thread safe: take the port lock. The LVGL task holds the
    // same recursive mutex around lv_timer_handler(), so this serialises the
    // label/status-bar update against rendering and the panel flush.
    if (!bsp_display_lock(1000)) {
        ESP_UTILS_LOGW("BLE state change: LVGL lock timeout, UI not updated");
        return;
    }
    // Status-bar Bluetooth icon (Settings owns it). Independent of whether
    // the BLE Mouse screen is open.
    Settings::setBluetoothConnected(s_connected);

    if (_instance != nullptr) {
        if (_instance->_lbl_status) {
            lv_label_set_text(_instance->_lbl_status, s_connected ? "Connected" : "Advertising...");
        }
        _instance->refreshStatusIcon();
    }
    bsp_display_unlock();
}

ESP_UTILS_REGISTER_PLUGIN_WITH_CONSTRUCTOR(systems::base::App, BLEMouse, APP_NAME, []()
{
    return std::shared_ptr<BLEMouse>(BLEMouse::requestInstance(), [](BLEMouse *p) {});
})

} // namespace esp_brookesia::apps

static void notify_app(bool connected)
{
    if (s_stopping) return;   // the switch handler owns the UI update while stopping
    esp_brookesia::apps::BLEMouse::onConnectionChanged(connected);
}
