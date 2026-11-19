#include "ui.h"
#include <math.h>

static lv_obj_t *scr_media = nullptr;
static lv_obj_t *scr_agent = nullptr;
static lv_obj_t *scr_blender = nullptr;
static lv_obj_t *scr_approval = nullptr;

// Media screen widgets (360x360 circular layout)
static lv_obj_t *m_img_art = nullptr;
static uint8_t *m_art_buf = nullptr;
static lv_img_dsc_t m_art_dsc;
static lv_obj_t *m_arc_vol = nullptr;
static lv_obj_t *m_lbl_app = nullptr;
static lv_obj_t *m_lbl_title = nullptr;
static lv_obj_t *m_lbl_artist = nullptr;
static lv_obj_t *m_wave_box = nullptr;
static lv_obj_t *m_wave_active = nullptr;
static lv_obj_t *m_wave_rest = nullptr;
static lv_obj_t *m_wave_thumb = nullptr;
static lv_point_t m_wave_pts[48];
static lv_point_t m_rest_pts[2];
static float m_wave_phase = 0.0f;
static float m_wave_amp = 0.0f;
static int m_media_progress = 0;
static bool m_media_playing = false;
static uint32_t m_accent_hex = 0x22D3EE;
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

// Blender 3D navigation widgets
static lv_obj_t *b_arc_ring = nullptr;
static lv_obj_t *b_pad = nullptr;
static lv_obj_t *b_lbl_mode = nullptr;
static lv_obj_t *b_lbl_hint = nullptr;
static BlenderMode current_blender_mode = BLENDER_ORBIT;

static UiView current_view = VIEW_MEDIA;
static UiView prev_main_view = VIEW_MEDIA;
static AgentState current_agent_state = AGENT_IDLE;
static String active_platform = "antigravity";
static ApprovalRequest active_approval = {false, "", "", "", "", 0};

static UiMediaCmdCb cb_media = nullptr;
static UiApprovalDecisionCb cb_approval = nullptr;
static UiFocusIdeCb cb_focus = nullptr;
static UiBlenderCb cb_blender = nullptr;

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

static void on_media_btn_click(lv_event_t *e) {
  (void)e;
  if (current_view == VIEW_AGENT || current_view == VIEW_BLENDER) {
    current_view = VIEW_MEDIA;
    prev_main_view = VIEW_MEDIA;
    lv_scr_load_anim(scr_media, LV_SCR_LOAD_ANIM_MOVE_RIGHT, 160, 0, false);
  }
}

static void on_blender_snap_click(lv_event_t *e) {
  const char *viewName = (const char *)lv_event_get_user_data(e);
  if (cb_blender && viewName) cb_blender("snap", viewName, 0, 0);
}

