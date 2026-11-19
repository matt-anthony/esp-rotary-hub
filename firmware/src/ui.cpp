#include "ui.h"

static lv_obj_t *scr_media = nullptr;
static lv_obj_t *scr_agent = nullptr;
static lv_obj_t *scr_approval = nullptr;

// Media screen widgets (360x360 circular layout)
static lv_obj_t *m_img_art = nullptr;
static uint8_t *m_art_buf = nullptr;
static lv_img_dsc_t m_art_dsc;
static lv_obj_t *m_arc_vol = nullptr;
static lv_obj_t *m_lbl_app = nullptr;
static lv_obj_t *m_lbl_title = nullptr;
static lv_obj_t *m_lbl_artist = nullptr;
static lv_obj_t *m_bar_prog = nullptr;
static lv_obj_t *m_btn_play = nullptr;
static lv_obj_t *m_lbl_play = nullptr;
static lv_obj_t *m_lbl_vol = nullptr;
static lv_obj_t *m_btn_agent_pill = nullptr;
static lv_obj_t *m_lbl_agent_pill = nullptr;

// Agent screen widgets
static lv_obj_t *a_arc_ring = nullptr;
static lv_obj_t *a_lbl_platform = nullptr;
static lv_obj_t *a_lbl_state = nullptr;
static lv_obj_t *a_lbl_title = nullptr;
static lv_obj_t *a_lbl_detail = nullptr;

// Approval screen widgets
static lv_obj_t *p_arc_ring = nullptr;
static lv_obj_t *p_lbl_platform = nullptr;
static lv_obj_t *p_lbl_tool = nullptr;
static lv_obj_t *p_lbl_detail = nullptr;
static lv_obj_t *p_btns[3] = {nullptr, nullptr, nullptr};

static UiView current_view = VIEW_MEDIA;
static UiView prev_main_view = VIEW_MEDIA;
static AgentState current_agent_state = AGENT_IDLE;
static String active_platform = "antigravity";
static ApprovalRequest active_approval = {false, "", "", "", "", 0};

static UiMediaCmdCb cb_media = nullptr;
static UiApprovalDecisionCb cb_approval = nullptr;
static UiFocusIdeCb cb_focus = nullptr;

// Color palette
static const uint32_t COL_BG      = 0x0B0E14;
static const uint32_t COL_CARD    = 0x161B26;
static const uint32_t COL_CYAN    = 0x22D3EE;
static const uint32_t COL_BLUE    = 0x3B82F6;
static const uint32_t COL_GREEN   = 0x10B981;
static const uint32_t COL_AMBER   = 0xF59E0B;
static const uint32_t COL_RED     = 0xEF4444;
static const uint32_t COL_MUTED   = 0x64748B;
static const uint32_t COL_TEXT    = 0xF8FAFC;
static const uint32_t COL_SUBTEXT = 0x94A3B8;

static void style_screen(lv_obj_t *scr) {
  lv_obj_set_style_bg_color(scr, lv_color_hex(COL_BG), 0);
  lv_obj_set_style_bg_opa(scr, LV_OPA_COVER, 0);
  lv_obj_clear_flag(scr, LV_OBJ_FLAG_SCROLLABLE);
}

static void on_prev_click(lv_event_t *e) {
  (void)e;
  if (cb_media) cb_media("prev");
}

static void on_play_click(lv_event_t *e) {
  (void)e;
  if (cb_media) cb_media("toggle");
}

static void on_next_click(lv_event_t *e) {
  (void)e;
  if (cb_media) cb_media("next");
}

static void on_focus_click(lv_event_t *e) {
  (void)e;
  if (cb_focus) cb_focus(active_platform.c_str());
}

static void on_agent_pill_click(lv_event_t *e) {
  (void)e;
  if (current_view == VIEW_MEDIA) {
    ui_toggle_main_view();
  }
}

static void on_approval_btn_click(lv_event_t *e) {
  int idx = (int)(intptr_t)lv_event_get_user_data(e);
  active_approval.selectedOption = idx;
  ui_approval_confirm();
}

