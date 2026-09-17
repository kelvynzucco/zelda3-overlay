#ifndef OVERLAY_H
#define OVERLAY_H

#include <stdbool.h>
#include <SDL.h>

#ifdef __cplusplus
extern "C" {
#endif

// Initializes Dear ImGui with SDL2 and appropriate renderer backend
bool Overlay_Init(SDL_Window *window, SDL_Renderer *renderer, bool is_opengl);

// Cleans up Dear ImGui resources
void Overlay_Shutdown(void);

// Processes SDL events for ImGui. Returns true if ImGui consumed the event.
bool Overlay_ProcessEvent(const SDL_Event *event);

// Checks if the overlay menu is currently open
bool Overlay_IsOpen(void);

// Toggles the overlay menu open/closed
void Overlay_Toggle(void);

// Sets the overlay menu open state explicitly
void Overlay_SetOpen(bool open);

// Renders the overlay (must be called before SDL_RenderPresent / SDL_GL_SwapWindow)
void Overlay_Render(SDL_Renderer *renderer, bool is_opengl);

// Window management helpers
void SetWindowScale(int scale);
void SetWindowResolution(int width, int height);
void SetFullscreenMode(int mode);

// Audio controls
int GetMasterVolume(void);
void SetMasterVolume(int percent);

#ifdef __cplusplus
}
#endif

#endif // OVERLAY_H
