#pragma once
#include <Arduino.h>

namespace Display {
#ifdef GERTY_GUITION
constexpr int WIDTH = 480;
#ifdef GERTY_JC4827W543
constexpr int HEIGHT = 272;
#else
constexpr int HEIGHT = 320;
#endif
constexpr size_t BUFFER_BYTES = WIDTH * HEIGHT * sizeof(uint16_t);
constexpr bool SUPPORTS_DEEP_SLEEP = false;
#else
constexpr int WIDTH = 960;
constexpr int HEIGHT = 540;
constexpr size_t BUFFER_BYTES = WIDTH * HEIGHT / 2;
constexpr bool SUPPORTS_DEEP_SLEEP = true;
#endif
bool begin();
void writeRow(uint8_t *buffer, int y, const uint16_t *pixels);
bool present(uint8_t *buffer);
bool showError(const char *message);
void idle();
bool nextPageTapped();
}
