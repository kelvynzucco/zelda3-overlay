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
}

static bool s_overlay_open = false;
static bool s_is_opengl = false;
static int s_selected_save_slot = 0;
static char s_status_message[128] = "";
static uint32_t s_status_message_time = 0;
static bool s_request_exit_game = false;

// =============================================================================
// MÁQUINA DE ESTADOS DO MENU HIERÁRQUICO
// =============================================================================
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
static bool s_needs_focus_first = true;

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
  "Português do Brasil (PT-BR)",
  "English (US)",
  "Deutsch (Alemão)",
  "Français (Francês)",
  "Español (Espanhol)",
  "Polski (Polonês)",
  "Nederlands (Holandês)",
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
  if (s_staged.res_idx < 0) s_staged.res_idx = 7; // Padrão 1280x720

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

void Overlay_Toggle(void) {
  s_overlay_open = !s_overlay_open;
  SDL_ShowCursor(s_overlay_open ? SDL_ENABLE : SDL_DISABLE);
  if (s_overlay_open) {
    s_current_screen = kScreen_MainMenu;
    s_needs_focus_first = true;
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
    s_needs_focus_first = true;
    SyncStagedSettingsFromActive();
  }
  SDL_ShowCursor(s_overlay_open ? SDL_ENABLE : SDL_DISABLE);
}

bool Overlay_ShouldExit(void) {
  return s_request_exit_game;
}

// =============================================================================
// COMPONENTES DE INTERFACE DE JOGO NATIVA (CONSOLE MENU ROWS & WIDGETS)
// =============================================================================

