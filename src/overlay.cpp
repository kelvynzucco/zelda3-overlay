#include "overlay.h"
#include <imgui.h>
#include <backends/imgui_impl_sdl2.h>
#include <backends/imgui_impl_sdlrenderer2.h>
#include <backends/imgui_impl_opengl3.h>

#include <stdio.h>
#include <string.h>

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


static void SetupZeldaTheme() {
  ImGuiStyle& style = ImGui::GetStyle();
  ImVec4* colors = style.Colors;

  // Window formatting
  style.WindowRounding = 8.0f;
  style.FrameRounding = 5.0f;
  style.GrabRounding = 5.0f;
  style.PopupRounding = 6.0f;
  style.ScrollbarRounding = 6.0f;
  style.TabRounding = 6.0f;
  style.WindowPadding = ImVec2(16.0f, 16.0f);
  style.FramePadding = ImVec2(8.0f, 6.0f);
  style.ItemSpacing = ImVec2(10.0f, 8.0f);
  style.WindowBorderSize = 1.5f;

  // Zelda-themed palette: dark slate background with Triforce gold and forest green accents
  colors[ImGuiCol_Text]                  = ImVec4(0.95f, 0.95f, 0.92f, 1.00f);
  colors[ImGuiCol_TextDisabled]          = ImVec4(0.55f, 0.55f, 0.50f, 1.00f);
  colors[ImGuiCol_WindowBg]              = ImVec4(0.08f, 0.10f, 0.09f, 0.94f);
  colors[ImGuiCol_ChildBg]               = ImVec4(0.10f, 0.13f, 0.11f, 0.85f);
  colors[ImGuiCol_PopupBg]               = ImVec4(0.09f, 0.11f, 0.10f, 0.96f);
  colors[ImGuiCol_Border]                = ImVec4(0.78f, 0.65f, 0.22f, 0.65f); // Triforce Gold border
  colors[ImGuiCol_BorderShadow]          = ImVec4(0.00f, 0.00f, 0.00f, 0.40f);
  colors[ImGuiCol_FrameBg]               = ImVec4(0.14f, 0.19f, 0.16f, 0.85f);
  colors[ImGuiCol_FrameBgHovered]        = ImVec4(0.20f, 0.28f, 0.22f, 0.90f);
  colors[ImGuiCol_FrameBgActive]         = ImVec4(0.25f, 0.35f, 0.28f, 1.00f);
  colors[ImGuiCol_TitleBg]               = ImVec4(0.07f, 0.12f, 0.09f, 1.00f);
  colors[ImGuiCol_TitleBgActive]         = ImVec4(0.12f, 0.22f, 0.15f, 1.00f);
  colors[ImGuiCol_TitleBgCollapsed]      = ImVec4(0.05f, 0.08f, 0.06f, 0.75f);
  colors[ImGuiCol_MenuBarBg]             = ImVec4(0.10f, 0.14f, 0.11f, 1.00f);
  colors[ImGuiCol_ScrollbarBg]           = ImVec4(0.08f, 0.10f, 0.09f, 0.60f);
  colors[ImGuiCol_ScrollbarGrab]         = ImVec4(0.25f, 0.35f, 0.28f, 0.80f);
  colors[ImGuiCol_ScrollbarGrabHovered]  = ImVec4(0.35f, 0.48f, 0.38f, 0.90f);
  colors[ImGuiCol_ScrollbarGrabActive]   = ImVec4(0.78f, 0.65f, 0.22f, 1.00f);
  colors[ImGuiCol_CheckMark]             = ImVec4(0.88f, 0.75f, 0.25f, 1.00f); // Gold check
  colors[ImGuiCol_SliderGrab]            = ImVec4(0.78f, 0.65f, 0.22f, 0.90f);
  colors[ImGuiCol_SliderGrabActive]      = ImVec4(0.95f, 0.85f, 0.35f, 1.00f);
  colors[ImGuiCol_Button]                = ImVec4(0.18f, 0.28f, 0.21f, 0.85f); // Forest green button
  colors[ImGuiCol_ButtonHovered]         = ImVec4(0.26f, 0.40f, 0.30f, 0.95f);
  colors[ImGuiCol_ButtonActive]          = ImVec4(0.78f, 0.65f, 0.22f, 0.90f);
  colors[ImGuiCol_Header]                = ImVec4(0.18f, 0.28f, 0.21f, 0.80f);
  colors[ImGuiCol_HeaderHovered]         = ImVec4(0.26f, 0.40f, 0.30f, 0.90f);
  colors[ImGuiCol_HeaderActive]          = ImVec4(0.78f, 0.65f, 0.22f, 0.85f);
  colors[ImGuiCol_Separator]             = ImVec4(0.78f, 0.65f, 0.22f, 0.35f);
  colors[ImGuiCol_SeparatorHovered]      = ImVec4(0.78f, 0.65f, 0.22f, 0.70f);
  colors[ImGuiCol_SeparatorActive]       = ImVec4(0.90f, 0.80f, 0.30f, 1.00f);
  colors[ImGuiCol_ResizeGrip]            = ImVec4(0.78f, 0.65f, 0.22f, 0.40f);
  colors[ImGuiCol_ResizeGripHovered]     = ImVec4(0.88f, 0.75f, 0.25f, 0.70f);
  colors[ImGuiCol_ResizeGripActive]      = ImVec4(0.98f, 0.88f, 0.35f, 1.00f);
  colors[ImGuiCol_Tab]                   = ImVec4(0.12f, 0.17f, 0.14f, 0.85f);
  colors[ImGuiCol_TabHovered]            = ImVec4(0.26f, 0.38f, 0.29f, 0.95f);
  colors[ImGuiCol_TabActive]             = ImVec4(0.20f, 0.32f, 0.23f, 1.00f);
  colors[ImGuiCol_TabUnfocused]          = ImVec4(0.10f, 0.14f, 0.11f, 0.85f);
  colors[ImGuiCol_TabUnfocusedActive]   = ImVec4(0.16f, 0.24f, 0.18f, 1.00f);
  colors[ImGuiCol_NavHighlight]          = ImVec4(0.98f, 0.85f, 0.25f, 1.00f); // Triforce Gold outline for gamepad
  colors[ImGuiCol_NavWindowingHighlight] = ImVec4(0.98f, 0.85f, 0.25f, 0.70f);
  colors[ImGuiCol_NavWindowingDimBg]     = ImVec4(0.00f, 0.00f, 0.00f, 0.60f);
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
  io.IniFilename = NULL; // Keep overlay responsive to current game resolution

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
    // When menu is open, intercept mouse and keyboard so they don't affect game controls
    if (io.WantCaptureMouse || io.WantCaptureKeyboard)
      return true;
    return true; // Keep capturing while menu is active
  }

  return false;
}

