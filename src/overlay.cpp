#include "overlay.h"
#include <imgui.h>
#include <backends/imgui_impl_sdl2.h>
#include <backends/imgui_impl_sdlrenderer2.h>
#include <backends/imgui_impl_opengl3.h>

#include <stdio.h>
#include <string.h>
#include <stdlib.h>

extern "C" {
#include "config.h"
#include "features.h"
#include "zelda_rtl.h"

  extern uint8 g_ram[131072];
  void ZeldaEnableMsu(uint8 enable);
  void SetFullscreenMode(int mode);
  void SetWindowScale(int scale);
  void SetWindowResolution(int width, int height);
  void SetAspectRatio(int index);
  int GetAspectRatioIndex(void);
  int GetMasterVolume(void);
  void SetMasterVolume(int volume);
  int GetActualFps(void);
}

// Estados e telas da arquitetura do Menu do Jogo
enum MenuScreen {
  kScreen_MainMenu = 0,
  kScreen_Video,
  kScreen_Audio,
  kScreen_Language,
  kScreen_Gameplay,
  kScreen_Cheats,
  kScreen_About,
  kScreen_ExitConfirm
};

static MenuScreen s_current_screen = kScreen_MainMenu;
static bool s_overlay_open = false;
static bool s_is_opengl = false;
static bool s_request_exit_game = false;
static int  s_selected_save_slot = 0;
static char s_status_message[128] = { 0 };
static uint32_t s_status_message_time = 0;

struct ResolutionPreset {
  const char *name;
  int width;
  int height;
};

static const ResolutionPreset kStandardResolutions[] = {
  { " 512 x 448   (SNES 2x)",               512,  448  },
  { " 640 x 480   (VGA - 4:3)",             640,  480  },
  { " 768 x 672   (SNES 3x)",               768,  672  },
  { " 800 x 600   (SVGA - 4:3)",            800,  600  },
  { " 960 x 540   (qHD - 16:9)",            960,  540  },
  { "1024 x 768   (XGA - 4:3)",             1024, 768  },
  { "1024 x 896   (SNES 4x)",               1024, 896  },
  { "1280 x 720   (HD 720p - 16:9)",        1280, 720  },
  { "1280 x 800   (WXGA - 16:10)",          1280, 800  },
  { "1280 x 960   (SXGA- - 4:3)",           1280, 960  },
  { "1280 x 1120  (SNES 5x)",               1280, 1120 },
  { "1366 x 768   (HD Notebook - 16:9)",    1366, 768  },
  { "1440 x 900   (WXGA+ - 16:10)",         1440, 900  },
  { "1600 x 900   (HD+ 900p - 16:9)",       1600, 900  },
  { "1600 x 1200  (UXGA - 4:3)",            1600, 1200 },
  { "1680 x 1050  (WSXGA+ - 16:10)",        1680, 1050 },
  { "1920 x 1080  (Full HD 1080p - 16:9)",  1920, 1080 },
  { "1920 x 1200  (WUXGA - 16:10)",         1920, 1200 },
  { "2560 x 1440  (Quad HD 2K - 16:9)",     2560, 1440 },
  { "2560 x 1600  (WQXGA - 16:10)",         2560, 1600 },
  { "3840 x 2160  (Ultra HD 4K - 16:9)",    3840, 2160 },
};

static const char *kLangNames[] = {
  "Portugues do Brasil (PT-BR)",
  "English (US)",
  "Deutsch (Alemao)",
  "Francais (Frances)",
  "Espanol (Espanhol)",
  "Polski (Polones)",
  "Nederlands (Holandes)",
  "Svenska (Sueco)"
};
static const char *kLangCodes[] = { "pt", "us", "de", "fr", "es", "pl", "nl", "sv" };

// Estrutura de Staging para permitir navegar e pré-visualizar antes de aplicar
struct StagedSettings {
  int res_idx;
  int fs_mode;
  int scale;
  int aspect_ratio;
  int lang_idx;
  int msu_mode;
  int fast_diag;
  int fast_diag_speed;
  int save_slot;
};

static StagedSettings s_staged;

static void SyncStagedSettingsFromActive() {
  int cur_w = g_config.window_width;
  int cur_h = g_config.window_height;
  if (cur_w == 0 || cur_h == 0) {
    int s = g_config.window_scale ? g_config.window_scale : 3;
    cur_w = (g_config.extended_aspect_ratio * 2 + 256) * s;
    cur_h = (g_config.extend_y ? 240 : 224) * s;
  }
  s_staged.res_idx = -1;
  for (size_t i = 0; i < IM_ARRAYSIZE(kStandardResolutions); i++) {
    if (kStandardResolutions[i].width == cur_w && kStandardResolutions[i].height == cur_h) {
      s_staged.res_idx = (int)i;
      break;
    }
  }
  if (s_staged.res_idx < 0) s_staged.res_idx = 7; // Padrao 1280x720

  s_staged.fs_mode = g_config.fullscreen;
  if (s_staged.fs_mode < 0 || s_staged.fs_mode > 2) s_staged.fs_mode = 0;

  s_staged.scale = (g_config.window_scale >= 1 && g_config.window_scale <= 10) ? (g_config.window_scale - 1) : 2;

  s_staged.aspect_ratio = GetAspectRatioIndex();
  if (s_staged.aspect_ratio < 0) s_staged.aspect_ratio = 0;

  s_staged.lang_idx = 0;
  if (g_config.language) {
    for (int i = 0; i < (int)IM_ARRAYSIZE(kLangCodes); i++) {
      if (strcmp(g_config.language, kLangCodes[i]) == 0) {
        s_staged.lang_idx = i;
        break;
      }
    }
  }

  if (g_config.enable_msu == 0) s_staged.msu_mode = 0;
  else if (g_config.enable_msu == kMsuEnabled_Msu) s_staged.msu_mode = 1;
  else if (g_config.enable_msu == kMsuEnabled_MsuDeluxe) s_staged.msu_mode = 2;
  else if (g_config.enable_msu == kMsuEnabled_Opuz) s_staged.msu_mode = 3;
  else s_staged.msu_mode = 4;

  s_staged.fast_diag = g_config.fast_dialogue;
  s_staged.fast_diag_speed = g_config.fast_dialogue_speed ? (g_config.fast_dialogue_speed - 1) : 2;
  if (s_staged.fast_diag_speed < 0) s_staged.fast_diag_speed = 0;
  if (s_staged.fast_diag_speed > 4) s_staged.fast_diag_speed = 4;

  s_staged.save_slot = s_selected_save_slot;
}

static void SetupZeldaTheme() {
  ImGuiStyle& style = ImGui::GetStyle();
  ImVec4* colors = style.Colors;

  style.WindowRounding = 0.0f;
  style.FrameRounding = 6.0f;
  style.GrabRounding = 6.0f;
  style.PopupRounding = 8.0f;
  style.ScrollbarRounding = 6.0f;
  style.TabRounding = 6.0f;
  style.WindowPadding = ImVec2(24.0f, 16.0f);
  style.FramePadding = ImVec2(10.0f, 6.0f);
  style.ItemSpacing = ImVec2(10.0f, 6.0f);
  style.WindowBorderSize = 0.0f;

  colors[ImGuiCol_Text]                  = ImVec4(0.95f, 0.95f, 0.92f, 1.00f);
  colors[ImGuiCol_TextDisabled]          = ImVec4(0.55f, 0.55f, 0.50f, 1.00f);
  colors[ImGuiCol_WindowBg]              = ImVec4(0.06f, 0.08f, 0.07f, 0.96f);
  colors[ImGuiCol_ChildBg]               = ImVec4(0.00f, 0.00f, 0.00f, 0.00f);
  colors[ImGuiCol_PopupBg]               = ImVec4(0.08f, 0.11f, 0.09f, 0.98f);
  colors[ImGuiCol_Border]                = ImVec4(0.78f, 0.65f, 0.22f, 0.55f);
  colors[ImGuiCol_BorderShadow]          = ImVec4(0.00f, 0.00f, 0.00f, 0.40f);
  colors[ImGuiCol_FrameBg]               = ImVec4(0.12f, 0.16f, 0.14f, 0.85f);
  colors[ImGuiCol_FrameBgHovered]        = ImVec4(0.18f, 0.26f, 0.20f, 0.90f);
  colors[ImGuiCol_FrameBgActive]         = ImVec4(0.25f, 0.35f, 0.28f, 1.00f);
  colors[ImGuiCol_TitleBg]               = ImVec4(0.07f, 0.12f, 0.09f, 1.00f);
  colors[ImGuiCol_TitleBgActive]         = ImVec4(0.12f, 0.22f, 0.15f, 1.00f);
  colors[ImGuiCol_TitleBgCollapsed]      = ImVec4(0.05f, 0.08f, 0.06f, 0.75f);
  colors[ImGuiCol_ScrollbarBg]           = ImVec4(0.06f, 0.08f, 0.07f, 0.50f);
  colors[ImGuiCol_ScrollbarGrab]         = ImVec4(0.25f, 0.35f, 0.28f, 0.80f);
  colors[ImGuiCol_ScrollbarGrabHovered]  = ImVec4(0.35f, 0.48f, 0.38f, 0.90f);
  colors[ImGuiCol_ScrollbarGrabActive]   = ImVec4(0.78f, 0.65f, 0.22f, 1.00f);
  colors[ImGuiCol_CheckMark]             = ImVec4(0.88f, 0.75f, 0.25f, 1.00f);
  colors[ImGuiCol_SliderGrab]            = ImVec4(0.78f, 0.65f, 0.22f, 0.90f);
  colors[ImGuiCol_SliderGrabActive]      = ImVec4(0.95f, 0.85f, 0.35f, 1.00f);
  colors[ImGuiCol_Button]                = ImVec4(0.16f, 0.24f, 0.19f, 0.85f);
  colors[ImGuiCol_ButtonHovered]         = ImVec4(0.24f, 0.36f, 0.27f, 0.95f);
  colors[ImGuiCol_ButtonActive]          = ImVec4(0.78f, 0.65f, 0.22f, 0.90f);
  colors[ImGuiCol_Header]                = ImVec4(0.78f, 0.65f, 0.22f, 0.20f);
  colors[ImGuiCol_HeaderHovered]         = ImVec4(0.78f, 0.65f, 0.22f, 0.30f);
  colors[ImGuiCol_HeaderActive]          = ImVec4(0.78f, 0.65f, 0.22f, 0.40f);
  colors[ImGuiCol_Separator]             = ImVec4(0.78f, 0.65f, 0.22f, 0.35f);
  colors[ImGuiCol_SeparatorHovered]      = ImVec4(0.78f, 0.65f, 0.22f, 0.70f);
  colors[ImGuiCol_SeparatorActive]       = ImVec4(0.90f, 0.80f, 0.30f, 1.00f);
  colors[ImGuiCol_NavHighlight]          = ImVec4(0.98f, 0.85f, 0.25f, 0.00f);
}

static void SetStatus(const char *msg) {
  snprintf(s_status_message, sizeof(s_status_message), "%s", msg);
  s_status_message_time = SDL_GetTicks();
}

bool Overlay_Init(SDL_Window *window, SDL_Renderer *renderer, bool is_opengl) {
  s_is_opengl = is_opengl;

  IMGUI_CHECKVERSION();
  ImGui::CreateContext();
  ImGuiIO& io = ImGui::GetIO();
  io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
  io.ConfigFlags |= ImGuiConfigFlags_NavEnableGamepad;
  io.IniFilename = NULL;

  SetupZeldaTheme();

  if (!is_opengl && renderer) {
    ImGui_ImplSDL2_InitForSDLRenderer(window, renderer);
    ImGui_ImplSDLRenderer2_Init(renderer);
  } else {
    ImGui_ImplSDL2_InitForOpenGL(window, NULL);
    ImGui_ImplOpenGL3_Init("#version 130");
  }

  ImGui_ImplSDL2_SetGamepadMode(ImGui_ImplSDL2_GamepadMode_AutoAll);

  return true;
}

void Overlay_Shutdown(void) {
  if (s_is_opengl) {
    ImGui_ImplOpenGL3_Shutdown();
  } else {
    ImGui_ImplSDLRenderer2_Shutdown();
  }
  ImGui_ImplSDL2_Shutdown();
  ImGui::DestroyContext();
}

bool Overlay_ProcessEvent(const SDL_Event *event) {
  ImGui_ImplSDL2_ProcessEvent(event);
  ImGuiIO& io = ImGui::GetIO();

  if (s_overlay_open) {
    if (io.WantCaptureMouse || io.WantCaptureKeyboard)
      return true;
    return true;
  }

  return false;
}

bool Overlay_IsOpen(void) {
  return s_overlay_open;
}

static int s_menu_cursor = 0;
static bool s_cursor_just_moved = false;

