/*
 * SPDX-FileCopyrightText: 2024-2025 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#include <algorithm>
#include "esp_brookesia_systems_internal.h"
#if !ESP_BROOKESIA_PHONE_RECENTS_SCREEN_ENABLE_DEBUG_LOG
#   define ESP_BROOKESIA_UTILS_DISABLE_DEBUG_LOG
#endif
#include "phone/private/esp_brookesia_phone_utils.hpp"
#include "esp_brookesia_recents_screen_snapshot.hpp"

using namespace std;
using namespace esp_brookesia::gui;

namespace esp_brookesia::systems::phone {

RecentsScreenSnapshot::RecentsScreenSnapshot(
    base::Context &core,
    const RecentsScreenSnapshot::Conf &conf,
    const RecentsScreenSnapshot::Data &data
)
    : _system_context(core)
    , _conf(conf)
    , _data(data)
{
}

RecentsScreenSnapshot::~RecentsScreenSnapshot()
{
    ESP_UTILS_LOGD("Destroy(@0x%p)", this);
    if (!del()) {
        ESP_UTILS_LOGE("Delete failed");
    }
}

bool RecentsScreenSnapshot::begin(lv_obj_t *parent)
{
    ESP_Brookesia_LvObj_t main_obj = NULL;
    ESP_Brookesia_LvObj_t drag_obj = NULL;
    ESP_Brookesia_LvObj_t title_obj = NULL;
    ESP_Brookesia_LvObj_t title_icon = NULL;
    ESP_Brookesia_LvObj_t title_label = NULL;
    ESP_Brookesia_LvObj_t snapshot_obj = NULL;
    ESP_Brookesia_LvObj_t snapshot_image = NULL;

    ESP_UTILS_LOGD("Begin@0x%p)", this);
    ESP_UTILS_CHECK_NULL_RETURN(parent, false, "Invalid parent object");
    ESP_UTILS_CHECK_NULL_RETURN(_conf.name, false, "Invalid name");
    ESP_UTILS_CHECK_NULL_RETURN(_conf.snapshot_image_resource, false, "Invalid snapshot image");
    ESP_UTILS_CHECK_NULL_RETURN(_conf.icon_image_resource, false, "Invalid icon image");
    ESP_UTILS_CHECK_FALSE_RETURN(!checkInitialized(), false, "Snapshot is already initialized");

    /* Create objects */
    main_obj = ESP_BROOKESIA_LV_OBJ(obj, parent);
    ESP_UTILS_CHECK_NULL_RETURN(main_obj, false, "Create main_obj failed");
    drag_obj = ESP_BROOKESIA_LV_OBJ(obj, main_obj.get());
    ESP_UTILS_CHECK_NULL_RETURN(drag_obj, false, "Create drag obj failed");
    title_obj = ESP_BROOKESIA_LV_OBJ(obj, drag_obj.get());
    ESP_UTILS_CHECK_NULL_RETURN(title_obj, false, "Create title obj failed");
    title_icon = ESP_BROOKESIA_LV_OBJ(img, title_obj.get());
    ESP_UTILS_CHECK_NULL_RETURN(title_icon, false, "Create title icon failed");
    title_label = ESP_BROOKESIA_LV_OBJ(label, title_obj.get());
    ESP_UTILS_CHECK_NULL_RETURN(title_label, false, "Create title label failed");
    snapshot_obj = ESP_BROOKESIA_LV_OBJ(obj, drag_obj.get());
    ESP_UTILS_CHECK_NULL_RETURN(snapshot_obj, false, "Create snapshot obj failed");
    snapshot_image = ESP_BROOKESIA_LV_OBJ(img, snapshot_obj.get());
    ESP_UTILS_CHECK_NULL_RETURN(snapshot_image, false, "Create snapshot image failed");

    /* Setup objects style */
    // Main
    lv_obj_add_style(main_obj.get(), _system_context.getDisplay().getCoreContainerStyle(), 0);
    lv_obj_clear_flag(main_obj.get(), LV_OBJ_FLAG_SCROLLABLE);
    // Drag
    lv_obj_add_style(drag_obj.get(), _system_context.getDisplay().getCoreContainerStyle(), 0);
    lv_obj_center(drag_obj.get());
    lv_obj_clear_flag(drag_obj.get(), LV_OBJ_FLAG_SCROLLABLE);
    // Title
    lv_obj_add_style(title_obj.get(), _system_context.getDisplay().getCoreContainerStyle(), 0);
    lv_obj_align(title_obj.get(), LV_ALIGN_TOP_MID, 0, 0);
    lv_obj_set_flex_flow(title_obj.get(), LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(title_obj.get(), LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_clear_flag(title_obj.get(), LV_OBJ_FLAG_SCROLLABLE);
    // Title icon
    lv_obj_add_style(title_icon.get(), _system_context.getDisplay().getCoreContainerStyle(), 0);
    // lv_obj_set_size(title_icon.get(), LV_SIZE_CONTENT, LV_SIZE_CONTENT);
    lv_image_set_inner_align(title_icon.get(), LV_IMAGE_ALIGN_CENTER);
    lv_img_set_src(title_icon.get(), _conf.icon_image_resource);
    // Tile label
    lv_obj_add_style(title_label.get(), _system_context.getDisplay().getCoreContainerStyle(), 0);
    lv_label_set_text_static(title_label.get(), _conf.name);
    // Snapshot
    lv_obj_add_style(snapshot_obj.get(), _system_context.getDisplay().getCoreContainerStyle(), 0);
    lv_obj_align(snapshot_obj.get(), LV_ALIGN_BOTTOM_MID, 0, 0);
    lv_obj_clear_flag(snapshot_obj.get(), LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_clip_corner(snapshot_obj.get(), true, 0);
    // Snapshot image
    lv_obj_add_style(snapshot_image.get(), _system_context.getDisplay().getCoreContainerStyle(), 0);
    lv_obj_center(snapshot_image.get());
    lv_image_set_inner_align(snapshot_image.get(), LV_IMAGE_ALIGN_CENTER);
    lv_obj_clear_flag(snapshot_image.get(), LV_OBJ_FLAG_SCROLLABLE);

    /* Save objects */
    _main_obj = main_obj;
    _drag_obj = drag_obj;
    _title_obj = title_obj;
    _title_icon = title_icon;
    _title_label = title_label;
    _snapshot_obj = snapshot_obj;
    _snapshot_image = snapshot_image;

    // Update style
    ESP_UTILS_CHECK_FALSE_GOTO(updateByNewData(), err, "Update failed");

    // Other operations
    _origin_y = getCurrentY();

    return true;

err:
    ESP_UTILS_CHECK_FALSE_RETURN(del(), false, "Delete failed");

    return false;
}

bool RecentsScreenSnapshot::del(void)
{
    ESP_UTILS_LOGD("Delete@0x%p)", this);

    if (!checkInitialized()) {
        return true;
    }

    _main_obj.reset();
    _drag_obj.reset();
    _title_obj.reset();
    _title_icon.reset();
    _title_label.reset();
    _snapshot_obj.reset();
    _snapshot_image.reset();
    freeScaledSnapshot();                       // after the image object that showed it

    return true;
}

int RecentsScreenSnapshot::getCurrentY(void) const
{
    ESP_UTILS_CHECK_FALSE_RETURN(checkInitialized(), 0, "Not initialized");

    lv_obj_update_layout(_drag_obj.get());
    lv_obj_refr_pos(_drag_obj.get());

    return lv_obj_get_y(_drag_obj.get());
}

bool RecentsScreenSnapshot::updateByNewData(void)
{
    int app_img_zoom = 0;
    float h_factor = 0;
    float w_factor = 0;

    ESP_UTILS_LOGD("Update(@0x%p)", this);
    ESP_UTILS_CHECK_FALSE_RETURN(checkInitialized(), false, "Not initialized");

    // Main
    lv_obj_set_size(_main_obj.get(), _data.main_size.width, _data.main_size.height);
    // Drag
    lv_obj_set_size(_drag_obj.get(), _data.main_size.width, _data.main_size.height);
    // Title
    lv_obj_set_size(_title_obj.get(), _data.title.main_size.width, _data.title.main_size.height);
    lv_obj_set_style_pad_column(_title_obj.get(), _data.title.main_layout_column_pad, 0);
    // Title icon
    h_factor = (float)(_data.title.icon_size.height) / ((const lv_img_dsc_t *)_conf.icon_image_resource)->header.h;
    w_factor = (float)(_data.title.icon_size.width) / ((const lv_img_dsc_t *)_conf.icon_image_resource)->header.w;
    if (h_factor < w_factor) {
        lv_image_set_scale(_title_icon.get(), (int)(h_factor * LV_SCALE_NONE));
    } else {
        lv_image_set_scale(_title_icon.get(), (int)(w_factor * LV_SCALE_NONE));
    }
    lv_obj_set_size(_title_icon.get(), _data.title.icon_size.width, _data.title.icon_size.height);
    lv_obj_refr_size(_title_icon.get());
    // Title label
    lv_obj_set_style_text_font(_title_label.get(), (lv_font_t *)_data.title.text_font.font_resource, 0);
    lv_obj_set_style_text_color(_title_label.get(), lv_color_hex(_data.title.text_color.color), 0);
    lv_obj_set_style_text_opa(_title_label.get(), _data.title.text_color.opacity, 0);
    // Snapshot
    lv_obj_set_size(_snapshot_obj.get(), _data.image.main_size.width, _data.image.main_size.height);
    lv_obj_set_style_radius(_snapshot_obj.get(), _data.image.radius, 0);
    // Snapshot image
    const void *image_src = _conf.snapshot_image_resource;
    if (_conf.snapshot_image_resource != _conf.icon_image_resource) {
        const lv_img_dsc_t *snapshot = (const lv_img_dsc_t *)_conf.snapshot_image_resource;
        h_factor = (float)(_data.image.main_size.height) / snapshot->header.h;
        w_factor = (float)(_data.image.main_size.width) / snapshot->header.w;
        if (h_factor < w_factor) {
            app_img_zoom = (int)(h_factor * LV_SCALE_NONE);
        } else {
            app_img_zoom = (int)(w_factor * LV_SCALE_NONE);
        }
        // S3-Watch: same size on screen, shrunk once here instead of on every frame
        const int32_t w = std::max<int32_t>((int32_t)snapshot->header.w * app_img_zoom / LV_SCALE_NONE, 1);
        const int32_t h = std::max<int32_t>((int32_t)snapshot->header.h * app_img_zoom / LV_SCALE_NONE, 1);
        if ((app_img_zoom < LV_SCALE_NONE) && updateScaledSnapshot(snapshot, w, h)) {
            image_src = _scaled_snapshot;
            lv_image_set_scale(_snapshot_image.get(), LV_SCALE_NONE);
        } else {
            freeScaledSnapshot();
            lv_image_set_scale(_snapshot_image.get(), app_img_zoom);
        }
        lv_obj_align(_snapshot_image.get(), LV_ALIGN_TOP_MID, 0, 0);
    } else {
        freeScaledSnapshot();
        lv_image_set_scale(_snapshot_image.get(), LV_SCALE_NONE);
        lv_obj_center(_snapshot_image.get());
    }
    lv_obj_set_size(_snapshot_image.get(), _data.image.main_size.width, _data.image.main_size.height);
    lv_img_set_src(_snapshot_image.get(), image_src);

    return true;
}

// Box filter: each output pixel is the mean of the source pixels it covers (2-3 per axis at 40 %), per RGB565
// channel. Runs once per recents screen opening and app (164 x 200 output: about 0.2 M source reads).
bool RecentsScreenSnapshot::updateScaledSnapshot(const lv_image_dsc_t *src, int32_t w, int32_t h)
{
    if ((src == nullptr) || (src->data == nullptr) || (src->header.cf != LV_COLOR_FORMAT_RGB565)) {
        return false;
    }
    const int32_t sw = src->header.w;
    const int32_t sh = src->header.h;
    const uint32_t s_stride = (src->header.stride != 0) ? src->header.stride : (uint32_t)sw * 2;
    if ((sw <= 0) || (sh <= 0) || (w > sw) || (h > sh)) {
        return false;
    }

    if ((_scaled_snapshot == nullptr) || ((int32_t)_scaled_snapshot->header.w != w) ||
            ((int32_t)_scaled_snapshot->header.h != h)) {
        freeScaledSnapshot();
        _scaled_snapshot = lv_draw_buf_create(w, h, LV_COLOR_FORMAT_RGB565, 0);
        if (_scaled_snapshot == nullptr) {
            ESP_UTILS_LOGW("Scaled snapshot %dx%d: out of memory, scaled draw used", (int)w, (int)h);
            return false;
        }
    }

    const uint32_t d_stride = _scaled_snapshot->header.stride;
    for (int32_t y = 0; y < h; y++) {
        const int32_t y0 = y * sh / h;
        const int32_t y1 = std::max((y + 1) * sh / h, y0 + 1);
        uint16_t *out = (uint16_t *)(_scaled_snapshot->data + (size_t)y * d_stride);
        for (int32_t x = 0; x < w; x++) {
            const int32_t x0 = x * sw / w;
            const int32_t x1 = std::max((x + 1) * sw / w, x0 + 1);
            uint32_t r = 0, g = 0, b = 0;
            for (int32_t sy = y0; sy < y1; sy++) {
                const uint16_t *row = (const uint16_t *)(src->data + (size_t)sy * s_stride);
                for (int32_t sx = x0; sx < x1; sx++) {
                    const uint16_t c = row[sx];
                    r += c >> 11;
                    g += (c >> 5) & 0x3F;
                    b += c & 0x1F;
                }
            }
            const uint32_t n = (uint32_t)((y1 - y0) * (x1 - x0));
            out[x] = (uint16_t)(((r / n) << 11) | ((g / n) << 5) | (b / n));
        }
    }
    lv_image_cache_drop(_scaled_snapshot);      // same buffer, new pixels
    return true;
}

void RecentsScreenSnapshot::freeScaledSnapshot(void)
{
    if (_scaled_snapshot == nullptr) {
        return;
    }
    lv_image_cache_drop(_scaled_snapshot);
    lv_draw_buf_destroy(_scaled_snapshot);
    _scaled_snapshot = nullptr;
}

} // namespace esp_brookesia::systems::phone
