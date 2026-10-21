    0x7e36, 0x7e5c, 0x7e5f, 0x7e9a, 0x7eb7, 0x7f4c, 0x7f8f, 0x7f9d,
    0x8015, 0x8046, 0x8093, 0x8133, 0x813c, 0x813d, 0x81cb, 0x81db,
    0x824c, 0x824d, 0x8292, 0x82b8, 0x8300, 0x8426, 0x8639, 0x8795,
    0x87b1, 0x87f4, 0x88ca, 0x890b, 0x890d, 0x8911, 0x891b, 0x8aea,
    0x8aed, 0x8af9, 0x8b07, 0x8b0a, 0x8b1e, 0x8b26, 0x8b2b, 0x8b2e,
    0x8b2f, 0x8b38, 0x8b40, 0x8b44, 0x8b4c, 0x8b80, 0x8c6e, 0x8c71,
    0x8c8d, 0x8cc0, 0x8cce, 0x8d3c, 0x8eb5, 0x8eb7, 0x8ec6, 0x8eda,
    0x8edc, 0x8f07, 0x8f10, 0x8f19, 0x8f1a, 0x8f1d, 0x8f21, 0x8f22,
    0x8f24, 0x8f27, 0x8f49, 0x8f52, 0x9031, 0x9096, 0x90db, 0x9113,
    0x9116, 0x9118, 0x91bd, 0x93e8, 0x940c, 0x9462, 0x9477, 0x94c8,
    0x9536, 0x9539, 0x953d, 0x9542, 0x9568, 0x957c, 0x957f, 0x9596,
    0x9599, 0x95ad, 0x95dd, 0x9631, 0x9633, 0x963f, 0x9649, 0x9665,
    0x967b, 0x969b, 0x96a2, 0x96ab, 0x973c, 0x97be, 0x97c2, 0x97cd,
    0x97da, 0x99d5, 0x9a21, 0x9e21, 0xfe51, 0xfe52, 0xfe55, 0xfe58,
    0xfe63, 0xfe64, 0xfe66, 0xfe68
};

/*Collect the unicode lists and glyph_id offsets*/
static const lv_font_fmt_txt_cmap_t cmaps[] =
{
    {
        .range_start = 32, .range_length = 95, .glyph_id_start = 1,
        .unicode_list = NULL, .glyph_id_ofs_list = NULL, .list_length = 0, .type = LV_FONT_FMT_TXT_CMAP_FORMAT0_TINY
    },
    {
        .range_start = 183, .range_length = 65129, .glyph_id_start = 96,
        .unicode_list = unicode_list_1, .glyph_id_ofs_list = NULL, .list_length = 468, .type = LV_FONT_FMT_TXT_CMAP_SPARSE_TINY
    }
};



/*--------------------
 *  ALL CUSTOM DATA
 *--------------------*/

#if LVGL_VERSION_MAJOR == 8
/*Store all the custom data of the font*/
static  lv_font_fmt_txt_glyph_cache_t cache;
#endif

#if LVGL_VERSION_MAJOR >= 8
static const lv_font_fmt_txt_dsc_t font_dsc = {
#else
static lv_font_fmt_txt_dsc_t font_dsc = {
#endif
    .glyph_bitmap = glyph_bitmap,
    .glyph_dsc = glyph_dsc,
    .cmaps = cmaps,
    .kern_dsc = NULL,
    .kern_scale = 0,
    .cmap_num = 2,
    .bpp = 4,
    .kern_classes = 0,
    .bitmap_format = 0,
#if LVGL_VERSION_MAJOR == 8
    .cache = &cache
#endif
};



/*-----------------
 *  PUBLIC FONT
 *----------------*/

/*Initialize a public general font descriptor*/
#if LVGL_VERSION_MAJOR >= 8
const lv_font_t product_ui_font_noto_sans_sc_12 = {
#else
lv_font_t product_ui_font_noto_sans_sc_12 = {
#endif
    .get_glyph_dsc = lv_font_get_glyph_dsc_fmt_txt,    /*Function pointer to get glyph's data*/
    .get_glyph_bitmap = lv_font_get_bitmap_fmt_txt,    /*Function pointer to get glyph's bitmap*/
    .line_height = 14,          /*The maximum line height required by the font*/
    .base_line = 3,             /*Baseline measured from the bottom of the line*/
#if !(LVGL_VERSION_MAJOR == 6 && LVGL_VERSION_MINOR == 0)
    .subpx = LV_FONT_SUBPX_NONE,
#endif
#if LV_VERSION_CHECK(7, 4, 0) || LVGL_VERSION_MAJOR >= 8
    .underline_position = -1,
    .underline_thickness = 1,
#endif
    .dsc = &font_dsc,          /*The custom font data. Will be accessed by `get_glyph_bitmap/dsc` */
#if LV_VERSION_CHECK(8, 2, 0) || LVGL_VERSION_MAJOR >= 9
    .fallback = &lv_font_montserrat_12,
#endif
    .user_data = NULL,
};
