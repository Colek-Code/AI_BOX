/**
 * @file product_ui_perf.h
 * @brief Optional Product UI aggregate performance diagnostics.
 */

#ifndef __PRODUCT_UI_PERF_H__
#define __PRODUCT_UI_PERF_H__

#include <stdint.h>

void product_ui_perf_tick(uint32_t elapsed_ms);
void product_ui_perf_cycle(uint32_t update_ms, uint32_t render_ms);
void product_ui_perf_page_created(const char *name, uint32_t started_at_ms);

#endif /* __PRODUCT_UI_PERF_H__ */
