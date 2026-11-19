/*
 * LVGL allocator: internal SRAM while plentiful, PSRAM beyond a floor.
 *
 * Selected with CONFIG_LV_USE_CUSTOM_MALLOC (LV_USE_STDLIB_MALLOC ==
 * LV_STDLIB_CUSTOM). Replaces the clib backend (plain malloc()).
 *
 * Why not plain malloc(): every allocation below
 * CONFIG_SPIRAM_MALLOC_ALWAYSINTERNAL lands in internal SRAM first, so the
 * whole UI drained it and the BLE controller/host (internal only) asserted:
 *   assert failed: bt_osi_mem_malloc bt_osi_mem.c:40 (mem != NULL)
 *
 * Why not "everything in PSRAM": LVGL allocates and frees draw tasks every
 * frame and reads the object/style tree for every redraw. Behind the PSRAM
 * cache that halved the frame rate and made the stripe-by-stripe flush
 * visible as tearing.
 *
 * Policy:
 *   - size >= LV_PSRAM_ALWAYS_BYTES (layer buffers, snapshots): PSRAM.
 *   - otherwise internal, as long as internal free stays above
 *     LV_INTERNAL_FLOOR_BYTES after the allocation; else PSRAM.
 *   - PSRAM full or absent: internal anyway.
 * The floor is what the radio needs: BLE controller + Wi-Fi/ESP-NOW live
 * entirely in internal SRAM (the NimBLE host heap is in PSRAM, see
 * CONFIG_BT_NIMBLE_MEM_ALLOC_MODE_EXTERNAL). When the stack is up it sits
 * inside the floor, so LVGL simply keeps spilling to PSRAM until it is
 * stopped again. BLEMouse refuses to start below 72 KB free (BLE_MIN_INTERNAL_FREE).
 *
 * The DMA draw buffers are not allocated here (main.cpp hands LVGL two
 * internal buffers via lv_display_set_buffers()).
 */
#include "lvgl.h"

#if LV_USE_STDLIB_MALLOC == LV_STDLIB_CUSTOM

#include "esp_heap_caps.h"
#include "esp_memory_utils.h"   /* esp_ptr_external_ram() */

#define LV_INTERNAL_FLOOR_BYTES   (112 * 1024)
#define LV_PSRAM_ALWAYS_BYTES     (4 * 1024)

#define LV_INT_CAPS   (MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT)
#define LV_PSRAM_CAPS (MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT)

static inline bool internal_has_room(size_t size)
{
    /* heap_caps_get_free_size() just sums the free counters of the internal
     * heaps: no walk, no lock, cheap enough per allocation. */
    size_t free_b = heap_caps_get_free_size(LV_INT_CAPS);
    return free_b > size + LV_INTERNAL_FLOOR_BYTES;
}

static inline uint32_t pick_caps(size_t size)
{
    if(size >= LV_PSRAM_ALWAYS_BYTES) return LV_PSRAM_CAPS;
    return internal_has_room(size) ? LV_INT_CAPS : LV_PSRAM_CAPS;
}

void lv_mem_init(void)
{
}

void lv_mem_deinit(void)
{
}

lv_mem_pool_t lv_mem_add_pool(void * mem, size_t bytes)
{
    LV_UNUSED(mem);
    LV_UNUSED(bytes);
    return NULL;    /* heap_caps manages the pools */
}

void lv_mem_remove_pool(lv_mem_pool_t pool)
{
    LV_UNUSED(pool);
}

void * lv_malloc_core(size_t size)
{
    uint32_t caps = pick_caps(size);
    void * p = heap_caps_malloc(size, caps);
    if(p == NULL) p = heap_caps_malloc(size, caps == LV_INT_CAPS ? LV_PSRAM_CAPS : LV_INT_CAPS);
    return p;
}

void * lv_realloc_core(void * p, size_t new_size)
{
    if(new_size == 0) {
        heap_caps_free(p);
        return NULL;
    }
    /* Keep a block where it is when it can grow in place; heap_caps_realloc
     * only moves it if the caps no longer match. Small blocks stay in their
     * current heap, large ones go to PSRAM. */
    uint32_t caps = (new_size >= LV_PSRAM_ALWAYS_BYTES) ? LV_PSRAM_CAPS
                    : (p != NULL && esp_ptr_external_ram(p)) ? LV_PSRAM_CAPS
                    : pick_caps(new_size);
    void * q = heap_caps_realloc(p, new_size, caps);
    if(q == NULL) q = heap_caps_realloc(p, new_size, caps == LV_INT_CAPS ? LV_PSRAM_CAPS : LV_INT_CAPS);
    return q;
}

void lv_free_core(void * p)
{
    heap_caps_free(p);
}

void lv_mem_monitor_core(lv_mem_monitor_t * mon_p)
{
    multi_heap_info_t info;
    heap_caps_get_info(&info, MALLOC_CAP_8BIT);
    mon_p->total_size        = info.total_free_bytes + info.total_allocated_bytes;
    mon_p->free_size         = info.total_free_bytes;
    mon_p->free_biggest_size = info.largest_free_block;
    mon_p->free_cnt          = info.free_blocks;
    mon_p->used_cnt          = info.allocated_blocks;
    mon_p->max_used          = mon_p->total_size - info.minimum_free_bytes;
    mon_p->used_pct          = mon_p->total_size ? (uint8_t)(100 - (100 * info.total_free_bytes) / mon_p->total_size) : 0;
    mon_p->frag_pct          = info.total_free_bytes ? (uint8_t)(100 - (100 * info.largest_free_block) / info.total_free_bytes) : 0;
}

lv_result_t lv_mem_test_core(void)
{
    return heap_caps_check_integrity(MALLOC_CAP_8BIT, false) ? LV_RESULT_OK : LV_RESULT_INVALID;
}

#endif /* LV_USE_STDLIB_MALLOC == LV_STDLIB_CUSTOM */
