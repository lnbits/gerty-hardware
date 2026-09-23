"""Exercise the real Seeed backend with fault-injected SPI/panel operations.

This checks buffer/power/commit behavior, not the physical panel waveform.
"""
from pathlib import Path
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]

FAKES = r'''
#include <cassert>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <vector>
#define RTC_DATA_ATTR
#define GERTY_EPAPER_FRAME_CACHE_H
namespace EpaperFrameCache {
static std::vector<uint8_t> disk;
uint32_t save(const uint8_t *data, size_t size) { disk.assign(data, data+size); return 42; }
bool load(uint8_t *data, size_t size, uint32_t expected) {
  if(expected != 42 || disk.size() != size) return false;
  memcpy(data, disk.data(), size); return true;
}
}
constexpr int LOW=0, HIGH=1, OUTPUT=1, MSBFIRST=1, SPI_MODE0=0;
using gpio_num_t = int;
constexpr int GPIO_NUM_43=43;
static uint32_t now=0;
static int pins[49]={};
static bool supplyHeld=false, allocationFails=false;
uint32_t millis() { return now; }
void delay(uint32_t ms) { now += ms; }
void pinMode(int, int) {}
void digitalWrite(int pin, int value) { pins[pin]=value; }
int digitalRead(int pin) { return pins[pin]; }
void *ps_malloc(size_t size) { return allocationFails ? nullptr : malloc(size); }
void gpio_hold_en(int pin) { assert(pin==43); supplyHeld=true; }
void gpio_hold_dis(int pin) { assert(pin==43); supplyHeld=false; }
void gpio_deep_sleep_hold_en() {}
void gpio_deep_sleep_hold_dis() {}
struct SerialPort { void printf(const char *, ...) {} } Serial;
struct SPISettings { SPISettings(int hz, int, int) { assert(hz==4000000); } };
struct SPIBus {
  void begin(int sck, int miso, int mosi, int cs) {
    assert(sck==7 && miso==-1 && mosi==9 && cs==44);
    assert(pins[43]==HIGH && !supplyHeld);
  }
} SPI;
class Adafruit_GFX {
 protected:
  // Deliberately retain the real library's member names: unqualified WIDTH in
  // a derived constructor must not read the uninitialized inherited member.
  int16_t WIDTH, HEIGHT;
  int cursorX=0, cursorY=0;
 public:
  Adafruit_GFX(int16_t w, int16_t h): WIDTH(w), HEIGHT(h) {
    assert(w==800 && h==480);
  }
  virtual void drawPixel(int16_t, int16_t, uint16_t)=0;
  void fillRect(int x, int y, int w, int h, uint16_t colour) {
    for(int row=y; row<y+h; ++row)
      for(int col=x; col<x+w; ++col) drawPixel(col,row,colour);
  }
  void fillScreen(uint16_t c) { fillRect(0,0,WIDTH,HEIGHT,c); }
  void setTextWrap(bool) {}
  void setTextColor(int) {}
  void setTextSize(int) {}
  void setCursor(int x, int y) { cursorX=x; cursorY=y; }
  void print(const char *) { drawPixel(cursorX,cursorY,0); }
};
static int fault=0, writes=0, refreshes=0, sleeps=0, ends=0;
static std::vector<uint8_t> transferred;
class GxEPD2_750_GDEY075T7 {
  void (*callback)(const void *)=nullptr;
  void operation(int phase) {
    if(fault==phase) {
      pins[4]=LOW;
      delay(9500);
      callback(nullptr);
      // BUSY can recover after the driver's timeout. The error must stay latched.
    }
    pins[4]=HIGH;
  }
 public:
  static constexpr int WIDTH=800, HEIGHT=480;
  GxEPD2_750_GDEY075T7(int cs, int dc, int rst, int busy) {
    assert(cs==44 && dc==10 && rst==38 && busy==4);
  }
  void end() { ++ends; }
  void selectSPI(SPIBus &, SPISettings) {}
  void init(int, bool initial, int, bool) { assert(initial); pins[4]=HIGH; }
  void setBusyCallback(void (*cb)(const void *)) { callback=cb; }
  void writeImageForFullRefresh(uint8_t *data, int x, int y, int w, int h) {
    assert(x==0 && y==0 && w==800 && h==480);
    ++writes;
    transferred.assign(data,data+48000);
    operation(1);
  }
  void refresh(bool partial) { assert(!partial); ++refreshes; operation(2); }
  void powerOff() { operation(3); }
  void hibernate() { ++sleeps; pins[4]=LOW; }
};
'''