static void on_blender_pad_event(lv_event_t *e) {
  static bool dragging = false;
  static lv_point_t lastPt = {0, 0};
  static unsigned long lastSendMs = 0;

  lv_event_code_t code = lv_event_get_code(e);
  lv_indev_t *indev = lv_indev_get_act();
  if (!indev) return;

  lv_point_t pt;
  lv_indev_get_point(indev, &pt);

  if (code == LV_EVENT_PRESSED) {
    dragging = true;
    lastPt = pt;
    lastSendMs = millis();
  } else if (code == LV_EVENT_PRESSING && dragging) {
    unsigned long now = millis();
    int dx = pt.x - lastPt.x;
    int dy = pt.y - lastPt.y;
    if ((abs(dx) >= 2 || abs(dy) >= 2) && (now - lastSendMs >= 28)) {
      lastPt = pt;
      lastSendMs = now;
      if (cb_blender) cb_blender("drag", ui_get_blender_mode_str(), dx, dy);
    }
  } else if (code == LV_EVENT_RELEASED || code == LV_EVENT_PRESS_LOST) {
    if (dragging) {
      dragging = false;
      if (cb_blender) cb_blender("end", ui_get_blender_mode_str(), 0, 0);
    }
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

static uint32_t brighten_hex(uint8_t r, uint8_t g, uint8_t b) {
  // Ensure dynamic Material color has vivid contrast on dark OLED/IPS backgrounds
  int mx = max((int)r, max((int)g, (int)b));
  if (mx < 140) {
    float boost = 190.0f / (float)max(mx, 1);
    r = (uint8_t)min(255, (int)(r * boost + 35));
    g = (uint8_t)min(255, (int)(g * boost + 35));
    b = (uint8_t)min(255, (int)(b * boost + 35));
  }
  return ((uint32_t)r << 16) | ((uint32_t)g << 8) | (uint32_t)b;
}

static void media_wave_timer_cb(lv_timer_t *t) {
  (void)t;
  if (!m_wave_active || !m_wave_rest || !m_wave_thumb) return;
  if (current_view != VIEW_MEDIA) return;

  // Smoothly ease amplitude between 4.2px (playing) and 0px (paused straight line)
  float targetAmp = m_media_playing ? 4.2f : 0.0f;
  m_wave_amp += (targetAmp - m_wave_amp) * 0.25f;
  if (fabsf(targetAmp - m_wave_amp) < 0.08f) m_wave_amp = targetAmp;

  if (m_media_playing) {
    m_wave_phase += 0.24f;
    if (m_wave_phase > 62.8318f) m_wave_phase -= 62.8318f;
  }

  const int totalW = 196;
  const int midY = 9;
  int splitX = (totalW * constrain(m_media_progress, 0, 100)) / 100;
  if (splitX < 4) splitX = 4;

  // Build active wavy segment (0 .. splitX)
  const int numPts = 42;
  for (int i = 0; i < numPts; i++) {
    float frac = (float)i / (float)(numPts - 1);
    int px = (int)(frac * splitX);
    float py = (float)midY + m_wave_amp * sinf((float)px * 0.23f - m_wave_phase);
    m_wave_pts[i].x = (lv_coord_t)px;
    m_wave_pts[i].y = (lv_coord_t)lroundf(py);
  }
  lv_line_set_points(m_wave_active, m_wave_pts, numPts);

  // Straight remaining track (splitX + 4 .. totalW)
  m_rest_pts[0].x = (lv_coord_t)min(totalW, splitX + 4);
  m_rest_pts[0].y = midY;
  m_rest_pts[1].x = totalW;
  m_rest_pts[1].y = midY;
  lv_line_set_points(m_wave_rest, m_rest_pts, 2);

  // Expressive vertical thumb pill
  lv_obj_set_pos(m_wave_thumb, splitX - 2, 2);
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

  // Compact live coding-agent status pill (always tappable to switch to Agent view)
  m_btn_agent_pill = lv_btn_create(scr_media);
  lv_obj_set_size(m_btn_agent_pill, 148, 24);
  lv_obj_set_style_radius(m_btn_agent_pill, 12, 0);
  lv_obj_set_style_bg_color(m_btn_agent_pill, lv_color_hex(COL_CARD), 0);
  lv_obj_set_style_bg_opa(m_btn_agent_pill, LV_OPA_80, 0);
  lv_obj_set_style_border_width(m_btn_agent_pill, 1, 0);
  lv_obj_set_style_border_color(m_btn_agent_pill, lv_color_hex(COL_MUTED), 0);
  lv_obj_align(m_btn_agent_pill, LV_ALIGN_TOP_MID, 0, 64);
  lv_obj_add_event_cb(m_btn_agent_pill, on_agent_pill_click, LV_EVENT_CLICKED, nullptr);

  m_lbl_agent_pill = lv_label_create(m_btn_agent_pill);
  lv_label_set_text(m_lbl_agent_pill, "AGENT: IDLE");
  lv_obj_set_style_text_font(m_lbl_agent_pill, &lv_font_montserrat_12, 0);
  lv_obj_set_style_text_color(m_lbl_agent_pill, lv_color_hex(COL_SUBTEXT), 0);
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

  // Material 3 Expressive wavy playback timeline
  m_wave_box = lv_obj_create(scr_media);
  lv_obj_set_size(m_wave_box, 200, 18);
  lv_obj_set_style_bg_opa(m_wave_box, LV_OPA_TRANSP, 0);
  lv_obj_set_style_border_width(m_wave_box, 0, 0);
  lv_obj_set_style_pad_all(m_wave_box, 0, 0);
  lv_obj_clear_flag(m_wave_box, LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_CLICKABLE);
  lv_obj_align(m_wave_box, LV_ALIGN_CENTER, 0, 0);

  m_wave_rest = lv_line_create(m_wave_box);
  lv_obj_set_style_line_width(m_wave_rest, 3, 0);
  lv_obj_set_style_line_color(m_wave_rest, lv_color_hex(0x334155), 0);
  lv_obj_set_style_line_rounded(m_wave_rest, true, 0);

  m_wave_active = lv_line_create(m_wave_box);
  lv_obj_set_style_line_width(m_wave_active, 4, 0);
  lv_obj_set_style_line_color(m_wave_active, lv_color_hex(COL_CYAN), 0);
  lv_obj_set_style_line_rounded(m_wave_active, true, 0);

  m_wave_thumb = lv_obj_create(m_wave_box);
  lv_obj_set_size(m_wave_thumb, 4, 14);
  lv_obj_set_style_radius(m_wave_thumb, 2, 0);
  lv_obj_set_style_border_width(m_wave_thumb, 0, 0);
  lv_obj_set_style_bg_color(m_wave_thumb, lv_color_hex(COL_TEXT), 0);
  lv_obj_clear_flag(m_wave_thumb, LV_OBJ_FLAG_CLICKABLE);

  lv_timer_create(media_wave_timer_cb, 40, nullptr);

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

  lv_obj_t *btn_media = lv_btn_create(scr_agent);
  lv_obj_set_size(btn_media, 84, 38);
  lv_obj_set_style_radius(btn_media, 19, 0);
  lv_obj_set_style_bg_color(btn_media, lv_color_hex(COL_CARD), 0);
  lv_obj_set_style_border_width(btn_media, 2, 0);
  lv_obj_set_style_border_color(btn_media, lv_color_hex(COL_CYAN), 0);
  lv_obj_align(btn_media, LV_ALIGN_BOTTOM_MID, -46, -54);
  lv_obj_add_event_cb(btn_media, on_media_btn_click, LV_EVENT_CLICKED, nullptr);
  lv_obj_t *lbl_media = lv_label_create(btn_media);
  lv_label_set_text(lbl_media, "MEDIA");
  lv_obj_set_style_text_font(lbl_media, &lv_font_montserrat_14, 0);
  lv_obj_set_style_text_color(lbl_media, lv_color_hex(COL_CYAN), 0);
  lv_obj_center(lbl_media);

  lv_obj_t *btn_focus = lv_btn_create(scr_agent);
  lv_obj_set_size(btn_focus, 84, 38);
  lv_obj_set_style_radius(btn_focus, 19, 0);
  lv_obj_set_style_bg_color(btn_focus, lv_color_hex(COL_CARD), 0);
  lv_obj_set_style_border_width(btn_focus, 2, 0);
  lv_obj_set_style_border_color(btn_focus, lv_color_hex(COL_BLUE), 0);
  lv_obj_align(btn_focus, LV_ALIGN_BOTTOM_MID, 46, -54);
  lv_obj_add_event_cb(btn_focus, on_focus_click, LV_EVENT_CLICKED, nullptr);
  lv_obj_t *lbl_focus = lv_label_create(btn_focus);
  lv_label_set_text(lbl_focus, "FOCUS");
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

static void update_blender_mode_styles() {
  uint32_t col = (current_blender_mode == BLENDER_PAN) ? COL_AMBER : COL_CYAN;
  if (b_arc_ring) {
    lv_obj_set_style_arc_color(b_arc_ring, lv_color_hex(col), LV_PART_INDICATOR);
  }
  if (b_pad) {
    lv_obj_set_style_border_color(b_pad, lv_color_hex(col), 0);
  }
  if (b_lbl_mode) {
    lv_label_set_text(b_lbl_mode, current_blender_mode == BLENDER_PAN ? "PAN" : "ORBIT");
    lv_obj_set_style_text_color(b_lbl_mode, lv_color_hex(col), 0);
  }
  if (b_lbl_hint) {
    lv_label_set_text(b_lbl_hint, "DRAG: MOVE\nDIAL: ZOOM");
  }
}

static void make_snap_btn(lv_obj_t *parent, const char *txt, const char *snapKey, int xOff, int yOff, int w, int h) {
  lv_obj_t *btn = lv_btn_create(parent);
  lv_obj_set_size(btn, w, h);
  lv_obj_set_style_radius(btn, h / 2, 0);
  lv_obj_set_style_bg_color(btn, lv_color_hex(COL_CARD), 0);
  lv_obj_set_style_border_width(btn, 1, 0);
  lv_obj_set_style_border_color(btn, lv_color_hex(0x334155), 0);
  lv_obj_align(btn, LV_ALIGN_CENTER, xOff, yOff);
  lv_obj_add_event_cb(btn, on_blender_snap_click, LV_EVENT_CLICKED, (void *)snapKey);

  lv_obj_t *lbl = lv_label_create(btn);
  lv_label_set_text(lbl, txt);
  lv_obj_set_style_text_font(lbl, &lv_font_montserrat_12, 0);
  lv_obj_set_style_text_color(lbl, lv_color_hex(COL_TEXT), 0);
  lv_obj_center(lbl);
}

static void build_blender_screen() {
  scr_blender = lv_obj_create(nullptr);
  style_screen(scr_blender);

  b_arc_ring = lv_arc_create(scr_blender);
  lv_obj_set_size(b_arc_ring, 344, 344);
  lv_obj_center(b_arc_ring);
  lv_arc_set_bg_angles(b_arc_ring, 0, 360);
  lv_arc_set_value(b_arc_ring, 100);
  lv_obj_remove_style(b_arc_ring, nullptr, LV_PART_KNOB);
  lv_obj_clear_flag(b_arc_ring, LV_OBJ_FLAG_CLICKABLE);
  lv_obj_set_style_arc_width(b_arc_ring, 8, LV_PART_MAIN);
  lv_obj_set_style_arc_width(b_arc_ring, 8, LV_PART_INDICATOR);
  lv_obj_set_style_arc_color(b_arc_ring, lv_color_hex(0x1E293B), LV_PART_MAIN);
  lv_obj_set_style_arc_color(b_arc_ring, lv_color_hex(COL_CYAN), LV_PART_INDICATOR);

  // Top exit pill
  lv_obj_t *btn_media = lv_btn_create(scr_blender);
  lv_obj_set_size(btn_media, 72, 22);
  lv_obj_set_style_radius(btn_media, 11, 0);
  lv_obj_set_style_bg_color(btn_media, lv_color_hex(COL_CARD), 0);
  lv_obj_set_style_border_width(btn_media, 1, 0);
  lv_obj_set_style_border_color(btn_media, lv_color_hex(COL_MUTED), 0);
  lv_obj_align(btn_media, LV_ALIGN_CENTER, 0, -142);
  lv_obj_add_event_cb(btn_media, on_media_btn_click, LV_EVENT_CLICKED, nullptr);
  lv_obj_t *lbl_m = lv_label_create(btn_media);
  lv_label_set_text(lbl_m, "MEDIA");
  lv_obj_set_style_text_font(lbl_m, &lv_font_montserrat_12, 0);
  lv_obj_set_style_text_color(lbl_m, lv_color_hex(COL_SUBTEXT), 0);
  lv_obj_center(lbl_m);

  // Center circular touchpad (168x168) for 2D Orbit / Pan
  b_pad = lv_obj_create(scr_blender);
  lv_obj_set_size(b_pad, 168, 168);
  lv_obj_set_style_radius(b_pad, LV_RADIUS_CIRCLE, 0);
  lv_obj_set_style_bg_color(b_pad, lv_color_hex(COL_CARD), 0);
  lv_obj_set_style_border_width(b_pad, 2, 0);
  lv_obj_set_style_border_color(b_pad, lv_color_hex(COL_CYAN), 0);
  lv_obj_clear_flag(b_pad, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_center(b_pad);
  lv_obj_add_event_cb(b_pad, on_blender_pad_event, LV_EVENT_PRESSED, nullptr);
  lv_obj_add_event_cb(b_pad, on_blender_pad_event, LV_EVENT_PRESSING, nullptr);
  lv_obj_add_event_cb(b_pad, on_blender_pad_event, LV_EVENT_RELEASED, nullptr);
  lv_obj_add_event_cb(b_pad, on_blender_pad_event, LV_EVENT_PRESS_LOST, nullptr);

  b_lbl_mode = lv_label_create(b_pad);
  lv_obj_set_style_text_font(b_lbl_mode, &lv_font_montserrat_20, 0);
  lv_obj_align(b_lbl_mode, LV_ALIGN_CENTER, 0, -18);

  b_lbl_hint = lv_label_create(b_pad);
  lv_obj_set_style_text_align(b_lbl_hint, LV_TEXT_ALIGN_CENTER, 0);
  lv_obj_set_style_text_font(b_lbl_hint, &lv_font_montserrat_12, 0);
  lv_obj_set_style_text_color(b_lbl_hint, lv_color_hex(COL_SUBTEXT), 0);
  lv_obj_align(b_lbl_hint, LV_ALIGN_CENTER, 0, 14);

  // Cardinal radial view-snap ring (N=TOP, S=FRONT, E=SIDE, W=ORTHO)
  make_snap_btn(scr_blender, "TOP",   "top",    0,   -110, 56, 26);
  make_snap_btn(scr_blender, "FRONT", "front",  0,    110, 62, 26);
  make_snap_btn(scr_blender, "SIDE",  "side",   114,  0,   48, 34);
  make_snap_btn(scr_blender, "ORTHO", "ortho", -114,  0,   52, 34);

  lv_obj_t *lbl_foot = lv_label_create(scr_blender);
  lv_label_set_text(lbl_foot, "1X:PAN  2X:FRAME");
  lv_obj_set_style_text_font(lbl_foot, &lv_font_montserrat_12, 0);
  lv_obj_set_style_text_color(lbl_foot, lv_color_hex(COL_MUTED), 0);
  lv_obj_align(lbl_foot, LV_ALIGN_CENTER, 0, 140);

  update_blender_mode_styles();
}

void ui_init(UiMediaCmdCb mediaCb, UiApprovalDecisionCb approvalCb, UiFocusIdeCb focusCb, UiBlenderCb blenderCb) {
  cb_media = mediaCb;
  cb_approval = approvalCb;
  cb_focus = focusCb;
  cb_blender = blenderCb;

  build_media_screen();
  build_agent_screen();
  build_blender_screen();
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
    lv_obj_set_style_arc_color(m_arc_vol, lv_color_hex(m_accent_hex), LV_PART_INDICATOR);
  }
}

void ui_update_media(const MediaInfo &info) {
  m_media_playing = info.playing;
  m_media_progress = constrain(info.progress, 0, 100);
  if (info.accentR || info.accentG || info.accentB) {
    m_accent_hex = brighten_hex(info.accentR, info.accentG, info.accentB);
  } else {
    m_accent_hex = COL_CYAN;
  }

  String appUpper = info.app.length() ? info.app : "MAC MEDIA";
  appUpper.toUpperCase();
  lv_label_set_text(m_lbl_app, appUpper.c_str());
  lv_obj_set_style_text_color(m_lbl_app, lv_color_hex(info.playing ? m_accent_hex : COL_SUBTEXT), 0);

  lv_label_set_text(m_lbl_title, info.title.length() ? info.title.c_str() : "Nothing Playing");
  lv_label_set_text(m_lbl_artist, info.artist.length() ? info.artist.c_str() : "macOS Now Playing");

  // Dynamic M3 Expressive theming + Play/Pause shape morph (squircle when playing, circle when paused)
  lv_obj_set_style_line_color(m_wave_active, lv_color_hex(m_accent_hex), 0);
  lv_obj_set_style_bg_color(m_btn_play, lv_color_hex(m_accent_hex), 0);
  lv_obj_set_style_radius(m_btn_play, info.playing ? 22 : LV_RADIUS_CIRCLE, 0);
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
  // always visible and tappable even while watching Now Playing.
  if (m_btn_agent_pill && m_lbl_agent_pill) {
    String shortPlat = active_platform;
    if (shortPlat == "antigravity") shortPlat = "AGY";
    else if (shortPlat == "claude-code") shortPlat = "CLAUDE";
    else shortPlat.toUpperCase();

    String pillTxt = shortPlat + ": " + String(stateTxt);
    lv_label_set_text(m_lbl_agent_pill, pillTxt.c_str());
    lv_obj_set_style_border_color(m_btn_agent_pill, lv_color_hex(col), 0);
    lv_obj_set_style_text_color(m_lbl_agent_pill, lv_color_hex(col), 0);
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
    lv_obj_t *target = scr_media;
    if (current_view == VIEW_AGENT) target = scr_agent;
    else if (current_view == VIEW_BLENDER) target = scr_blender;
    lv_scr_load_anim(target, LV_SCR_LOAD_ANIM_FADE_ON, 120, 0, false);
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
  } else if (current_view == VIEW_AGENT) {
    current_view = VIEW_BLENDER;
    prev_main_view = VIEW_BLENDER;
    lv_scr_load_anim(scr_blender, LV_SCR_LOAD_ANIM_MOVE_LEFT, 160, 0, false);
  } else {
    current_view = VIEW_MEDIA;
    prev_main_view = VIEW_MEDIA;
    lv_scr_load_anim(scr_media, LV_SCR_LOAD_ANIM_MOVE_LEFT, 160, 0, false);
  }
}

void ui_blender_set_mode(BlenderMode mode) {
  current_blender_mode = mode;
  update_blender_mode_styles();
}

void ui_blender_cycle_mode() {
  current_blender_mode = (current_blender_mode == BLENDER_ORBIT) ? BLENDER_PAN : BLENDER_ORBIT;
  update_blender_mode_styles();
}

BlenderMode ui_get_blender_mode() { return current_blender_mode; }

const char *ui_get_blender_mode_str() {
  return (current_blender_mode == BLENDER_PAN) ? "pan" : "orbit";
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