void Overlay_Toggle(void) {
  s_overlay_open = !s_overlay_open;
  SDL_ShowCursor(s_overlay_open ? SDL_ENABLE : SDL_DISABLE);
  if (s_overlay_open) {
    s_current_screen = kScreen_MainMenu;
    s_menu_cursor = 0;
    s_cursor_just_moved = true;
    SyncStagedSettingsFromActive();
  } else {
    SaveConfigFile(NULL);
  }
}

void Overlay_SetOpen(bool open) {
  if (s_overlay_open && !open) {
    SaveConfigFile(NULL);
  }
  s_overlay_open = open;
  if (s_overlay_open) {
    s_current_screen = kScreen_MainMenu;
    s_menu_cursor = 0;
    s_cursor_just_moved = true;
    SyncStagedSettingsFromActive();
  }
  SDL_ShowCursor(s_overlay_open ? SDL_ENABLE : SDL_DISABLE);
}

bool Overlay_ShouldExit(void) {
  return s_request_exit_game;
}

// =============================================================================
// NAVEGAÇÃO UNIFICADA E ESTRUTURA DE CONTROLES
// =============================================================================

struct MenuNavInputs {
  bool up;
  bool down;
  bool left;
  bool right;
  bool confirm; // A / Enter / Space
  bool cancel;  // B / ESC
  bool fast_up; // L1 / PageUp
  bool fast_down; // R1 / PageDown
};

static MenuNavInputs s_nav = {0};

static MenuNavInputs ReadNavInputs() {
  MenuNavInputs in = {0};

  // Up: D-Pad Up, Analog Stick Up, Keyboard Up Arrow
  in.up = ImGui::IsKeyPressed(ImGuiKey_GamepadDpadUp, true) ||
          ImGui::IsKeyPressed(ImGuiKey_GamepadLStickUp, true) ||
          ImGui::IsKeyPressed(ImGuiKey_UpArrow, true);

  // Down: D-Pad Down, Analog Stick Down, Keyboard Down Arrow
  in.down = ImGui::IsKeyPressed(ImGuiKey_GamepadDpadDown, true) ||
            ImGui::IsKeyPressed(ImGuiKey_GamepadLStickDown, true) ||
            ImGui::IsKeyPressed(ImGuiKey_DownArrow, true);

  // Left: D-Pad Left, Analog Stick Left, Keyboard Left Arrow
  in.left = ImGui::IsKeyPressed(ImGuiKey_GamepadDpadLeft, true) ||
            ImGui::IsKeyPressed(ImGuiKey_GamepadLStickLeft, true) ||
            ImGui::IsKeyPressed(ImGuiKey_LeftArrow, true);

  // Right: D-Pad Right, Analog Stick Right, Keyboard Right Arrow
  in.right = ImGui::IsKeyPressed(ImGuiKey_GamepadDpadRight, true) ||
             ImGui::IsKeyPressed(ImGuiKey_GamepadLStickRight, true) ||
             ImGui::IsKeyPressed(ImGuiKey_RightArrow, true);

  // Confirm: Gamepad A / Cross, Enter, Keypad Enter, Space
  in.confirm = ImGui::IsKeyPressed(ImGuiKey_GamepadFaceDown, false) ||
               ImGui::IsKeyPressed(ImGuiKey_Enter, false) ||
               ImGui::IsKeyPressed(ImGuiKey_KeypadEnter, false) ||
               ImGui::IsKeyPressed(ImGuiKey_Space, false);

  // Cancel / Back: Gamepad B / Circle, Escape
  in.cancel = ImGui::IsKeyPressed(ImGuiKey_GamepadFaceRight, false) ||
              ImGui::IsKeyPressed(ImGuiKey_Escape, false);

  // Shoulders (L1/R1, PageUp/PageDown) para pular 4 itens
  in.fast_up = ImGui::IsKeyPressed(ImGuiKey_GamepadL1, true) || ImGui::IsKeyPressed(ImGuiKey_PageUp, true);
  in.fast_down = ImGui::IsKeyPressed(ImGuiKey_GamepadR1, true) || ImGui::IsKeyPressed(ImGuiKey_PageDown, true);

  return in;
}

static void UpdateMenuNavigation(int total_items) {
  if (total_items <= 0) return;

  if (s_nav.fast_up) {
    s_menu_cursor = (s_menu_cursor - 4 + total_items) % total_items;
    s_cursor_just_moved = true;
  } else if (s_nav.fast_down) {
    s_menu_cursor = (s_menu_cursor + 4) % total_items;
    s_cursor_just_moved = true;
  } else if (s_nav.up) {
    s_menu_cursor = (s_menu_cursor - 1 + total_items) % total_items;
    s_cursor_just_moved = true;
  } else if (s_nav.down) {
    s_menu_cursor = (s_menu_cursor + 1) % total_items;
    s_cursor_just_moved = true;
  }

  if (s_menu_cursor < 0) s_menu_cursor = 0;
  if (s_menu_cursor >= total_items) s_menu_cursor = total_items - 1;
}

static void ReturnToMainMenu(void) {
  int prev_cat = (int)s_current_screen - 1;
  s_current_screen = kScreen_MainMenu;
  s_menu_cursor = (prev_cat >= 0 && prev_cat < 7) ? prev_cat : 0;
  s_cursor_just_moved = true;
  SyncStagedSettingsFromActive();
}

static void SetScreen(MenuScreen screen) {
  s_current_screen = screen;
  s_menu_cursor = 0;
  s_cursor_just_moved = true;
  SyncStagedSettingsFromActive();
}

// Divisor decorativo de seção
static void CardSection_Header(const char *title) {
  float avail_w = ImGui::GetContentRegionAvail().x;
  ImVec2 pos = ImGui::GetCursorScreenPos();
  ImDrawList *draw_list = ImGui::GetWindowDrawList();

  float line_h = ImGui::GetTextLineHeight();
  pos.y += 6.0f;

  draw_list->AddText(pos, IM_COL32(250, 217, 64, 255), "+");

  char header_text[128];
  snprintf(header_text, sizeof(header_text), "%s", title);
  ImVec2 ts = ImGui::CalcTextSize(header_text);
  draw_list->AddText(ImVec2(pos.x + 18.0f, pos.y), IM_COL32(250, 217, 64, 255), header_text);

  float line_x0 = pos.x + 26.0f + ts.x;
  float line_x1 = pos.x + avail_w - 8.0f;
  float line_y = pos.y + ts.y * 0.5f;
  if (line_x1 > line_x0 + 15.0f) {
    draw_list->AddLine(ImVec2(line_x0, line_y), ImVec2(line_x1, line_y), IM_COL32(199, 166, 56, 75), 1.2f);
  }

  ImGui::Dummy(ImVec2(avail_w, line_h + 12.0f));
}

// Card do Menu Principal (Categorias)
static bool Card_MainMenu(
    int index,
    const char *id,
    const char *title,
    const char *desc,
    bool is_danger = false)
{
  ImGuiIO& io = ImGui::GetIO();
  float card_w = ImGui::GetContentRegionAvail().x;
  if (card_w < 100.0f) card_w = 400.0f;
  float font_scale = io.FontGlobalScale;
  float line_h = ImGui::GetTextLineHeight();
  float card_h = 64.0f * font_scale;

  ImVec2 cursor_pos = ImGui::GetCursorScreenPos();
  bool is_selected = (s_menu_cursor == index);

  if (is_selected && s_cursor_just_moved) {
    ImGui::SetScrollHereY(0.4f);
  }

  ImGui::PushID(id);
  ImGui::InvisibleButton("##hitbox", ImVec2(card_w, card_h));
  bool is_hovered = ImGui::IsItemHovered();
  bool is_clicked = ImGui::IsItemClicked();

  if (is_hovered && (io.MouseDelta.x != 0.0f || io.MouseDelta.y != 0.0f)) {
    s_menu_cursor = index;
    is_selected = true;
  }
  if (is_clicked) {
    s_menu_cursor = index;
    is_selected = true;
  }

  ImDrawList *draw_list = ImGui::GetWindowDrawList();
  ImVec2 min_p = cursor_pos;
  ImVec2 max_p = ImVec2(cursor_pos.x + card_w, cursor_pos.y + card_h);

  // Background and Border
  if (is_selected) {
    draw_list->AddRectFilled(min_p, max_p, is_danger ? IM_COL32(100, 30, 30, 240) : IM_COL32(32, 54, 42, 240), 7.0f);
    draw_list->AddRect(min_p, max_p, is_danger ? IM_COL32(255, 90, 90, 255) : IM_COL32(250, 217, 64, 255), 7.0f, 0, 2.0f);
  } else if (is_hovered) {
    draw_list->AddRectFilled(min_p, max_p, is_danger ? IM_COL32(70, 22, 22, 200) : IM_COL32(24, 36, 30, 200), 7.0f);
    draw_list->AddRect(min_p, max_p, is_danger ? IM_COL32(200, 70, 70, 180) : IM_COL32(80, 115, 95, 180), 7.0f, 0, 1.2f);
  } else {
    draw_list->AddRectFilled(min_p, max_p, is_danger ? IM_COL32(40, 18, 18, 170) : IM_COL32(18, 24, 20, 180), 7.0f);
    draw_list->AddRect(min_p, max_p, is_danger ? IM_COL32(110, 40, 40, 120) : IM_COL32(45, 62, 52, 120), 7.0f, 0, 1.0f);
  }

  // Header Title & Indicator
  float text_x = min_p.x + 18.0f;
  float title_y = min_p.y + 11.0f;

  if (is_selected) {
    draw_list->AddText(ImVec2(text_x, title_y), is_danger ? IM_COL32(255, 110, 110, 255) : IM_COL32(250, 217, 64, 255), ">");
    text_x += 20.0f;
  }

  ImU32 title_col = is_selected ? (is_danger ? IM_COL32(255, 130, 130, 255) : IM_COL32(255, 235, 100, 255))
                                : (is_danger ? IM_COL32(240, 160, 160, 255) : IM_COL32(240, 245, 240, 255));
  draw_list->AddText(ImVec2(text_x, title_y), title_col, title);

  // Description
  if (desc && desc[0]) {
    float desc_y = title_y + line_h + 3.0f;
    draw_list->AddText(ImVec2(text_x, desc_y), IM_COL32(148, 175, 160, 255), desc);
  }

  // Right Badge Pill
  float badge_w = is_danger ? (125.0f * font_scale) : (115.0f * font_scale);
  float badge_h = 32.0f * font_scale;
  float badge_x = max_p.x - badge_w - 18.0f;
  float badge_y = min_p.y + (card_h - badge_h) * 0.5f;

  ImVec2 b_min = ImVec2(badge_x, badge_y);
  ImVec2 b_max = ImVec2(badge_x + badge_w, badge_y + badge_h);

  ImU32 b_bg = is_danger ? (is_selected ? IM_COL32(190, 45, 45, 240) : IM_COL32(110, 28, 28, 190))
                         : (is_selected ? IM_COL32(50, 120, 75, 240) : IM_COL32(28, 44, 35, 190));
  ImU32 b_border = is_danger ? IM_COL32(255, 90, 90, 230)
                             : (is_selected ? IM_COL32(250, 217, 64, 240) : IM_COL32(75, 105, 90, 150));

  draw_list->AddRectFilled(b_min, b_max, b_bg, 5.0f);
  draw_list->AddRect(b_min, b_max, b_border, 5.0f, 0, is_selected ? 1.8f : 1.0f);

  const char *badge_text = is_danger ? "SAIR  [X]" : "ENTRAR  >";
  ImVec2 b_sz = ImGui::CalcTextSize(badge_text);
  float bx = b_min.x + (badge_w - b_sz.x) * 0.5f;
  float by = b_min.y + (badge_h - b_sz.y) * 0.5f;
  draw_list->AddText(ImVec2(bx, by), IM_COL32(255, 255, 255, 255), badge_text);

  bool triggered = (is_selected && s_nav.confirm) || is_clicked;

  ImGui::PopID();
  return triggered;
}