CHECKS = r'''
static void powerOff() {
  assert(pins[43]==LOW && supplyHeld);
  assert(pins[6]==LOW);
  for(int p : {7,9,44,10,38}) assert(pins[p]==LOW);
}
int main(int argc, char **argv) {
  const int scenario=atoi(argv[1]);
  if(scenario==4) allocationFails=true;
  assert(Display::begin() == !allocationFails);
  powerOff();
  if(allocationFails) return 0;
  assert(!Display::showError("no frame after wake"));
  assert(writes==0);
  if(scenario==5) {
    assert(Display::showSetup());
    assert(transferred.front()==0xFF && transferred.back()==0xFF);
    assert(transferred[12*100+20/8] != 0xFF);
    powerOff();
    return 0;
  }
  if(scenario >= 6) {
    std::vector<uint8_t> original(48000, 0xA5), next(48000, 0x3C);
    assert(Display::present(original.data()));
    if(scenario==7) {
      // Emulate PSRAM loss with the RTC cache identity and flash preserved.
      memset(Display::retainedFrame, 0, 48000);
      Display::frameKnown = false;
      assert(Display::begin());
      assert(Display::frameKnown);
    }
    if(scenario==10) {
      Display::cachedFrame=0;
      Display::frameKnown=false;
      assert(Display::begin());
      const int count=writes;
      assert(!Display::showThinking());
      assert(Display::hideThinking());
      assert(writes==count); // Missing cache never blanks the retained page.
      return 0;
    }
    if(scenario==11) fault=2;
    assert(Display::showThinking() == (scenario!=11));
    fault=0;
    assert(transferred != original);
    for(int y=0; y<480; ++y) for(int x=0; x<100; ++x) {
      const bool badge = y>=424 && y<472 && x>=91 && x<99;
      if(!badge) assert(transferred[y*100+x]==original[y*100+x]);
    }
    assert(EpaperFrameCache::disk == original); // Never persist the temporary face.
    if(scenario==8) {
      assert(Display::present(next.data()));
      const int count = writes;
      assert(Display::hideThinking());
      assert(writes == count && transferred == next); // Never restore old pixels over new page.
    } else if(scenario==9) {
      fault=2;
      assert(!Display::present(next.data()));
      fault=0;
      assert(Display::hideThinking());
      assert(transferred == original);
    } else if(scenario==12) {
      fault=2;
      assert(!Display::hideThinking());
      assert(!Display::frameKnown && !Display::cachedFrame);
      fault=0;
      assert(Display::present(next.data()));
      assert(Display::hideThinking() && transferred==next);
    } else {
      assert(Display::hideThinking());
      assert(transferred == original); // Unchanged page / failed fetch / sleep response.
    }
    powerOff();
    return 0;
  }
  std::vector<uint8_t> image(48000,0xA5);
  fault=scenario;
  assert(Display::present(image.data()) == (fault==0));
  powerOff();
  assert(writes==1 && ends==1);
  if(fault) {
    assert(refreshes==(fault==1 ? 0 : 1));
    assert(sleeps==0);
    assert(!Display::showError("refresh failed"));
    assert(writes==1); // No destructive error repaint after uncertain output.
    fault=0;
    assert(Display::present(image.data())); // Retry can recover.
    powerOff();
  } else {
    assert(transferred==image);
    assert(Display::showError("Wi-Fi unavailable"));
    for(int i=0; i<44400; ++i) assert(transferred[i]==image[i]);
    assert(transferred.back()==0xFF); // Error strip replaces only bottom pixels.
    powerOff();
  }
  Display::idle();
  powerOff();
}
'''


class SeeedBackendTest(unittest.TestCase):
    def test_refresh_failures_power_down_and_preserve_known_frames(self):
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            for name in ('Arduino.h', 'Adafruit_GFX.h', 'SPI.h', 'driver/gpio.h',
                         'gdey/GxEPD2_750_GDEY075T7.h'):
                header = root / name
                header.parent.mkdir(parents=True, exist_ok=True)
                header.write_text('#pragma once\n')
            source = root / 'test.cpp'
            source.write_text(FAKES + '\n#include "' +
                              str(ROOT / 'src/display_seeed.cpp') + '"\n' + CHECKS)
            executable = root / 'test'
            subprocess.run(['c++', '-std=c++11', '-DGERTY_SEEED_TRMNL',
                            '-I', str(root), '-I', str(ROOT / 'include'),
                            str(source), '-o', str(executable)], check=True)
            for scenario in range(13):
                with self.subTest(scenario=scenario):
                    subprocess.run([str(executable), str(scenario)], check=True)


if __name__ == '__main__':
    unittest.main()