bool Overlay_IsOpen(void) {
  return s_overlay_open;
}

void Overlay_Toggle(void) {
  s_overlay_open = !s_overlay_open;
  SDL_ShowCursor(s_overlay_open ? SDL_ENABLE : SDL_DISABLE);
  if (!s_overlay_open) {
    SaveConfigFile(NULL);
  }
}

void Overlay_SetOpen(bool open) {
  if (s_overlay_open && !open) {
    SaveConfigFile(NULL);
  }
  s_overlay_open = open;
  SDL_ShowCursor(s_overlay_open ? SDL_ENABLE : SDL_DISABLE);
}


static bool s_request_exit_game = false;

bool Overlay_ShouldExit(void) {
  return s_request_exit_game;
}

// Estrutura de Grade Responsiva para Blocos / Cartões
struct CardGrid {
  bool two_cols;
  bool in_table;
  int card_index;

  void Begin(const char *grid_id, float avail_w) {
    two_cols = (avail_w >= 700.0f);
    card_index = 0;
    if (two_cols) {
      in_table = ImGui::BeginTable(grid_id, 2, ImGuiTableFlags_SizingStretchSame);
    } else {
      in_table = false;
    }
  }

  void Next() {
    if (in_table) {
      ImGui::TableNextColumn();
    } else if (card_index > 0) {
      ImGui::Spacing();
      ImGui::Spacing();
    }
    card_index++;
  }

  void End() {
    if (in_table) {
      ImGui::EndTable();
    }
  }
};

static bool BeginCard(const char *id, const char *title, const char *subtitle = nullptr) {
  ImGui::PushStyleVar(ImGuiStyleVar_ChildRounding, 8.0f);
  ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(16.0f, 14.0f));
  ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(10.0f, 10.0f));
  ImGui::PushStyleColor(ImGuiCol_ChildBg, ImVec4(0.10f, 0.14f, 0.12f, 0.88f));
  ImGui::PushStyleColor(ImGuiCol_Border, ImVec4(0.78f, 0.65f, 0.22f, 0.40f));

  ImGuiChildFlags child_flags = ImGuiChildFlags_Borders | ImGuiChildFlags_AutoResizeY | ImGuiChildFlags_NavFlattened;
  bool open = ImGui::BeginChild(id, ImVec2(0.0f, 0.0f), child_flags, ImGuiWindowFlags_None);

  if (title && title[0]) {
    ImGui::TextColored(ImVec4(0.98f, 0.85f, 0.25f, 1.0f), "%s", title);
    if (subtitle && subtitle[0]) {
      ImGui::TextColored(ImVec4(0.60f, 0.72f, 0.65f, 1.0f), "%s", subtitle);
    }
    ImGui::Separator();
    ImGui::Spacing();
  }

  return open;
}

static void EndCard() {
  ImGui::EndChild();
  ImGui::PopStyleColor(2);
  ImGui::PopStyleVar(3);
}

