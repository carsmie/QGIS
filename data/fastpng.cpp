// Prototype: PNG encoder on plain zlib with a controllable row filter and zlib level/strategy.
#include <QGuiApplication>
#include <QImage>
#include <QBuffer>
#include <QElapsedTimer>
#include <zlib.h>
#include <cstdio>
#include <cstring>
#include <vector>
#include <cstdlib>

static void chunk(QByteArray &out, const char *type, const unsigned char *data, uint32_t len) {
  unsigned char hdr[8] = {(unsigned char)(len >> 24), (unsigned char)(len >> 16), (unsigned char)(len >> 8), (unsigned char)len,
                          (unsigned char)type[0], (unsigned char)type[1], (unsigned char)type[2], (unsigned char)type[3]};
  out.append((const char *)hdr, 8);
  if (len) out.append((const char *)data, len);
  uLong crc = crc32(0, hdr + 4, 4); if (len) crc = crc32(crc, data, len);
  unsigned char c[4] = {(unsigned char)(crc >> 24), (unsigned char)(crc >> 16), (unsigned char)(crc >> 8), (unsigned char)crc};
  out.append((const char *)c, 4);
}
static inline int paeth(int a, int b, int c) { int p = a + b - c, pa = abs(p - a), pb = abs(p - b), pc = abs(p - c); return (pa <= pb && pa <= pc) ? a : (pb <= pc ? b : c); }

// filterMode: 0..4 fixed PNG filter, 5 = adaptive (min sum of abs over all 5, like libpng)
static QByteArray encode(const QImage &src, int filterMode, int level, int strategy) {
  const QImage img = src.convertToFormat(QImage::Format_RGBA8888);  // un-premultiply, RGBA byte order = PNG order
  const int w = img.width(), h = img.height(), bpp = 4; const size_t stride = size_t(w) * bpp;
  QByteArray out("\x89PNG\r\n\x1a\n", 8);
  unsigned char ihdr[13] = {(unsigned char)(w >> 24), (unsigned char)(w >> 16), (unsigned char)(w >> 8), (unsigned char)w,
                            (unsigned char)(h >> 24), (unsigned char)(h >> 16), (unsigned char)(h >> 8), (unsigned char)h, 8, 6, 0, 0, 0};
  chunk(out, "IHDR", ihdr, 13);
  z_stream zs{}; deflateInit2(&zs, level, Z_DEFLATED, 15, 8, strategy);
  std::vector<unsigned char> zbuf(1 << 17), cand[5], zero(stride, 0);
  for (auto &c : cand) c.resize(stride + 1);
  QByteArray idat;
  auto feed = [&](const unsigned char *d, size_t n, int flush) {
    zs.next_in = (Bytef *)d; zs.avail_in = (uInt)n;
    do { zs.next_out = zbuf.data(); zs.avail_out = (uInt)zbuf.size(); deflate(&zs, flush);
         idat.append((const char *)zbuf.data(), zbuf.size() - zs.avail_out); } while (zs.avail_out == 0);
  };
  for (int y = 0; y < h; ++y) {
    const unsigned char *row = img.constScanLine(y), *prev = y ? img.constScanLine(y - 1) : zero.data();
    int best = filterMode;
    auto doFilter = [&](int f) {
      unsigned char *o = cand[f].data(); o[0] = (unsigned char)f; o++;
      switch (f) {
        case 0: memcpy(o, row, stride); break;
        case 1: for (size_t i = 0; i < stride; ++i) o[i] = row[i] - (i >= bpp ? row[i - bpp] : 0); break;
        case 2: for (size_t i = 0; i < stride; ++i) o[i] = row[i] - prev[i]; break;
        case 3: for (size_t i = 0; i < stride; ++i) o[i] = row[i] - (((i >= bpp ? row[i - bpp] : 0) + prev[i]) >> 1); break;
        case 4: for (size_t i = 0; i < stride; ++i) o[i] = row[i] - paeth(i >= bpp ? row[i - bpp] : 0, prev[i], i >= bpp ? prev[i - bpp] : 0); break;
      }
    };
    if (filterMode == 5) {
      unsigned long bestSum = ~0UL;
      for (int f = 0; f < 5; ++f) { doFilter(f); unsigned long s = 0; const unsigned char *o = cand[f].data() + 1;
        for (size_t i = 0; i < stride; ++i) s += o[i] < 128 ? o[i] : 256 - o[i];
        if (s < bestSum) { bestSum = s; best = f; } }
    } else doFilter(filterMode);
    feed(cand[best].data(), stride + 1, Z_NO_FLUSH);
  }
  feed(nullptr, 0, Z_FINISH); deflateEnd(&zs);
  chunk(out, "IDAT", (const unsigned char *)idat.constData(), (uint32_t)idat.size());
  chunk(out, "IEND", nullptr, 0);
  return out;
}

int main(int argc, char **argv) {
  qputenv("QT_QPA_PLATFORM", "offscreen"); QGuiApplication app(argc, argv);
  const char *fn[] = {"none", "sub", "up", "avg", "paeth", "adaptive"};
  for (int a = 1; a < argc; ++a) {
    const QImage img = QImage(argv[a]).convertToFormat(QImage::Format_ARGB32_Premultiplied);  // what QGIS renders into
    printf("== %s\n", argv[a]);
    auto bench = [&](const char *label, auto fn2) {
      QByteArray ba; qint64 best = 1LL << 60;
      for (int r = 0; r < 4; ++r) { QElapsedTimer t; t.start(); ba = fn2(); best = std::min(best, t.nsecsElapsed()); }
      printf("  %-34s %7.1f ms %9lld bytes\n", label, best / 1e6, (long long)ba.size());
      return ba;
    };
    bench("Qt QImage::save (QGIS now)", [&] { QByteArray b; QBuffer buf(&b); buf.open(QIODevice::WriteOnly); img.save(&buf, "PNG"); return b; });
    struct V { int f, l, s; const char *sn; } vs[] = {
      {5, 6, Z_DEFAULT_STRATEGY, "default"}, {5, 1, Z_DEFAULT_STRATEGY, "default"}, {5, 3, Z_DEFAULT_STRATEGY, "default"},
      {2, 1, Z_DEFAULT_STRATEGY, "default"}, {4, 1, Z_DEFAULT_STRATEGY, "default"}, {1, 1, Z_DEFAULT_STRATEGY, "default"},
      {4, 3, Z_DEFAULT_STRATEGY, "default"}, {2, 1, Z_RLE, "rle"}, {1, 1, Z_RLE, "rle"}, {4, 1, Z_FILTERED, "filtered"}};
    for (auto v : vs) {
      char label[64]; snprintf(label, sizeof label, "zlib %d %-8s filter %s", v.l, v.sn, fn[v.f]);
      QByteArray ba = bench(label, [&] { return encode(img, v.f, v.l, v.s); });
      if (v.f == 4 && v.l == 1 && v.s == Z_DEFAULT_STRATEGY) { QString o = QString(argv[a]) + ".fast.png"; FILE *f = fopen(qPrintable(o), "wb"); fwrite(ba.data(), 1, ba.size(), f); fclose(f); }
    }
  }
}
