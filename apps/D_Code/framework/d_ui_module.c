/**
 * @file d_ui_module.c
 * @brief Static UI page-module registry.
 */

#include "config/app_config.h"

#ifdef D_LVGL_ENABLE

#include "d_ui_module.h"
#include "d_page_types.h"
#include <string.h>

#define LOG_TAG "[UI_MODULE] "

//*********************************************************************************//
//                              内部状态                                            //
//*********************************************************************************//

static const d_ui_module_t *s_modules[D_MODULE_MAX_COUNT];
static int s_page_types[D_MODULE_MAX_COUNT];  /* 每个模块对应的页面类型 */
static int s_module_count = 0;
static int s_next_page_type = 1;  /* 从1开始，0保留给主页面 */

//*********************************************************************************//
//                              公共接口                                            //
//*********************************************************************************//

void d_ui_module_init(void)
{
    s_module_count = 0;
    s_next_page_type = 1;
    memset(s_modules, 0, sizeof(s_modules));
    memset(s_page_types, 0, sizeof(s_page_types));
    printf(LOG_TAG "UI module registry initialized (max %d modules)\n",
           D_MODULE_MAX_COUNT);
}

int d_ui_module_register(const d_ui_module_t *module)
{
    int page_type = PAGE_TYPE_INVALID;
    int register_ret = 0;

    if (module == NULL || module->id == NULL || module->register_page == NULL) {
        printf(LOG_TAG "Register failed: invalid UI module\n");
        return -1;
    }

    if (s_module_count >= D_MODULE_MAX_COUNT) {
        printf(LOG_TAG "Register failed: module list full\n");
        return -1;
    }

    /* 检查重复 */
    for (int i = 0; i < s_module_count; i++) {
        if (s_modules[i]->id && module->id &&
            strcmp(s_modules[i]->id, module->id) == 0) {
            printf(LOG_TAG "Register failed: module '%s' already registered\n", module->id);
            return -1;
        }
    }

    page_type = s_next_page_type;
    register_ret = module->register_page(page_type);
    if (register_ret != 0) {
        printf(LOG_TAG "Register failed: page '%s' register error=%d\n",
               module->id, register_ret);
        return -1;
    }
    s_next_page_type++;

    s_page_types[s_module_count] = page_type;
    s_modules[s_module_count] = module;
    s_module_count++;

    printf(LOG_TAG "UI module '%s' registered (page_type=%d, total=%d)\n",
           module->id, page_type, s_module_count);

    return page_type;
}

int d_ui_module_get_count(void)
{
    return s_module_count;
}

const d_ui_module_t *d_ui_module_get(int index)
{
    if (index < 0 || index >= s_module_count) {
        return NULL;
    }
    return s_modules[index];
}

int d_ui_module_get_page_type(int index)
{
    if (index < 0 || index >= s_module_count) {
        return -1;
    }
    return s_page_types[index];
}

d_err_t d_ui_module_init_all(void)
{
    const d_ui_module_t *module;
    const char *module_id;
    d_err_t init_result;

    for (int i = 0; i < s_module_count; i++) {
        module = s_modules[i];
        if (module == NULL || module->init == NULL) {
            continue;
        }
        module_id = (module->id != NULL) ? module->id : "<null>";
        printf(LOG_TAG "Initializing module '%s'...\n", module_id);
        init_result = module->init();
        if (init_result != D_OK) {
            printf(LOG_TAG "Module '%s' initialization failed: %d\n",
                   module_id, init_result);
            return init_result;
        }
    }

    return D_OK;
}

int d_ui_module_has_page(const d_ui_module_t *module)
{
    return (module && module->register_page) ? 1 : 0;
}

#endif /* D_LVGL_ENABLE */