// Divisor decorativo de seção
static void MenuSection_Header(const char *title) {
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

// Linha de item do Menu Principal (Categorias)
static bool MenuRow_MainMenuItem(
    const char *id,
    const char *title,
    const char *desc,
    bool is_danger = false)
{
  ImGuiIO& io = ImGui::GetIO();
  float row_w = ImGui::GetContentRegionAvail().x;
  if (row_w < 100.0f) row_w = 400.0f;
  float font_scale = io.FontGlobalScale;
  float line_h = ImGui::GetTextLineHeight();
  float row_h = 56.0f * font_scale;

  ImVec2 cursor_pos = ImGui::GetCursorScreenPos();

  ImGui::PushID(id);
  ImGui::PushStyleColor(ImGuiCol_Header, ImVec4(0.0f, 0.0f, 0.0f, 0.0f));
  ImGui::PushStyleColor(ImGuiCol_HeaderHovered, ImVec4(0.0f, 0.0f, 0.0f, 0.0f));
  ImGui::PushStyleColor(ImGuiCol_HeaderActive, ImVec4(0.0f, 0.0f, 0.0f, 0.0f));

  bool clicked = ImGui::Selectable("##item", false, ImGuiSelectableFlags_AllowOverlap, ImVec2(row_w, row_h));
  if (s_needs_focus_first) {
    ImGui::SetItemDefaultFocus();
    s_needs_focus_first = false;
  }
  bool is_focused = ImGui::IsItemFocused();
  bool is_hovered = ImGui::IsItemHovered();
  ImGui::PopStyleColor(3);

  ImDrawList *draw_list = ImGui::GetWindowDrawList();
  ImVec2 min_p = cursor_pos;
  ImVec2 max_p = ImVec2(cursor_pos.x + row_w, cursor_pos.y + row_h);

  if (is_focused) {
    draw_list->AddRectFilled(min_p, max_p, is_danger ? IM_COL32(180, 45, 45, 60) : IM_COL32(199, 166, 56, 55), 8.0f);
    draw_list->AddRect(min_p, max_p, is_danger ? IM_COL32(240, 70, 70, 240) : IM_COL32(250, 217, 64, 240), 8.0f, 0, 2.0f);
  } else if (is_hovered) {
    draw_list->AddRectFilled(min_p, max_p, is_danger ? IM_COL32(120, 30, 30, 100) : IM_COL32(35, 48, 40, 160), 8.0f);
    draw_list->AddRect(min_p, max_p, is_danger ? IM_COL32(180, 50, 50, 180) : IM_COL32(80, 110, 95, 140), 8.0f, 0, 1.0f);
  } else {
    draw_list->AddRectFilled(min_p, max_p, is_danger ? IM_COL32(60, 20, 20, 120) : IM_COL32(20, 26, 22, 140), 8.0f);
    draw_list->AddRect(min_p, max_p, is_danger ? IM_COL32(140, 40, 40, 100) : IM_COL32(50, 68, 58, 90), 8.0f, 0, 1.0f);
  }

  float text_x = min_p.x + 20.0f;
  float title_y = min_p.y + 10.0f;

  if (is_focused) {
    draw_list->AddText(ImVec2(text_x, title_y), is_danger ? IM_COL32(255, 90, 90, 255) : IM_COL32(250, 217, 64, 255), ">");
    text_x += 18.0f;
  }

  ImU32 title_col = is_focused ? (is_danger ? IM_COL32(255, 120, 120, 255) : IM_COL32(255, 235, 110, 255))
                               : (is_danger ? IM_COL32(240, 160, 160, 255) : IM_COL32(240, 242, 238, 255));
  draw_list->AddText(ImVec2(text_x, title_y), title_col, title);

  if (desc && desc[0]) {
    float desc_y = title_y + line_h + 3.0f;
    draw_list->AddText(ImVec2(text_x, desc_y), IM_COL32(145, 170, 155, 255), desc);
  }

  float badge_w = is_danger ? (120.0f * font_scale) : (110.0f * font_scale);
  float badge_h = 32.0f * font_scale;
  float badge_x = max_p.x - badge_w - 16.0f;
  float badge_y = min_p.y + (row_h - badge_h) * 0.5f;

  ImVec2 b_min = ImVec2(badge_x, badge_y);
  ImVec2 b_max = ImVec2(badge_x + badge_w, badge_y + badge_h);

  ImU32 b_bg = is_danger ? (is_focused ? IM_COL32(180, 40, 40, 240) : IM_COL32(100, 25, 25, 180))
                         : (is_focused ? IM_COL32(60, 115, 80, 240) : IM_COL32(28, 42, 34, 180));
  ImU32 b_border = is_danger ? IM_COL32(240, 80, 80, 220)
                             : (is_focused ? IM_COL32(250, 217, 64, 240) : IM_COL32(70, 100, 85, 140));

  draw_list->AddRectFilled(b_min, b_max, b_bg, 4.0f);
  draw_list->AddRect(b_min, b_max, b_border, 4.0f, 0, is_focused ? 1.8f : 1.0f);

  const char *badge_text = is_danger ? "SAIR  [X]" : "ENTRAR  >";
  ImVec2 b_sz = ImGui::CalcTextSize(badge_text);
  float bx = b_min.x + (badge_w - b_sz.x) * 0.5f;
  float by = b_min.y + (badge_h - b_sz.y) * 0.5f;
  draw_list->AddText(ImVec2(bx, by), IM_COL32(255, 255, 255, 255), badge_text);

  bool triggered = false;
  if (is_focused) {
    if (ImGui::IsKeyPressed(ImGuiKey_GamepadFaceDown) ||
        ImGui::IsKeyPressed(ImGuiKey_Enter) ||
        ImGui::IsKeyPressed(ImGuiKey_Space)) {
      triggered = true;
    }
  }

  if (clicked) {
    triggered = true;
  }

  ImGui::PopID();
  return triggered;
}

// Linha com Stepper com STAGING: permite navegar e pré-visualizar sem aplicar!
// Só aplica quando o usuário pressiona [A] ou clica no botão Aplicar.
static bool MenuRow_StagedStepper(
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
  float row_w = ImGui::GetContentRegionAvail().x;
  if (row_w < 100.0f) row_w = 400.0f;
  float font_scale = io.FontGlobalScale;
  float line_h = ImGui::GetTextLineHeight();
  float row_h = (desc && desc[0]) ? (52.0f * font_scale) : (40.0f * font_scale);
  if (row_h < line_h + 14.0f) row_h = line_h + 14.0f;

  ImVec2 cursor_pos = ImGui::GetCursorScreenPos();

  ImGui::PushID(id);
  ImGui::PushStyleColor(ImGuiCol_Header, ImVec4(0.0f, 0.0f, 0.0f, 0.0f));
  ImGui::PushStyleColor(ImGuiCol_HeaderHovered, ImVec4(0.0f, 0.0f, 0.0f, 0.0f));
  ImGui::PushStyleColor(ImGuiCol_HeaderActive, ImVec4(0.0f, 0.0f, 0.0f, 0.0f));

  bool row_clicked = ImGui::Selectable("##row", false, ImGuiSelectableFlags_AllowOverlap, ImVec2(row_w, row_h));
  if (s_needs_focus_first) {
    ImGui::SetItemDefaultFocus();
    s_needs_focus_first = false;
  }
  bool is_focused = ImGui::IsItemFocused();
  bool is_hovered = ImGui::IsItemHovered();
  ImGui::PopStyleColor(3);

  ImDrawList *draw_list = ImGui::GetWindowDrawList();
  ImVec2 min_p = cursor_pos;
  ImVec2 max_p = ImVec2(cursor_pos.x + row_w, cursor_pos.y + row_h);

  bool has_pending_change = (*staged_val != active_val);

  if (is_focused) {
    draw_list->AddRectFilled(min_p, max_p, has_pending_change ? IM_COL32(199, 140, 40, 60) : IM_COL32(199, 166, 56, 50), 6.0f);
    draw_list->AddRect(min_p, max_p, has_pending_change ? IM_COL32(255, 180, 50, 240) : IM_COL32(250, 217, 64, 230), 6.0f, 0, 1.8f);
  } else if (is_hovered) {
    draw_list->AddRectFilled(min_p, max_p, IM_COL32(35, 48, 40, 160), 6.0f);
    draw_list->AddRect(min_p, max_p, IM_COL32(80, 110, 95, 140), 6.0f, 0, 1.0f);
  } else {
    draw_list->AddRectFilled(min_p, max_p, IM_COL32(20, 26, 22, 130), 6.0f);
    draw_list->AddRect(min_p, max_p, IM_COL32(50, 68, 58, 80), 6.0f, 0, 1.0f);
  }

  float text_x = min_p.x + 16.0f;
  float title_y = min_p.y + ((desc && desc[0]) ? 8.0f : ((row_h - line_h) * 0.5f));

  if (is_focused) {
    draw_list->AddText(ImVec2(text_x, title_y), IM_COL32(250, 217, 64, 255), ">");
    text_x += 16.0f;
  }

  ImU32 title_col = is_focused ? IM_COL32(255, 235, 110, 255) : IM_COL32(240, 242, 238, 255);
  draw_list->AddText(ImVec2(text_x, title_y), title_col, title);

  if (desc && desc[0]) {
    float desc_y = title_y + line_h + 3.0f;
    if (has_pending_change) {
      draw_list->AddText(ImVec2(text_x, desc_y), IM_COL32(255, 200, 80, 255), "Alteração pendente - Pressione [A] para aplicar");
    } else {
      draw_list->AddText(ImVec2(text_x, desc_y), IM_COL32(145, 170, 155, 255), desc);
    }
  }

  // Controle Stepper no lado direito: [ < ]  Opção  [ > ]  [ APLICAR (A) ]
  float apply_btn_w = has_pending_change ? (125.0f * font_scale) : (75.0f * font_scale);
  float arrow_btn_w = 30.0f * font_scale;
  float ctrl_h = 30.0f * font_scale;
  float ctrl_y = min_p.y + (row_h - ctrl_h) * 0.5f;

  float total_ctrl_w = 380.0f * font_scale;
  if (total_ctrl_w > row_w * 0.55f) total_ctrl_w = row_w * 0.55f;

  float apply_btn_x = max_p.x - apply_btn_w - 12.0f;
  ImVec2 app_btn_min = ImVec2(apply_btn_x, ctrl_y);
  ImVec2 app_btn_max = ImVec2(apply_btn_x + apply_btn_w, ctrl_y + ctrl_h);

  float stepper_area_w = total_ctrl_w - apply_btn_w - 8.0f;
  float stepper_x = apply_btn_x - stepper_area_w - 8.0f;

  ImVec2 left_btn_min = ImVec2(stepper_x, ctrl_y);
  ImVec2 left_btn_max = ImVec2(stepper_x + arrow_btn_w, ctrl_y + ctrl_h);

  ImVec2 right_btn_min = ImVec2(stepper_x + stepper_area_w - arrow_btn_w, ctrl_y);
  ImVec2 right_btn_max = ImVec2(stepper_x + stepper_area_w, ctrl_y + ctrl_h);

  ImVec2 val_min = ImVec2(left_btn_max.x + 3.0f, ctrl_y);
  ImVec2 val_max = ImVec2(right_btn_min.x - 3.0f, ctrl_y + ctrl_h);

  // Badge da opção
  draw_list->AddRectFilled(val_min, val_max, IM_COL32(12, 16, 14, 230), 4.0f);
  draw_list->AddRect(val_min, val_max, has_pending_change ? IM_COL32(250, 180, 50, 200) : (is_focused ? IM_COL32(199, 166, 56, 180) : IM_COL32(50, 70, 60, 130)), 4.0f);

  const char *opt_text = (*staged_val >= 0 && *staged_val < option_count) ? options[*staged_val] : "";
  ImVec2 opt_sz = ImGui::CalcTextSize(opt_text);
  float opt_x = val_min.x + (val_max.x - val_min.x - opt_sz.x) * 0.5f;
  float opt_y = val_min.y + (ctrl_h - opt_sz.y) * 0.5f;
  if (opt_x < val_min.x + 4.0f) opt_x = val_min.x + 4.0f;
  draw_list->AddText(ImVec2(opt_x, opt_y), has_pending_change ? IM_COL32(255, 225, 120, 255) : (is_focused ? IM_COL32(255, 240, 160, 255) : IM_COL32(220, 230, 220, 255)), opt_text);

  bool in_left = io.MousePos.x >= left_btn_min.x && io.MousePos.x <= left_btn_max.x &&
                 io.MousePos.y >= left_btn_min.y && io.MousePos.y <= left_btn_max.y;
  bool in_right = io.MousePos.x >= right_btn_min.x && io.MousePos.x <= right_btn_max.x &&
                  io.MousePos.y >= right_btn_min.y && io.MousePos.y <= right_btn_max.y;
  bool in_apply = io.MousePos.x >= app_btn_min.x && io.MousePos.x <= app_btn_max.x &&
                  io.MousePos.y >= app_btn_min.y && io.MousePos.y <= app_btn_max.y;

  draw_list->AddRectFilled(left_btn_min, left_btn_max, in_left ? IM_COL32(60, 90, 70, 240) : IM_COL32(26, 36, 30, 200), 4.0f);
  draw_list->AddRect(left_btn_min, left_btn_max, IM_COL32(80, 110, 90, 160), 4.0f);
  ImVec2 al_sz = ImGui::CalcTextSize("<");
  draw_list->AddText(ImVec2(left_btn_min.x + (arrow_btn_w - al_sz.x) * 0.5f, ctrl_y + (ctrl_h - al_sz.y) * 0.5f),
                     in_left ? IM_COL32(255, 230, 100, 255) : IM_COL32(200, 200, 190, 255), "<");

  draw_list->AddRectFilled(right_btn_min, right_btn_max, in_right ? IM_COL32(60, 90, 70, 240) : IM_COL32(26, 36, 30, 200), 4.0f);
  draw_list->AddRect(right_btn_min, right_btn_max, IM_COL32(80, 110, 90, 160), 4.0f);
  ImVec2 ar_sz = ImGui::CalcTextSize(">");
  draw_list->AddText(ImVec2(right_btn_min.x + (arrow_btn_w - ar_sz.x) * 0.5f, ctrl_y + (ctrl_h - ar_sz.y) * 0.5f),
                     in_right ? IM_COL32(255, 230, 100, 255) : IM_COL32(200, 200, 190, 255), ">");

  // Botão de Aplicar ou Badge Ativo
  if (has_pending_change) {
    ImU32 app_bg = (is_focused || in_apply) ? IM_COL32(200, 150, 40, 240) : IM_COL32(160, 110, 30, 200);
    draw_list->AddRectFilled(app_btn_min, app_btn_max, app_bg, 4.0f);
    draw_list->AddRect(app_btn_min, app_btn_max, IM_COL32(255, 220, 80, 255), 4.0f, 0, 1.5f);
    const char *app_str = "APLICAR (A)";
    ImVec2 asz = ImGui::CalcTextSize(app_str);
    draw_list->AddText(ImVec2(app_btn_min.x + (apply_btn_w - asz.x) * 0.5f, ctrl_y + (ctrl_h - asz.y) * 0.5f), IM_COL32(255, 255, 255, 255), app_str);
  } else {
    draw_list->AddRectFilled(app_btn_min, app_btn_max, IM_COL32(25, 45, 32, 180), 4.0f);
    draw_list->AddRect(app_btn_min, app_btn_max, IM_COL32(60, 110, 75, 160), 4.0f);
    const char *act_str = "ATIVO";
    ImVec2 asz = ImGui::CalcTextSize(act_str);
    draw_list->AddText(ImVec2(app_btn_min.x + (apply_btn_w - asz.x) * 0.5f, ctrl_y + (ctrl_h - asz.y) * 0.5f), IM_COL32(140, 200, 160, 255), act_str);
  }

  *out_applied = false;

  // Entrada de navegação: Left/Right só altera a seleção no buffer de exibição (staging)
  if (is_focused) {
    if (ImGui::IsKeyPressed(ImGuiKey_GamepadDpadLeft, true) ||
        ImGui::IsKeyPressed(ImGuiKey_GamepadLStickLeft, true) ||
        ImGui::IsKeyPressed(ImGuiKey_LeftArrow, true)) {
      *staged_val = (*staged_val - 1 + option_count) % option_count;
    } else if (ImGui::IsKeyPressed(ImGuiKey_GamepadDpadRight, true) ||
               ImGui::IsKeyPressed(ImGuiKey_GamepadLStickRight, true) ||
               ImGui::IsKeyPressed(ImGuiKey_RightArrow, true)) {
      *staged_val = (*staged_val + 1) % option_count;
    } else if (ImGui::IsKeyPressed(ImGuiKey_GamepadFaceDown) ||
               ImGui::IsKeyPressed(ImGuiKey_Enter) ||
               ImGui::IsKeyPressed(ImGuiKey_Space)) {
      if (has_pending_change) {
        *out_applied = true;
      }
    }
  }

  if (row_clicked) {
    if (in_left) {
      *staged_val = (*staged_val - 1 + option_count) % option_count;
    } else if (in_right) {
      *staged_val = (*staged_val + 1) % option_count;
    } else if (in_apply || has_pending_change) {
      *out_applied = true;
    } else {
      *staged_val = (*staged_val + 1) % option_count;
    }
  }

  ImGui::PopID();
  return *out_applied;
}

// Linha com Toggle (LIGADO / DESLIGADO)
static bool MenuRow_Toggle(
    const char *id,
    const char *title,
    const char *desc,
    bool *value)
{
  ImGuiIO& io = ImGui::GetIO();
  float row_w = ImGui::GetContentRegionAvail().x;
  if (row_w < 100.0f) row_w = 400.0f;
  float font_scale = io.FontGlobalScale;
  float line_h = ImGui::GetTextLineHeight();
  float row_h = (desc && desc[0]) ? (52.0f * font_scale) : (40.0f * font_scale);
  if (row_h < line_h + 14.0f) row_h = line_h + 14.0f;

  ImVec2 cursor_pos = ImGui::GetCursorScreenPos();

  ImGui::PushID(id);
  ImGui::PushStyleColor(ImGuiCol_Header, ImVec4(0.0f, 0.0f, 0.0f, 0.0f));
  ImGui::PushStyleColor(ImGuiCol_HeaderHovered, ImVec4(0.0f, 0.0f, 0.0f, 0.0f));
  ImGui::PushStyleColor(ImGuiCol_HeaderActive, ImVec4(0.0f, 0.0f, 0.0f, 0.0f));

  bool row_clicked = ImGui::Selectable("##row", false, ImGuiSelectableFlags_AllowOverlap, ImVec2(row_w, row_h));
  if (s_needs_focus_first) {
    ImGui::SetItemDefaultFocus();
    s_needs_focus_first = false;
  }
  bool is_focused = ImGui::IsItemFocused();
  bool is_hovered = ImGui::IsItemHovered();
  ImGui::PopStyleColor(3);

  ImDrawList *draw_list = ImGui::GetWindowDrawList();
  ImVec2 min_p = cursor_pos;
  ImVec2 max_p = ImVec2(cursor_pos.x + row_w, cursor_pos.y + row_h);

  if (is_focused) {
    draw_list->AddRectFilled(min_p, max_p, IM_COL32(199, 166, 56, 50), 6.0f);
    draw_list->AddRect(min_p, max_p, IM_COL32(250, 217, 64, 230), 6.0f, 0, 1.8f);
  } else if (is_hovered) {
    draw_list->AddRectFilled(min_p, max_p, IM_COL32(35, 48, 40, 160), 6.0f);
    draw_list->AddRect(min_p, max_p, IM_COL32(80, 110, 95, 140), 6.0f, 0, 1.0f);
  } else {
    draw_list->AddRectFilled(min_p, max_p, IM_COL32(20, 26, 22, 130), 6.0f);
    draw_list->AddRect(min_p, max_p, IM_COL32(50, 68, 58, 80), 6.0f, 0, 1.0f);
  }

  float text_x = min_p.x + 16.0f;
  float title_y = min_p.y + ((desc && desc[0]) ? 8.0f : ((row_h - line_h) * 0.5f));

  if (is_focused) {
    draw_list->AddText(ImVec2(text_x, title_y), IM_COL32(250, 217, 64, 255), ">");
    text_x += 16.0f;
  }

  ImU32 title_col = is_focused ? IM_COL32(255, 235, 110, 255) : IM_COL32(240, 242, 238, 255);
  draw_list->AddText(ImVec2(text_x, title_y), title_col, title);

  if (desc && desc[0]) {
    float desc_y = title_y + line_h + 3.0f;
    draw_list->AddText(ImVec2(text_x, desc_y), IM_COL32(145, 170, 155, 255), desc);
  }

  float pill_w = 145.0f * font_scale;
  float pill_h = 30.0f * font_scale;
  float pill_x = max_p.x - pill_w - 14.0f;
  float pill_y = min_p.y + (row_h - pill_h) * 0.5f;

  ImVec2 pill_min = ImVec2(pill_x, pill_y);
  ImVec2 pill_max = ImVec2(pill_x + pill_w, pill_y + pill_h);

  bool active = *value;
  ImU32 pill_bg = active ? IM_COL32(35, 95, 55, 230) : IM_COL32(30, 36, 33, 200);
  ImU32 pill_border = active ? (is_focused ? IM_COL32(250, 217, 64, 255) : IM_COL32(60, 180, 95, 220))
                             : (is_focused ? IM_COL32(250, 217, 64, 180) : IM_COL32(70, 80, 75, 150));

  draw_list->AddRectFilled(pill_min, pill_max, pill_bg, 15.0f);
  draw_list->AddRect(pill_min, pill_max, pill_border, 15.0f, 0, is_focused ? 2.0f : 1.5f);

  const char *pill_text = active ? "LIGADO" : "DESLIGADO";
  ImVec2 ts = ImGui::CalcTextSize(pill_text);
  float tx = pill_min.x + (pill_w - ts.x) * 0.5f;
  float ty = pill_min.y + (pill_h - ts.y) * 0.5f;
  draw_list->AddText(ImVec2(tx, ty), active ? IM_COL32(230, 255, 235, 255) : IM_COL32(160, 170, 165, 255), pill_text);

  bool changed = false;
  if (is_focused) {
    if (ImGui::IsKeyPressed(ImGuiKey_GamepadFaceDown) ||
        ImGui::IsKeyPressed(ImGuiKey_GamepadDpadLeft) ||
        ImGui::IsKeyPressed(ImGuiKey_GamepadDpadRight) ||
        ImGui::IsKeyPressed(ImGuiKey_LeftArrow) ||
        ImGui::IsKeyPressed(ImGuiKey_RightArrow) ||
        ImGui::IsKeyPressed(ImGuiKey_Enter) ||
        ImGui::IsKeyPressed(ImGuiKey_Space)) {
      *value = !*value;
      changed = true;
    }
  }

  if (row_clicked) {
    *value = !*value;
    changed = true;
  }

  ImGui::PopID();
  return changed;
}

// Linha com Slider de Volume
static bool MenuRow_Slider(
    const char *id,
    const char *title,
    const char *desc,
    int *value,
    int min_val,
    int max_val,
    int step = 5,
    const char *unit = "%")
{
  ImGuiIO& io = ImGui::GetIO();
  float row_w = ImGui::GetContentRegionAvail().x;
  if (row_w < 100.0f) row_w = 400.0f;
  float font_scale = io.FontGlobalScale;
  float line_h = ImGui::GetTextLineHeight();
  float row_h = (desc && desc[0]) ? (52.0f * font_scale) : (40.0f * font_scale);
  if (row_h < line_h + 14.0f) row_h = line_h + 14.0f;

  ImVec2 cursor_pos = ImGui::GetCursorScreenPos();

  ImGui::PushID(id);
  ImGui::PushStyleColor(ImGuiCol_Header, ImVec4(0.0f, 0.0f, 0.0f, 0.0f));
  ImGui::PushStyleColor(ImGuiCol_HeaderHovered, ImVec4(0.0f, 0.0f, 0.0f, 0.0f));
  ImGui::PushStyleColor(ImGuiCol_HeaderActive, ImVec4(0.0f, 0.0f, 0.0f, 0.0f));

  bool row_clicked = ImGui::Selectable("##row", false, ImGuiSelectableFlags_AllowOverlap, ImVec2(row_w, row_h));
  if (s_needs_focus_first) {
    ImGui::SetItemDefaultFocus();
    s_needs_focus_first = false;
  }
  bool is_focused = ImGui::IsItemFocused();
  bool is_hovered = ImGui::IsItemHovered();
  ImGui::PopStyleColor(3);

  ImDrawList *draw_list = ImGui::GetWindowDrawList();
  ImVec2 min_p = cursor_pos;
  ImVec2 max_p = ImVec2(cursor_pos.x + row_w, cursor_pos.y + row_h);

  if (is_focused) {
    draw_list->AddRectFilled(min_p, max_p, IM_COL32(199, 166, 56, 50), 6.0f);
    draw_list->AddRect(min_p, max_p, IM_COL32(250, 217, 64, 230), 6.0f, 0, 1.8f);
  } else if (is_hovered) {
    draw_list->AddRectFilled(min_p, max_p, IM_COL32(35, 48, 40, 160), 6.0f);
    draw_list->AddRect(min_p, max_p, IM_COL32(80, 110, 95, 140), 6.0f, 0, 1.0f);
  } else {
    draw_list->AddRectFilled(min_p, max_p, IM_COL32(20, 26, 22, 130), 6.0f);
    draw_list->AddRect(min_p, max_p, IM_COL32(50, 68, 58, 80), 6.0f, 0, 1.0f);
  }

  float text_x = min_p.x + 16.0f;
  float title_y = min_p.y + ((desc && desc[0]) ? 8.0f : ((row_h - line_h) * 0.5f));

  if (is_focused) {
    draw_list->AddText(ImVec2(text_x, title_y), IM_COL32(250, 217, 64, 255), ">");
    text_x += 16.0f;
  }

  ImU32 title_col = is_focused ? IM_COL32(255, 235, 110, 255) : IM_COL32(240, 242, 238, 255);
  draw_list->AddText(ImVec2(text_x, title_y), title_col, title);

  if (desc && desc[0]) {
    float desc_y = title_y + line_h + 3.0f;
    draw_list->AddText(ImVec2(text_x, desc_y), IM_COL32(145, 170, 155, 255), desc);
  }

  float bar_ctrl_w = 320.0f * font_scale;
  if (bar_ctrl_w > row_w * 0.48f) bar_ctrl_w = row_w * 0.48f;
  float arrow_btn_w = 30.0f * font_scale;
  float ctrl_h = 30.0f * font_scale;
  float ctrl_y = min_p.y + (row_h - ctrl_h) * 0.5f;
  float ctrl_x = max_p.x - bar_ctrl_w - 14.0f;

  ImVec2 left_btn_min = ImVec2(ctrl_x, ctrl_y);
  ImVec2 left_btn_max = ImVec2(ctrl_x + arrow_btn_w, ctrl_y + ctrl_h);

  ImVec2 right_btn_min = ImVec2(max_p.x - arrow_btn_w - 14.0f, ctrl_y);
  ImVec2 right_btn_max = ImVec2(max_p.x - 14.0f, ctrl_y + ctrl_h);

  float text_box_w = 56.0f * font_scale;
  ImVec2 bar_min = ImVec2(left_btn_max.x + 8.0f, ctrl_y + (ctrl_h - 10.0f) * 0.5f);
  ImVec2 bar_max = ImVec2(right_btn_min.x - text_box_w - 8.0f, bar_min.y + 10.0f);

  draw_list->AddRectFilled(bar_min, bar_max, IM_COL32(15, 22, 18, 255), 5.0f);
  draw_list->AddRect(bar_min, bar_max, IM_COL32(60, 80, 70, 180), 5.0f);

  float frac = (float)(*value - min_val) / (float)(max_val - min_val);
  if (frac < 0.0f) frac = 0.0f;
  if (frac > 1.0f) frac = 1.0f;

  if (frac > 0.0f) {
    ImVec2 fill_max = ImVec2(bar_min.x + (bar_max.x - bar_min.x) * frac, bar_max.y);
    draw_list->AddRectFilled(bar_min, fill_max, is_focused ? IM_COL32(250, 217, 64, 255) : IM_COL32(60, 180, 95, 220), 5.0f);
  }

  char val_str[32];
  snprintf(val_str, sizeof(val_str), "%d%s", *value, unit);
  ImVec2 val_sz = ImGui::CalcTextSize(val_str);
  float val_x = bar_max.x + 8.0f + (text_box_w - val_sz.x) * 0.5f;
  float val_y = ctrl_y + (ctrl_h - val_sz.y) * 0.5f;
  draw_list->AddText(ImVec2(val_x, val_y), is_focused ? IM_COL32(255, 230, 100, 255) : IM_COL32(220, 230, 220, 255), val_str);

  bool in_left = io.MousePos.x >= left_btn_min.x && io.MousePos.x <= left_btn_max.x &&
                 io.MousePos.y >= left_btn_min.y && io.MousePos.y <= left_btn_max.y;
  bool in_right = io.MousePos.x >= right_btn_min.x && io.MousePos.x <= right_btn_max.x &&
                  io.MousePos.y >= right_btn_min.y && io.MousePos.y <= right_btn_max.y;

  draw_list->AddRectFilled(left_btn_min, left_btn_max, in_left ? IM_COL32(60, 90, 70, 240) : IM_COL32(26, 36, 30, 200), 4.0f);
  draw_list->AddRect(left_btn_min, left_btn_max, IM_COL32(80, 110, 90, 160), 4.0f);
  ImVec2 al_sz = ImGui::CalcTextSize("<");
  draw_list->AddText(ImVec2(left_btn_min.x + (arrow_btn_w - al_sz.x) * 0.5f, ctrl_y + (ctrl_h - al_sz.y) * 0.5f),
                     in_left ? IM_COL32(255, 230, 100, 255) : IM_COL32(200, 200, 190, 255), "<");

  draw_list->AddRectFilled(right_btn_min, right_btn_max, in_right ? IM_COL32(60, 90, 70, 240) : IM_COL32(26, 36, 30, 200), 4.0f);
  draw_list->AddRect(right_btn_min, right_btn_max, IM_COL32(80, 110, 90, 160), 4.0f);
  ImVec2 ar_sz = ImGui::CalcTextSize(">");
  draw_list->AddText(ImVec2(right_btn_min.x + (arrow_btn_w - ar_sz.x) * 0.5f, ctrl_y + (ctrl_h - ar_sz.y) * 0.5f),
                     in_right ? IM_COL32(255, 230, 100, 255) : IM_COL32(200, 200, 190, 255), ">");

  bool changed = false;
  if (is_focused) {
    if (ImGui::IsKeyPressed(ImGuiKey_GamepadDpadLeft, true) ||
        ImGui::IsKeyPressed(ImGuiKey_GamepadLStickLeft, true) ||
        ImGui::IsKeyPressed(ImGuiKey_LeftArrow, true)) {
      *value -= step;
      if (*value < min_val) *value = min_val;
      changed = true;
    } else if (ImGui::IsKeyPressed(ImGuiKey_GamepadDpadRight, true) ||
               ImGui::IsKeyPressed(ImGuiKey_GamepadLStickRight, true) ||
               ImGui::IsKeyPressed(ImGuiKey_RightArrow, true)) {
      *value += step;
      if (*value > max_val) *value = max_val;
      changed = true;
    }
  }

  if (row_clicked) {
    if (in_left) {
      *value -= step;
      if (*value < min_val) *value = min_val;
      changed = true;
    } else if (in_right) {
      *value += step;
      if (*value > max_val) *value = max_val;
      changed = true;
    } else if (io.MousePos.x >= bar_min.x && io.MousePos.x <= bar_max.x) {
      float click_frac = (io.MousePos.x - bar_min.x) / (bar_max.x - bar_min.x);
      *value = min_val + (int)(click_frac * (max_val - min_val) + 0.5f);
      if (*value < min_val) *value = min_val;
      if (*value > max_val) *value = max_val;
      changed = true;
    }
  }

  ImGui::PopID();
  return changed;
}

// Linha com Botão de Ação
static bool MenuRow_Button(
    const char *id,
    const char *title,
    const char *desc,
    const char *button_label,
    bool danger = false)
{
  ImGuiIO& io = ImGui::GetIO();
  float row_w = ImGui::GetContentRegionAvail().x;
  if (row_w < 100.0f) row_w = 400.0f;
  float font_scale = io.FontGlobalScale;
  float line_h = ImGui::GetTextLineHeight();
  float row_h = (desc && desc[0]) ? (52.0f * font_scale) : (40.0f * font_scale);
  if (row_h < line_h + 14.0f) row_h = line_h + 14.0f;

  ImVec2 cursor_pos = ImGui::GetCursorScreenPos();

  ImGui::PushID(id);
  ImGui::PushStyleColor(ImGuiCol_Header, ImVec4(0.0f, 0.0f, 0.0f, 0.0f));
  ImGui::PushStyleColor(ImGuiCol_HeaderHovered, ImVec4(0.0f, 0.0f, 0.0f, 0.0f));
  ImGui::PushStyleColor(ImGuiCol_HeaderActive, ImVec4(0.0f, 0.0f, 0.0f, 0.0f));

  bool row_clicked = ImGui::Selectable("##row", false, ImGuiSelectableFlags_AllowOverlap, ImVec2(row_w, row_h));
  if (s_needs_focus_first) {
    ImGui::SetItemDefaultFocus();
    s_needs_focus_first = false;
  }
  bool is_focused = ImGui::IsItemFocused();
  bool is_hovered = ImGui::IsItemHovered();
  ImGui::PopStyleColor(3);

  ImDrawList *draw_list = ImGui::GetWindowDrawList();
  ImVec2 min_p = cursor_pos;
  ImVec2 max_p = ImVec2(cursor_pos.x + row_w, cursor_pos.y + row_h);

  if (is_focused) {
    draw_list->AddRectFilled(min_p, max_p, danger ? IM_COL32(180, 50, 50, 55) : IM_COL32(199, 166, 56, 50), 6.0f);
    draw_list->AddRect(min_p, max_p, danger ? IM_COL32(240, 70, 70, 230) : IM_COL32(250, 217, 64, 230), 6.0f, 0, 1.8f);
  } else if (is_hovered) {
    draw_list->AddRectFilled(min_p, max_p, IM_COL32(35, 48, 40, 160), 6.0f);
    draw_list->AddRect(min_p, max_p, IM_COL32(80, 110, 95, 140), 6.0f, 0, 1.0f);
  } else {
    draw_list->AddRectFilled(min_p, max_p, IM_COL32(20, 26, 22, 130), 6.0f);
    draw_list->AddRect(min_p, max_p, IM_COL32(50, 68, 58, 80), 6.0f, 0, 1.0f);
  }

  float text_x = min_p.x + 16.0f;
  float title_y = min_p.y + ((desc && desc[0]) ? 8.0f : ((row_h - line_h) * 0.5f));

  if (is_focused) {
    draw_list->AddText(ImVec2(text_x, title_y), danger ? IM_COL32(255, 90, 90, 255) : IM_COL32(250, 217, 64, 255), ">");
    text_x += 16.0f;
  }

  ImU32 title_col = is_focused ? (danger ? IM_COL32(255, 120, 120, 255) : IM_COL32(255, 235, 110, 255))
                               : IM_COL32(240, 242, 238, 255);
  draw_list->AddText(ImVec2(text_x, title_y), title_col, title);

  if (desc && desc[0]) {
    float desc_y = title_y + line_h + 3.0f;
    draw_list->AddText(ImVec2(text_x, desc_y), IM_COL32(145, 170, 155, 255), desc);
  }

  float btn_w = 175.0f * font_scale;
  float btn_h = 32.0f * font_scale;
  float btn_x = max_p.x - btn_w - 14.0f;
  float btn_y = min_p.y + (row_h - btn_h) * 0.5f;

  ImVec2 b_min = ImVec2(btn_x, btn_y);
  ImVec2 b_max = ImVec2(btn_x + btn_w, btn_y + btn_h);

  ImU32 btn_bg = danger ? (is_focused ? IM_COL32(190, 40, 40, 240) : IM_COL32(130, 30, 30, 210))
                        : (is_focused ? IM_COL32(55, 115, 75, 240) : IM_COL32(32, 65, 45, 210));
  ImU32 btn_border = danger ? IM_COL32(240, 80, 80, 220)
                            : (is_focused ? IM_COL32(250, 217, 64, 255) : IM_COL32(75, 130, 95, 180));

  draw_list->AddRectFilled(b_min, b_max, btn_bg, 5.0f);
  draw_list->AddRect(b_min, b_max, btn_border, 5.0f, 0, is_focused ? 2.0f : 1.5f);

  ImVec2 bs = ImGui::CalcTextSize(button_label);
  float bx = b_min.x + (btn_w - bs.x) * 0.5f;
  float by = b_min.y + (btn_h - bs.y) * 0.5f;
  draw_list->AddText(ImVec2(bx, by), IM_COL32(255, 255, 255, 255), button_label);

  bool triggered = false;
  if (is_focused) {
    if (ImGui::IsKeyPressed(ImGuiKey_GamepadFaceDown) ||
        ImGui::IsKeyPressed(ImGuiKey_Enter) ||
        ImGui::IsKeyPressed(ImGuiKey_Space)) {
      triggered = true;
    }
  }

  if (row_clicked) {
    triggered = true;
  }

  ImGui::PopID();
  return triggered;
}

// =============================================================================
// RENDERIZAÇÃO DO MENU HIERÁRQUICO
// =============================================================================

static void RenderOverlayWindow() {
  ImGuiIO& io = ImGui::GetIO();
  ImGuiViewport* viewport = ImGui::GetMainViewport();

  // Tratamento global do botão B ou Tecla ESC para voltar ou fechar
  if (ImGui::IsKeyPressed(ImGuiKey_GamepadFaceRight) || ImGui::IsKeyPressed(ImGuiKey_Escape)) {
    if (s_current_screen == kScreen_MainMenu) {
      Overlay_Toggle();
      return;
    } else {
      // Retorna para o Menu Principal
      s_current_screen = kScreen_MainMenu;
      s_needs_focus_first = true;
      SyncStagedSettingsFromActive();
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
      ImGui::TextColored(ImVec4(0.55f, 0.75f, 0.60f, 1.0f), "|   CONFIGURAÇÕES DE VÍDEO & GRÁFICOS");
    } else if (s_current_screen == kScreen_Audio) {
      ImGui::TextColored(ImVec4(0.55f, 0.75f, 0.60f, 1.0f), "|   CONFIGURAÇÕES DE ÁUDIO & MSU-1");
    } else if (s_current_screen == kScreen_Language) {
      ImGui::TextColored(ImVec4(0.55f, 0.75f, 0.60f, 1.0f), "|   SELEÇÃO DE IDIOMA / LANGUAGE");
    } else if (s_current_screen == kScreen_Gameplay) {
      ImGui::TextColored(ImVec4(0.55f, 0.75f, 0.60f, 1.0f), "|   MELHORIAS DE JOGABILIDADE (QOL)");
    } else if (s_current_screen == kScreen_Cheats) {
      ImGui::TextColored(ImVec4(0.55f, 0.75f, 0.60f, 1.0f), "|   TRAPAÇAS & ESTADOS DE JOGO (SAVES)");
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
      // TELA 0: MENU PRINCIPAL (LISTA VERTICAL DE CATEGORIAS)
      // =======================================================================
      if (s_current_screen == kScreen_MainMenu) {
        ImGui::TextColored(ImVec4(0.70f, 0.75f, 0.72f, 1.0f), "Selecione uma categoria para configurar:");
        ImGui::Spacing();

        if (MenuRow_MainMenuItem("menu_video", "VÍDEO & GRÁFICOS", "Resolução, tela cheia, proporção de tela, Modo 7 e filtros visuais")) {
          s_current_screen = kScreen_Video;
          s_needs_focus_first = true;
          SyncStagedSettingsFromActive();
        }
        ImGui::Spacing();

        if (MenuRow_MainMenuItem("menu_audio", "ÁUDIO & MSU-1", "Volume geral, efeitos sonoros e trilhas orquestradas em alta fidelidade")) {
          s_current_screen = kScreen_Audio;
          s_needs_focus_first = true;
          SyncStagedSettingsFromActive();
        }
        ImGui::Spacing();

        if (MenuRow_MainMenuItem("menu_lang", "IDIOMA & TEXTOS", "Seleção de tradução (Português PT-BR, Inglês, etc.) e recursos de fontes")) {
          s_current_screen = kScreen_Language;
          s_needs_focus_first = true;
          SyncStagedSettingsFromActive();
        }
        ImGui::Spacing();

        if (MenuRow_MainMenuItem("menu_gameplay", "JOGABILIDADE (QoL)", "Aceleração de diálogos, troca rápida L/R e melhorias de conveniência")) {
          s_current_screen = kScreen_Gameplay;
          s_needs_focus_first = true;
          SyncStagedSettingsFromActive();
        }
        ImGui::Spacing();

        if (MenuRow_MainMenuItem("menu_cheats", "TRAPAÇAS & SAVE STATES", "Ações rápidas de itens/vida e salvamento em 10 slots de memória")) {
          s_current_screen = kScreen_Cheats;
          s_needs_focus_first = true;
          SyncStagedSettingsFromActive();
        }
        ImGui::Spacing();

        if (MenuRow_MainMenuItem("menu_about", "SOBRE O PROJETO", "Guia completo de controles do menu, histórico e créditos do projeto")) {
          s_current_screen = kScreen_About;
          s_needs_focus_first = true;
        }
        ImGui::Spacing();

        if (MenuRow_MainMenuItem("menu_exit", "SAIR DO JOGO", "Salvar configurações e fechar o jogo para a Área de Trabalho", true)) {
          s_current_screen = kScreen_ExitConfirm;
          s_needs_focus_first = true;
        }
      }

      // =======================================================================
      // TELA 1: VÍDEO & GRÁFICOS
      // =======================================================================
      else if (s_current_screen == kScreen_Video) {
        if (MenuRow_Button("btn_back_video", "◄ VOLTAR AO MENU PRINCIPAL", "Retorna para a lista de categorias do menu", "VOLTAR (B)")) {
          s_current_screen = kScreen_MainMenu;
          s_needs_focus_first = true;
          SyncStagedSettingsFromActive();
        }
        ImGui::Spacing();

        MenuSection_Header("EXIBIÇÃO & RESOLUÇÃO");

        // 1. Resolução com Staging
        const char *res_names[IM_ARRAYSIZE(kStandardResolutions)];
        for (size_t i = 0; i < IM_ARRAYSIZE(kStandardResolutions); i++) {
          res_names[i] = kStandardResolutions[i].name;
        }

        // Obtém resolução atual real para comparação
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
        if (MenuRow_StagedStepper("row_res", "Resolução da Tela", "Ajusta as dimensões da janela ou resolução física", &s_staged.res_idx, active_res_idx, res_names, IM_ARRAYSIZE(res_names), &res_applied)) {
          SetWindowResolution(kStandardResolutions[s_staged.res_idx].width, kStandardResolutions[s_staged.res_idx].height);
          SaveConfigFile(NULL);
          char buf[128];
          snprintf(buf, sizeof(buf), "Resolução aplicada: %dx%d!", kStandardResolutions[s_staged.res_idx].width, kStandardResolutions[s_staged.res_idx].height);
          SetStatus(buf);
        }

        // 2. Modo de Tela com Staging
        const char *fs_names[] = {
          "Janela (Windowed)",
          "Tela Cheia sem Bordas (Borderless)",
          "Tela Cheia Exclusiva (Fullscreen)"
        };
        bool fs_applied = false;
        if (MenuRow_StagedStepper("row_fs", "Modo de Exibição", "Alterna entre janela normal e tela cheia", &s_staged.fs_mode, g_config.fullscreen, fs_names, IM_ARRAYSIZE(fs_names), &fs_applied)) {
          g_config.fullscreen = (uint8)s_staged.fs_mode;
          SetFullscreenMode(s_staged.fs_mode);
          SaveConfigFile(NULL);
          SetStatus("Modo de tela aplicado!");
        }

        // 3. Escala com Staging
        const char *scale_names[] = { "1x (SNES Nativo)", "2x", "3x (Padrão)", "4x", "5x", "6x", "7x", "8x", "9x", "10x (Ultra)" };
        int active_scale = (g_config.window_scale >= 1 && g_config.window_scale <= 10) ? (g_config.window_scale - 1) : 2;
        bool scale_applied = false;
        if (MenuRow_StagedStepper("row_scale", "Escala da Janela", "Multiplicador de tamanho dos pixels originais", &s_staged.scale, active_scale, scale_names, IM_ARRAYSIZE(scale_names), &scale_applied)) {
          SetWindowScale(s_staged.scale + 1);
          SaveConfigFile(NULL);
          SetStatus("Escala aplicada!");
        }

        MenuSection_Header("PROPORÇÃO DE TELA & WIDESCREEN");

        // 4. Proporção com Staging
        const char *ar_names[] = {
          "Auto (Ajustar à Janela / Livre)",
          "4:3 (Original SNES)",
          "16:9 (Widescreen Padrão)",
          "16:10 (Handhelds / Telas 16:10)",
          "18:9 (Smartphones / 2:1)",
          "21:9 (Monitores Ultrawide)",
          "32:9 (Super Ultrawide)"
        };
        int active_ar = GetAspectRatioIndex();
        if (active_ar < 0) active_ar = 0;
        bool ar_applied = false;
        if (MenuRow_StagedStepper("row_ar", "Proporção de Tela (Aspect Ratio)", "Expansão de visão horizontal sem esticar personagens", &s_staged.aspect_ratio, active_ar, ar_names, IM_ARRAYSIZE(ar_names), &ar_applied)) {
          SetAspectRatio(s_staged.aspect_ratio);
          SaveConfigFile(NULL);
          SetStatus("Proporção de tela aplicada!");
        }

        bool ext_adj = g_config.extend_adjacent_areas;
        if (MenuRow_Toggle("row_ext_adj", "Carregar Áreas Adjacentes no Limite", "Elimina barras pretas ao aproximar da borda do mapa em Widescreen", &ext_adj)) {
          g_config.extend_adjacent_areas = ext_adj;
          SaveConfigFile(NULL);
          SetStatus("Áreas adjacentes atualizadas!");
        }

        MenuSection_Header("GRÁFICOS & FIDELIDADE PPU");

        bool new_ppu = g_config.new_renderer;
        if (MenuRow_Toggle("row_ppu", "Renderizador PPU Otimizado", "Processador de imagem multithreaded moderno e veloz", &new_ppu)) {
          g_config.new_renderer = new_ppu;
          SaveConfigFile(NULL);
        }

        bool mode7 = g_config.enhanced_mode7;
        if (MenuRow_Toggle("row_mode7", "Modo 7 Aprimorado em Alta Resolução", "Renderiza o mapa geral e rotações 3D com clareza máxima", &mode7)) {
          g_config.enhanced_mode7 = mode7;
          SaveConfigFile(NULL);
        }

        bool no_spr_lim = g_config.no_sprite_limits;
        if (MenuRow_Toggle("row_spr_lim", "Remover Limite de Sprites", "Elimina o piscar (flickering) quando há muitos monstros na tela", &no_spr_lim)) {
          g_config.no_sprite_limits = no_spr_lim;
          SaveConfigFile(NULL);
        }

        bool lin_filt = g_config.linear_filtering;
        if (MenuRow_Toggle("row_lin_filt", "Filtro Linear (Bilinear Filtering)", "Suaviza as bordas dos pixels na tela para uma imagem mais macia", &lin_filt)) {
          g_config.linear_filtering = lin_filt;
          SaveConfigFile(NULL);
        }

        MenuSection_Header("DESEMPENHO & ACESSIBILIDADE");

        bool limit_60 = !g_config.disable_frame_delay;
        if (MenuRow_Toggle("row_lim60", "Limitar em 60 FPS", "Mantém a velocidade e o timing da física original do console", &limit_60)) {
          g_config.disable_frame_delay = !limit_60;
          SaveConfigFile(NULL);
        }

        bool fps_hud = g_config.display_fps;
        if (MenuRow_Toggle("row_fps_hud", "Exibir Contador de FPS na Tela (HUD)", "Mostra a taxa real de quadros no canto superior direito", &fps_hud)) {
          g_config.display_fps = fps_hud;
          SaveConfigFile(NULL);
        }

        bool dim_flash = (g_config.features0 & kFeatures0_DimFlashes) != 0;
        if (MenuRow_Toggle("row_dim_flash", "Diminuir Flashes de Luz", "Atenua relâmpagos e clarões (proteção para fotossensibilidade)", &dim_flash)) {
          if (dim_flash) g_config.features0 |= kFeatures0_DimFlashes;
          else g_config.features0 &= ~kFeatures0_DimFlashes;
          SaveConfigFile(NULL);
        }
      }

      // =======================================================================
      // TELA 2: ÁUDIO & MSU-1
      // =======================================================================
      else if (s_current_screen == kScreen_Audio) {
        if (MenuRow_Button("btn_back_audio", "◄ VOLTAR AO MENU PRINCIPAL", "Retorna para a lista de categorias do menu", "VOLTAR (B)")) {
          s_current_screen = kScreen_MainMenu;
          s_needs_focus_first = true;
          SyncStagedSettingsFromActive();
        }
        ImGui::Spacing();

        MenuSection_Header("ÁUDIO GLOBAL (MASTER)");

        bool audio_en = g_config.enable_audio;
        if (MenuRow_Toggle("row_audio_en", "Áudio do Jogo Ativado", "Habilita ou muta completamente todos os efeitos sonoros e músicas", &audio_en)) {
          g_config.enable_audio = audio_en;
          SaveConfigFile(NULL);
        }

        int master_vol = GetMasterVolume();
        if (MenuRow_Slider("row_master_vol", "Volume Geral (Master)", "Volume sonoro global das músicas e efeitos do jogo", &master_vol, 0, 100, 5, "%")) {
          SetMasterVolume(master_vol);
          SaveConfigFile(NULL);
        }

        MenuSection_Header("TRILHAS ORQUESTRADAS MSU-1");

        const char *msu_names[] = {
          "Desativado (Original SNES)",
          "MSU-1 Padrão (Orquestra em CD)",
          "MSU-1 Deluxe",
          "Opuz (Áudio Comprimido)",
          "Deluxe + Opuz"
        };
        int active_msu = 0;
        if (g_config.enable_msu == kMsuEnabled_Msu) active_msu = 1;
        else if (g_config.enable_msu == kMsuEnabled_MsuDeluxe) active_msu = 2;
        else if (g_config.enable_msu == kMsuEnabled_Opuz) active_msu = 3;
        else if (g_config.enable_msu == (kMsuEnabled_MsuDeluxe | kMsuEnabled_Opuz)) active_msu = 4;

        bool msu_applied = false;
        if (MenuRow_StagedStepper("row_msu_mode", "Modo de Áudio MSU-1", "Substitui os sintetizadores por orquestra real em alta fidelidade", &s_staged.msu_mode, active_msu, msu_names, IM_ARRAYSIZE(msu_names), &msu_applied)) {
          if (s_staged.msu_mode == 0) g_config.enable_msu = 0;
          else if (s_staged.msu_mode == 1) g_config.enable_msu = kMsuEnabled_Msu;
          else if (s_staged.msu_mode == 2) g_config.enable_msu = kMsuEnabled_MsuDeluxe;
          else if (s_staged.msu_mode == 3) g_config.enable_msu = kMsuEnabled_Opuz;
          else if (s_staged.msu_mode == 4) g_config.enable_msu = kMsuEnabled_MsuDeluxe | kMsuEnabled_Opuz;
          ZeldaEnableMsu(g_config.enable_msu);
          SaveConfigFile(NULL);
          SetStatus("Modo MSU-1 aplicado!");
        }

        int msu_vol = g_config.msuvolume;
        if (MenuRow_Slider("row_msu_vol", "Volume das Músicas MSU-1", "Equilíbrio sonoro individual das faixas orquestradas", &msu_vol, 0, 100, 5, "%")) {
          g_config.msuvolume = (uint8)msu_vol;
          SaveConfigFile(NULL);
        }

        bool resume_msu = g_config.resume_msu;
        if (MenuRow_Toggle("row_resume_msu", "Continuar Faixa ao Retornar", "Retoma a música de onde parou ao voltar para a mesma área", &resume_msu)) {
          g_config.resume_msu = resume_msu;
          SaveConfigFile(NULL);
        }
      }

      // =======================================================================
      // TELA 3: IDIOMA / LANGUAGE
      // =======================================================================
      else if (s_current_screen == kScreen_Language) {
        if (MenuRow_Button("btn_back_lang", "◄ VOLTAR AO MENU PRINCIPAL", "Retorna para a lista de categorias do menu", "VOLTAR (B)")) {
          s_current_screen = kScreen_MainMenu;
          s_needs_focus_first = true;
          SyncStagedSettingsFromActive();
        }
        ImGui::Spacing();

        MenuSection_Header("SELEÇÃO DE IDIOMA");

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
        if (MenuRow_StagedStepper("row_lang", "Idioma do Jogo", "Tradução de textos, nomes de itens e menus em tempo real", &s_staged.lang_idx, active_lang, kLangNames, IM_ARRAYSIZE(kLangNames), &lang_applied)) {
          g_config.language = kLangCodes[s_staged.lang_idx];
          ZeldaSetLanguage(g_config.language);
          SaveConfigFile(NULL);
          SetStatus("Idioma aplicado com sucesso!");
        }

        MenuSection_Header("RECURSOS DE TRADUÇÃO");
        MenuRow_Button("info_ptbr_font", "Suporte a Caracteres Acentuados", "Acentuação gráfica completa (ç, ã, õ, á, é, í, ó, ú, â, ê)", "ATIVO");
        MenuRow_Button("info_ptbr_text", "Diálogos Nativos em Português", "Textos extraídos e adaptados diretamente da versão brasileira", "INCLUSO");
        MenuRow_Button("info_ptbr_save", "Persistência Automática", "Sua preferência de idioma é mantida no arquivo zelda3.ini", "GRAVADO");
      }

      // =======================================================================
      // TELA 4: JOGABILIDADE (QOL)
      // =======================================================================
      else if (s_current_screen == kScreen_Gameplay) {
        if (MenuRow_Button("btn_back_qol", "◄ VOLTAR AO MENU PRINCIPAL", "Retorna para a lista de categorias do menu", "VOLTAR (B)")) {
          s_current_screen = kScreen_MainMenu;
          s_needs_focus_first = true;
          SyncStagedSettingsFromActive();
        }
        ImGui::Spacing();

        MenuSection_Header("DIÁLOGOS & TEXTOS");

        const char *fast_diag_modes[] = {
          "Desativado (Original SNES)",
          "Ao Segurar Botão (A/B/X/Y) [Recomendado]",
          "Sempre Rápido"
        };
        bool fd_applied = false;
        if (MenuRow_StagedStepper("row_fast_diag", "Aceleração de Diálogos", "Acelera a digitação até o final da caixa no estilo dos Zeldas modernos", &s_staged.fast_diag, g_config.fast_dialogue, fast_diag_modes, IM_ARRAYSIZE(fast_diag_modes), &fd_applied)) {
          g_config.fast_dialogue = (uint8)s_staged.fast_diag;
          SaveConfigFile(NULL);
          SetStatus("Modo de diálogo aplicado!");
        }

        if (s_staged.fast_diag != 0) {
          const char *speed_labels[] = {
            "1: Suave (2x)",
            "2: Rápida (4x)",
            "3: Muito Rápida (8x - Padrão)",
            "4: Ultrarrápida (16x)",
            "5: Instantânea (Máxima)"
          };
          int active_spd = g_config.fast_dialogue_speed ? (g_config.fast_dialogue_speed - 1) : 2;
          if (active_spd < 0) active_spd = 0;
          if (active_spd > 4) active_spd = 4;
          bool spd_applied = false;
          if (MenuRow_StagedStepper("row_fast_speed", "Velocidade da Aceleração", "Rapidez com que as letras preenchem a caixa de mensagem", &s_staged.fast_diag_speed, active_spd, speed_labels, IM_ARRAYSIZE(speed_labels), &spd_applied)) {
            g_config.fast_dialogue_speed = (uint8)(s_staged.fast_diag_speed + 1);
            SaveConfigFile(NULL);
            SetStatus("Velocidade do diálogo aplicada!");
          }
        }

        auto CheckFeatureRow = [](const char *id, const char *title, const char *desc, uint32_t mask) {
          bool val = (g_config.features0 & mask) != 0;
          if (MenuRow_Toggle(id, title, desc, &val)) {
            if (val) g_config.features0 |= mask;
            else g_config.features0 &= ~mask;
            enhanced_features0 = g_config.features0;
            SaveConfigFile(NULL);
          }
        };

        MenuSection_Header("CONTROLES & AÇÕES RÁPIDAS");
        CheckFeatureRow("f_switch_lr", "Troca Rápida de Itens com L / R", "Alterna o item equipado com os botões de ombro sem abrir o inventário", kFeatures0_SwitchLR);
        CheckFeatureRow("f_switch_lr_lim", "Limitar Troca L/R a 4 Itens", "Restringe a troca rápida aos primeiros quatro itens do inventário", kFeatures0_SwitchLRLimit);
        CheckFeatureRow("f_turn_dash", "Virar de Direção com Botas de Pégasus", "Permite mudar de rumo durante a corrida com as Pegasus Boots", kFeatures0_TurnWhileDashing);
        CheckFeatureRow("f_collect_sword", "Coletar Itens com a Espada", "Coleta corações, rupees e chaves ao acertá-los com golpes de espada", kFeatures0_CollectItemsWithSword);
        CheckFeatureRow("f_pots_sword", "Quebrar Potes com a Master Sword", "Permite estilhaçar vasos e jarros atacando com a Master Sword", kFeatures0_BreakPotsWithSword);

        MenuSection_Header("ECONOMIA & CAPACIDADE EXPANDIDA");
        CheckFeatureRow("f_carry_rupees", "Carteira Expandida (9999 Rupees)", "Aumenta a capacidade máxima de Rupees para 9999", kFeatures0_CarryMoreRupees);
        CheckFeatureRow("f_more_bombs", "Permitir 4 Bombas Simultâneas", "Permite colocar até 4 bombas ativas ao mesmo tempo no chão", kFeatures0_MoreActiveBombs);
        CheckFeatureRow("f_yellow_max", "Destacar Itens no Máximo em Amarelo", "Destaca o contador numérico em amarelo quando atinge a capacidade máxima", kFeatures0_ShowMaxItemsInYellow);
        CheckFeatureRow("f_mirror_dark", "Espelho Mágico Livre", "Permite usar o Magic Mirror em qualquer lugar para retornar ao Dark World", kFeatures0_MirrorToDarkworld);

        MenuSection_Header("CONVENIÊNCIA & CORREÇÕES");
        CheckFeatureRow("f_low_health", "Silenciar Bipe de Pouca Vida", "Desativa o alarme sonoro repetitivo quando Link estiver com pouca vida", kFeatures0_DisableLowHealthBeep);
        CheckFeatureRow("f_skip_intro", "Pular Introdução da Triforce", "Pula o logotipo inicial da Triforce pressionando qualquer botão", kFeatures0_SkipIntroOnKeypress);
        CheckFeatureRow("f_cancel_bird", "Cancelar Viagem do Pássaro com X", "Cancela a viagem rápida da flauta pressionando o botão X", kFeatures0_CancelBirdTravel);
        CheckFeatureRow("f_misc_fixes", "Correções de Glitches Originais", "Aplica correções a pequenos bugs visuais conhecidos do cartucho original", kFeatures0_MiscBugFixes);
      }

      // =======================================================================
      // TELA 5: TRAPAÇAS & ESTADOS DE JOGO
      // =======================================================================
      else if (s_current_screen == kScreen_Cheats) {
        if (MenuRow_Button("btn_back_cheats", "◄ VOLTAR AO MENU PRINCIPAL", "Retorna para a lista de categorias do menu", "VOLTAR (B)")) {
          s_current_screen = kScreen_MainMenu;
          s_needs_focus_first = true;
          SyncStagedSettingsFromActive();
        }
        ImGui::Spacing();

        MenuSection_Header("AÇÕES RÁPIDAS (CHEATS)");

        if (MenuRow_Button("btn_full_life", "Restaurar Vida & Magia Total", "Enche todos os corações e a barra de magia imediatamente", "RESTAURAR")) {
          PatchCommand('w');
          SetStatus("Vida e magia restauradas ao máximo!");
        }

        if (MenuRow_Button("btn_full_items", "99 Bombas, 99 Flechas & 9999 Rupees", "Enche todos os consumíveis e dinheiro ao máximo", "PREENCHER")) {
          PatchCommand('W');
          SetStatus("Itens e rupees preenchidos!");
        }

        if (MenuRow_Button("btn_give_key", "Ganhar 1 Chave Pequena", "Adiciona uma Small Key ao inventário da dungeon atual", "ADICIONAR")) {
          PatchCommand('o');
          SetStatus("Chave adicionada ao inventário!");
        }

        if (MenuRow_Button("btn_soft_reset", "Reiniciar Jogo (Soft Reset)", "Executa um reinício idêntico ao console original", "REINICIAR", true)) {
          ZeldaReset(true);
          SetStatus("Jogo reiniciado!");
        }

        MenuSection_Header("ESTADOS DE JOGO (SAVE STATES)");

        const char *slot_names[] = {
          "Slot 0", "Slot 1", "Slot 2", "Slot 3", "Slot 4",
          "Slot 5", "Slot 6", "Slot 7", "Slot 8", "Slot 9"
        };
        bool slot_applied = false;
        MenuRow_StagedStepper("row_save_slot", "Slot de Salvamento Ativo", "Escolha a partição de memória para salvar ou carregar", &s_staged.save_slot, s_selected_save_slot, slot_names, IM_ARRAYSIZE(slot_names), &slot_applied);
        s_selected_save_slot = s_staged.save_slot;

        if (MenuRow_Button("btn_save_slot", "Salvar Estado no Slot", "Grava o estado exato da sua gameplay na memória", "SALVAR ESTADO")) {
          SaveLoadSlot(kSaveLoad_Save, s_selected_save_slot);
          char buf[64];
          snprintf(buf, sizeof(buf), "Estado salvo no slot %d!", s_selected_save_slot);
          SetStatus(buf);
        }

        if (MenuRow_Button("btn_load_slot", "Carregar Estado do Slot", "Recupera o estado gravado anteriormente no slot selecionado", "CARREGAR ESTADO")) {
          SaveLoadSlot(kSaveLoad_Load, s_selected_save_slot);
          char buf[64];
          snprintf(buf, sizeof(buf), "Estado carregado do slot %d!", s_selected_save_slot);
          SetStatus(buf);
        }
      }

      // =======================================================================
      // TELA 6: SOBRE & CONTROLES
      // =======================================================================
      else if (s_current_screen == kScreen_About) {
        if (MenuRow_Button("btn_back_about", "◄ VOLTAR AO MENU PRINCIPAL", "Retorna para a lista de categorias do menu", "VOLTAR (B)")) {
          s_current_screen = kScreen_MainMenu;
          s_needs_focus_first = true;
        }
        ImGui::Spacing();

        MenuSection_Header("GUIA DE CONTROLES DO MENU");

        MenuRow_Button("guide_dpad", "D-Pad / Analógico", "Navega livremente pelas opções de cima a baixo", "NAVEGAR");
        MenuRow_Button("guide_lr", "D-Pad ◄ ►  /  Analógico", "Percorre opções e pré-visualiza antes de aplicar", "EXPLORAR");
        MenuRow_Button("guide_a", "Botão A  /  Tecla Enter", "Entra nas categorias e aplica a opção selecionada", "CONFIRMAR");
        MenuRow_Button("guide_b", "Botão B  /  Tecla ESC", "Retorna ao menu anterior ou fecha e volta ao jogo", "VOLTAR");
        MenuRow_Button("guide_combo", "Atalho Global: Start + Select", "Segure ambos juntos no controle para abrir ou fechar o menu", "ATALHO");

        MenuSection_Header("SOBRE O PROJETO");

        MenuRow_Button("about_engine", "Motor Nativo em C/C++", "Reimplementação de Zelda: A Link to the Past com SDL2 e OpenGL", "SNES REV");
        MenuRow_Button("about_trans", "Versão Brasileira PT-BR", "Fontes acentuadas e diálogos nativos integrados ao motor", "PT-BR");
        MenuRow_Button("about_author", "Créditos & Desenvolvimento", "snesrev, ocornut/imgui e kelvynzucco/zelda3-overlay", "CRÉDITOS");
      }

      // =======================================================================
      // TELA 7: CONFIRMAÇÃO DE SAÍDA (SAIR DO JOGO)
      // =======================================================================
      else if (s_current_screen == kScreen_ExitConfirm) {
        MenuSection_Header("ENCERRAR O JOGO");

        ImVec2 avail = ImGui::GetContentRegionAvail();
        float center_w = 640.0f * io.FontGlobalScale;
        if (center_w > avail.x - 20.0f) center_w = avail.x - 20.0f;

        ImGui::Spacing();
        ImGui::SetCursorPosX((avail.x - center_w) * 0.5f);
        ImGui::TextWrapped("Deseja realmente sair de The Legend of Zelda: A Link to the Past e voltar para a Área de Trabalho?");
        ImGui::Spacing();
        ImGui::SetCursorPosX((avail.x - center_w) * 0.5f);
        ImGui::TextColored(ImVec4(0.40f, 0.90f, 0.50f, 1.0f), "O progresso salvo na bateria (SRAM) e as configurações do zelda3.ini serão preservados com segurança.");
        ImGui::Spacing();
        ImGui::Spacing();

        if (MenuRow_Button("btn_exit_confirm", "Sim, Sair do Jogo", "Encerra o aplicativo e descarrega os dispositivos de vídeo e áudio", "SAIR AGORA", true)) {
          SaveConfigFile(NULL);
          s_request_exit_game = true;
          SDL_Event quit_ev;
          quit_ev.type = SDL_QUIT;
          SDL_PushEvent(&quit_ev);
        }
        ImGui::Spacing();

        if (MenuRow_Button("btn_exit_cancel", "Continuar Jogando", "Cancela a saída e retorna ao Menu Principal", "VOLTAR (B)", false)) {
          s_current_screen = kScreen_MainMenu;
          s_needs_focus_first = true;
          SyncStagedSettingsFromActive();
        }
      }

    }
    ImGui::EndChild();

    // 3. Rodapé Informativo
    ImGui::Spacing();
    ImGui::Separator();
    ImGui::Spacing();

    if (s_status_message[0] != '\0' && (SDL_GetTicks() - s_status_message_time < 3500)) {
      ImGui::TextColored(ImVec4(0.40f, 0.95f, 0.45f, 1.0f), "[OK] %s", s_status_message);
    } else {
      ImGui::TextColored(ImVec4(0.55f, 0.68f, 0.58f, 1.0f), "Configurações salvas automaticamente em zelda3.ini");
    }

    ImGui::SameLine();
    float right_w = 660.0f * io.FontGlobalScale;
    float r_pos = ImGui::GetWindowWidth() - right_w - 20.0f;
    if (r_pos > ImGui::GetCursorPosX()) {
      ImGui::SetCursorPosX(r_pos);
    }

    if (s_current_screen == kScreen_MainMenu) {
      ImGui::TextColored(ImVec4(0.92f, 0.82f, 0.35f, 1.0f), "[D-Pad ▲▼]: Navegar | [A]: Entrar na Categoria | [B]: Fechar Menu");
    } else {
      ImGui::TextColored(ImVec4(0.92f, 0.82f, 0.35f, 1.0f), "[D-Pad ▲▼]: Navegar | [◄ ►]: Pré-visualizar | [A]: Aplicar | [B]: Voltar");
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