// Card com Stepper (Staged: navega e pré-visualiza com muito espaço, aplica com [A])
static bool Card_StagedStepper(
    int index,
    const char *id,
    const char *title,
    const char *desc,
    int *staged_val,
    int active_val,
    const char * const *options,
    int option_count,
    bool *out_applied)
{
  ImGuiIO& io = ImGui::GetIO();
  float card_w = ImGui::GetContentRegionAvail().x;
  if (card_w < 100.0f) card_w = 400.0f;
  float font_scale = io.FontGlobalScale;
  float line_h = ImGui::GetTextLineHeight();

  bool has_pending = (*staged_val != active_val);
  float card_h = has_pending ? (120.0f * font_scale) : (88.0f * font_scale);

  ImVec2 cursor_pos = ImGui::GetCursorScreenPos();
  bool is_selected = (s_menu_cursor == index);

  if (is_selected && s_cursor_just_moved) {
    ImGui::SetScrollHereY(0.4f);
  }

  ImGui::PushID(id);
  ImGui::InvisibleButton("##hitbox", ImVec2(card_w, card_h));
  bool is_hovered = ImGui::IsItemHovered();
  bool is_clicked = ImGui::IsItemClicked();

  if (is_hovered && (io.MouseDelta.x != 0.0f || io.MouseDelta.y != 0.0f)) {
    s_menu_cursor = index;
    is_selected = true;
  }
  if (is_clicked) {
    s_menu_cursor = index;
    is_selected = true;
  }

  ImDrawList *draw_list = ImGui::GetWindowDrawList();
  ImVec2 min_p = cursor_pos;
  ImVec2 max_p = ImVec2(cursor_pos.x + card_w, cursor_pos.y + card_h);

  // Background and Border
  if (is_selected) {
    draw_list->AddRectFilled(min_p, max_p, has_pending ? IM_COL32(38, 48, 32, 245) : IM_COL32(30, 50, 38, 240), 7.0f);
    draw_list->AddRect(min_p, max_p, has_pending ? IM_COL32(255, 190, 50, 255) : IM_COL32(250, 217, 64, 255), 7.0f, 0, 2.0f);
  } else if (is_hovered) {
    draw_list->AddRectFilled(min_p, max_p, IM_COL32(24, 36, 30, 200), 7.0f);
    draw_list->AddRect(min_p, max_p, IM_COL32(80, 115, 95, 180), 7.0f, 0, 1.2f);
  } else {
    draw_list->AddRectFilled(min_p, max_p, IM_COL32(18, 24, 20, 180), 7.0f);
    draw_list->AddRect(min_p, max_p, IM_COL32(45, 62, 52, 120), 7.0f, 0, 1.0f);
  }

  // 1. Header Line: Title (left) and Status Pill Badge (right)
  float text_x = min_p.x + 18.0f;
  float title_y = min_p.y + 9.0f;

  if (is_selected) {
    draw_list->AddText(ImVec2(text_x, title_y), IM_COL32(250, 217, 64, 255), ">");
    text_x += 20.0f;
  }

  ImU32 title_col = is_selected ? IM_COL32(255, 235, 100, 255) : IM_COL32(240, 245, 240, 255);
  draw_list->AddText(ImVec2(text_x, title_y), title_col, title);

  // Status Badge Pill at top-right
  const char *badge_str = has_pending ? "PENDENTE" : "ATIVO";
  ImVec2 badge_sz = ImGui::CalcTextSize(badge_str);
  float badge_w = badge_sz.x + 24.0f * font_scale;
  float badge_h = 24.0f * font_scale;
  float badge_x = max_p.x - badge_w - 16.0f;
  float badge_y = min_p.y + 8.0f;

  ImVec2 b_min = ImVec2(badge_x, badge_y);
  ImVec2 b_max = ImVec2(badge_x + badge_w, badge_y + badge_h);
  ImU32 b_bg = has_pending ? IM_COL32(180, 110, 20, 230) : IM_COL32(30, 95, 55, 200);
  ImU32 b_border = has_pending ? IM_COL32(255, 180, 40, 255) : IM_COL32(50, 160, 95, 220);

  draw_list->AddRectFilled(b_min, b_max, b_bg, 4.0f);
  draw_list->AddRect(b_min, b_max, b_border, 4.0f, 0, 1.2f);
  draw_list->AddText(ImVec2(b_min.x + (badge_w - badge_sz.x) * 0.5f, b_min.y + (badge_h - badge_sz.y) * 0.5f),
                     has_pending ? IM_COL32(255, 240, 180, 255) : IM_COL32(180, 245, 200, 255), badge_str);

  // 2. Full-Width Description
  float desc_y = title_y + line_h + 2.0f;
  if (has_pending) {
    draw_list->AddText(ImVec2(text_x, desc_y), IM_COL32(255, 205, 90, 255), "Pre-visualizacao ativa - Pressione [A] para aplicar");
  } else if (desc && desc[0]) {
    draw_list->AddText(ImVec2(text_x, desc_y), IM_COL32(148, 175, 160, 255), desc);
  }

  // 3. Full-Width Stepper Row: [ < ] ====== Option Value ====== [ > ]
  float ctrl_y = desc_y + line_h + 5.0f;
  float ctrl_h = 32.0f * font_scale;
  float arrow_btn_w = 48.0f * font_scale;

  ImVec2 left_btn_min = ImVec2(min_p.x + 16.0f, ctrl_y);
  ImVec2 left_btn_max = ImVec2(left_btn_min.x + arrow_btn_w, ctrl_y + ctrl_h);

  ImVec2 right_btn_max = ImVec2(max_p.x - 16.0f, ctrl_y + ctrl_h);
  ImVec2 right_btn_min = ImVec2(right_btn_max.x - arrow_btn_w, ctrl_y);

  ImVec2 val_min = ImVec2(left_btn_max.x + 8.0f, ctrl_y);
  ImVec2 val_max = ImVec2(right_btn_min.x - 8.0f, ctrl_y + ctrl_h);

  bool in_left = io.MousePos.x >= left_btn_min.x && io.MousePos.x <= left_btn_max.x &&
                 io.MousePos.y >= left_btn_min.y && io.MousePos.y <= left_btn_max.y;
  bool in_right = io.MousePos.x >= right_btn_min.x && io.MousePos.x <= right_btn_max.x &&
                  io.MousePos.y >= right_btn_min.y && io.MousePos.y <= right_btn_max.y;
  bool in_val = io.MousePos.x >= val_min.x && io.MousePos.x <= val_max.x &&
                io.MousePos.y >= val_min.y && io.MousePos.y <= val_max.y;

  // Left Arrow Button
  draw_list->AddRectFilled(left_btn_min, left_btn_max, in_left ? IM_COL32(50, 100, 70, 240) : (is_selected ? IM_COL32(28, 48, 36, 230) : IM_COL32(18, 26, 22, 200)), 5.0f);
  draw_list->AddRect(left_btn_min, left_btn_max, is_selected ? IM_COL32(250, 217, 64, 200) : IM_COL32(60, 90, 75, 160), 5.0f, 0, 1.2f);
  ImVec2 al_sz = ImGui::CalcTextSize("<");
  draw_list->AddText(ImVec2(left_btn_min.x + (arrow_btn_w - al_sz.x) * 0.5f, ctrl_y + (ctrl_h - al_sz.y) * 0.5f),
                     is_selected ? IM_COL32(255, 230, 100, 255) : IM_COL32(180, 200, 190, 255), "<");

  // Right Arrow Button
  draw_list->AddRectFilled(right_btn_min, right_btn_max, in_right ? IM_COL32(50, 100, 70, 240) : (is_selected ? IM_COL32(28, 48, 36, 230) : IM_COL32(18, 26, 22, 200)), 5.0f);
  draw_list->AddRect(right_btn_min, right_btn_max, is_selected ? IM_COL32(250, 217, 64, 200) : IM_COL32(60, 90, 75, 160), 5.0f, 0, 1.2f);
  ImVec2 ar_sz = ImGui::CalcTextSize(">");
  draw_list->AddText(ImVec2(right_btn_min.x + (arrow_btn_w - ar_sz.x) * 0.5f, ctrl_y + (ctrl_h - ar_sz.y) * 0.5f),
                     is_selected ? IM_COL32(255, 230, 100, 255) : IM_COL32(180, 200, 190, 255), ">");

  // Center Option Display Box (Plenty of width for long strings!)
  ImU32 val_bg = has_pending ? IM_COL32(24, 30, 22, 240) : IM_COL32(12, 18, 15, 240);
  ImU32 val_border = has_pending ? IM_COL32(250, 180, 40, 220) : (is_selected ? IM_COL32(199, 166, 56, 180) : IM_COL32(50, 70, 60, 130));
  draw_list->AddRectFilled(val_min, val_max, val_bg, 5.0f);
  draw_list->AddRect(val_min, val_max, val_border, 5.0f, 0, is_selected ? 1.5f : 1.0f);

  const char *opt_text = (*staged_val >= 0 && *staged_val < option_count) ? options[*staged_val] : "";
  ImVec2 opt_sz = ImGui::CalcTextSize(opt_text);
  float opt_x = val_min.x + (val_max.x - val_min.x - opt_sz.x) * 0.5f;
  float opt_y = val_min.y + (ctrl_h - opt_sz.y) * 0.5f;
  if (opt_x < val_min.x + 8.0f) opt_x = val_min.x + 8.0f;
  draw_list->AddText(ImVec2(opt_x, opt_y), has_pending ? IM_COL32(255, 225, 100, 255) : (is_selected ? IM_COL32(255, 245, 180, 255) : IM_COL32(220, 235, 225, 255)), opt_text);

  // 4. Staged Apply Banner (shown when staged != active)
  ImVec2 app_btn_min = ImVec2(min_p.x + 16.0f, ctrl_y + ctrl_h + 6.0f * font_scale);
  ImVec2 app_btn_max = ImVec2(max_p.x - 16.0f, app_btn_min.y + 26.0f * font_scale);
  bool in_apply = false;

  if (has_pending) {
    in_apply = io.MousePos.x >= app_btn_min.x && io.MousePos.x <= app_btn_max.x &&
               io.MousePos.y >= app_btn_min.y && io.MousePos.y <= app_btn_max.y;

    ImU32 app_bg = (is_selected || in_apply) ? IM_COL32(195, 130, 25, 245) : IM_COL32(150, 95, 20, 210);
    ImU32 app_border = (is_selected || in_apply) ? IM_COL32(255, 220, 80, 255) : IM_COL32(210, 150, 40, 200);

    draw_list->AddRectFilled(app_btn_min, app_btn_max, app_bg, 5.0f);
    draw_list->AddRect(app_btn_min, app_btn_max, app_border, 5.0f, 0, 1.5f);

    const char *app_str = "* CONFIRMAR E APLICAR ESTA OPCAO (Pressione Botao A)";
    ImVec2 asz = ImGui::CalcTextSize(app_str);
    draw_list->AddText(ImVec2(app_btn_min.x + (card_w - 32.0f - asz.x) * 0.5f, app_btn_min.y + (26.0f * font_scale - asz.y) * 0.5f),
                       IM_COL32(255, 255, 255, 255), app_str);
  }

  *out_applied = false;

  // Controller / Keyboard Navigation
  if (is_selected) {
    if (s_nav.left) {
      *staged_val = (*staged_val - 1 + option_count) % option_count;
    } else if (s_nav.right) {
      *staged_val = (*staged_val + 1) % option_count;
    } else if (s_nav.confirm) {
      if (has_pending) {
        *out_applied = true;
      } else {
        *staged_val = (*staged_val + 1) % option_count;
      }
    }
  }

  // Mouse Interaction
  if (is_clicked) {
    if (in_left) {
      *staged_val = (*staged_val - 1 + option_count) % option_count;
    } else if (in_right) {
      *staged_val = (*staged_val + 1) % option_count;
    } else if (in_apply || (has_pending && in_val)) {
      *out_applied = true;
    } else if (in_val) {
      *staged_val = (*staged_val + 1) % option_count;
    }
  }

  ImGui::PopID();
  return *out_applied;
}

