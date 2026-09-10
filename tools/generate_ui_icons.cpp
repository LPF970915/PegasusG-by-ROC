// Google Material Icons (round), Apache-2.0; see THIRD_PARTY_NOTICES.md.
// Offline generator: SDL_image >= 2.6, run from the repository root.
// g++ tools/generate_ui_icons.cpp $(pkg-config --cflags --libs sdl2 SDL2_image) -o build/generate_ui_icons
// ./build/generate_ui_icons > src/ui_icons_bitmap.h
#include <SDL.h>
#include <SDL_image.h>
#include <array>
#include <iostream>

int main() {
  constexpr int size = 28, scale = 4, count = 7, width = size * count;
  const char *svgs[] = {
      R"svg(<svg xmlns="http://www.w3.org/2000/svg" height="24" viewBox="0 0 24 24" width="24"><path d="M0 0h24v24H0z" fill="none"/><path d="M2.5 5.5C2.5 6.33 3.17 7 4 7h3.5v10.5c0 .83.67 1.5 1.5 1.5s1.5-.67 1.5-1.5V7H14c.83 0 1.5-.67 1.5-1.5S14.83 4 14 4H4c-.83 0-1.5.67-1.5 1.5zM20 9h-6c-.83 0-1.5.67-1.5 1.5S13.17 12 14 12h1.5v5.5c0 .83.67 1.5 1.5 1.5s1.5-.67 1.5-1.5V12H20c.83 0 1.5-.67 1.5-1.5S20.83 9 20 9z"/></svg>)svg",
      R"svg(<svg xmlns="http://www.w3.org/2000/svg" height="24" viewBox="0 0 24 24" width="24"><path d="M0 0h24v24H0V0z" fill="none"/><path d="M18 10v3H6v-3c0-.55-.45-1-1-1s-1 .45-1 1v4c0 .55.45 1 1 1h14c.55 0 1-.45 1-1v-4c0-.55-.45-1-1-1s-1 .45-1 1z"/></svg>)svg",
      R"svg(<svg xmlns="http://www.w3.org/2000/svg" height="24" viewBox="0 0 24 24" width="24"><path d="M0 0h24v24H0V0z" fill="none"/><path d="M22 3H7c-.69 0-1.23.35-1.59.88L.37 11.45c-.22.34-.22.77 0 1.11l5.04 7.56c.36.52.9.88 1.59.88h15c1.1 0 2-.9 2-2V5c0-1.1-.9-2-2-2zm-3.7 13.3c-.39.39-1.02.39-1.41 0L14 13.41l-2.89 2.89c-.39.39-1.02.39-1.41 0-.39-.39-.39-1.02 0-1.41L12.59 12 9.7 9.11c-.39-.39-.39-1.02 0-1.41.39-.39 1.02-.39 1.41 0L14 10.59l2.89-2.89c.39-.39 1.02-.39 1.41 0 .39.39.39 1.02 0 1.41L15.41 12l2.89 2.89c.38.38.38 1.02 0 1.41z"/></svg>)svg",
      R"svg(<svg xmlns="http://www.w3.org/2000/svg" height="24" viewBox="0 0 24 24" width="24"><path d="M19 8v3H5.83l2.88-2.88c.39-.39.39-1.02 0-1.41-.39-.39-1.02-.39-1.41 0L2.71 11.3c-.39.39-.39 1.02 0 1.41L7.3 17.3c.39.39 1.02.39 1.41 0 .39-.39.39-1.02 0-1.41L5.83 13H20c.55 0 1-.45 1-1V8c0-.55-.45-1-1-1s-1 .45-1 1z"/></svg>)svg",
      R"svg(<svg xmlns="http://www.w3.org/2000/svg" height="24" viewBox="0 0 24 24" width="24"><path d="M0 0h24v24H0V0z" fill="none"/><path d="M12 2C6.48 2 2 6.48 2 12s4.48 10 10 10 10-4.48 10-10S17.52 2 12 2zM9.29 16.29 5.7 12.7c-.39-.39-.39-1.02 0-1.41.39-.39 1.02-.39 1.41 0L10 14.17l6.88-6.88c.39-.39 1.02-.39 1.41 0 .39.39.39 1.02 0 1.41l-7.59 7.59c-.38.39-1.02.39-1.41 0z"/></svg>)svg",
      R"svg(<svg xmlns="http://www.w3.org/2000/svg" height="24" viewBox="0 0 24 24" width="24"><path d="M0 0h24v24H0V0z" fill="none"/><path d="M16 16h2c.55 0 1 .45 1 1s-.45 1-1 1h-2c-.55 0-1-.45-1-1s.45-1 1-1zm0-8h5c.55 0 1 .45 1 1s-.45 1-1 1h-5c-.55 0-1-.45-1-1s.45-1 1-1zm0 4h4c.55 0 1 .45 1 1s-.45 1-1 1h-4c-.55 0-1-.45-1-1s.45-1 1-1zM3 18c0 1.1.9 2 2 2h6c1.1 0 2-.9 2-2V8H3v10zM13 5h-2l-.71-.71c-.18-.18-.44-.29-.7-.29H6.41c-.26 0-.52.11-.7.29L5 5H3c-.55 0-1 .45-1 1s.45 1 1 1h10c.55 0 1-.45 1-1s-.45-1-1-1z"/></svg>)svg",
      R"svg(<svg xmlns="http://www.w3.org/2000/svg" height="24" viewBox="0 0 24 24" width="24"><path d="M0 0h24v24H0V0z" fill="none"/><path d="M13.35 20.13c-.76.69-1.93.69-2.69-.01l-.11-.1C5.3 15.27 1.87 12.16 2 8.28c.06-1.7.93-3.33 2.34-4.29 2.64-1.8 5.9-.96 7.66 1.1 1.76-2.06 5.02-2.91 7.66-1.1 1.41.96 2.28 2.59 2.34 4.29.14 3.88-3.3 6.99-8.55 11.76l-.1.09z"/></svg>)svg",
  };
  std::array<unsigned char, width * size> alpha{};
  for (int icon = 0; icon < count; ++icon) {
    SDL_RWops *input = SDL_RWFromConstMem(svgs[icon], SDL_strlen(svgs[icon]));
    if (!input) { std::cerr << SDL_GetError(); return 1; }
    SDL_Surface *svg = IMG_LoadSizedSVG_RW(input, size * scale, size * scale);
    SDL_RWclose(input);
    if (!svg) { std::cerr << IMG_GetError(); return 1; }
    SDL_Surface *rgba = SDL_ConvertSurfaceFormat(svg, SDL_PIXELFORMAT_RGBA32, 0);
    SDL_FreeSurface(svg);
    if (!rgba) { std::cerr << SDL_GetError(); return 1; }
    for (int y = 0; y < size; ++y) for (int x = 0; x < size; ++x) {
      unsigned sum = 0;
      for (int dy = 0; dy < scale; ++dy) for (int dx = 0; dx < scale; ++dx)
        sum += static_cast<unsigned char *>(rgba->pixels)[(y * scale + dy) * rgba->pitch + (x * scale + dx) * 4 + 3];
      alpha[y * width + icon * size + x] = (sum + scale * scale / 2) / (scale * scale);
    }
    SDL_FreeSurface(rgba);
  }
  std::cout << "// Generated from Google Material Icons (round), Apache-2.0.\n"
               "// See THIRD_PARTY_NOTICES.md. Do not edit by hand.\n"
               "#pragma once\n#include <cstdint>\nnamespace ui_icons {\n"
               "inline constexpr int size = 28, count = 7, width = size * count;\n"
               "inline constexpr std::uint8_t alpha[] = {\n";
  for (size_t i = 0; i < alpha.size(); ++i) {
    std::cout << unsigned(alpha[i]) << ',';
    if (i % width == width - 1) std::cout << '\n';
  }
  std::cout << "};\nstatic_assert(sizeof(alpha) == width * size);\n}\n";
}
