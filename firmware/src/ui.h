#pragma once

#include <Arduino.h>
#include <lvgl.h>

enum UiView : uint8_t {
  VIEW_MEDIA = 0,
  VIEW_AGENT = 1,
  VIEW_BLENDER = 2,
  VIEW_APPROVAL = 3
};

enum BlenderMode : uint8_t {
  BLENDER_ORBIT = 0,
  BLENDER_PAN   = 1
};

enum AgentState : uint8_t {
  AGENT_IDLE = 0,
  AGENT_WORKING = 1,
  AGENT_DONE = 2,
  AGENT_ERROR = 3,
  AGENT_WAITING = 4
};

struct MediaInfo {
  String title;
  String artist;
  String app;
  bool playing;
  int volume;    // 0..100
  bool muted;
  int progress;  // 0..100
  uint8_t accentR;
  uint8_t accentG;
  uint8_t accentB;
};

struct AgentInfo {
  String platform;  // "antigravity", "claude-code", etc.
  AgentState state;
  String title;
  String detail;
};

struct ApprovalRequest {
  bool active;
  String id;
  String platform;
  String tool;
  String detail;
  int selectedOption;  // 0 = ALLOW, 1 = DENY, 2 = IDE
};

// Callbacks from UI touch buttons to main.cpp
typedef void (*UiMediaCmdCb)(const char *cmd);
typedef void (*UiApprovalDecisionCb)(const char *id, const char *decision);
typedef void (*UiFocusIdeCb)(const char *platform);
typedef void (*UiBlenderCb)(const char *act, const char *mode, int dx, int dy);

#define ART_WIDTH  120
#define ART_HEIGHT 120
#define ART_BYTES  (ART_WIDTH * ART_HEIGHT * 2)

void ui_init(UiMediaCmdCb mediaCb, UiApprovalDecisionCb approvalCb, UiFocusIdeCb focusCb, UiBlenderCb blenderCb);
void ui_set_connected(bool connected);
void ui_update_media(const MediaInfo &info);
void ui_update_volume_local(int vol, bool muted, bool showHud = false);
void ui_update_agent(const AgentInfo &info);
void ui_show_approval(const ApprovalRequest &req);
void ui_clear_approval();
void ui_approval_move_selection(int delta);
void ui_approval_confirm();
void ui_toggle_main_view();
void ui_blender_cycle_mode();
void ui_blender_set_mode(BlenderMode mode);
BlenderMode ui_get_blender_mode();
const char *ui_get_blender_mode_str();
uint8_t *ui_get_art_buffer();
void ui_commit_art();
void ui_clear_art();
UiView ui_get_current_view();
AgentState ui_get_agent_state();
bool ui_is_approval_active();
const char *ui_get_active_platform();