// Card com Toggle (LIGADO / DESLIGADO)
static bool Card_Toggle(
    int index,
    const char *id,
    const char *title,
    const char *desc,
    bool *value)
{
  ImGuiIO& io = ImGui::GetIO();
  float card_w = ImGui::GetContentRegionAvail().x;
  if (card_w < 100.0f) card_w = 400.0f;
  float font_scale = io.FontGlobalScale;
  float line_h = ImGui::GetTextLineHeight();
  float card_h = 78.0f * font_scale;

  ImVec2 cursor_pos = ImGui::GetCursorScreenPos();
  bool is_selected = (s_menu_cursor == index);

  if (is_selected && s_cursor_just_moved) {
    ImGui::SetScrollHereY(0.4f);
  }

  ImGui::PushID(id);
  ImGui::InvisibleButton("##hitbox", ImVec2(card_w, card_h));
  bool is_hovered = ImGui::IsItemHovered();
  bool is_clicked = ImGui::IsItemClicked();

  if (is_hovered && (io.MouseDelta.x != 0.0f || io.MouseDelta.y != 0.0f)) {
    s_menu_cursor = index;
    is_selected = true;
  }
  if (is_clicked) {
    s_menu_cursor = index;
    is_selected = true;
  }

  ImDrawList *draw_list = ImGui::GetWindowDrawList();
  ImVec2 min_p = cursor_pos;
  ImVec2 max_p = ImVec2(cursor_pos.x + card_w, cursor_pos.y + card_h);

  // Background and Border
  if (is_selected) {
    draw_list->AddRectFilled(min_p, max_p, IM_COL32(30, 50, 38, 240), 7.0f);
    draw_list->AddRect(min_p, max_p, IM_COL32(250, 217, 64, 255), 7.0f, 0, 2.0f);
  } else if (is_hovered) {
    draw_list->AddRectFilled(min_p, max_p, IM_COL32(24, 36, 30, 200), 7.0f);
    draw_list->AddRect(min_p, max_p, IM_COL32(80, 115, 95, 180), 7.0f, 0, 1.2f);
  } else {
    draw_list->AddRectFilled(min_p, max_p, IM_COL32(18, 24, 20, 180), 7.0f);
    draw_list->AddRect(min_p, max_p, IM_COL32(45, 62, 52, 120), 7.0f, 0, 1.0f);
  }

  // 1. Header Line: Title (left) and Status Pill Badge (right)
  float text_x = min_p.x + 18.0f;
  float title_y = min_p.y + 9.0f;

  if (is_selected) {
    draw_list->AddText(ImVec2(text_x, title_y), IM_COL32(250, 217, 64, 255), ">");
    text_x += 20.0f;
  }

  ImU32 title_col = is_selected ? IM_COL32(255, 235, 100, 255) : IM_COL32(240, 245, 240, 255);
  draw_list->AddText(ImVec2(text_x, title_y), title_col, title);

  // Status Badge Pill
  const char *badge_str = *value ? "LIGADO" : "DESLIGADO";
  ImVec2 badge_sz = ImGui::CalcTextSize(badge_str);
  float badge_w = badge_sz.x + 24.0f * font_scale;
  float badge_h = 24.0f * font_scale;
  float badge_x = max_p.x - badge_w - 16.0f;
  float badge_y = min_p.y + 8.0f;

  ImVec2 b_min = ImVec2(badge_x, badge_y);
  ImVec2 b_max = ImVec2(badge_x + badge_w, badge_y + badge_h);
  ImU32 b_bg = *value ? IM_COL32(35, 115, 65, 230) : IM_COL32(50, 60, 55, 190);
  ImU32 b_border = *value ? IM_COL32(60, 180, 100, 255) : IM_COL32(80, 95, 90, 180);

  draw_list->AddRectFilled(b_min, b_max, b_bg, 4.0f);
  draw_list->AddRect(b_min, b_max, b_border, 4.0f, 0, 1.2f);
  draw_list->AddText(ImVec2(b_min.x + (badge_w - badge_sz.x) * 0.5f, b_min.y + (badge_h - badge_sz.y) * 0.5f),
                     *value ? IM_COL32(200, 255, 220, 255) : IM_COL32(180, 190, 185, 255), badge_str);

  // 2. Full-Width Description
  float desc_y = title_y + line_h + 2.0f;
  if (desc && desc[0]) {
    draw_list->AddText(ImVec2(text_x, desc_y), IM_COL32(148, 175, 160, 255), desc);
  }

  // 3. Control Bar across the card
  float ctrl_y = desc_y + line_h + 5.0f;
  float ctrl_h = 26.0f * font_scale;
  ImVec2 bar_min = ImVec2(min_p.x + 16.0f, ctrl_y);
  ImVec2 bar_max = ImVec2(max_p.x - 16.0f, ctrl_y + ctrl_h);

  ImU32 bar_bg = *value ? (is_selected ? IM_COL32(40, 120, 70, 240) : IM_COL32(28, 85, 50, 200))
                        : (is_selected ? IM_COL32(32, 42, 36, 240) : IM_COL32(20, 28, 24, 200));
  ImU32 bar_border = *value ? IM_COL32(70, 190, 110, 240) : IM_COL32(60, 80, 70, 160);

  draw_list->AddRectFilled(bar_min, bar_max, bar_bg, 5.0f);
  draw_list->AddRect(bar_min, bar_max, bar_border, 5.0f, 0, is_selected ? 1.5f : 1.0f);

  const char *toggle_label = *value ? "[ <  ATIVADO / LIGADO  > ]" : "[ <  DESATIVADO  > ]";
  ImVec2 tsz = ImGui::CalcTextSize(toggle_label);
  draw_list->AddText(ImVec2(bar_min.x + (bar_max.x - bar_min.x - tsz.x) * 0.5f, bar_min.y + (ctrl_h - tsz.y) * 0.5f),
                     *value ? IM_COL32(255, 255, 255, 255) : IM_COL32(180, 195, 185, 255), toggle_label);

  bool changed = false;

  // Controller / Keyboard Navigation
  if (is_selected) {
    if (s_nav.confirm || s_nav.left || s_nav.right) {
      *value = !*value;
      changed = true;
    }
  }

  // Mouse Interaction
  if (is_clicked) {
    *value = !*value;
    changed = true;
  }

  ImGui::PopID();
  return changed;
}

// Card com Slider Interativo (Volume, etc.)
static bool Card_Slider(
    int index,
    const char *id,
    const char *title,
    const char *desc,
    int *value,
    int min_val,
    int max_val,
    int step,
    const char *unit = "%")
{
  ImGuiIO& io = ImGui::GetIO();
  float card_w = ImGui::GetContentRegionAvail().x;
  if (card_w < 100.0f) card_w = 400.0f;
  float font_scale = io.FontGlobalScale;
  float line_h = ImGui::GetTextLineHeight();
  float card_h = 84.0f * font_scale;

  ImVec2 cursor_pos = ImGui::GetCursorScreenPos();
  bool is_selected = (s_menu_cursor == index);

  if (is_selected && s_cursor_just_moved) {
    ImGui::SetScrollHereY(0.4f);
  }

  ImGui::PushID(id);
  ImGui::InvisibleButton("##hitbox", ImVec2(card_w, card_h));
  bool is_hovered = ImGui::IsItemHovered();
  bool is_clicked = ImGui::IsItemClicked();

  if (is_hovered && (io.MouseDelta.x != 0.0f || io.MouseDelta.y != 0.0f)) {
    s_menu_cursor = index;
    is_selected = true;
  }
  if (is_clicked) {
    s_menu_cursor = index;
    is_selected = true;
  }

  ImDrawList *draw_list = ImGui::GetWindowDrawList();
  ImVec2 min_p = cursor_pos;
  ImVec2 max_p = ImVec2(cursor_pos.x + card_w, cursor_pos.y + card_h);

  // Background and Border
  if (is_selected) {
    draw_list->AddRectFilled(min_p, max_p, IM_COL32(30, 50, 38, 240), 7.0f);
    draw_list->AddRect(min_p, max_p, IM_COL32(250, 217, 64, 255), 7.0f, 0, 2.0f);
  } else if (is_hovered) {
    draw_list->AddRectFilled(min_p, max_p, IM_COL32(24, 36, 30, 200), 7.0f);
    draw_list->AddRect(min_p, max_p, IM_COL32(80, 115, 95, 180), 7.0f, 0, 1.2f);
  } else {
    draw_list->AddRectFilled(min_p, max_p, IM_COL32(18, 24, 20, 180), 7.0f);
    draw_list->AddRect(min_p, max_p, IM_COL32(45, 62, 52, 120), 7.0f, 0, 1.0f);
  }

  // 1. Header Line: Title (left) and Status Pill Badge (right)
  float text_x = min_p.x + 18.0f;
  float title_y = min_p.y + 9.0f;

  if (is_selected) {
    draw_list->AddText(ImVec2(text_x, title_y), IM_COL32(250, 217, 64, 255), ">");
    text_x += 20.0f;
  }

  ImU32 title_col = is_selected ? IM_COL32(255, 235, 100, 255) : IM_COL32(240, 245, 240, 255);
  draw_list->AddText(ImVec2(text_x, title_y), title_col, title);

  // Status Badge Pill with numeric value
  char badge_str[32];
  snprintf(badge_str, sizeof(badge_str), "%d%s", *value, unit);
  ImVec2 badge_sz = ImGui::CalcTextSize(badge_str);
  float badge_w = badge_sz.x + 24.0f * font_scale;
  float badge_h = 24.0f * font_scale;
  float badge_x = max_p.x - badge_w - 16.0f;
  float badge_y = min_p.y + 8.0f;

  ImVec2 b_min = ImVec2(badge_x, badge_y);
  ImVec2 b_max = ImVec2(badge_x + badge_w, badge_y + badge_h);
  draw_list->AddRectFilled(b_min, b_max, IM_COL32(25, 45, 35, 200), 4.0f);
  draw_list->AddRect(b_min, b_max, is_selected ? IM_COL32(250, 217, 64, 220) : IM_COL32(60, 95, 75, 180), 4.0f, 0, 1.2f);
  draw_list->AddText(ImVec2(b_min.x + (badge_w - badge_sz.x) * 0.5f, b_min.y + (badge_h - badge_sz.y) * 0.5f),
                     is_selected ? IM_COL32(255, 235, 120, 255) : IM_COL32(200, 240, 210, 255), badge_str);

  // 2. Full-Width Description
  float desc_y = title_y + line_h + 2.0f;
  if (desc && desc[0]) {
    draw_list->AddText(ImVec2(text_x, desc_y), IM_COL32(148, 175, 160, 255), desc);
  }

  // 3. Slider Row: [ < - ] ====== Slider Track ====== [ + > ]
  float ctrl_y = desc_y + line_h + 5.0f;
  float ctrl_h = 30.0f * font_scale;
  float arrow_btn_w = 48.0f * font_scale;

  ImVec2 left_btn_min = ImVec2(min_p.x + 16.0f, ctrl_y);
  ImVec2 left_btn_max = ImVec2(left_btn_min.x + arrow_btn_w, ctrl_y + ctrl_h);

  ImVec2 right_btn_max = ImVec2(max_p.x - 16.0f, ctrl_y + ctrl_h);
  ImVec2 right_btn_min = ImVec2(right_btn_max.x - arrow_btn_w, ctrl_y);

  ImVec2 track_min = ImVec2(left_btn_max.x + 10.0f, ctrl_y + (ctrl_h - 12.0f * font_scale) * 0.5f);
  ImVec2 track_max = ImVec2(right_btn_min.x - 10.0f, track_min.y + 12.0f * font_scale);

  bool in_left = io.MousePos.x >= left_btn_min.x && io.MousePos.x <= left_btn_max.x &&
                 io.MousePos.y >= left_btn_min.y && io.MousePos.y <= left_btn_max.y;
  bool in_right = io.MousePos.x >= right_btn_min.x && io.MousePos.x <= right_btn_max.x &&
                  io.MousePos.y >= right_btn_min.y && io.MousePos.y <= right_btn_max.y;

  // Left Button
  draw_list->AddRectFilled(left_btn_min, left_btn_max, in_left ? IM_COL32(50, 100, 70, 240) : (is_selected ? IM_COL32(28, 48, 36, 230) : IM_COL32(18, 26, 22, 200)), 5.0f);
  draw_list->AddRect(left_btn_min, left_btn_max, is_selected ? IM_COL32(250, 217, 64, 200) : IM_COL32(60, 90, 75, 160), 5.0f, 0, 1.2f);
  ImVec2 al_sz = ImGui::CalcTextSize("< -");
  draw_list->AddText(ImVec2(left_btn_min.x + (arrow_btn_w - al_sz.x) * 0.5f, ctrl_y + (ctrl_h - al_sz.y) * 0.5f),
                     is_selected ? IM_COL32(255, 230, 100, 255) : IM_COL32(180, 200, 190, 255), "< -");

  // Right Button
  draw_list->AddRectFilled(right_btn_min, right_btn_max, in_right ? IM_COL32(50, 100, 70, 240) : (is_selected ? IM_COL32(28, 48, 36, 230) : IM_COL32(18, 26, 22, 200)), 5.0f);
  draw_list->AddRect(right_btn_min, right_btn_max, is_selected ? IM_COL32(250, 217, 64, 200) : IM_COL32(60, 90, 75, 160), 5.0f, 0, 1.2f);
  ImVec2 ar_sz = ImGui::CalcTextSize("+ >");
  draw_list->AddText(ImVec2(right_btn_min.x + (arrow_btn_w - ar_sz.x) * 0.5f, ctrl_y + (ctrl_h - ar_sz.y) * 0.5f),
                     is_selected ? IM_COL32(255, 230, 100, 255) : IM_COL32(180, 200, 190, 255), "+ >");

  // Track Background
  draw_list->AddRectFilled(track_min, track_max, IM_COL32(12, 16, 14, 255), 6.0f);
  draw_list->AddRect(track_min, track_max, IM_COL32(55, 75, 65, 180), 6.0f);

  // Fill Bar
  float frac = (max_val > min_val) ? ((float)(*value - min_val) / (float)(max_val - min_val)) : 0.0f;
  if (frac < 0.0f) frac = 0.0f;
  if (frac > 1.0f) frac = 1.0f;

  if (frac > 0.0f) {
    ImVec2 fill_max = ImVec2(track_min.x + (track_max.x - track_min.x) * frac, track_max.y);
    draw_list->AddRectFilled(track_min, fill_max, is_selected ? IM_COL32(250, 217, 64, 255) : IM_COL32(50, 175, 90, 220), 6.0f);
  }

  // Thumb Knob
  float knob_x = track_min.x + (track_max.x - track_min.x) * frac;
  float knob_y = track_min.y + (track_max.y - track_min.y) * 0.5f;
  draw_list->AddCircleFilled(ImVec2(knob_x, knob_y), 9.0f * font_scale, is_selected ? IM_COL32(255, 255, 255, 255) : IM_COL32(210, 225, 215, 255));
  draw_list->AddCircle(ImVec2(knob_x, knob_y), 9.0f * font_scale, is_selected ? IM_COL32(250, 217, 64, 255) : IM_COL32(50, 80, 65, 255), 0, 2.0f);

  bool changed = false;

  // Controller / Keyboard Navigation
  if (is_selected) {
    if (s_nav.left) {
      *value -= step;
      if (*value < min_val) *value = min_val;
      changed = true;
    } else if (s_nav.right) {
      *value += step;
      if (*value > max_val) *value = max_val;
      changed = true;
    }
  }

  // Mouse Interaction
  if (is_clicked) {
    if (in_left) {
      *value -= step;
      if (*value < min_val) *value = min_val;
      changed = true;
    } else if (in_right) {
      *value += step;
      if (*value > max_val) *value = max_val;
      changed = true;
    } else if (io.MousePos.x >= track_min.x && io.MousePos.x <= track_max.x) {
      float click_frac = (io.MousePos.x - track_min.x) / (track_max.x - track_min.x);
      *value = min_val + (int)(click_frac * (max_val - min_val) + 0.5f);
      if (*value < min_val) *value = min_val;
      if (*value > max_val) *value = max_val;
      changed = true;
    }
  }

  ImGui::PopID();
  return changed;
}

