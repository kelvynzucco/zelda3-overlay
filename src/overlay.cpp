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


static void RenderOverlayWindow() {
  ImGuiIO& io = ImGui::GetIO();

  // Dynamic responsive sizing: fits nicely regardless of window scale or resolution
  float margin_x = 24.0f;
  float margin_y = 20.0f;
  float avail_w = io.DisplaySize.x - (margin_x * 2.0f);
  float avail_h = io.DisplaySize.y - (margin_y * 2.0f);

  float target_w = avail_w;
  if (target_w > 680.0f) target_w = 680.0f;
  if (target_w < 260.0f) target_w = (io.DisplaySize.x > 280.0f) ? io.DisplaySize.x - 16.0f : io.DisplaySize.x;

  float target_h = avail_h;
  if (target_h > 520.0f) target_h = 520.0f;
  if (target_h < 190.0f) target_h = (io.DisplaySize.y > 210.0f) ? io.DisplaySize.y - 16.0f : io.DisplaySize.y;

  float pos_x = (io.DisplaySize.x - target_w) * 0.5f;
  float pos_y = (io.DisplaySize.y - target_h) * 0.5f;
  if (pos_x < 8.0f) pos_x = 8.0f;
  if (pos_y < 8.0f) pos_y = 8.0f;

  ImGui::SetNextWindowSize(ImVec2(target_w, target_h), ImGuiCond_Always);
  ImGui::SetNextWindowPos(ImVec2(pos_x, pos_y), ImGuiCond_Always);
  ImGui::SetNextWindowSizeConstraints(ImVec2(260.0f, 190.0f), ImVec2(io.DisplaySize.x, io.DisplaySize.y));

  // Dynamic UI font scaling
  if (io.DisplaySize.y >= 1400.0f) {
    io.FontGlobalScale = 1.4f;
  } else if (io.DisplaySize.y < 460.0f) {
    io.FontGlobalScale = 0.85f;
  } else {
    io.FontGlobalScale = 1.0f;
  }

  if (ImGui::Begin("The Legend of Zelda: A Link to the Past - Configurações", &s_overlay_open, ImGuiWindowFlags_NoCollapse)) {
    if (ImGui::BeginTabBar("OverlayTabs")) {

      // TAB 1: GRÁFICOS & VÍDEO
      if (ImGui::BeginTabItem("Vídeo & Gráficos")) {
        ImGui::Spacing();
        ImGui::TextColored(ImVec4(0.88f, 0.75f, 0.25f, 1.0f), "Configurações de Tela");
        ImGui::Separator();

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

        if (ImGui::BeginCombo("Resolução da Janela", current_res_str)) {
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

        // Fullscreen
        const char *fs_items[] = { "Janela (Windowed)", "Tela Cheia sem Bordas (Borderless)", "Tela Cheia Exclusiva (Fullscreen)" };
        int fs_current = g_config.fullscreen;
        if (ImGui::Combo("Modo de Exibição", &fs_current, fs_items, IM_ARRAYSIZE(fs_items))) {
          g_config.fullscreen = (uint8)fs_current;
          SetFullscreenMode(fs_current);
        }

        // Aspect ratio
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

        if (ImGui::Combo("Proporção de Tela (Aspect Ratio)", &ar_current, ar_items, IM_ARRAYSIZE(ar_items))) {
          SetAspectRatio(ar_current);
        }
        if (g_config.aspect_ratio_auto) {
          ImGui::TextColored(ImVec4(0.40f, 0.85f, 0.40f, 1.0f), "Modo Livre Ativo: O jogo preenche 100% da janela sem barras pretas.");
        }

        bool ext_adj = g_config.extend_adjacent_areas;
        if (ImGui::Checkbox("Carregar Áreas Adjacentes no Limite da Tela", &ext_adj)) {
          g_config.extend_adjacent_areas = ext_adj;
        }
        ImGui::TextColored(ImVec4(0.60f, 0.60f, 0.60f, 1.0f), "Elimina as barras pretas ao se aproximar das bordas do mapa em Widescreen,\ncarregando visualmente a área vizinha em tempo real.");

        ImGui::Spacing();
        ImGui::TextColored(ImVec4(0.88f, 0.75f, 0.25f, 1.0f), "Filtros & Renderização");
        ImGui::Separator();

        bool lin_filt = g_config.linear_filtering;
        if (ImGui::Checkbox("Filtro Linear (Bilinear Filtering)", &lin_filt)) {
          g_config.linear_filtering = lin_filt;
        }

        bool no_spr_lim = g_config.no_sprite_limits;
        if (ImGui::Checkbox("Remover Limite de Sprites (Elimina flickering original do SNES)", &no_spr_lim)) {
          g_config.no_sprite_limits = no_spr_lim;
        }

        bool mode7 = g_config.enhanced_mode7;
        if (ImGui::Checkbox("Modo 7 Aprimorado (Enhanced Mode 7 - Mapa em Alta Resolução)", &mode7)) {
          g_config.enhanced_mode7 = mode7;
        }

        bool new_ppu = g_config.new_renderer;
        if (ImGui::Checkbox("Renderizador PPU Otimizado (Novo Renderer mais rápido)", &new_ppu)) {
          g_config.new_renderer = new_ppu;
        }

        bool dim_flash = (g_config.features0 & kFeatures0_DimFlashes) != 0;
        if (ImGui::Checkbox("Diminuir Flashes de Luz (Acessibilidade / Fotossensibilidade)", &dim_flash)) {
          if (dim_flash) g_config.features0 |= kFeatures0_DimFlashes;
          else g_config.features0 &= ~kFeatures0_DimFlashes;
        }

        bool fps_hud = g_config.display_fps;
        if (ImGui::Checkbox("Exibir Contador de FPS na Tela (HUD / OSD)", &fps_hud)) {
          g_config.display_fps = fps_hud;
        }

        ImGui::SameLine();
        bool limit_60 = !g_config.disable_frame_delay;
        if (ImGui::Checkbox("Limitar em 60 FPS", &limit_60)) {
          g_config.disable_frame_delay = !limit_60;
        }
        ImGui::TextColored(ImVec4(0.60f, 0.60f, 0.60f, 1.0f), "A velocidade original do SNES é de 60 FPS.");

        ImGui::EndTabItem();
      }

      // TAB 2: ÁUDIO & MSU-1
      if (ImGui::BeginTabItem("Áudio & MSU-1")) {
        ImGui::Spacing();
        ImGui::TextColored(ImVec4(0.88f, 0.75f, 0.25f, 1.0f), "Opções de Som");
        ImGui::Separator();

        bool audio_en = g_config.enable_audio;
        if (ImGui::Checkbox("Áudio Ativado", &audio_en)) {
          g_config.enable_audio = audio_en;
        }

        int master_vol = GetMasterVolume();
        if (ImGui::SliderInt("Volume Geral (Master)", &master_vol, 0, 100, "%d%%")) {
          SetMasterVolume(master_vol);
        }
        ImGui::TextColored(ImVec4(0.60f, 0.60f, 0.60f, 1.0f), "Controla o volume global (músicas originais do SNES e efeitos sonoros).");

        ImGui::Spacing();
        ImGui::TextColored(ImVec4(0.88f, 0.75f, 0.25f, 1.0f), "MSU-1 (Trilhas Orquestradas / CD Audio)");
        ImGui::Separator();
        ImGui::TextWrapped("O MSU-1 permite substituir a trilha sonora sintetizada do SNES por faixas orquestradas reais em áudio de alta definição.");

        const char *msu_modes[] = { "Desativado", "MSU-1 Padrão", "MSU-1 Deluxe", "Opuz", "Deluxe + Opuz" };
        int msu_curr = 0;
        if (g_config.enable_msu == kMsuEnabled_Msu) msu_curr = 1;
        else if (g_config.enable_msu == kMsuEnabled_MsuDeluxe) msu_curr = 2;
        else if (g_config.enable_msu == kMsuEnabled_Opuz) msu_curr = 3;
        else if (g_config.enable_msu == (kMsuEnabled_MsuDeluxe | kMsuEnabled_Opuz)) msu_curr = 4;

        if (ImGui::Combo("Modo MSU-1", &msu_curr, msu_modes, IM_ARRAYSIZE(msu_modes))) {
          if (msu_curr == 0) g_config.enable_msu = 0;
          else if (msu_curr == 1) g_config.enable_msu = kMsuEnabled_Msu;
          else if (msu_curr == 2) g_config.enable_msu = kMsuEnabled_MsuDeluxe;
          else if (msu_curr == 3) g_config.enable_msu = kMsuEnabled_Opuz;
          else if (msu_curr == 4) g_config.enable_msu = kMsuEnabled_MsuDeluxe | kMsuEnabled_Opuz;
          ZeldaEnableMsu(g_config.enable_msu);
        }

        int msu_vol = g_config.msuvolume;
        if (ImGui::SliderInt("Volume das Músicas MSU-1", &msu_vol, 0, 100, "%d%%")) {
          g_config.msuvolume = (uint8)msu_vol;
        }
        ImGui::TextColored(ImVec4(0.60f, 0.60f, 0.60f, 1.0f), "Ajusta o ganho das músicas orquestradas MSU-1 relativo aos efeitos sonoros.");

        bool resume_msu = g_config.resume_msu;
        if (ImGui::Checkbox("Continuar faixa de onde parou ao retornar para uma área", &resume_msu)) {
          g_config.resume_msu = resume_msu;
        }

        ImGui::EndTabItem();
      }

      // TAB 3: IDIOMA
      if (ImGui::BeginTabItem("Idioma / Language")) {
        ImGui::Spacing();
        ImGui::TextColored(ImVec4(0.88f, 0.75f, 0.25f, 1.0f), "Seleção de Idioma");
        ImGui::Separator();

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

        int current_lang_idx = 1; // default us
        if (g_config.language) {
          for (int i = 0; i < (int)IM_ARRAYSIZE(lang_codes); i++) {
            if (strcmp(g_config.language, lang_codes[i]) == 0) {
              current_lang_idx = i;
              break;
            }
          }
        }

        if (ImGui::Combo("Idioma Ativo", &current_lang_idx, lang_names, IM_ARRAYSIZE(lang_names))) {
          g_config.language = lang_codes[current_lang_idx];
          ZeldaSetLanguage(g_config.language);
          SetStatus("Idioma alterado com sucesso!");
        }

        ImGui::Spacing();
        ImGui::TextWrapped("Dica: Para o idioma em português, o jogo utiliza as fontes acentuadas e os diálogos extraídos da sua cópia brasileira.");

        ImGui::EndTabItem();
      }

      // TAB 4: MELHORIAS (QOL)
      if (ImGui::BeginTabItem("Melhorias (QoL)")) {
        ImGui::Spacing();
        ImGui::TextColored(ImVec4(0.88f, 0.75f, 0.25f, 1.0f), "Recursos Modernos de Jogabilidade");
        ImGui::Separator();

        auto CheckFeature = [](const char *label, uint32_t mask) {
          bool val = (g_config.features0 & mask) != 0;
          if (ImGui::Checkbox(label, &val)) {
            if (val) g_config.features0 |= mask;
            else g_config.features0 &= ~mask;
            enhanced_features0 = g_config.features0;
          }
        };

        // Aceleração de Diálogos
        const char *fast_diag_modes[] = {
          "Desativado (Original)",
          "Ao Segurar Botão (A / B / X / Y) [Zelda Moderno]",
          "Sempre Rápido"
        };
        int fd_curr = g_config.fast_dialogue;
        if (ImGui::Combo("Acelerar Diálogos", &fd_curr, fast_diag_modes, IM_ARRAYSIZE(fast_diag_modes))) {
          g_config.fast_dialogue = (uint8)fd_curr;
        }
        ImGui::TextColored(ImVec4(0.60f, 0.60f, 0.60f, 1.0f), "Acelera a digitação do texto para o final da caixa e agiliza a saída do diálogo.");
        ImGui::Spacing();

        CheckFeature("Troca Rápida de Itens com botões L / R", kFeatures0_SwitchLR);
        CheckFeature("Limitar troca rápida L/R apenas aos primeiros 4 itens", kFeatures0_SwitchLRLimit);
        CheckFeature("Virar de direção enquanto corre com as Botas de Pégasus", kFeatures0_TurnWhileDashing);
        CheckFeature("Espelho Mágico funciona de qualquer mundo para o Dark World", kFeatures0_MirrorToDarkworld);
        CheckFeature("Coletar itens (rupees, corações) com a Espada", kFeatures0_CollectItemsWithSword);
        CheckFeature("Quebrar potes atacando com a Master Sword", kFeatures0_BreakPotsWithSword);
        CheckFeature("Desativar som contínuo (bipe) de pouca vida", kFeatures0_DisableLowHealthBeep);
        CheckFeature("Pular introdução da Triforce pressionando qualquer botão", kFeatures0_SkipIntroOnKeypress);
        CheckFeature("Destacar números de itens no máximo em amarelo", kFeatures0_ShowMaxItemsInYellow);
        CheckFeature("Permitir até 4 bombas ativas ao mesmo tempo (o original limita a 2)", kFeatures0_MoreActiveBombs);
        CheckFeature("Carteira expandida: carregar até 9999 Rupees", kFeatures0_CarryMoreRupees);
        CheckFeature("Cancelar viagem com a Flauta/Pássaro pressionando botão X", kFeatures0_CancelBirdTravel);
        CheckFeature("Correção de bugs visuais menores do jogo original", kFeatures0_MiscBugFixes);

        ImGui::EndTabItem();
      }

      // TAB 5: CHEATS & ESTADOS
      if (ImGui::BeginTabItem("Cheats & Estados")) {
        ImGui::Spacing();
        ImGui::TextColored(ImVec4(0.88f, 0.75f, 0.25f, 1.0f), "Ações Rápidas (Cheats)");
        ImGui::Separator();

        if (ImGui::Button("Restaurar Vida & Magia Total", ImVec2(240, 32))) {
          PatchCommand('w');
          SetStatus("Vida e magia restauradas ao máximo!");
        }

        ImGui::SameLine();
        if (ImGui::Button("99 Bombas, 99 Flechas & 9999 Rupees", ImVec2(280, 32))) {
          PatchCommand('W');
          SetStatus("Itens e rupees preenchidos!");
        }

        if (ImGui::Button("Ganhar 1 Chave Pequena para a Dungeon", ImVec2(240, 32))) {
          PatchCommand('o');
          SetStatus("Chave adicionada ao inventário!");
        }

        ImGui::SameLine();
        if (ImGui::Button("Reiniciar Jogo (Soft Reset)", ImVec2(280, 32))) {
          ZeldaReset(true);
          SetStatus("Jogo reiniciado!");
        }

        ImGui::Spacing();
        ImGui::TextColored(ImVec4(0.88f, 0.75f, 0.25f, 1.0f), "Estados de Jogo (Save / Load State)");
        ImGui::Separator();

        ImGui::SliderInt("Slot de Estado (SaveSlot)", &s_selected_save_slot, 0, 9);

        if (ImGui::Button("Salvar Estado no Slot", ImVec2(200, 32))) {
          SaveLoadSlot(kSaveLoad_Save, s_selected_save_slot);
          char buf[64];
          snprintf(buf, sizeof(buf), "Estado salvo no slot %d!", s_selected_save_slot);
          SetStatus(buf);
        }

        ImGui::SameLine();
        if (ImGui::Button("Carregar Estado do Slot", ImVec2(200, 32))) {
          SaveLoadSlot(kSaveLoad_Load, s_selected_save_slot);
          char buf[64];
          snprintf(buf, sizeof(buf), "Estado carregado do slot %d!", s_selected_save_slot);
          SetStatus(buf);
        }

        ImGui::EndTabItem();
      }

      // TAB 6: SOBRE
      if (ImGui::BeginTabItem("Sobre")) {
        ImGui::Spacing();
        ImGui::TextColored(ImVec4(0.88f, 0.75f, 0.25f, 1.0f), "The Legend of Zelda: A Link to the Past - PC Port");
        ImGui::Separator();
        ImGui::TextWrapped("Este projeto é uma reimplementação nativa em C do clássico do Super Nintendo.");
        ImGui::Spacing();
        ImGui::Text("Créditos do Decompilador:");
        ImGui::BulletText("Criador Original: snesrev e contribuidores");
        ImGui::BulletText("Repositório Oficial: https://github.com/snesrev/zelda3");
        ImGui::Spacing();
        ImGui::Text("Overlay Edition:");
        ImGui::BulletText("Interface In-Game: Dear ImGui (ocornut/imgui)");
        ImGui::BulletText("Suporte PT-BR & Overlay: kelvynzucco/zelda3-overlay");
        ImGui::Spacing();
        ImGui::TextColored(ImVec4(0.70f, 0.70f, 0.70f, 1.0f), "Atalho de Teclado: Pressione ESC ou F12 a qualquer momento para abrir ou fechar este menu.");
        ImGui::EndTabItem();
      }

      ImGui::EndTabBar();
    }

    ImGui::Spacing();
    ImGui::Separator();

    // Rodapé com botões de ação e status
    if (ImGui::Button("Salvar", ImVec2(120, 30))) {
      if (SaveConfigFile(NULL)) {
        SetStatus("Configurações salvas em zelda3.ini");
      } else {
        char err_buf[320];
        snprintf(err_buf, sizeof(err_buf), "Erro ao salvar: %s", g_last_save_error[0] ? g_last_save_error : "Acesso negado");
        SetStatus(err_buf);
      }
    }

    ImGui::SameLine();
    if (ImGui::Button("Fechar Menu (ESC)", ImVec2(140, 30))) {
      Overlay_Toggle();
    }

    if (s_status_message[0] != '\0') {
      if (SDL_GetTicks() - s_status_message_time < 4000) {
        ImGui::SameLine();
        ImGui::TextColored(ImVec4(0.35f, 0.90f, 0.40f, 1.0f), "%s", s_status_message);
      } else {
        s_status_message[0] = '\0';
      }
    }
  }
  ImGui::End();
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
