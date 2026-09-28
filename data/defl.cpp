// zlib vs libdeflate on PNG-filtered ("up") RGBA rows of real QGIS output
#include <QGuiApplication>
#include <QImage>
#include <QElapsedTimer>
#include <zlib.h>
#include <libdeflate.h>
#include <vector>
#include <cstdio>
#include <cstring>
int main(int argc, char **argv) {
  qputenv("QT_QPA_PLATFORM", "offscreen"); QGuiApplication app(argc, argv);
  for (int a = 1; a < argc; ++a) {
    QImage img = QImage(argv[a]).convertToFormat(QImage::Format_RGBA8888);
    const int w = img.width(), h = img.height(); const size_t stride = size_t(w) * 4;
    std::vector<unsigned char> raw(h * (stride + 1)), zero(stride, 0);
    for (int y = 0; y < h; ++y) {
      unsigned char *o = raw.data() + y * (stride + 1); o[0] = 2;
      const unsigned char *r = img.constScanLine(y), *p = y ? img.constScanLine(y - 1) : zero.data();
      for (size_t i = 0; i < stride; ++i) o[1 + i] = r[i] - p[i];
    }
    printf("== %s (%zu bytes filtered)\n", argv[a], raw.size());
    std::vector<unsigned char> out(compressBound(raw.size()));
    for (int lvl : {1, 6}) {
      qint64 best = 1LL << 60; uLongf n = 0;
      for (int r = 0; r < 5; ++r) { n = out.size(); QElapsedTimer t; t.start(); compress2(out.data(), &n, raw.data(), raw.size(), lvl); best = std::min(best, t.nsecsElapsed()); }
      printf("  zlib       level %d  %6.2f ms  %8lu bytes\n", lvl, best / 1e6, (unsigned long)n);
    }
    for (int lvl : {1, 2, 3, 4, 6}) {
      libdeflate_compressor *c = libdeflate_alloc_compressor(lvl);
      qint64 best = 1LL << 60; size_t n = 0;
      for (int r = 0; r < 5; ++r) { QElapsedTimer t; t.start(); n = libdeflate_zlib_compress(c, raw.data(), raw.size(), out.data(), out.size()); best = std::min(best, t.nsecsElapsed()); }
      // verify round trip with zlib
      std::vector<unsigned char> back(raw.size()); uLongf bn = back.size();
      const bool ok = uncompress(back.data(), &bn, out.data(), n) == Z_OK && bn == raw.size() && memcmp(back.data(), raw.data(), raw.size()) == 0;
      printf("  libdeflate level %d  %6.2f ms  %8zu bytes  zlib-decodable+identical: %s\n", lvl, best / 1e6, n, ok ? "yes" : "NO");
      libdeflate_free_compressor(c);
    }
  }
}