// Card com Botão de Ação
static bool Card_Button(
    int index,
    const char *id,
    const char *title,
    const char *desc,
    const char *btn_label,
    bool is_danger = false)
{
  ImGuiIO& io = ImGui::GetIO();
  float card_w = ImGui::GetContentRegionAvail().x;
  if (card_w < 100.0f) card_w = 400.0f;
  float font_scale = io.FontGlobalScale;
  float line_h = ImGui::GetTextLineHeight();
  float card_h = 76.0f * font_scale;

  ImVec2 cursor_pos = ImGui::GetCursorScreenPos();
  bool is_selected = (s_menu_cursor == index);

  if (is_selected && s_cursor_just_moved) {
    ImGui::SetScrollHereY(0.4f);
  }

  ImGui::PushID(id);
  ImGui::InvisibleButton("##hitbox", ImVec2(card_w, card_h));
  bool is_hovered = ImGui::IsItemHovered();
  bool is_clicked = ImGui::IsItemClicked();

  if (is_hovered && (io.MouseDelta.x != 0.0f || io.MouseDelta.y != 0.0f)) {
    s_menu_cursor = index;
    is_selected = true;
  }
  if (is_clicked) {
    s_menu_cursor = index;
    is_selected = true;
  }

  ImDrawList *draw_list = ImGui::GetWindowDrawList();
  ImVec2 min_p = cursor_pos;
  ImVec2 max_p = ImVec2(cursor_pos.x + card_w, cursor_pos.y + card_h);

  // Background and Border
  if (is_selected) {
    draw_list->AddRectFilled(min_p, max_p, is_danger ? IM_COL32(100, 30, 30, 240) : IM_COL32(30, 50, 38, 240), 7.0f);
    draw_list->AddRect(min_p, max_p, is_danger ? IM_COL32(255, 90, 90, 255) : IM_COL32(250, 217, 64, 255), 7.0f, 0, 2.0f);
  } else if (is_hovered) {
    draw_list->AddRectFilled(min_p, max_p, is_danger ? IM_COL32(70, 22, 22, 200) : IM_COL32(24, 36, 30, 200), 7.0f);
    draw_list->AddRect(min_p, max_p, is_danger ? IM_COL32(200, 70, 70, 180) : IM_COL32(80, 115, 95, 180), 7.0f, 0, 1.2f);
  } else {
    draw_list->AddRectFilled(min_p, max_p, is_danger ? IM_COL32(40, 18, 18, 170) : IM_COL32(18, 24, 20, 180), 7.0f);
    draw_list->AddRect(min_p, max_p, is_danger ? IM_COL32(110, 40, 40, 120) : IM_COL32(45, 62, 52, 120), 7.0f, 0, 1.0f);
  }

  // 1. Header Line: Title (left)
  float text_x = min_p.x + 18.0f;
  float title_y = min_p.y + 9.0f;

  if (is_selected) {
    draw_list->AddText(ImVec2(text_x, title_y), is_danger ? IM_COL32(255, 110, 110, 255) : IM_COL32(250, 217, 64, 255), ">");
    text_x += 20.0f;
  }

  ImU32 title_col = is_selected ? (is_danger ? IM_COL32(255, 130, 130, 255) : IM_COL32(255, 235, 100, 255))
                                : (is_danger ? IM_COL32(240, 160, 160, 255) : IM_COL32(240, 245, 240, 255));
  draw_list->AddText(ImVec2(text_x, title_y), title_col, title);

  // Status Badge Pill on top-right
  ImVec2 badge_sz = ImGui::CalcTextSize(btn_label);
  float badge_w = badge_sz.x + 24.0f * font_scale;
  float badge_h = 24.0f * font_scale;
  float badge_x = max_p.x - badge_w - 16.0f;
  float badge_y = min_p.y + 8.0f;

  ImVec2 b_min = ImVec2(badge_x, badge_y);
  ImVec2 b_max = ImVec2(badge_x + badge_w, badge_y + badge_h);
  ImU32 b_bg = is_danger ? IM_COL32(180, 40, 40, 230) : IM_COL32(45, 110, 70, 220);
  ImU32 b_border = is_danger ? IM_COL32(255, 90, 90, 240) : (is_selected ? IM_COL32(250, 217, 64, 240) : IM_COL32(70, 140, 95, 180));

  draw_list->AddRectFilled(b_min, b_max, b_bg, 4.0f);
  draw_list->AddRect(b_min, b_max, b_border, 4.0f, 0, 1.2f);
  draw_list->AddText(ImVec2(b_min.x + (badge_w - badge_sz.x) * 0.5f, b_min.y + (badge_h - badge_sz.y) * 0.5f),
                     IM_COL32(255, 255, 255, 255), btn_label);

  // 2. Full-Width Description
  float desc_y = title_y + line_h + 2.0f;
  if (desc && desc[0]) {
    draw_list->AddText(ImVec2(text_x, desc_y), IM_COL32(148, 175, 160, 255), desc);
  }

  // 3. Wide Action Bar across card
  float ctrl_y = desc_y + line_h + 5.0f;
  float ctrl_h = 26.0f * font_scale;
  ImVec2 bar_min = ImVec2(min_p.x + 16.0f, ctrl_y);
  ImVec2 bar_max = ImVec2(max_p.x - 16.0f, ctrl_y + ctrl_h);

  ImU32 bar_bg = is_danger ? (is_selected ? IM_COL32(190, 45, 45, 240) : IM_COL32(130, 32, 32, 210))
                           : (is_selected ? IM_COL32(50, 115, 75, 240) : IM_COL32(28, 65, 42, 210));
  ImU32 bar_border = is_danger ? IM_COL32(255, 90, 90, 230)
                               : (is_selected ? IM_COL32(250, 217, 64, 255) : IM_COL32(65, 120, 85, 180));

  draw_list->AddRectFilled(bar_min, bar_max, bar_bg, 5.0f);
  draw_list->AddRect(bar_min, bar_max, bar_border, 5.0f, 0, is_selected ? 1.5f : 1.0f);

  char act_str[128];
  snprintf(act_str, sizeof(act_str), "%s  (Pressione Botao A)", btn_label);
  ImVec2 asz = ImGui::CalcTextSize(act_str);
  draw_list->AddText(ImVec2(bar_min.x + (bar_max.x - bar_min.x - asz.x) * 0.5f, bar_min.y + (ctrl_h - asz.y) * 0.5f),
                     IM_COL32(255, 255, 255, 255), act_str);

  bool triggered = (is_selected && s_nav.confirm) || is_clicked;

  ImGui::PopID();
  return triggered;
}

// Card de Informação Estática (Sobre o projeto, recursos, etc.)
static void Card_Info(
    int index,
    const char *id,
    const char *title,
    const char *desc,
    const char *badge_label)
{
  ImGuiIO& io = ImGui::GetIO();
  float card_w = ImGui::GetContentRegionAvail().x;
  if (card_w < 100.0f) card_w = 400.0f;
  float font_scale = io.FontGlobalScale;
  float line_h = ImGui::GetTextLineHeight();
  float card_h = 58.0f * font_scale;

  ImVec2 cursor_pos = ImGui::GetCursorScreenPos();
  bool is_selected = (s_menu_cursor == index);

  if (is_selected && s_cursor_just_moved) {
    ImGui::SetScrollHereY(0.4f);
  }

  ImGui::PushID(id);
  ImGui::InvisibleButton("##hitbox", ImVec2(card_w, card_h));
  bool is_hovered = ImGui::IsItemHovered();
  bool is_clicked = ImGui::IsItemClicked();

  if (is_hovered && (io.MouseDelta.x != 0.0f || io.MouseDelta.y != 0.0f)) {
    s_menu_cursor = index;
    is_selected = true;
  }
  if (is_clicked) {
    s_menu_cursor = index;
    is_selected = true;
  }

  ImDrawList *draw_list = ImGui::GetWindowDrawList();
  ImVec2 min_p = cursor_pos;
  ImVec2 max_p = ImVec2(cursor_pos.x + card_w, cursor_pos.y + card_h);

  // Background and Border
  if (is_selected) {
    draw_list->AddRectFilled(min_p, max_p, IM_COL32(30, 50, 38, 240), 7.0f);
    draw_list->AddRect(min_p, max_p, IM_COL32(250, 217, 64, 255), 7.0f, 0, 2.0f);
  } else if (is_hovered) {
    draw_list->AddRectFilled(min_p, max_p, IM_COL32(24, 36, 30, 200), 7.0f);
    draw_list->AddRect(min_p, max_p, IM_COL32(80, 115, 95, 180), 7.0f, 0, 1.2f);
  } else {
    draw_list->AddRectFilled(min_p, max_p, IM_COL32(18, 24, 20, 180), 7.0f);
    draw_list->AddRect(min_p, max_p, IM_COL32(45, 62, 52, 120), 7.0f, 0, 1.0f);
  }

  // 1. Header Line: Title (left)
  float text_x = min_p.x + 18.0f;
  float title_y = min_p.y + 9.0f;

  if (is_selected) {
    draw_list->AddText(ImVec2(text_x, title_y), IM_COL32(250, 217, 64, 255), ">");
    text_x += 20.0f;
  }

  ImU32 title_col = is_selected ? IM_COL32(255, 235, 100, 255) : IM_COL32(240, 245, 240, 255);
  draw_list->AddText(ImVec2(text_x, title_y), title_col, title);

  // Right Badge Pill
  if (badge_label && badge_label[0]) {
    ImVec2 badge_sz = ImGui::CalcTextSize(badge_label);
    float badge_w = badge_sz.x + 24.0f * font_scale;
    float badge_h = 24.0f * font_scale;
    float badge_x = max_p.x - badge_w - 16.0f;
    float badge_y = min_p.y + (card_h - badge_h) * 0.5f;

    ImVec2 b_min = ImVec2(badge_x, badge_y);
    ImVec2 b_max = ImVec2(badge_x + badge_w, badge_y + badge_h);
    draw_list->AddRectFilled(b_min, b_max, IM_COL32(30, 75, 50, 200), 4.0f);
    draw_list->AddRect(b_min, b_max, is_selected ? IM_COL32(250, 217, 64, 220) : IM_COL32(65, 110, 85, 160), 4.0f, 0, 1.2f);
    draw_list->AddText(ImVec2(b_min.x + (badge_w - badge_sz.x) * 0.5f, b_min.y + (badge_h - badge_sz.y) * 0.5f),
                       is_selected ? IM_COL32(255, 235, 120, 255) : IM_COL32(200, 230, 210, 255), badge_label);
  }

  // 2. Full-Width Description
  float desc_y = title_y + line_h + 2.0f;
  if (desc && desc[0]) {
    draw_list->AddText(ImVec2(text_x, desc_y), IM_COL32(148, 175, 160, 255), desc);
  }

  ImGui::PopID();
}

// =============================================================================
// RENDERIZAÇÃO DO MENU HIERÁRQUICO
// =============================================================================

