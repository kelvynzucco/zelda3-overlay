TARGET_EXEC:=zelda3.exe
CC:=gcc
CXX:=g++

SRCS_C:=$(wildcard src/*.c snes/*.c) third_party/gl_core/gl_core_3_1.c third_party/opus-1.3.1-stripped/opus_decoder_amalgam.c
SRCS_CXX:=src/overlay.cpp third_party/imgui/imgui.cpp third_party/imgui/imgui_draw.cpp third_party/imgui/imgui_tables.cpp third_party/imgui/imgui_widgets.cpp third_party/imgui/backends/imgui_impl_sdl2.cpp third_party/imgui/backends/imgui_impl_sdlrenderer2.cpp third_party/imgui/backends/imgui_impl_opengl3.cpp

OBJS:=$(SRCS_C:%.c=%.o) $(SRCS_CXX:%.cpp=%.o)

CFLAGS:=-std=gnu11 -O2 -I. -Ithird_party/SDL2-2.26.3/include -Ithird_party/imgui -Ithird_party/imgui/backends -DSYSTEM_VOLUME_MIXER_AVAILABLE=0 -DSTBI_NO_SIMD=1 -DHAVE_STDINT_H=1 -D_HAVE_STDINT_H=1
CXXFLAGS:=-std=gnu++17 -O2 -I. -Ithird_party/SDL2-2.26.3/include -Ithird_party/imgui -Ithird_party/imgui/backends -DSYSTEM_VOLUME_MIXER_AVAILABLE=0
LDFLAGS:=-Lthird_party/SDL2-2.26.3/lib/x64 -lSDL2 -lopengl32 -lgdi32 -limm32 -lole32 -loleaut32 -luuid -lversion -static-libgcc -static-libstdc++

all: $(TARGET_EXEC)

$(TARGET_EXEC): $(OBJS)
	$(CXX) $(OBJS) -o $@ $(LDFLAGS)

%.o: %.c
	$(CC) -c $(CFLAGS) $< -o $@

%.o: %.cpp
	$(CXX) -c $(CXXFLAGS) $< -o $@

clean:
	rm -f $(OBJS) $(TARGET_EXEC)