static void RenderOverlayWindow() {
  ImGuiIO& io = ImGui::GetIO();
  ImGuiViewport* viewport = ImGui::GetMainViewport();

  // Permite fechar o menu pelo botão B no controle, se nenhum popup estiver aberto
  if (!ImGui::IsPopupOpen(nullptr, ImGuiPopupFlags_AnyPopupId)) {
    if (ImGui::IsKeyPressed(ImGuiKey_GamepadFaceRight)) {
      Overlay_Toggle();
      return;
    }
  }

  // Fullscreen overlay cobrindo 100% da viewport da tela
  ImGui::SetNextWindowPos(viewport->Pos, ImGuiCond_Always);
  ImGui::SetNextWindowSize(viewport->Size, ImGuiCond_Always);

  // Escala dinâmica responsiva de fonte baseada na altura da tela
  if (viewport->Size.y >= 1400.0f) {
    io.FontGlobalScale = 1.35f;
  } else if (viewport->Size.y >= 900.0f) {
    io.FontGlobalScale = 1.15f;
  } else if (viewport->Size.y < 500.0f) {
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
  ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(24.0f, 16.0f));

  if (ImGui::Begin("##FullscreenZeldaOverlay", &s_overlay_open, window_flags)) {
    // Cabeçalho Principal
    ImGui::TextColored(ImVec4(0.98f, 0.85f, 0.25f, 1.0f), "THE LEGEND OF ZELDA: A LINK TO THE PAST");
    ImGui::SameLine();
    ImGui::TextColored(ImVec4(0.55f, 0.75f, 0.60f, 1.0f), "|   MENU DE CONFIGURAÇÕES & OVERLAY");
    ImGui::Separator();
    ImGui::Spacing();

    // Área central com scroll automático para as abas
    float footer_height = 52.0f * io.FontGlobalScale;
    float content_height = ImGui::GetContentRegionAvail().y - footer_height;
    if (content_height < 100.0f) content_height = 100.0f;

    if (ImGui::BeginChild("OverlayBody", ImVec2(0, content_height), false, ImGuiWindowFlags_None)) {
      static int s_active_tab = 0;
      static int s_requested_tab = -1;

      if (!ImGui::IsPopupOpen(nullptr, ImGuiPopupFlags_AnyPopupId)) {
        if (ImGui::IsKeyPressed(ImGuiKey_GamepadL1)) {
          s_requested_tab = (s_active_tab + 6 - 1) % 6;
        } else if (ImGui::IsKeyPressed(ImGuiKey_GamepadR1)) {
          s_requested_tab = (s_active_tab + 1) % 6;
        }
      }

      if (ImGui::BeginTabBar("OverlayTabs", ImGuiTabBarFlags_None)) {

      // TAB 0: VÍDEO & GRÁFICOS
      ImGuiTabItemFlags tab0_flags = (s_requested_tab == 0) ? ImGuiTabItemFlags_SetSelected : 0;
      if (ImGui::BeginTabItem("Vídeo & Gráficos", nullptr, tab0_flags)) {
        s_active_tab = 0;
        ImGui::Spacing();

        // Resolução da Tela
        int cur_w = g_config.window_width;
        int cur_h = g_config.window_height;
        if (cur_w == 0 || cur_h == 0) {
          int s = g_config.window_scale ? g_config.window_scale : 3;
          cur_w = (g_config.extended_aspect_ratio * 2 + 256) * s;
          cur_h = (g_config.extend_y ? 240 : 224) * s;
        }

        int selected_res_idx = -1;
        for (size_t i = 0; i < IM_ARRAYSIZE(kStandardResolutions); i++) {
          if (kStandardResolutions[i].width == cur_w && kStandardResolutions[i].height == cur_h) {
            selected_res_idx = (int)i;
            break;
          }
        }

        char current_res_str[64];
        if (selected_res_idx >= 0) {
          snprintf(current_res_str, sizeof(current_res_str), "%s", kStandardResolutions[selected_res_idx].name);
        } else {
          snprintf(current_res_str, sizeof(current_res_str), "Personalizada (%d x %d)", cur_w, cur_h);
        }

        float avail_w = ImGui::GetContentRegionAvail().x;
        CardGrid grid;
        grid.Begin("VideoGrid", avail_w);

        // BLOCO 1: Resolução & Exibição
        grid.Next();
        if (BeginCard("CardRes", "Resolução & Exibição", "Tamanho da janela e modo de tela")) {
          ImGui::Text("Resolução da Janela:");
          ImGui::SetNextItemWidth(-1.0f);
          if (ImGui::BeginCombo("##ResCombo", current_res_str)) {
            for (size_t i = 0; i < IM_ARRAYSIZE(kStandardResolutions); i++) {
              bool is_selected = (selected_res_idx == (int)i);
              if (ImGui::Selectable(kStandardResolutions[i].name, is_selected)) {
                SetWindowResolution(kStandardResolutions[i].width, kStandardResolutions[i].height);
              }
              if (is_selected) {
                ImGui::SetItemDefaultFocus();
              }
            }
            ImGui::EndCombo();
          }

          ImGui::Spacing();
          ImGui::Text("Modo de Exibição:");
          const char *fs_items[] = { "Janela (Windowed)", "Tela Cheia sem Bordas (Borderless)", "Tela Cheia Exclusiva (Fullscreen)" };
          int fs_current = g_config.fullscreen;
          ImGui::SetNextItemWidth(-1.0f);
          if (ImGui::Combo("##FsCombo", &fs_current, fs_items, IM_ARRAYSIZE(fs_items))) {
            g_config.fullscreen = (uint8)fs_current;
            SetFullscreenMode(fs_current);
          }

          ImGui::Spacing();
          ImGui::Text("Escala da Janela:");
          int cur_scale = g_config.window_scale ? g_config.window_scale : 3;
          ImGui::SetNextItemWidth(-1.0f);
          if (ImGui::SliderInt("##ScaleSlider", &cur_scale, 1, 10, "%dx Escala")) {
            SetWindowScale(cur_scale);
          }
        }
        EndCard();

        // BLOCO 2: Proporção de Tela & Widescreen
        grid.Next();
        if (BeginCard("CardAspect", "Proporção de Tela & Áreas", "Aspect Ratio e expansão horizontal")) {
          ImGui::Text("Proporção de Tela (Aspect Ratio):");
          const char *ar_items[] = {
            "Auto (Ajustar à Janela / Livre)",
            "4:3 (Original SNES)",
            "16:9 (Widescreen)",
            "16:10",
            "18:9",
            "21:9 (Ultrawide)",
            "32:9 (Super Ultrawide)"
          };
          int ar_current = GetAspectRatioIndex();
          ImGui::SetNextItemWidth(-1.0f);
          if (ImGui::Combo("##ArCombo", &ar_current, ar_items, IM_ARRAYSIZE(ar_items))) {
            SetAspectRatio(ar_current);
          }
          if (g_config.aspect_ratio_auto) {
            ImGui::TextColored(ImVec4(0.40f, 0.85f, 0.40f, 1.0f), "Modo Livre Ativo: Preenche 100% da tela sem barras pretas.");
          }

          ImGui::Spacing();
          bool ext_adj = g_config.extend_adjacent_areas;
          if (ImGui::Checkbox("Carregar Áreas Adjacentes no Limite da Tela", &ext_adj)) {
            g_config.extend_adjacent_areas = ext_adj;
          }
          ImGui::TextColored(ImVec4(0.60f, 0.60f, 0.60f, 1.0f), "Elimina barras pretas ao aproximar da borda do mapa em Widescreen,\ncarregando a área vizinha em tempo real.");
        }
        EndCard();

        // BLOCO 3: Renderizador & Filtros
        grid.Next();
        if (BeginCard("CardRenderer", "Renderizador & Filtros PPU", "Melhorias gráficas e fidelidade visual")) {
          bool new_ppu = g_config.new_renderer;
          if (ImGui::Checkbox("Renderizador PPU Otimizado (Mais rápido / Moderno)", &new_ppu)) {
            g_config.new_renderer = new_ppu;
          }

          bool mode7 = g_config.enhanced_mode7;
          if (ImGui::Checkbox("Modo 7 Aprimorado (Enhanced Mode 7 em Alta Resolução)", &mode7)) {
            g_config.enhanced_mode7 = mode7;
          }

          bool no_spr_lim = g_config.no_sprite_limits;
          if (ImGui::Checkbox("Remover Limite de Sprites (Sem flickering do SNES)", &no_spr_lim)) {
            g_config.no_sprite_limits = no_spr_lim;
          }

          bool lin_filt = g_config.linear_filtering;
          if (ImGui::Checkbox("Filtro Linear (Bilinear Filtering)", &lin_filt)) {
            g_config.linear_filtering = lin_filt;
          }
        }
        EndCard();

        // BLOCO 4: Desempenho & Acessibilidade
        grid.Next();
        if (BeginCard("CardPerf", "Desempenho & Acessibilidade", "Taxa de quadros e conforto visual")) {
          bool limit_60 = !g_config.disable_frame_delay;
          if (ImGui::Checkbox("Limitar em 60 FPS (Velocidade original do SNES)", &limit_60)) {
            g_config.disable_frame_delay = !limit_60;
          }

          bool fps_hud = g_config.display_fps;
          if (ImGui::Checkbox("Exibir Contador de FPS na Tela (HUD / OSD)", &fps_hud)) {
            g_config.display_fps = fps_hud;
          }

          bool dim_flash = (g_config.features0 & kFeatures0_DimFlashes) != 0;
          if (ImGui::Checkbox("Diminuir Flashes de Luz (Fotossensibilidade)", &dim_flash)) {
            if (dim_flash) g_config.features0 |= kFeatures0_DimFlashes;
            else g_config.features0 &= ~kFeatures0_DimFlashes;
          }
        }
        EndCard();

        grid.End();
        ImGui::EndTabItem();
      }

      // TAB 1: ÁUDIO & MSU-1
      ImGuiTabItemFlags tab1_flags = (s_requested_tab == 1) ? ImGuiTabItemFlags_SetSelected : 0;
      if (ImGui::BeginTabItem("Áudio & MSU-1", nullptr, tab1_flags)) {
        s_active_tab = 1;
        ImGui::Spacing();

        float avail_w = ImGui::GetContentRegionAvail().x;
        CardGrid grid;
        grid.Begin("AudioGrid", avail_w);

        // BLOCO 1: Áudio Global
        grid.Next();
        if (BeginCard("CardMasterAudio", "Áudio Global (Master)", "Volume geral de músicas e efeitos sonoros")) {
          bool audio_en = g_config.enable_audio;
          if (ImGui::Checkbox("Áudio Ativado", &audio_en)) {
            g_config.enable_audio = audio_en;
          }

          ImGui::Spacing();
          ImGui::Text("Volume Geral:");
          int master_vol = GetMasterVolume();
          ImGui::SetNextItemWidth(-1.0f);
          if (ImGui::SliderInt("##MasterVolSlider", &master_vol, 0, 100, "%d%%")) {
            SetMasterVolume(master_vol);
          }
          ImGui::TextColored(ImVec4(0.60f, 0.60f, 0.60f, 1.0f), "Controla o volume global do emulador (efeitos e músicas).");
        }
        EndCard();

        // BLOCO 2: MSU-1
        grid.Next();
        if (BeginCard("CardMsu", "MSU-1 (Áudio Orquestrado em CD)", "Substitui trilha sintetizada por faixas orquestradas reais")) {
          ImGui::Text("Modo MSU-1:");
          const char *msu_modes[] = { "Desativado", "MSU-1 Padrão", "MSU-1 Deluxe", "Opuz", "Deluxe + Opuz" };
          int msu_curr = 0;
          if (g_config.enable_msu == kMsuEnabled_Msu) msu_curr = 1;
          else if (g_config.enable_msu == kMsuEnabled_MsuDeluxe) msu_curr = 2;
          else if (g_config.enable_msu == kMsuEnabled_Opuz) msu_curr = 3;
          else if (g_config.enable_msu == (kMsuEnabled_MsuDeluxe | kMsuEnabled_Opuz)) msu_curr = 4;

          ImGui::SetNextItemWidth(-1.0f);
          if (ImGui::Combo("##MsuCombo", &msu_curr, msu_modes, IM_ARRAYSIZE(msu_modes))) {
            if (msu_curr == 0) g_config.enable_msu = 0;
            else if (msu_curr == 1) g_config.enable_msu = kMsuEnabled_Msu;
            else if (msu_curr == 2) g_config.enable_msu = kMsuEnabled_MsuDeluxe;
            else if (msu_curr == 3) g_config.enable_msu = kMsuEnabled_Opuz;
            else if (msu_curr == 4) g_config.enable_msu = kMsuEnabled_MsuDeluxe | kMsuEnabled_Opuz;
            ZeldaEnableMsu(g_config.enable_msu);
          }

          ImGui::Spacing();
          ImGui::Text("Volume das Músicas MSU-1:");
          int msu_vol = g_config.msuvolume;
          ImGui::SetNextItemWidth(-1.0f);
          if (ImGui::SliderInt("##MsuVolSlider", &msu_vol, 0, 100, "%d%%")) {
            g_config.msuvolume = (uint8)msu_vol;
          }

          ImGui::Spacing();
          bool resume_msu = g_config.resume_msu;
          if (ImGui::Checkbox("Continuar faixa de onde parou ao voltar para a área", &resume_msu)) {
            g_config.resume_msu = resume_msu;
          }
        }
        EndCard();

        grid.End();
        ImGui::EndTabItem();
      }

      // TAB 2: IDIOMA
      ImGuiTabItemFlags tab2_flags = (s_requested_tab == 2) ? ImGuiTabItemFlags_SetSelected : 0;
      if (ImGui::BeginTabItem("Idioma / Language", nullptr, tab2_flags)) {
        s_active_tab = 2;
        ImGui::Spacing();

        float avail_w = ImGui::GetContentRegionAvail().x;
        CardGrid grid;
        grid.Begin("LangGrid", avail_w);

        // BLOCO 1: Seleção de Idioma
        grid.Next();
        if (BeginCard("CardLanguage", "Seleção de Idioma / Language", "Escolha a tradução dos textos e fontes")) {
          const char *lang_names[] = {
            "Português do Brasil (PT-BR)",
            "English (US)",
            "Deutsch (Alemão)",
            "Français (Francês)",
            "Español (Espanhol)",
            "Polski (Polonês)",
            "Nederlands (Holandês)",
            "Svenska (Sueco)"
          };
          const char *lang_codes[] = { "pt", "us", "de", "fr", "es", "pl", "nl", "sv" };

          int current_lang_idx = 1;
          if (g_config.language) {
            for (int i = 0; i < (int)IM_ARRAYSIZE(lang_codes); i++) {
              if (strcmp(g_config.language, lang_codes[i]) == 0) {
                current_lang_idx = i;
                break;
              }
            }
          }

          ImGui::Text("Idioma Ativo:");
          ImGui::SetNextItemWidth(-1.0f);
          if (ImGui::Combo("##LangCombo", &current_lang_idx, lang_names, IM_ARRAYSIZE(lang_names))) {
            g_config.language = lang_codes[current_lang_idx];
            ZeldaSetLanguage(g_config.language);
            SetStatus("Idioma alterado com sucesso!");
          }

          ImGui::Spacing();
          ImGui::TextColored(ImVec4(0.40f, 0.85f, 0.45f, 1.0f), "Dica PT-BR:");
          ImGui::TextWrapped("Para o português, o jogo utiliza as fontes acentuadas e os diálogos nativos extraídos da sua cópia brasileira.");
        }
        EndCard();

        // BLOCO 2: Recursos de Tradução
        grid.Next();
        if (BeginCard("CardLangInfo", "Recursos de Tradução", "Fontes e acentuação gráfica")) {
          ImGui::BulletText("Suporte completo a caracteres acentuados (ç, ã, õ, á, é, í, ó, ú, â, ê).");
          ImGui::BulletText("A troca de idioma é aplicada em tempo real durante a gameplay.");
          ImGui::BulletText("Configuração salva no zelda3.ini e mantida automaticamente.");
        }
        EndCard();

        grid.End();
        ImGui::EndTabItem();
      }

      // TAB 3: MELHORIAS (QOL)
      ImGuiTabItemFlags tab3_flags = (s_requested_tab == 3) ? ImGuiTabItemFlags_SetSelected : 0;
      if (ImGui::BeginTabItem("Melhorias (QoL)", nullptr, tab3_flags)) {
        s_active_tab = 3;
        ImGui::Spacing();

        float avail_w = ImGui::GetContentRegionAvail().x;
        CardGrid grid;
        grid.Begin("QolGrid", avail_w);

        auto CheckFeature = [](const char *label, uint32_t mask) {
          bool val = (g_config.features0 & mask) != 0;
          if (ImGui::Checkbox(label, &val)) {
            if (val) g_config.features0 |= mask;
            else g_config.features0 &= ~mask;
            enhanced_features0 = g_config.features0;
          }
        };

        // BLOCO 1: Diálogos Rápidos
        grid.Next();
        if (BeginCard("CardFastDiag", "Diálogos Acelerados (Fast Dialogue)", "Acelera o texto no estilo dos Zeldas modernos")) {
          ImGui::Text("Modo de Diálogo:");
          const char *fast_diag_modes[] = {
            "Desativado (Original SNES)",
            "Ao Segurar Botão (A/B/X/Y) [Recomendado]",
            "Sempre Rápido"
          };
          int fd_curr = g_config.fast_dialogue;
          ImGui::SetNextItemWidth(-1.0f);
          if (ImGui::Combo("##FdModeCombo", &fd_curr, fast_diag_modes, IM_ARRAYSIZE(fast_diag_modes))) {
            g_config.fast_dialogue = (uint8)fd_curr;
          }
          ImGui::TextColored(ImVec4(0.60f, 0.60f, 0.60f, 1.0f), "Acelera a digitação até o final da caixa ao segurar um botão de ação.");

          if (g_config.fast_dialogue != 0) {
            ImGui::Spacing();
            ImGui::Text("Velocidade da Aceleração:");
            const char *speed_labels[] = {
              "1: Suave (2x)",
              "2: Rápida (4x)",
              "3: Muito Rápida (8x - Padrão)",
              "4: Ultrarrápida (16x)",
              "5: Instantânea (Máxima)"
            };
            int spd_idx = g_config.fast_dialogue_speed ? g_config.fast_dialogue_speed - 1 : 2;
            if (spd_idx < 0) spd_idx = 0;
            if (spd_idx > 4) spd_idx = 4;
            ImGui::SetNextItemWidth(-1.0f);
            if (ImGui::Combo("##FdSpeedCombo", &spd_idx, speed_labels, IM_ARRAYSIZE(speed_labels))) {
              g_config.fast_dialogue_speed = (uint8)(spd_idx + 1);
            }
          }
        }
        EndCard();

        // BLOCO 2: Controles & Ações Rápidas
        grid.Next();
        if (BeginCard("CardActions", "Controles & Ações Rápidas", "Facilidades de jogabilidade para o controle")) {
          CheckFeature("Troca Rápida de Itens com botões L / R", kFeatures0_SwitchLR);
          CheckFeature("Limitar troca rápida L/R apenas aos primeiros 4 itens", kFeatures0_SwitchLRLimit);
          CheckFeature("Virar de direção correndo com as Botas de Pégasus", kFeatures0_TurnWhileDashing);
          CheckFeature("Coletar itens (rupees, corações) com a Espada", kFeatures0_CollectItemsWithSword);
          CheckFeature("Quebrar potes atacando com a Master Sword", kFeatures0_BreakPotsWithSword);
        }
        EndCard();

        // BLOCO 3: Itens & Economia Expandida
        grid.Next();
        if (BeginCard("CardItems", "Itens & Economia Expandida", "Limites e capacidades estendidas")) {
          CheckFeature("Carteira expandida: carregar até 9999 Rupees", kFeatures0_CarryMoreRupees);
          CheckFeature("Permitir até 4 bombas ativas ao mesmo tempo", kFeatures0_MoreActiveBombs);
          CheckFeature("Destacar números de itens no máximo em amarelo", kFeatures0_ShowMaxItemsInYellow);
          CheckFeature("Espelho Mágico funciona de qualquer mundo para o Dark World", kFeatures0_MirrorToDarkworld);
        }
        EndCard();

        // BLOCO 4: Conveniência & Áudio
        grid.Next();
        if (BeginCard("CardConvenience", "Conveniência & Correções", "Ajustes de interface e correções visuais")) {
          CheckFeature("Desativar som contínuo (bipe) de pouca vida", kFeatures0_DisableLowHealthBeep);
          CheckFeature("Pular introdução da Triforce pressionando qualquer botão", kFeatures0_SkipIntroOnKeypress);
          CheckFeature("Cancelar viagem com a Flauta/Pássaro pressionando botão X", kFeatures0_CancelBirdTravel);
          CheckFeature("Correção de bugs visuais menores do jogo original", kFeatures0_MiscBugFixes);
        }
        EndCard();

        grid.End();
        ImGui::EndTabItem();
      }

      // TAB 4: CHEATS & ESTADOS
      ImGuiTabItemFlags tab4_flags = (s_requested_tab == 4) ? ImGuiTabItemFlags_SetSelected : 0;
      if (ImGui::BeginTabItem("Cheats & Estados", nullptr, tab4_flags)) {
        s_active_tab = 4;
        ImGui::Spacing();

        float avail_w = ImGui::GetContentRegionAvail().x;
        CardGrid grid;
        grid.Begin("CheatsGrid", avail_w);

        // BLOCO 1: Ações Rápidas (Cheats)
        grid.Next();
        if (BeginCard("CardCheats", "Ações Rápidas (Cheats)", "Trapaças para testes e facilidades")) {
          if (ImGui::Button("Restaurar Vida & Magia Total", ImVec2(-1.0f, 34))) {
            PatchCommand('w');
            SetStatus("Vida e magia restauradas ao máximo!");
          }

          if (ImGui::Button("99 Bombas, 99 Flechas & 9999 Rupees", ImVec2(-1.0f, 34))) {
            PatchCommand('W');
            SetStatus("Itens e rupees preenchidos!");
          }

          if (ImGui::Button("Ganhar 1 Chave Pequena para a Dungeon", ImVec2(-1.0f, 34))) {
            PatchCommand('o');
            SetStatus("Chave adicionada ao inventário!");
          }

          if (ImGui::Button("Reiniciar Jogo (Soft Reset)", ImVec2(-1.0f, 34))) {
            ZeldaReset(true);
            SetStatus("Jogo reiniciado!");
          }
        }
        EndCard();

        // BLOCO 2: Estados de Jogo (Save / Load State)
        grid.Next();
        if (BeginCard("CardStates", "Estados de Jogo (Save / Load State)", "Salvar e carregar posições instantaneamente")) {
          ImGui::Text("Slot de Estado (SaveSlot):");
          ImGui::SetNextItemWidth(-1.0f);
          ImGui::SliderInt("##SaveSlotSlider", &s_selected_save_slot, 0, 9, "Slot %d");

          ImGui::Spacing();
          float btn_w = (ImGui::GetContentRegionAvail().x - ImGui::GetStyle().ItemSpacing.x) * 0.5f;
          if (ImGui::Button("Salvar no Slot", ImVec2(btn_w, 36))) {
            SaveLoadSlot(kSaveLoad_Save, s_selected_save_slot);
            char buf[64];
            snprintf(buf, sizeof(buf), "Estado salvo no slot %d!", s_selected_save_slot);
            SetStatus(buf);
          }
          ImGui::SameLine();
          if (ImGui::Button("Carregar do Slot", ImVec2(btn_w, 36))) {
            SaveLoadSlot(kSaveLoad_Load, s_selected_save_slot);
            char buf[64];
            snprintf(buf, sizeof(buf), "Estado carregado do slot %d!", s_selected_save_slot);
            SetStatus(buf);
          }

          ImGui::Spacing();
          ImGui::TextColored(ImVec4(0.60f, 0.60f, 0.60f, 1.0f), "Atalhos rápidos no teclado:\n[F1-F10]: Carregar Estado | [Shift+F1-F10]: Salvar Estado");
        }
        EndCard();

        grid.End();
        ImGui::EndTabItem();
      }

      // TAB 5: SOBRE & CONTROLES
      ImGuiTabItemFlags tab5_flags = (s_requested_tab == 5) ? ImGuiTabItemFlags_SetSelected : 0;
      if (ImGui::BeginTabItem("Sobre", nullptr, tab5_flags)) {
        s_active_tab = 5;
        ImGui::Spacing();

        float avail_w = ImGui::GetContentRegionAvail().x;
        CardGrid grid;
        grid.Begin("AboutGrid", avail_w);

        // BLOCO 1: Guia de Navegação
        grid.Next();
        if (BeginCard("CardControlsGuide", "Guia de Controles do Menu", "Como navegar em qualquer controle")) {
          ImGui::BulletText("Abrir / Fechar Menu: Segurar Start + Select");
          ImGui::BulletText("Alternar Abas: Botões LB e RB (ou L1 e R1)");
          ImGui::BulletText("Navegar nos Blocos: D-Pad (Direcionais)");
          ImGui::BulletText("Confirmar / Interagir: Botão A");
          ImGui::BulletText("Voltar / Fechar Menu: Botão B");
          ImGui::BulletText("Teclado: Teclas ESC ou F12 para abrir e fechar");
        }
        EndCard();

        // BLOCO 2: Sobre o Projeto
        grid.Next();
        if (BeginCard("CardAboutInfo", "The Legend of Zelda: A Link to the Past", "PC Port & Overlay Edition")) {
          ImGui::TextWrapped("Reimplementação nativa em C do clássico do Super Nintendo, com suporte a resoluções widescreen, áudio orquestrado MSU-1 e tradução PT-BR.");
          ImGui::Spacing();
          ImGui::TextColored(ImVec4(0.88f, 0.75f, 0.25f, 1.0f), "Créditos:");
          ImGui::BulletText("Decompilador Original: snesrev e contribuidores");
          ImGui::BulletText("Interface & Overlay: Dear ImGui (ocornut/imgui)");
          ImGui::BulletText("Versão PT-BR & Overlay: kelvynzucco/zelda3-overlay");
        }
        EndCard();

        grid.End();
        ImGui::EndTabItem();
      }

        ImGui::EndTabBar();
        s_requested_tab = -1;
      }
    }
    ImGui::EndChild();

    ImGui::Spacing();
    ImGui::Separator();

    // Rodapé com botões de ação e status
    if (ImGui::Button("Salvar", ImVec2(130, 34))) {
      if (SaveConfigFile(NULL)) {
        SetStatus("Configurações salvas em zelda3.ini");
      } else {
        char err_buf[320];
        snprintf(err_buf, sizeof(err_buf), "Erro ao salvar: %s", g_last_save_error[0] ? g_last_save_error : "Acesso negado");
        SetStatus(err_buf);
      }
    }

    ImGui::SameLine();
    if (ImGui::Button("Fechar Menu (ESC / B)", ImVec2(170, 34))) {
      Overlay_Toggle();
    }

    ImGui::SameLine();
    ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.48f, 0.16f, 0.16f, 0.85f));
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.68f, 0.22f, 0.22f, 0.95f));
    ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(0.85f, 0.28f, 0.28f, 1.00f));
    if (ImGui::Button("Sair do Jogo", ImVec2(130, 34))) {
      ImGui::OpenPopup("ConfirmExitPopup");
    }
    ImGui::PopStyleColor(3);

    // Modal de Confirmação de Saída
    ImVec2 center = viewport->GetCenter();
    ImGui::SetNextWindowPos(center, ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
    ImGui::SetNextWindowSize(ImVec2(440.0f * io.FontGlobalScale, 0.0f), ImGuiCond_Always);

    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 8.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(24.0f, 20.0f));
    ImGui::PushStyleColor(ImGuiCol_PopupBg, ImVec4(0.09f, 0.12f, 0.10f, 0.98f));
    ImGui::PushStyleColor(ImGuiCol_Border, ImVec4(0.85f, 0.28f, 0.28f, 0.80f));

    if (ImGui::BeginPopupModal("ConfirmExitPopup", nullptr, ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoTitleBar)) {
      ImGui::TextColored(ImVec4(0.95f, 0.35f, 0.35f, 1.0f), "ENCERRAR O JOGO");
      ImGui::Separator();
      ImGui::Spacing();

      ImGui::TextWrapped("Deseja realmente sair e fechar o jogo?\nO progresso salvo e as configurações serão preservados.");
      ImGui::Spacing();
      ImGui::Spacing();

      ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.55f, 0.16f, 0.16f, 0.90f));
      ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.75f, 0.22f, 0.22f, 1.0f));
      ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(0.90f, 0.30f, 0.30f, 1.0f));
      if (ImGui::Button("Sim, Sair do Jogo", ImVec2(180, 36))) {
        SaveConfigFile(NULL);
        s_request_exit_game = true;
        SDL_Event quit_ev;
        quit_ev.type = SDL_QUIT;
        SDL_PushEvent(&quit_ev);
        ImGui::CloseCurrentPopup();
      }
      ImGui::PopStyleColor(3);

      ImGui::SameLine();
      if (ImGui::Button("Cancelar (B)", ImVec2(140, 36)) || ImGui::IsKeyPressed(ImGuiKey_GamepadFaceRight) || ImGui::IsKeyPressed(ImGuiKey_Escape)) {
        ImGui::CloseCurrentPopup();
      }

      ImGui::EndPopup();
    }
    ImGui::PopStyleColor(2);
    ImGui::PopStyleVar(2);

    if (s_status_message[0] != '\0') {
      if (SDL_GetTicks() - s_status_message_time < 4000) {
        ImGui::SameLine();
        ImGui::TextColored(ImVec4(0.35f, 0.90f, 0.40f, 1.0f), "%s", s_status_message);
      } else {
        s_status_message[0] = '\0';
      }
    }

    // Dicas de navegação por controle no rodapé (lado direito)
    float hints_w = 640.0f * io.FontGlobalScale;
    float right_pos = ImGui::GetWindowWidth() - hints_w;
    if (right_pos > ImGui::GetCursorPosX() + 15.0f) {
      ImGui::SameLine();
      ImGui::SetCursorPosX(right_pos);
      ImGui::TextColored(ImVec4(0.65f, 0.65f, 0.55f, 1.0f), "[LB/RB]: Trocar Abas | [D-Pad]: Navegar | [A]: Confirmar | [B]: Fechar");
    }
  }
  ImGui::End();
  ImGui::PopStyleVar(3);
}

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