static void RenderOverlayWindow() {
  ImGuiIO& io = ImGui::GetIO();
  ImGuiViewport* viewport = ImGui::GetMainViewport();

  // Leitura de entradas unificadas (D-Pad, Analog Stick, Teclado)
  s_nav = ReadNavInputs();

  // Atalho Start para fechar rapidamente o menu e voltar ao jogo
  if (ImGui::IsKeyPressed(ImGuiKey_GamepadStart, false)) {
    Overlay_Toggle();
    return;
  }

  // Botão B ou Tecla ESC para voltar ao Menu Principal ou fechar
  if (s_nav.cancel) {
    if (s_current_screen == kScreen_MainMenu) {
      Overlay_Toggle();
      return;
    } else {
      ReturnToMainMenu();
      return;
    }
  }

  ImGui::SetNextWindowPos(viewport->Pos, ImGuiCond_Always);
  ImGui::SetNextWindowSize(viewport->Size, ImGuiCond_Always);

  if (viewport->Size.y >= 1400.0f) {
    io.FontGlobalScale = 1.35f;
  } else if (viewport->Size.y >= 900.0f) {
    io.FontGlobalScale = 1.15f;
  } else if (viewport->Size.y < 550.0f) {
    io.FontGlobalScale = 0.85f;
  } else {
    io.FontGlobalScale = 1.0f;
  }

  ImGuiWindowFlags window_flags = 
      ImGuiWindowFlags_NoResize | 
      ImGuiWindowFlags_NoMove | 
      ImGuiWindowFlags_NoCollapse | 
      ImGuiWindowFlags_NoTitleBar |
      ImGuiWindowFlags_NoBringToFrontOnFocus;

  ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 0.0f);
  ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);
  ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(24.0f, 18.0f));

  if (ImGui::Begin("##FullscreenZeldaGameMenu", &s_overlay_open, window_flags)) {
    // 1. Cabeçalho de Jogo
    ImGui::TextColored(ImVec4(0.98f, 0.85f, 0.25f, 1.0f), "THE LEGEND OF ZELDA: A LINK TO THE PAST");
    ImGui::SameLine();
    if (s_current_screen == kScreen_MainMenu) {
      ImGui::TextColored(ImVec4(0.55f, 0.75f, 0.60f, 1.0f), "|   MENU PRINCIPAL");
    } else if (s_current_screen == kScreen_Video) {
      ImGui::TextColored(ImVec4(0.55f, 0.75f, 0.60f, 1.0f), "|   CONFIGURACOES DE VIDEO & GRAFICOS");
    } else if (s_current_screen == kScreen_Audio) {
      ImGui::TextColored(ImVec4(0.55f, 0.75f, 0.60f, 1.0f), "|   CONFIGURACOES DE AUDIO & MSU-1");
    } else if (s_current_screen == kScreen_Language) {
      ImGui::TextColored(ImVec4(0.55f, 0.75f, 0.60f, 1.0f), "|   SELECAO DE IDIOMA / LANGUAGE");
    } else if (s_current_screen == kScreen_Gameplay) {
      ImGui::TextColored(ImVec4(0.55f, 0.75f, 0.60f, 1.0f), "|   MELHORIAS DE JOGABILIDADE (QOL)");
    } else if (s_current_screen == kScreen_Cheats) {
      ImGui::TextColored(ImVec4(0.55f, 0.75f, 0.60f, 1.0f), "|   TRAPACAS & ESTADOS DE JOGO (SAVES)");
    } else if (s_current_screen == kScreen_About) {
      ImGui::TextColored(ImVec4(0.55f, 0.75f, 0.60f, 1.0f), "|   SOBRE O PROJETO & CONTROLES");
    } else if (s_current_screen == kScreen_ExitConfirm) {
      ImGui::TextColored(ImVec4(0.95f, 0.35f, 0.35f, 1.0f), "|   ENCERRAR O JOGO");
    }

    ImGui::Spacing();
    ImGui::Separator();
    ImGui::Spacing();

    // 2. Área Central de Conteúdo
    float footer_h = 50.0f * io.FontGlobalScale;
    float content_h = ImGui::GetContentRegionAvail().y - footer_h;
    if (content_h < 100.0f) content_h = 100.0f;

    if (ImGui::BeginChild("MenuScrollableBody", ImVec2(0, content_h), false, ImGuiWindowFlags_None)) {

      // =======================================================================
      // TELA 0: MENU PRINCIPAL (LISTA VERTICAL DE CATEGORIAS - 7 ITENS)
      // =======================================================================
      if (s_current_screen == kScreen_MainMenu) {
        UpdateMenuNavigation(7);

        ImGui::TextColored(ImVec4(0.70f, 0.75f, 0.72f, 1.0f), "Selecione uma categoria para configurar:");
        ImGui::Spacing();

        if (Card_MainMenu(0, "menu_video", "VIDEO & GRAFICOS", "Resolucao, tela cheia, proporcao de tela, Modo 7 e filtros visuais")) {
          SetScreen(kScreen_Video);
        }
        ImGui::Spacing();

        if (Card_MainMenu(1, "menu_audio", "AUDIO & MSU-1", "Volume geral, efeitos sonoros e trilhas orquestradas em alta fidelidade")) {
          SetScreen(kScreen_Audio);
        }
        ImGui::Spacing();

        if (Card_MainMenu(2, "menu_lang", "IDIOMA & TEXTOS", "Selecao de traducao (Portugues PT-BR, Ingles, etc.) e recursos de fontes")) {
          SetScreen(kScreen_Language);
        }
        ImGui::Spacing();

        if (Card_MainMenu(3, "menu_gameplay", "JOGABILIDADE (QoL)", "Aceleracao de dialogos, troca rapida L/R e melhorias de conveniencia")) {
          SetScreen(kScreen_Gameplay);
        }
        ImGui::Spacing();

        if (Card_MainMenu(4, "menu_cheats", "TRAPACAS & SAVE STATES", "Acoes rapidas de itens/vida e salvamento em 10 slots de memoria")) {
          SetScreen(kScreen_Cheats);
        }
        ImGui::Spacing();

        if (Card_MainMenu(5, "menu_about", "SOBRE O PROJETO", "Guia completo de controles do menu, historico e creditos do projeto")) {
          SetScreen(kScreen_About);
        }
        ImGui::Spacing();

        if (Card_MainMenu(6, "menu_exit", "SAIR DO JOGO", "Salvar configuracoes e fechar o jogo para a Area de Trabalho", true)) {
          SetScreen(kScreen_ExitConfirm);
        }
      }

      // =======================================================================
      // TELA 1: VÍDEO & GRÁFICOS (13 ITENS)
      // =======================================================================
      else if (s_current_screen == kScreen_Video) {
        UpdateMenuNavigation(13);

        CardSection_Header("EXIBICAO & RESOLUCAO");

        // 0. Resolução com Staging
        const char *res_names[IM_ARRAYSIZE(kStandardResolutions)];
        for (size_t i = 0; i < IM_ARRAYSIZE(kStandardResolutions); i++) {
          res_names[i] = kStandardResolutions[i].name;
        }

        int cur_w = g_config.window_width;
        int cur_h = g_config.window_height;
        if (cur_w == 0 || cur_h == 0) {
          int s = g_config.window_scale ? g_config.window_scale : 3;
          cur_w = (g_config.extended_aspect_ratio * 2 + 256) * s;
          cur_h = (g_config.extend_y ? 240 : 224) * s;
        }
        int active_res_idx = -1;
        for (size_t i = 0; i < IM_ARRAYSIZE(kStandardResolutions); i++) {
          if (kStandardResolutions[i].width == cur_w && kStandardResolutions[i].height == cur_h) {
            active_res_idx = (int)i;
            break;
          }
        }
        if (active_res_idx < 0) active_res_idx = 7;

        bool res_applied = false;
        if (Card_StagedStepper(0, "row_res", "Resolucao da Tela", "Ajusta as dimensoes da janela ou resolucao fisica do monitor", &s_staged.res_idx, active_res_idx, res_names, IM_ARRAYSIZE(res_names), &res_applied)) {
          SetWindowResolution(kStandardResolutions[s_staged.res_idx].width, kStandardResolutions[s_staged.res_idx].height);
          SaveConfigFile(NULL);
          char buf[128];
          snprintf(buf, sizeof(buf), "Resolucao aplicada: %dx%d!", kStandardResolutions[s_staged.res_idx].width, kStandardResolutions[s_staged.res_idx].height);
          SetStatus(buf);
        }
        ImGui::Spacing();

        // 1. Modo de Tela com Staging
        const char *fs_names[] = {
          "Janela (Windowed)",
          "Tela Cheia sem Bordas (Borderless)",
          "Tela Cheia Exclusiva (Fullscreen Direct)"
        };
        bool fs_applied = false;
        if (Card_StagedStepper(1, "row_fs", "Modo de Exibicao", "Alterna entre janela normal e tela cheia", &s_staged.fs_mode, g_config.fullscreen, fs_names, IM_ARRAYSIZE(fs_names), &fs_applied)) {
          g_config.fullscreen = (uint8)s_staged.fs_mode;
          SetFullscreenMode(s_staged.fs_mode);
          SaveConfigFile(NULL);
          SetStatus("Modo de tela aplicado!");
        }
        ImGui::Spacing();

        // 2. Escala com Staging
        const char *scale_names[] = { "1x (SNES Nativo)", "2x", "3x (Padrao)", "4x", "5x", "6x", "7x", "8x", "9x", "10x (Ultra)" };
        int active_scale = (g_config.window_scale >= 1 && g_config.window_scale <= 10) ? (g_config.window_scale - 1) : 2;
        bool scale_applied = false;
        if (Card_StagedStepper(2, "row_scale", "Escala da Janela", "Multiplicador de tamanho dos pixels originais", &s_staged.scale, active_scale, scale_names, IM_ARRAYSIZE(scale_names), &scale_applied)) {
          SetWindowScale(s_staged.scale + 1);
          SaveConfigFile(NULL);
          SetStatus("Escala aplicada!");
        }
        ImGui::Spacing();

        CardSection_Header("PROPORCAO DE TELA & WIDESCREEN");

        // 3. Proporção com Staging
        const char *ar_names[] = {
          "Auto (Ajustar a Janela / Livre)",
          "4:3 (Original SNES)",
          "16:9 (Widescreen Padrao)",
          "16:10 (Handhelds / Telas 16:10)",
          "18:9 (Smartphones / 2:1)",
          "21:9 (Monitores Ultrawide)",
          "32:9 (Super Ultrawide)"
        };
        int active_ar = GetAspectRatioIndex();
        if (active_ar < 0) active_ar = 0;
        bool ar_applied = false;
        if (Card_StagedStepper(3, "row_ar", "Proporcao de Tela (Aspect Ratio)", "Expansao de visao horizontal sem esticar personagens", &s_staged.aspect_ratio, active_ar, ar_names, IM_ARRAYSIZE(ar_names), &ar_applied)) {
          SetAspectRatio(s_staged.aspect_ratio);
          SaveConfigFile(NULL);
          SetStatus("Proporcao de tela aplicada!");
        }
        ImGui::Spacing();

        // 4. Áreas Adjacentes
        bool ext_adj = g_config.extend_adjacent_areas;
        if (Card_Toggle(4, "row_ext_adj", "Carregar Areas Adjacentes no Limite", "Elimina barras pretas ao aproximar da borda do mapa em Widescreen", &ext_adj)) {
          g_config.extend_adjacent_areas = ext_adj;
          SaveConfigFile(NULL);
          SetStatus("Areas adjacentes atualizadas!");
        }
        ImGui::Spacing();

        CardSection_Header("GRAFICOS & FIDELIDADE PPU");

        // 5. Novo Renderizador PPU
        bool new_ppu = g_config.new_renderer;
        if (Card_Toggle(5, "row_ppu", "Renderizador PPU Otimizado", "Processador de imagem multithreaded moderno e veloz", &new_ppu)) {
          g_config.new_renderer = new_ppu;
          SaveConfigFile(NULL);
        }
        ImGui::Spacing();

        // 6. Modo 7 Aprimorado
        bool mode7 = g_config.enhanced_mode7;
        if (Card_Toggle(6, "row_mode7", "Modo 7 Aprimorado em Alta Resolucao", "Renderiza o mapa geral e rotacoes 3D com clareza maxima", &mode7)) {
          g_config.enhanced_mode7 = mode7;
          SaveConfigFile(NULL);
        }
        ImGui::Spacing();

        // 7. Limite de Sprites
        bool no_spr_lim = g_config.no_sprite_limits;
        if (Card_Toggle(7, "row_spr_lim", "Remover Limite de Sprites", "Elimina o piscar (flickering) quando ha muitos monstros na tela", &no_spr_lim)) {
          g_config.no_sprite_limits = no_spr_lim;
          SaveConfigFile(NULL);
        }
        ImGui::Spacing();

        // 8. Filtro Linear
        bool lin_filt = g_config.linear_filtering;
        if (Card_Toggle(8, "row_lin_filt", "Filtro Linear (Bilinear Filtering)", "Suaviza as bordas dos pixels na tela para uma imagem mais macia", &lin_filt)) {
          g_config.linear_filtering = lin_filt;
          SaveConfigFile(NULL);
        }
        ImGui::Spacing();

        CardSection_Header("DESEMPENHO & ACESSIBILIDADE");

        // 9. Limitar 60 FPS
        bool limit_60 = !g_config.disable_frame_delay;
        if (Card_Toggle(9, "row_lim60", "Limitar em 60 FPS", "Mantem a velocidade e o timing da fisica original do console", &limit_60)) {
          g_config.disable_frame_delay = !limit_60;
          SaveConfigFile(NULL);
        }
        ImGui::Spacing();

        // 10. FPS HUD
        bool fps_hud = g_config.display_fps;
        if (Card_Toggle(10, "row_fps_hud", "Exibir Contador de FPS na Tela (HUD)", "Mostra a taxa real de quadros no canto superior direito", &fps_hud)) {
          g_config.display_fps = fps_hud;
          SaveConfigFile(NULL);
        }
        ImGui::Spacing();

        // 11. Flashes de Luz
        bool dim_flash = (g_config.features0 & kFeatures0_DimFlashes) != 0;
        if (Card_Toggle(11, "row_dim_flash", "Diminuir Flashes de Luz", "Atenua relampagos e claroes (protecao para fotossensibilidade)", &dim_flash)) {
          if (dim_flash) g_config.features0 |= kFeatures0_DimFlashes;
          else g_config.features0 &= ~kFeatures0_DimFlashes;
          SaveConfigFile(NULL);
        }
        ImGui::Spacing();

        // 12. Botão Voltar
        if (Card_Button(12, "btn_back_video", "< VOLTAR AO MENU PRINCIPAL", "Retorna para a lista de categorias do menu principal", "VOLTAR (B)")) {
          ReturnToMainMenu();
        }
      }

      // =======================================================================
      // TELA 2: ÁUDIO & MSU-1 (6 ITENS)
      // =======================================================================
      else if (s_current_screen == kScreen_Audio) {
        UpdateMenuNavigation(6);

        CardSection_Header("AUDIO GLOBAL (MASTER)");

        // 0. Áudio Ativado
        bool audio_en = g_config.enable_audio;
        if (Card_Toggle(0, "row_audio_en", "Audio do Jogo Ativado", "Habilita ou muta completamente todos os efeitos sonoros e musicas", &audio_en)) {
          g_config.enable_audio = audio_en;
          SaveConfigFile(NULL);
        }
        ImGui::Spacing();

        // 1. Volume Geral
        int master_vol = GetMasterVolume();
        if (Card_Slider(1, "row_master_vol", "Volume Geral (Master)", "Volume sonoro global das musicas e efeitos do jogo", &master_vol, 0, 100, 5, "%")) {
          SetMasterVolume(master_vol);
          SaveConfigFile(NULL);
        }
        ImGui::Spacing();

        CardSection_Header("TRILHAS ORQUESTRADAS MSU-1");

        // 2. Modo MSU-1
        const char *msu_names[] = {
          "Desativado (Original SNES)",
          "MSU-1 Padrao (Orquestra em CD)",
          "MSU-1 Deluxe",
          "Opuz (Audio Comprimido)",
          "Deluxe + Opuz"
        };
        int active_msu = 0;
        if (g_config.enable_msu == kMsuEnabled_Msu) active_msu = 1;
        else if (g_config.enable_msu == kMsuEnabled_MsuDeluxe) active_msu = 2;
        else if (g_config.enable_msu == kMsuEnabled_Opuz) active_msu = 3;
        else if (g_config.enable_msu == (kMsuEnabled_MsuDeluxe | kMsuEnabled_Opuz)) active_msu = 4;

        bool msu_applied = false;
        if (Card_StagedStepper(2, "row_msu_mode", "Modo de Audio MSU-1", "Substitui os sintetizadores por orquestra real em alta fidelidade", &s_staged.msu_mode, active_msu, msu_names, IM_ARRAYSIZE(msu_names), &msu_applied)) {
          if (s_staged.msu_mode == 0) g_config.enable_msu = 0;
          else if (s_staged.msu_mode == 1) g_config.enable_msu = kMsuEnabled_Msu;
          else if (s_staged.msu_mode == 2) g_config.enable_msu = kMsuEnabled_MsuDeluxe;
          else if (s_staged.msu_mode == 3) g_config.enable_msu = kMsuEnabled_Opuz;
          else if (s_staged.msu_mode == 4) g_config.enable_msu = kMsuEnabled_MsuDeluxe | kMsuEnabled_Opuz;
          ZeldaEnableMsu(g_config.enable_msu);
          SaveConfigFile(NULL);
          SetStatus("Modo MSU-1 aplicado!");
        }
        ImGui::Spacing();

        // 3. Volume MSU-1
        int msu_vol = g_config.msuvolume;
        if (Card_Slider(3, "row_msu_vol", "Volume das Musicas MSU-1", "Equilibrio sonoro individual das faixas orquestradas", &msu_vol, 0, 100, 5, "%")) {
          g_config.msuvolume = (uint8)msu_vol;
          SaveConfigFile(NULL);
        }
        ImGui::Spacing();

        // 4. Continuar Faixa
        bool resume_msu = g_config.resume_msu;
        if (Card_Toggle(4, "row_resume_msu", "Continuar Faixa ao Retornar", "Retoma a musica de onde parou ao voltar para a mesma area", &resume_msu)) {
          g_config.resume_msu = resume_msu;
          SaveConfigFile(NULL);
        }
        ImGui::Spacing();

        // 5. Botão Voltar
        if (Card_Button(5, "btn_back_audio", "< VOLTAR AO MENU PRINCIPAL", "Retorna para a lista de categorias do menu principal", "VOLTAR (B)")) {
          ReturnToMainMenu();
        }
      }

      // =======================================================================
      // TELA 3: IDIOMA / LANGUAGE (5 ITENS)
      // =======================================================================
      else if (s_current_screen == kScreen_Language) {
        UpdateMenuNavigation(5);

        CardSection_Header("SELECAO DE IDIOMA");

        // 0. Idioma
        int active_lang = 0;
        if (g_config.language) {
          for (int i = 0; i < (int)IM_ARRAYSIZE(kLangCodes); i++) {
            if (strcmp(g_config.language, kLangCodes[i]) == 0) {
              active_lang = i;
              break;
            }
          }
        }

        bool lang_applied = false;
        if (Card_StagedStepper(0, "row_lang", "Idioma do Jogo", "Traducao de textos, nomes de itens e menus em tempo real", &s_staged.lang_idx, active_lang, kLangNames, IM_ARRAYSIZE(kLangNames), &lang_applied)) {
          g_config.language = kLangCodes[s_staged.lang_idx];
          ZeldaSetLanguage(g_config.language);
          SaveConfigFile(NULL);
          SetStatus("Idioma aplicado com sucesso!");
        }
        ImGui::Spacing();

        CardSection_Header("RECURSOS DE TRADUCAO");
        Card_Info(1, "info_ptbr_font", "Suporte a Caracteres Acentuados", "Acentuacao grafica completa (c, a, o, a, e, i, o, u, a, e)", "ATIVO");
        ImGui::Spacing();
        Card_Info(2, "info_ptbr_text", "Dialogos Nativos em Portugues", "Textos extraidos e adaptados diretamente da versao brasileira", "INCLUSO");
        ImGui::Spacing();
        Card_Info(3, "info_ptbr_save", "Persistencia Automatica", "Sua preferencia de idioma e mantida no arquivo zelda3.ini", "GRAVADO");
        ImGui::Spacing();

        // 4. Botão Voltar
        if (Card_Button(4, "btn_back_lang", "< VOLTAR AO MENU PRINCIPAL", "Retorna para a lista de categorias do menu principal", "VOLTAR (B)")) {
          ReturnToMainMenu();
        }
      }

      // =======================================================================
      // TELA 4: JOGABILIDADE - QOL (16 ITENS)
      // =======================================================================
      else if (s_current_screen == kScreen_Gameplay) {
        UpdateMenuNavigation(16);

        CardSection_Header("DIALOGOS & TEXTOS");

        // 0. Aceleração de Diálogos
        const char *fast_diag_modes[] = {
          "Desativado (Original SNES)",
          "Ao Segurar Botao (A/B/X/Y) [Recomendado]",
          "Sempre Rapido"
        };
        bool fd_applied = false;
        if (Card_StagedStepper(0, "row_fast_diag", "Aceleracao de Dialogos", "Acelera a digitacao ate o final da caixa no estilo dos Zeldas modernos", &s_staged.fast_diag, g_config.fast_dialogue, fast_diag_modes, IM_ARRAYSIZE(fast_diag_modes), &fd_applied)) {
          g_config.fast_dialogue = (uint8)s_staged.fast_diag;
          SaveConfigFile(NULL);
          SetStatus("Modo de dialogo aplicado!");
        }
        ImGui::Spacing();

        // 1. Velocidade da Aceleração
        const char *speed_labels[] = {
          "1: Suave (2x)",
          "2: Rapida (4x)",
          "3: Muito Rapida (8x - Padrao)",
          "4: Ultrarrapida (16x)",
          "5: Instantanea (Maxima)"
        };
        int active_spd = g_config.fast_dialogue_speed ? (g_config.fast_dialogue_speed - 1) : 2;
        if (active_spd < 0) active_spd = 0;
        if (active_spd > 4) active_spd = 4;
        bool spd_applied = false;
        if (Card_StagedStepper(1, "row_fast_speed", "Velocidade da Aceleracao", "Rapidez com que as letras preenchem a caixa de mensagem", &s_staged.fast_diag_speed, active_spd, speed_labels, IM_ARRAYSIZE(speed_labels), &spd_applied)) {
          g_config.fast_dialogue_speed = (uint8)(s_staged.fast_diag_speed + 1);
          SaveConfigFile(NULL);
          SetStatus("Velocidade do dialogo aplicada!");
        }
        ImGui::Spacing();

        auto CheckFeatureCard = [](int idx, const char *id, const char *title, const char *desc, uint32_t mask) {
          bool val = (g_config.features0 & mask) != 0;
          if (Card_Toggle(idx, id, title, desc, &val)) {
            if (val) g_config.features0 |= mask;
            else g_config.features0 &= ~mask;
            enhanced_features0 = g_config.features0;
            SaveConfigFile(NULL);
          }
          ImGui::Spacing();
        };

        CardSection_Header("CONTROLES & ACOES RAPIDAS");
        CheckFeatureCard(2, "f_switch_lr", "Troca Rapida de Itens com L / R", "Alterna o item equipado com os botoes de ombro sem abrir o inventario", kFeatures0_SwitchLR);
        CheckFeatureCard(3, "f_switch_lr_lim", "Limitar Troca L/R a 4 Itens", "Restringe a troca rapida aos primeiros quatro itens do inventario", kFeatures0_SwitchLRLimit);
        CheckFeatureCard(4, "f_turn_dash", "Virar de Direcao com Botas de Pegasus", "Permite mudar de rumo durante a corrida com as Pegasus Boots", kFeatures0_TurnWhileDashing);
        CheckFeatureCard(5, "f_collect_sword", "Coletar Itens com a Espada", "Coleta coracoes, rupees e chaves ao acerta-los com golpes de espada", kFeatures0_CollectItemsWithSword);
        CheckFeatureCard(6, "f_pots_sword", "Quebrar Potes com a Master Sword", "Permite estilhacar vasos e jarros atacando com a Master Sword", kFeatures0_BreakPotsWithSword);

        CardSection_Header("ECONOMIA & CAPACIDADE EXPANDIDA");
        CheckFeatureCard(7, "f_carry_rupees", "Carteira Expandida (9999 Rupees)", "Aumenta a capacidade maxima de Rupees para 9999", kFeatures0_CarryMoreRupees);
        CheckFeatureCard(8, "f_more_bombs", "Permitir 4 Bombas Simultaneas", "Permite colocar ate 4 bombas ativas ao mesmo tempo no chao", kFeatures0_MoreActiveBombs);
        CheckFeatureCard(9, "f_yellow_max", "Destacar Itens no Maximo em Amarelo", "Destaca o contador numerico em amarelo quando atinge a capacidade maxima", kFeatures0_ShowMaxItemsInYellow);
        CheckFeatureCard(10, "f_mirror_dark", "Espelho Magico Livre", "Permite usar o Magic Mirror em qualquer lugar para retornar ao Dark World", kFeatures0_MirrorToDarkworld);

        CardSection_Header("CONVENIENCIA & CORRECOES");
        CheckFeatureCard(11, "f_low_health", "Silenciar Bipe de Pouca Vida", "Desativa o alarme sonoro repetitivo quando Link estiver com pouca vida", kFeatures0_DisableLowHealthBeep);
        CheckFeatureCard(12, "f_skip_intro", "Pular Introducao da Triforce", "Pula o logotipo inicial da Triforce pressionando qualquer botao", kFeatures0_SkipIntroOnKeypress);
        CheckFeatureCard(13, "f_cancel_bird", "Cancelar Viagem do Passaro com X", "Cancela a viagem rapida da flauta pressionando o botao X", kFeatures0_CancelBirdTravel);
        CheckFeatureCard(14, "f_misc_fixes", "Correcoes de Glitches Originais", "Aplica correcoes a pequenos bugs visuais conhecidos do cartucho original", kFeatures0_MiscBugFixes);

        // 15. Botão Voltar
        if (Card_Button(15, "btn_back_qol", "< VOLTAR AO MENU PRINCIPAL", "Retorna para a lista de categorias do menu principal", "VOLTAR (B)")) {
          ReturnToMainMenu();
        }
      }

      // =======================================================================
      // TELA 5: TRAPAÇAS & SAVE STATES (8 ITENS)
      // =======================================================================
      else if (s_current_screen == kScreen_Cheats) {
        UpdateMenuNavigation(8);

        CardSection_Header("ACOES RAPIDAS (CHEATS)");

        // 0. Vida & Magia Total
        if (Card_Button(0, "btn_full_life", "Restaurar Vida & Magia Total", "Enche todos os coracoes e a barra de magia imediatamente", "RESTAURAR")) {
          PatchCommand('w');
          SetStatus("Vida e magia restauradas ao maximo!");
        }
        ImGui::Spacing();

        // 1. Full Items
        if (Card_Button(1, "btn_full_items", "99 Bombas, 99 Flechas & 9999 Rupees", "Enche todos os consumiveis e dinheiro ao maximo", "PREENCHER")) {
          PatchCommand('W');
          SetStatus("Itens e rupees preenchidos!");
        }
        ImGui::Spacing();

        // 2. Chave Pequena
        if (Card_Button(2, "btn_give_key", "Ganhar 1 Chave Pequena", "Adiciona uma Small Key ao inventario da dungeon atual", "ADICIONAR")) {
          PatchCommand('o');
          SetStatus("Chave adicionada ao inventario!");
        }
        ImGui::Spacing();

        // 3. Reiniciar Jogo
        if (Card_Button(3, "btn_soft_reset", "Reiniciar Jogo (Soft Reset)", "Executa um reinicio identico ao console original", "REINICIAR", true)) {
          ZeldaReset(true);
          SetStatus("Jogo reiniciado!");
        }
        ImGui::Spacing();

        CardSection_Header("ESTADOS DE JOGO (SAVE STATES)");

        // 4. Slot de Salvamento
        const char *slot_names[] = {
          "Slot 0", "Slot 1", "Slot 2", "Slot 3", "Slot 4",
          "Slot 5", "Slot 6", "Slot 7", "Slot 8", "Slot 9"
        };
        bool slot_applied = false;
        Card_StagedStepper(4, "row_save_slot", "Slot de Salvamento Ativo", "Escolha a particao de memoria para salvar ou carregar", &s_staged.save_slot, s_selected_save_slot, slot_names, IM_ARRAYSIZE(slot_names), &slot_applied);
        s_selected_save_slot = s_staged.save_slot;
        ImGui::Spacing();

        // 5. Salvar Estado
        if (Card_Button(5, "btn_save_slot", "Salvar Estado no Slot", "Grava o estado exato da sua gameplay na memoria", "SALVAR ESTADO")) {
          SaveLoadSlot(kSaveLoad_Save, s_selected_save_slot);
          char buf[64];
          snprintf(buf, sizeof(buf), "Estado salvo no slot %d!", s_selected_save_slot);
          SetStatus(buf);
        }
        ImGui::Spacing();

        // 6. Carregar Estado
        if (Card_Button(6, "btn_load_slot", "Carregar Estado do Slot", "Recupera o estado gravado anteriormente no slot selecionado", "CARREGAR ESTADO")) {
          SaveLoadSlot(kSaveLoad_Load, s_selected_save_slot);
          char buf[64];
          snprintf(buf, sizeof(buf), "Estado carregado do slot %d!", s_selected_save_slot);
          SetStatus(buf);
        }
        ImGui::Spacing();

        // 7. Botão Voltar
        if (Card_Button(7, "btn_back_cheats", "< VOLTAR AO MENU PRINCIPAL", "Retorna para a lista de categorias do menu principal", "VOLTAR (B)")) {
          ReturnToMainMenu();
        }
      }

      // =======================================================================
      // TELA 6: SOBRE O PROJETO & CONTROLES (8 ITENS)
      // =======================================================================
      else if (s_current_screen == kScreen_About) {
        UpdateMenuNavigation(8);

        CardSection_Header("GUIA DE CONTROLES DO MENU");
        Card_Info(0, "guide_dpad", "D-Pad / Analogico", "Navega livremente pelas opcoes de cima a baixo com rolagem automatica", "NAVEGAR");
        ImGui::Spacing();
        Card_Info(1, "guide_lr", "D-Pad < >  /  Analogico", "Percorre e pre-visualiza opcoes seguras com ampla leitura de texto", "EXPLORAR");
        ImGui::Spacing();
        Card_Info(2, "guide_a", "Botao A  /  Tecla Enter", "Entra nas categorias e aplica confirmacoes e modificacoes", "CONFIRMAR");
        ImGui::Spacing();
        Card_Info(3, "guide_b", "Botao B  /  Tecla ESC", "Retorna ao menu principal ou fecha o overlay e volta ao jogo", "VOLTAR");
        ImGui::Spacing();
        Card_Info(4, "guide_combo", "Atalho Global: Start + Select", "Segure ambos juntos no controle a qualquer momento para abrir/fechar", "ATALHO");
        ImGui::Spacing();

        CardSection_Header("SOBRE O PROJETO");
        Card_Info(5, "about_engine", "Motor Nativo em C/C++", "Reimplementacao de Zelda: A Link to the Past com SDL2 e OpenGL", "SNES REV");
        ImGui::Spacing();
        Card_Info(6, "about_trans", "Versao Brasileira PT-BR", "Fontes acentuadas e dialogos nativos integrados ao motor", "PT-BR");
        ImGui::Spacing();

        // 7. Botão Voltar
        if (Card_Button(7, "btn_back_about", "< VOLTAR AO MENU PRINCIPAL", "Retorna para a lista de categorias do menu principal", "VOLTAR (B)")) {
          ReturnToMainMenu();
        }
      }

      // =======================================================================
      // TELA 7: CONFIRMAÇÃO DE SAÍDA (2 ITENS)
      // =======================================================================
      else if (s_current_screen == kScreen_ExitConfirm) {
        UpdateMenuNavigation(2);

        CardSection_Header("ENCERRAR O JOGO");

        ImVec2 avail = ImGui::GetContentRegionAvail();
        float center_w = 640.0f * io.FontGlobalScale;
        if (center_w > avail.x - 20.0f) center_w = avail.x - 20.0f;

        ImGui::Spacing();
        ImGui::SetCursorPosX((avail.x - center_w) * 0.5f);
        ImGui::TextWrapped("Deseja realmente sair de The Legend of Zelda: A Link to the Past e voltar para a Area de Trabalho?");
        ImGui::Spacing();
        ImGui::SetCursorPosX((avail.x - center_w) * 0.5f);
        ImGui::TextColored(ImVec4(0.40f, 0.90f, 0.50f, 1.0f), "O progresso salvo na bateria (SRAM) e as configuracoes do zelda3.ini serao preservados com seguranca.");
        ImGui::Spacing();
        ImGui::Spacing();

        // 0. Continuar Jogando
        if (Card_Button(0, "btn_exit_cancel", "Continuar Jogando", "Cancela a saida e retorna ao Menu Principal", "VOLTAR (B)", false)) {
          ReturnToMainMenu();
        }
        ImGui::Spacing();

        // 1. Sim, Sair do Jogo
        if (Card_Button(1, "btn_exit_confirm", "Sim, Sair do Jogo", "Encerra o aplicativo e descarrega os dispositivos de video e audio", "SAIR AGORA", true)) {
          SaveConfigFile(NULL);
          s_request_exit_game = true;
          SDL_Event quit_ev;
          quit_ev.type = SDL_QUIT;
          SDL_PushEvent(&quit_ev);
        }
      }

    }
    ImGui::EndChild();

    s_cursor_just_moved = false;

    // 3. Rodapé Informativo
    ImGui::Spacing();
    ImGui::Separator();
    ImGui::Spacing();

    if (s_status_message[0] != '\0' && (SDL_GetTicks() - s_status_message_time < 3500)) {
      ImGui::TextColored(ImVec4(0.40f, 0.95f, 0.45f, 1.0f), "[OK] %s", s_status_message);
    } else {
      ImGui::TextColored(ImVec4(0.55f, 0.68f, 0.58f, 1.0f), "Configuracoes salvas automaticamente em zelda3.ini");
    }

    ImGui::SameLine();
    float right_w = 680.0f * io.FontGlobalScale;
    float r_pos = ImGui::GetWindowWidth() - right_w - 20.0f;
    if (r_pos > ImGui::GetCursorPosX()) {
      ImGui::SetCursorPosX(r_pos);
    }

    if (s_current_screen == kScreen_MainMenu) {
      ImGui::TextColored(ImVec4(0.92f, 0.82f, 0.35f, 1.0f), "[D-Pad / Stick]: Navegar | [A]: Entrar na Categoria | [B / Start]: Fechar");
    } else {
      ImGui::TextColored(ImVec4(0.92f, 0.82f, 0.35f, 1.0f), "[D-Pad / Stick]: Navegar | [< >]: Alterar | [A]: Confirmar | [B]: Voltar");
    }
  }
  ImGui::End();
  ImGui::PopStyleVar(3);
}