static void update_approval_selection_styles() {
  for (int i = 0; i < 3; i++) {
    if (!p_btns[i]) continue;
    if (i == active_approval.selectedOption) {
      lv_obj_set_style_border_width(p_btns[i], 3, 0);
      lv_obj_set_style_border_color(p_btns[i], lv_color_hex(0xFFFFFF), 0);
      lv_obj_set_style_bg_opa(p_btns[i], LV_OPA_COVER, 0);
    } else {
      lv_obj_set_style_border_width(p_btns[i], 0, 0);
      lv_obj_set_style_bg_opa(p_btns[i], LV_OPA_50, 0);
    }
  }
}

static void build_media_screen() {
  scr_media = lv_obj_create(nullptr);
  style_screen(scr_media);

  // Full-bleed album artwork background (180x180 upscaled 2x -> 360x360)
  m_art_buf = (uint8_t *)ps_malloc(ART_BYTES);
  if (m_art_buf) {
    memset(m_art_buf, 0, ART_BYTES);
    m_art_dsc.header.always_zero = 0;
    m_art_dsc.header.w = ART_WIDTH;
    m_art_dsc.header.h = ART_HEIGHT;
    m_art_dsc.header.cf = LV_IMG_CF_TRUE_COLOR;
    m_art_dsc.data_size = ART_BYTES;
    m_art_dsc.data = m_art_buf;

    m_img_art = lv_img_create(scr_media);
    lv_img_set_src(m_img_art, &m_art_dsc);
    lv_img_set_antialias(m_img_art, false);
    lv_img_set_zoom(m_img_art, 768);  // 3.0x scale: 120x120 -> 360x360
    lv_obj_center(m_img_art);
    lv_obj_clear_flag(m_img_art, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_flag(m_img_art, LV_OBJ_FLAG_HIDDEN);
  }

  // Outer volume arc (344x344 for 360x360 round panel)
  m_arc_vol = lv_arc_create(scr_media);
  lv_obj_set_size(m_arc_vol, 344, 344);
  lv_obj_center(m_arc_vol);
  lv_arc_set_rotation(m_arc_vol, 135);
  lv_arc_set_bg_angles(m_arc_vol, 0, 270);
  lv_arc_set_range(m_arc_vol, 0, 100);
  lv_arc_set_value(m_arc_vol, 35);
  lv_obj_remove_style(m_arc_vol, nullptr, LV_PART_KNOB);
  lv_obj_clear_flag(m_arc_vol, LV_OBJ_FLAG_CLICKABLE);
  lv_obj_set_style_arc_width(m_arc_vol, 10, LV_PART_MAIN);
  lv_obj_set_style_arc_width(m_arc_vol, 10, LV_PART_INDICATOR);
  lv_obj_set_style_arc_color(m_arc_vol, lv_color_hex(0x1E293B), LV_PART_MAIN);
  lv_obj_set_style_arc_color(m_arc_vol, lv_color_hex(COL_CYAN), LV_PART_INDICATOR);

  // Top app badge
  m_lbl_app = lv_label_create(scr_media);
  lv_label_set_text(m_lbl_app, "MAC MEDIA");
  lv_obj_set_style_text_font(m_lbl_app, &lv_font_montserrat_14, 0);
  lv_obj_set_style_text_color(m_lbl_app, lv_color_hex(COL_CYAN), 0);
  lv_obj_align(m_lbl_app, LV_ALIGN_TOP_MID, 0, 40);

  // Compact live coding-agent status pill (shown when an agent is active)
  m_btn_agent_pill = lv_btn_create(scr_media);
  lv_obj_set_size(m_btn_agent_pill, 148, 24);
  lv_obj_set_style_radius(m_btn_agent_pill, 12, 0);
  lv_obj_set_style_bg_color(m_btn_agent_pill, lv_color_hex(COL_CARD), 0);
  lv_obj_set_style_bg_opa(m_btn_agent_pill, LV_OPA_80, 0);
  lv_obj_set_style_border_width(m_btn_agent_pill, 1, 0);
  lv_obj_set_style_border_color(m_btn_agent_pill, lv_color_hex(COL_BLUE), 0);
  lv_obj_align(m_btn_agent_pill, LV_ALIGN_TOP_MID, 0, 64);
  lv_obj_add_event_cb(m_btn_agent_pill, on_agent_pill_click, LV_EVENT_CLICKED, nullptr);
  lv_obj_add_flag(m_btn_agent_pill, LV_OBJ_FLAG_HIDDEN);

  m_lbl_agent_pill = lv_label_create(m_btn_agent_pill);
  lv_label_set_text(m_lbl_agent_pill, "AGENT READY");
  lv_obj_set_style_text_font(m_lbl_agent_pill, &lv_font_montserrat_12, 0);
  lv_obj_set_style_text_color(m_lbl_agent_pill, lv_color_hex(COL_TEXT), 0);
  lv_obj_center(m_lbl_agent_pill);

  // Track title (scrolling marquee)
  m_lbl_title = lv_label_create(scr_media);
  lv_label_set_long_mode(m_lbl_title, LV_LABEL_LONG_SCROLL_CIRCULAR);
  lv_obj_set_width(m_lbl_title, 260);
  lv_obj_set_style_text_align(m_lbl_title, LV_TEXT_ALIGN_CENTER, 0);
  lv_obj_set_style_text_font(m_lbl_title, &lv_font_montserrat_20, 0);
  lv_obj_set_style_text_color(m_lbl_title, lv_color_hex(COL_TEXT), 0);
  lv_label_set_text(m_lbl_title, "Waiting for Mac...");
  lv_obj_align(m_lbl_title, LV_ALIGN_CENTER, 0, -64);

  // Artist
  m_lbl_artist = lv_label_create(scr_media);
  lv_label_set_long_mode(m_lbl_artist, LV_LABEL_LONG_SCROLL_CIRCULAR);
  lv_obj_set_width(m_lbl_artist, 240);
  lv_obj_set_style_text_align(m_lbl_artist, LV_TEXT_ALIGN_CENTER, 0);
  lv_obj_set_style_text_font(m_lbl_artist, &lv_font_montserrat_16, 0);
  lv_obj_set_style_text_color(m_lbl_artist, lv_color_hex(COL_SUBTEXT), 0);
  lv_label_set_text(m_lbl_artist, "Run rotary_daemon.py");
  lv_obj_align(m_lbl_artist, LV_ALIGN_CENTER, 0, -30);

  // Playback progress bar
  m_bar_prog = lv_bar_create(scr_media);
  lv_obj_set_size(m_bar_prog, 196, 6);
  lv_bar_set_range(m_bar_prog, 0, 100);
  lv_bar_set_value(m_bar_prog, 0, LV_ANIM_OFF);
  lv_obj_set_style_bg_color(m_bar_prog, lv_color_hex(0x1E293B), LV_PART_MAIN);
  lv_obj_set_style_bg_color(m_bar_prog, lv_color_hex(COL_CYAN), LV_PART_INDICATOR);
  lv_obj_align(m_bar_prog, LV_ALIGN_CENTER, 0, 0);

  // Media control buttons: Prev, Play/Pause, Next
  lv_obj_t *btn_prev = lv_btn_create(scr_media);
  lv_obj_set_size(btn_prev, 56, 56);
  lv_obj_set_style_radius(btn_prev, LV_RADIUS_CIRCLE, 0);
  lv_obj_set_style_bg_color(btn_prev, lv_color_hex(COL_CARD), 0);
  lv_obj_align(btn_prev, LV_ALIGN_CENTER, -84, 52);
  lv_obj_add_event_cb(btn_prev, on_prev_click, LV_EVENT_CLICKED, nullptr);
  lv_obj_t *lbl_prev = lv_label_create(btn_prev);
  lv_label_set_text(lbl_prev, LV_SYMBOL_PREV);
  lv_obj_set_style_text_font(lbl_prev, &lv_font_montserrat_20, 0);
  lv_obj_center(lbl_prev);

  m_btn_play = lv_btn_create(scr_media);
  lv_obj_set_size(m_btn_play, 72, 72);
  lv_obj_set_style_radius(m_btn_play, LV_RADIUS_CIRCLE, 0);
  lv_obj_set_style_bg_color(m_btn_play, lv_color_hex(COL_CYAN), 0);
  lv_obj_align(m_btn_play, LV_ALIGN_CENTER, 0, 52);
  lv_obj_add_event_cb(m_btn_play, on_play_click, LV_EVENT_CLICKED, nullptr);
  m_lbl_play = lv_label_create(m_btn_play);
  lv_label_set_text(m_lbl_play, LV_SYMBOL_PLAY);
  lv_obj_set_style_text_color(m_lbl_play, lv_color_hex(COL_BG), 0);
  lv_obj_set_style_text_font(m_lbl_play, &lv_font_montserrat_28, 0);
  lv_obj_center(m_lbl_play);

  lv_obj_t *btn_next = lv_btn_create(scr_media);
  lv_obj_set_size(btn_next, 56, 56);
  lv_obj_set_style_radius(btn_next, LV_RADIUS_CIRCLE, 0);
  lv_obj_set_style_bg_color(btn_next, lv_color_hex(COL_CARD), 0);
  lv_obj_align(btn_next, LV_ALIGN_CENTER, 84, 52);
  lv_obj_add_event_cb(btn_next, on_next_click, LV_EVENT_CLICKED, nullptr);
  lv_obj_t *lbl_next = lv_label_create(btn_next);
  lv_label_set_text(lbl_next, LV_SYMBOL_NEXT);
  lv_obj_set_style_text_font(lbl_next, &lv_font_montserrat_20, 0);
  lv_obj_center(lbl_next);

  // Bottom volume readout (safely inside circular chord)
  m_lbl_vol = lv_label_create(scr_media);
  lv_label_set_text(m_lbl_vol, "VOL --%");
  lv_obj_set_style_text_font(m_lbl_vol, &lv_font_montserrat_20, 0);
  lv_obj_set_style_text_color(m_lbl_vol, lv_color_hex(COL_TEXT), 0);
  lv_obj_align(m_lbl_vol, LV_ALIGN_BOTTOM_MID, 0, -38);
}

static void build_agent_screen() {
  scr_agent = lv_obj_create(nullptr);
  style_screen(scr_agent);

  // Full 360 status ring
  a_arc_ring = lv_arc_create(scr_agent);
  lv_obj_set_size(a_arc_ring, 344, 344);
  lv_obj_center(a_arc_ring);
  lv_arc_set_bg_angles(a_arc_ring, 0, 360);
  lv_arc_set_value(a_arc_ring, 100);
  lv_obj_remove_style(a_arc_ring, nullptr, LV_PART_KNOB);
  lv_obj_clear_flag(a_arc_ring, LV_OBJ_FLAG_CLICKABLE);
  lv_obj_set_style_arc_width(a_arc_ring, 9, LV_PART_MAIN);
  lv_obj_set_style_arc_width(a_arc_ring, 9, LV_PART_INDICATOR);
  lv_obj_set_style_arc_color(a_arc_ring, lv_color_hex(0x1E293B), LV_PART_MAIN);
  lv_obj_set_style_arc_color(a_arc_ring, lv_color_hex(COL_MUTED), LV_PART_INDICATOR);

  a_lbl_platform = lv_label_create(scr_agent);
  lv_label_set_long_mode(a_lbl_platform, LV_LABEL_LONG_SCROLL_CIRCULAR);
  lv_obj_set_width(a_lbl_platform, 156);
  lv_obj_set_style_text_align(a_lbl_platform, LV_TEXT_ALIGN_CENTER, 0);
  lv_label_set_text(a_lbl_platform, "ANTIGRAVITY");
  lv_obj_set_style_text_font(a_lbl_platform, &lv_font_montserrat_14, 0);
  lv_obj_set_style_text_color(a_lbl_platform, lv_color_hex(COL_BLUE), 0);
  lv_obj_align(a_lbl_platform, LV_ALIGN_TOP_MID, 0, 46);

  a_lbl_state = lv_label_create(scr_agent);
  lv_label_set_text(a_lbl_state, "IDLE");
  lv_obj_set_style_text_font(a_lbl_state, &lv_font_montserrat_28, 0);
  lv_obj_set_style_text_color(a_lbl_state, lv_color_hex(COL_TEXT), 0);
  lv_obj_align(a_lbl_state, LV_ALIGN_CENTER, 0, -52);

  a_lbl_title = lv_label_create(scr_agent);
  lv_label_set_long_mode(a_lbl_title, LV_LABEL_LONG_SCROLL_CIRCULAR);
  lv_obj_set_width(a_lbl_title, 250);
  lv_obj_set_style_text_align(a_lbl_title, LV_TEXT_ALIGN_CENTER, 0);
  lv_obj_set_style_text_font(a_lbl_title, &lv_font_montserrat_20, 0);
  lv_obj_set_style_text_color(a_lbl_title, lv_color_hex(COL_TEXT), 0);
  lv_label_set_text(a_lbl_title, "No active task");
  lv_obj_align(a_lbl_title, LV_ALIGN_CENTER, 0, -4);

  a_lbl_detail = lv_label_create(scr_agent);
  lv_label_set_long_mode(a_lbl_detail, LV_LABEL_LONG_WRAP);
  lv_obj_set_width(a_lbl_detail, 236);
  lv_obj_set_style_max_height(a_lbl_detail, 56, 0);
  lv_obj_set_style_text_align(a_lbl_detail, LV_TEXT_ALIGN_CENTER, 0);
  lv_obj_set_style_text_font(a_lbl_detail, &lv_font_montserrat_14, 0);
  lv_obj_set_style_text_color(a_lbl_detail, lv_color_hex(COL_SUBTEXT), 0);
  lv_label_set_text(a_lbl_detail, "Swipe or long-press for Media");
  lv_obj_align(a_lbl_detail, LV_ALIGN_CENTER, 0, 42);

  lv_obj_t *btn_focus = lv_btn_create(scr_agent);
  lv_obj_set_size(btn_focus, 132, 40);
  lv_obj_set_style_radius(btn_focus, 20, 0);
  lv_obj_set_style_bg_color(btn_focus, lv_color_hex(COL_CARD), 0);
  lv_obj_set_style_border_width(btn_focus, 2, 0);
  lv_obj_set_style_border_color(btn_focus, lv_color_hex(COL_BLUE), 0);
  lv_obj_align(btn_focus, LV_ALIGN_BOTTOM_MID, 0, -48);
  lv_obj_add_event_cb(btn_focus, on_focus_click, LV_EVENT_CLICKED, nullptr);
  lv_obj_t *lbl_focus = lv_label_create(btn_focus);
  lv_label_set_text(lbl_focus, "FOCUS IDE");
  lv_obj_set_style_text_font(lbl_focus, &lv_font_montserrat_14, 0);
  lv_obj_center(lbl_focus);
}

static void build_approval_screen() {
  scr_approval = lv_obj_create(nullptr);
  style_screen(scr_approval);

  p_arc_ring = lv_arc_create(scr_approval);
  lv_obj_set_size(p_arc_ring, 344, 344);
  lv_obj_center(p_arc_ring);
  lv_arc_set_bg_angles(p_arc_ring, 0, 360);
  lv_arc_set_value(p_arc_ring, 100);
  lv_obj_remove_style(p_arc_ring, nullptr, LV_PART_KNOB);
  lv_obj_clear_flag(p_arc_ring, LV_OBJ_FLAG_CLICKABLE);
  lv_obj_set_style_arc_width(p_arc_ring, 10, LV_PART_MAIN);
  lv_obj_set_style_arc_width(p_arc_ring, 10, LV_PART_INDICATOR);
  lv_obj_set_style_arc_color(p_arc_ring, lv_color_hex(0x1E293B), LV_PART_MAIN);
  lv_obj_set_style_arc_color(p_arc_ring, lv_color_hex(COL_AMBER), LV_PART_INDICATOR);

  p_lbl_platform = lv_label_create(scr_approval);
  lv_label_set_long_mode(p_lbl_platform, LV_LABEL_LONG_SCROLL_CIRCULAR);
  lv_obj_set_width(p_lbl_platform, 156);
  lv_obj_set_style_text_align(p_lbl_platform, LV_TEXT_ALIGN_CENTER, 0);
  lv_label_set_text(p_lbl_platform, "APPROVAL");
  lv_obj_set_style_text_font(p_lbl_platform, &lv_font_montserrat_14, 0);
  lv_obj_set_style_text_color(p_lbl_platform, lv_color_hex(COL_AMBER), 0);
  lv_obj_align(p_lbl_platform, LV_ALIGN_TOP_MID, 0, 44);

  p_lbl_tool = lv_label_create(scr_approval);
  lv_label_set_long_mode(p_lbl_tool, LV_LABEL_LONG_SCROLL_CIRCULAR);
  lv_obj_set_width(p_lbl_tool, 232);
  lv_obj_set_style_text_align(p_lbl_tool, LV_TEXT_ALIGN_CENTER, 0);
  lv_obj_set_style_text_font(p_lbl_tool, &lv_font_montserrat_20, 0);
  lv_obj_set_style_text_color(p_lbl_tool, lv_color_hex(COL_TEXT), 0);
  lv_label_set_text(p_lbl_tool, "run_command");
  lv_obj_align(p_lbl_tool, LV_ALIGN_CENTER, 0, -68);

  p_lbl_detail = lv_label_create(scr_approval);
  lv_label_set_long_mode(p_lbl_detail, LV_LABEL_LONG_WRAP);
  lv_obj_set_width(p_lbl_detail, 244);
  lv_obj_set_style_max_height(p_lbl_detail, 66, 0);
  lv_obj_set_style_text_align(p_lbl_detail, LV_TEXT_ALIGN_CENTER, 0);
  lv_obj_set_style_text_font(p_lbl_detail, &lv_font_montserrat_14, 0);
  lv_obj_set_style_text_color(p_lbl_detail, lv_color_hex(0xCBD5E1), 0);
  lv_label_set_text(p_lbl_detail, "");
  lv_obj_align(p_lbl_detail, LV_ALIGN_CENTER, 0, -18);

  const char *labels[3] = {"ALLOW", "DENY", "IDE"};
  const uint32_t colors[3] = {COL_GREEN, COL_RED, COL_BLUE};
  const int xs[3] = {-74, 0, 72};
  const int ws[3] = {72, 68, 56};

  for (int i = 0; i < 3; i++) {
    p_btns[i] = lv_btn_create(scr_approval);
    lv_obj_set_size(p_btns[i], ws[i], 42);
    lv_obj_set_style_radius(p_btns[i], 18, 0);
    lv_obj_set_style_bg_color(p_btns[i], lv_color_hex(colors[i]), 0);
    lv_obj_align(p_btns[i], LV_ALIGN_BOTTOM_MID, xs[i], -72);
    lv_obj_add_event_cb(p_btns[i], on_approval_btn_click, LV_EVENT_CLICKED, (void *)(intptr_t)i);

    lv_obj_t *lbl = lv_label_create(p_btns[i]);
    lv_label_set_text(lbl, labels[i]);
    lv_obj_set_style_text_font(lbl, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(lbl, lv_color_hex(0xFFFFFF), 0);
    lv_obj_center(lbl);
  }
  update_approval_selection_styles();
}

void ui_init(UiMediaCmdCb mediaCb, UiApprovalDecisionCb approvalCb, UiFocusIdeCb focusCb) {
  cb_media = mediaCb;
  cb_approval = approvalCb;
  cb_focus = focusCb;

  build_media_screen();
  build_agent_screen();
  build_approval_screen();

  current_view = VIEW_MEDIA;
  prev_main_view = VIEW_MEDIA;
  lv_scr_load(scr_media);
}

void ui_set_connected(bool connected) {
  if (!connected) {
    lv_label_set_text(m_lbl_app, "USB WAITING");
    lv_obj_set_style_text_color(m_lbl_app, lv_color_hex(COL_MUTED), 0);
  }
}

void ui_update_volume_local(int vol, bool muted) {
  if (vol < 0) vol = 0;
  if (vol > 100) vol = 100;
  lv_arc_set_value(m_arc_vol, vol);
  if (muted) {
    lv_label_set_text(m_lbl_vol, "MUTED");
    lv_obj_set_style_arc_color(m_arc_vol, lv_color_hex(COL_MUTED), LV_PART_INDICATOR);
  } else {
    lv_label_set_text_fmt(m_lbl_vol, "VOL %d%%", vol);
    lv_obj_set_style_arc_color(m_arc_vol, lv_color_hex(COL_CYAN), LV_PART_INDICATOR);
  }
}

void ui_update_media(const MediaInfo &info) {
  String appUpper = info.app.length() ? info.app : "MAC MEDIA";
  appUpper.toUpperCase();
  lv_label_set_text(m_lbl_app, appUpper.c_str());
  lv_obj_set_style_text_color(m_lbl_app, lv_color_hex(info.playing ? COL_CYAN : COL_SUBTEXT), 0);

  lv_label_set_text(m_lbl_title, info.title.length() ? info.title.c_str() : "Nothing Playing");
  lv_label_set_text(m_lbl_artist, info.artist.length() ? info.artist.c_str() : "macOS Now Playing");
  lv_bar_set_value(m_bar_prog, constrain(info.progress, 0, 100), LV_ANIM_OFF);
  lv_label_set_text(m_lbl_play, info.playing ? LV_SYMBOL_PAUSE : LV_SYMBOL_PLAY);
  ui_update_volume_local(info.volume, info.muted);
}

void ui_update_agent(const AgentInfo &info) {
  current_agent_state = info.state;
  if (info.platform.length()) active_platform = info.platform;

  String platUpper = active_platform;
  platUpper.toUpperCase();
  lv_label_set_text(a_lbl_platform, platUpper.c_str());

  uint32_t col = COL_MUTED;
  const char *stateTxt = "IDLE";
  switch (info.state) {
    case AGENT_WORKING: col = COL_BLUE;  stateTxt = "WORKING"; break;
    case AGENT_DONE:    col = COL_GREEN; stateTxt = "DONE";    break;
    case AGENT_ERROR:   col = COL_RED;   stateTxt = "ERROR";   break;
    case AGENT_WAITING: col = COL_AMBER; stateTxt = "WAITING"; break;
    default:            col = COL_MUTED; stateTxt = "IDLE";    break;
  }

  lv_obj_set_style_arc_color(a_arc_ring, lv_color_hex(col), LV_PART_INDICATOR);
  lv_obj_set_style_text_color(a_lbl_platform, lv_color_hex(col), 0);
  lv_obj_set_style_text_color(a_lbl_state, lv_color_hex(col), 0);
  lv_label_set_text(a_lbl_state, stateTxt);
  lv_label_set_text(a_lbl_title, info.title.length() ? info.title.c_str() : "Ready");
  lv_label_set_text(a_lbl_detail, info.detail.c_str());

  // Update the compact status pill on the Media screen so coding updates are
  // always visible even while watching Now Playing.
  if (m_btn_agent_pill && m_lbl_agent_pill) {
    if (info.state == AGENT_IDLE && info.title == "Connected") {
      lv_obj_add_flag(m_btn_agent_pill, LV_OBJ_FLAG_HIDDEN);
    } else {
      String shortPlat = active_platform;
      if (shortPlat == "antigravity") shortPlat = "AGY";
      else if (shortPlat == "claude-code") shortPlat = "CLAUDE";
      else shortPlat.toUpperCase();

      String pillTxt = shortPlat + ": " + String(stateTxt);
      lv_label_set_text(m_lbl_agent_pill, pillTxt.c_str());
      lv_obj_set_style_border_color(m_btn_agent_pill, lv_color_hex(col), 0);
      lv_obj_set_style_text_color(m_lbl_agent_pill, lv_color_hex(col), 0);
      lv_obj_clear_flag(m_btn_agent_pill, LV_OBJ_FLAG_HIDDEN);
    }
  }

  // Auto-surface the Agent screen on actionable states (done, error, waiting)
  // so coding notifications are never missed.
  if (!active_approval.active && current_view == VIEW_MEDIA &&
      (info.state == AGENT_DONE || info.state == AGENT_ERROR || info.state == AGENT_WAITING)) {
    current_view = VIEW_AGENT;
    prev_main_view = VIEW_AGENT;
    lv_scr_load_anim(scr_agent, LV_SCR_LOAD_ANIM_MOVE_LEFT, 160, 0, false);
  }
}

void ui_show_approval(const ApprovalRequest &req) {
  active_approval = req;
  active_approval.active = true;
  active_approval.selectedOption = 0;  // Default to ALLOW
  if (req.platform.length()) active_platform = req.platform;

  String hdr = active_platform;
  hdr.toUpperCase();
  hdr += " APPROVAL";
  lv_label_set_text(p_lbl_platform, hdr.c_str());
  lv_label_set_text(p_lbl_tool, req.tool.length() ? req.tool.c_str() : "Action");
  lv_label_set_text(p_lbl_detail, req.detail.c_str());
  update_approval_selection_styles();

  if (current_view != VIEW_APPROVAL) {
    prev_main_view = current_view;
    current_view = VIEW_APPROVAL;
    lv_scr_load_anim(scr_approval, LV_SCR_LOAD_ANIM_FADE_ON, 120, 0, false);
  }
}

void ui_clear_approval() {
  active_approval.active = false;
  if (current_view == VIEW_APPROVAL) {
    current_view = prev_main_view;
    lv_scr_load_anim(current_view == VIEW_MEDIA ? scr_media : scr_agent,
                     LV_SCR_LOAD_ANIM_FADE_ON, 120, 0, false);
  }
}

void ui_approval_move_selection(int delta) {
  if (!active_approval.active) return;
  int next = (active_approval.selectedOption + delta) % 3;
  if (next < 0) next += 3;
  active_approval.selectedOption = next;
  update_approval_selection_styles();
}

void ui_approval_confirm() {
  if (!active_approval.active) return;
  const char *decisions[3] = {"allow", "deny", "focus"};
  String idCopy = active_approval.id;
  const char *dec = decisions[constrain(active_approval.selectedOption, 0, 2)];
  ui_clear_approval();
  if (cb_approval) cb_approval(idCopy.c_str(), dec);
}

void ui_toggle_main_view() {
  if (current_view == VIEW_APPROVAL) return;
  if (current_view == VIEW_MEDIA) {
    current_view = VIEW_AGENT;
    prev_main_view = VIEW_AGENT;
    lv_scr_load_anim(scr_agent, LV_SCR_LOAD_ANIM_MOVE_LEFT, 160, 0, false);
  } else {
    current_view = VIEW_MEDIA;
    prev_main_view = VIEW_MEDIA;
    lv_scr_load_anim(scr_media, LV_SCR_LOAD_ANIM_MOVE_RIGHT, 160, 0, false);
  }
}

uint8_t *ui_get_art_buffer() { return m_art_buf; }

void ui_commit_art() {
  if (!m_img_art || !m_art_buf) return;
  lv_obj_clear_flag(m_img_art, LV_OBJ_FLAG_HIDDEN);
  lv_img_set_src(m_img_art, &m_art_dsc);
  lv_obj_invalidate(m_img_art);
}

void ui_clear_art() {
  if (!m_img_art) return;
  lv_obj_add_flag(m_img_art, LV_OBJ_FLAG_HIDDEN);
}

UiView ui_get_current_view() { return current_view; }
AgentState ui_get_agent_state() { return current_agent_state; }
bool ui_is_approval_active() { return active_approval.active; }
const char *ui_get_active_platform() { return active_platform.c_str(); }
