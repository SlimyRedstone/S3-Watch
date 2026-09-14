#include "BLEMouse.hpp"
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

static void on_sync(void)
{
    ble_hs_id_infer_auto(0, &s_addr_type);
    start_advertising();
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

static int read_finger_count(void)
{
    if (s_tp_dev == nullptr) {
        i2c_master_bus_handle_t bus = bsp_i2c_get_handle();
        if (!bus) return 0;
        i2c_device_config_t cfg = {};
        cfg.dev_addr_length = I2C_ADDR_BIT_LEN_7;
        cfg.device_address  = 0x38;   // FT5x06
        cfg.scl_speed_hz    = 400000;
        if (i2c_master_bus_add_device(bus, &cfg, &s_tp_dev) != ESP_OK) return 0;
    }
    uint8_t reg = 0x02;   // TD_STATUS register
    uint8_t v = 0;
    if (i2c_master_transmit_receive(s_tp_dev, &reg, 1, &v, 1, 50) != ESP_OK) return 0;
    return v & 0x0F;
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
    if (!s_nimble_up && gpio_get_level(BTN_GPIO) == 0) {
        ble_mouse_init("S3 Watch");
        s_nimble_up = true;
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
    lv_obj_t *scr = lv_scr_act();
    lv_obj_set_style_bg_color(scr, lv_color_hex(0x0A0A0A), 0);
    lv_obj_set_style_bg_opa(scr, LV_OPA_COVER, 0);
    lv_obj_clear_flag(scr, LV_OBJ_FLAG_SCROLLABLE);

    _pad = lv_obj_create(scr);
    lv_obj_set_size(_pad, lv_pct(100), lv_pct(100));
    lv_obj_set_style_bg_color(_pad, lv_color_hex(0x080808), 0);
    lv_obj_set_style_border_width(_pad, 0, 0);
    lv_obj_set_style_radius(_pad, 0, 0);
    lv_obj_clear_flag(_pad, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(_pad, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(_pad, &BLEMouse::onPadPress,    LV_EVENT_PRESSED,  this);
    lv_obj_add_event_cb(_pad, &BLEMouse::onPadPressing, LV_EVENT_PRESSING, this);
    lv_obj_add_event_cb(_pad, &BLEMouse::onPadRelease,  LV_EVENT_RELEASED, this);

    _lbl_status = lv_label_create(scr);
    lv_obj_set_style_text_color(_lbl_status, lv_color_white(), 0);
    lv_obj_set_style_text_font(_lbl_status, &lv_font_montserrat_18, 0);
    lv_obj_align(_lbl_status, LV_ALIGN_BOTTOM_MID, 0, 0);
    lv_label_set_text(_lbl_status, s_connected ? "Connected" : "Advertising...");

    refreshStatusIcon();

    _poll = lv_timer_create(&BLEMouse::onPollTimer, 30, this);
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
    _lbl_status = nullptr;
    _poll = nullptr;
    _has_last = false;
    return true;
}

void BLEMouse::onPadPress(lv_event_t *e)
{
    auto *self = static_cast<BLEMouse *>(lv_event_get_user_data(e));
    lv_indev_t *ind = lv_indev_active();
    if (!ind) return;
    lv_point_t p; lv_indev_get_point(ind, &p);
    self->_last_point  = p;
    self->_start_point = p;
    self->_has_last    = true;
    // Mark as candidate "back" gesture if press starts within 30 px of left/right edge.
    int w = lv_obj_get_width(self->_pad);
    self->_edge_swipe  = (p.x < 30) || (p.x > w - 30);
}

void BLEMouse::onPadPressing(lv_event_t *e)
{
    auto *self = static_cast<BLEMouse *>(lv_event_get_user_data(e));
    if (!self->_has_last) return;
    lv_indev_t *ind = lv_indev_active();
    if (!ind) return;
    lv_point_t p; lv_indev_get_point(ind, &p);
    int dx = (int)((p.x - self->_last_point.x) * MOVE_GAIN);
    int dy = (int)((p.y - self->_last_point.y) * MOVE_GAIN);
    if (dx > 127) dx = 127;
    if (dx < -127) dx = -127;
    if (dy > 127) dy = 127;
    if (dy < -127) dy = -127;

    int fingers = read_finger_count();
    if (fingers >= 2) {
        // Two-finger drag = wheel scroll. Up swipe = wheel +, down = wheel -.
        int wheel = -(int)(dy / 8);
        if (wheel > 127) wheel = 127; 
        if (wheel < -127) wheel = -127;
        if (wheel != 0) {
            send_mouse_report(0, 0, 0, (int8_t)wheel);
            self->_last_point = p;
        }
    } else if (dx != 0 || dy != 0) {
        send_mouse_report(0, (int8_t)dx, (int8_t)dy);
        self->_last_point = p;
    }
}

void BLEMouse::onPadRelease(lv_event_t *e)
{
    auto *self = static_cast<BLEMouse *>(lv_event_get_user_data(e));
    self->_has_last = false;

    // Per-app "back" gesture: edge-start drag, only when BLE is connected.
    // Lower threshold than the global gesture (15 px instead of 30).
    if (self->_edge_swipe && s_connected) {
        lv_indev_t *ind = lv_indev_active();
        if (ind) {
            lv_point_t p; lv_indev_get_point(ind, &p);
            int dx = p.x - self->_start_point.x;
            if (dx > 15 || dx < -15) {
                self->notifyCoreClosed();
            }
        }
    }
    self->_edge_swipe = false;
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
    auto *phone = getSystem();
    if (!phone) return;
    auto *bar = phone->getDisplay().getStatusBar();
    if (!bar) return;
    bar->setWifiIconState(s_connected
        ? systems::phone::StatusBar::WifiState::SIGNAL_3
        : systems::phone::StatusBar::WifiState::DISCONNECTED);
}

void BLEMouse::onConnectionChanged(bool /*connected*/)
{
    if (_instance == nullptr) return;
    if (_instance->_lbl_status) {
        lv_label_set_text(_instance->_lbl_status, s_connected ? "Connected" : "Advertising...");
    }
    _instance->refreshStatusIcon();
}

ESP_UTILS_REGISTER_PLUGIN_WITH_CONSTRUCTOR(systems::base::App, BLEMouse, APP_NAME, []()
{
    return std::shared_ptr<BLEMouse>(BLEMouse::requestInstance(), [](BLEMouse *p) {});
})

} // namespace esp_brookesia::apps

static void notify_app(bool connected)
{
    esp_brookesia::apps::BLEMouse::onConnectionChanged(connected);
}
