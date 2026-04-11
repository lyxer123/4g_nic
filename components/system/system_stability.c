#include "system_stability.h"

#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_system.h"
#include "esp_timer.h"
#include "sdkconfig.h"

static const char *TAG = "stability";

#if CONFIG_SYSTEM_STABILITY_HEAP_LOG_INTERVAL_S > 0
static void heap_log_cb(void *arg)
{
    (void)arg;
    const size_t free_now = esp_get_free_heap_size();
    const size_t min_ever = esp_get_minimum_free_heap_size();
    const size_t internal_free = heap_caps_get_free_size(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
    ESP_LOGI(TAG, "heap: free=%zu min_ever=%zu internal_free=%zu (lwIP/PPP pressure)", free_now, min_ever,
             internal_free);
#if CONFIG_SPIRAM
    ESP_LOGI(TAG, "psram free=%zu", (size_t)heap_caps_get_free_size(MALLOC_CAP_SPIRAM));
#endif
}
#endif

esp_err_t system_stability_init(void)
{
#if CONFIG_SYSTEM_STABILITY_HEAP_LOG_INTERVAL_S > 0
    const uint64_t us = (uint64_t)CONFIG_SYSTEM_STABILITY_HEAP_LOG_INTERVAL_S * 1000000ULL;
    const esp_timer_create_args_t args = {
        .callback = &heap_log_cb,
        .arg = NULL,
        .dispatch_method = ESP_TIMER_TASK,
        .name = "heap_log",
        .skip_unhandled_events = true,
    };
    esp_timer_handle_t t = NULL;
    esp_err_t e = esp_timer_create(&args, &t);
    if (e != ESP_OK) {
        return e;
    }
    e = esp_timer_start_periodic(t, us);
    if (e != ESP_OK) {
        return e;
    }
    ESP_LOGI(TAG, "periodic heap log every %ds", CONFIG_SYSTEM_STABILITY_HEAP_LOG_INTERVAL_S);
#endif
    return ESP_OK;
}