// HUD de FPS discreto no canto superior direito
static void RenderFpsOverlay(int fps) {
  const float PAD_X = 12.0f;
  const float PAD_Y = 10.0f;
  const ImGuiViewport* viewport = ImGui::GetMainViewport();
  ImVec2 work_pos = viewport->WorkPos;
  ImVec2 work_size = viewport->WorkSize;
  ImVec2 window_pos = ImVec2(work_pos.x + work_size.x - PAD_X, work_pos.y + PAD_Y);
  ImVec2 window_pos_pivot = ImVec2(1.0f, 0.0f);
  ImGui::SetNextWindowPos(window_pos, ImGuiCond_Always, window_pos_pivot);
  ImGui::SetNextWindowBgAlpha(0.65f);

  ImGuiWindowFlags flags = ImGuiWindowFlags_NoDecoration |
                          ImGuiWindowFlags_AlwaysAutoResize |
                          ImGuiWindowFlags_NoSavedSettings |
                          ImGuiWindowFlags_NoFocusOnAppearing |
                          ImGuiWindowFlags_NoNav |
                          ImGuiWindowFlags_NoMove |
                          ImGuiWindowFlags_NoInputs;

  ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 1.0f);
  ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 5.0f);
  ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(8.0f, 4.0f));

  if (ImGui::Begin("##FPS_Overlay", NULL, flags)) {
    ImVec4 col = (fps >= 55) ? ImVec4(0.35f, 0.95f, 0.40f, 1.0f) :
                 (fps >= 30) ? ImVec4(0.95f, 0.85f, 0.20f, 1.0f) :
                               ImVec4(0.95f, 0.30f, 0.30f, 1.0f);
    ImGui::TextColored(col, "%d FPS", fps);
  }
  ImGui::End();

  ImGui::PopStyleVar(3);
}

void Overlay_Render(SDL_Renderer *renderer, bool is_opengl) {
  if (!s_overlay_open && !g_config.display_fps)
    return;

  if (is_opengl) {
    ImGui_ImplOpenGL3_NewFrame();
  } else {
    ImGui_ImplSDLRenderer2_NewFrame();
  }
  ImGui_ImplSDL2_NewFrame();
  ImGui::NewFrame();

  if (g_config.display_fps) {
    RenderFpsOverlay(GetActualFps());
  }

  bool was_open = s_overlay_open;
  if (s_overlay_open) {
    RenderOverlayWindow();
  }

  ImGui::Render();

  if (is_opengl) {
    ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
  } else if (renderer) {
    ImGui_ImplSDLRenderer2_RenderDrawData(ImGui::GetDrawData(), renderer);
  }

  if (was_open && !s_overlay_open) {
    SDL_ShowCursor(SDL_DISABLE);
    SaveConfigFile(NULL);
  }
}
