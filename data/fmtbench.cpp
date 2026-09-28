#include <QGuiApplication>
#include <QImage>
#include <QBuffer>
#include <QImageWriter>
#include <QElapsedTimer>
#include <cstdio>
int main(int argc, char **argv) {
  qputenv("QT_QPA_PLATFORM", "offscreen");
  QGuiApplication app(argc, argv);
  QImage img = QImage(argv[1]).convertToFormat(QImage::Format_ARGB32_Premultiplied);
  struct V { const char *fmt; int compression; const char *label; } vs[] = {
    {"PNG", -1, "PNG default (QGIS now)"}, {"PNG", 20, "PNG zlib 1"},
    {"TIFF", 0, "TIFF uncompressed"}, {"TIFF", 1, "TIFF LZW"}};
  for (auto v : vs) {
    QByteArray ba; qint64 best = 1LL << 60;
    for (int r = 0; r < 5; ++r) {
      ba.clear(); QBuffer buf(&ba); buf.open(QIODevice::WriteOnly);
      QElapsedTimer t; t.start();
      QImageWriter w(&buf, v.fmt); if (v.compression >= 0) w.setCompression(v.compression);
      if (!w.write(img)) { printf("%s failed: %s\n", v.label, qPrintable(w.errorString())); break; }
      best = std::min(best, t.nsecsElapsed());
    }
    char name[64]; snprintf(name, sizeof name, "out_%s_%d.bin", v.fmt, v.compression);
    FILE *f = fopen(name, "wb"); fwrite(ba.data(), 1, ba.size(), f); fclose(f);
    printf("%-24s encode %6.1f ms  %8lld bytes  -> %s\n", v.label, best / 1e6, (long long)ba.size(), name);
  }
}
