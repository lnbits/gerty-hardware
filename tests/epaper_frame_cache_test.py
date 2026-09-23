"""Verify the real frame cache rejects stale, truncated, and damaged snapshots."""
from pathlib import Path
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]
FAKE_FS = r'''
#pragma once
#include <cstdint>
#include <cstring>
#include <algorithm>
#include <map>
#include <string>
#include <vector>
static std::map<std::string, std::vector<uint8_t>> files;
static bool mountOk=true, shortWrite=false, renameOk=true;
struct File {
  std::string path;
  bool valid=false;
  explicit operator bool() const { return valid; }
  size_t size() { return files[path].size(); }
  size_t read(uint8_t *out, size_t count) {
    count=std::min(count, size()); memcpy(out, files[path].data(), count); return count;
  }
  size_t write(const uint8_t *data, size_t count) {
    if(shortWrite) --count;
    files[path].assign(data, data+count); return count;
  }
  void close() {}
};
struct Filesystem {
  bool begin(bool) { return mountOk; }
  File open(const char *path, const char *mode) {
    if(*mode=='w') files[path].clear();
    return File{path, files.count(path)!=0};
  }
  bool rename(const char *from, const char *to) {
    if(!renameOk) return false;
    files[to]=files[from]; files.erase(from); return true;
  }
} LittleFS;
'''
CHECKS = r'''
#include <cassert>
#include "epaper_frame_cache.h"
int main() {
  uint8_t page[32], copy[32];
  memset(page,0xA5,sizeof(page));
  assert(!EpaperFrameCache::load(copy,sizeof(copy),0));
  const auto identity=EpaperFrameCache::save(page,sizeof(page));
  assert(identity && EpaperFrameCache::load(copy,sizeof(copy),identity));
  assert(memcmp(page,copy,sizeof(page))==0);
  files["/display-frame.bin"][0]^=1;
  assert(!EpaperFrameCache::load(copy,sizeof(copy),identity));
  files["/display-frame.bin"].pop_back();
  assert(!EpaperFrameCache::load(copy,sizeof(copy),identity));
  shortWrite=true;
  assert(!EpaperFrameCache::save(page,sizeof(page)));
  shortWrite=false; renameOk=false;
  assert(!EpaperFrameCache::save(page,sizeof(page)));
  renameOk=true; mountOk=false;
  assert(!EpaperFrameCache::save(page,sizeof(page)));
  assert(!EpaperFrameCache::load(copy,sizeof(copy),identity));
}
'''

class FrameCacheTest(unittest.TestCase):
    def test_recovery_and_storage_failures(self):
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            (root / 'LittleFS.h').write_text(FAKE_FS)
            (root / 'test.cpp').write_text(CHECKS)
            subprocess.run(['c++', '-std=c++14', '-I', str(root), '-I', str(ROOT / 'include'),
                            str(root / 'test.cpp'), '-o', str(root / 'test')], check=True)
            subprocess.run([str(root / 'test')], check=True)
