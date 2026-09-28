#include <QGuiApplication>
#include <QImage>
#include <QBuffer>
#include <QImageWriter>
#include <QElapsedTimer>
#include <cstdio>
int main(int argc, char **argv) {
  qputenv("QT_QPA_PLATFORM", "offscreen");
  QGuiApplication app(argc, argv);
  for (int f = 1; f < argc; ++f) {
    QImage img(argv[f]);
    img = img.convertToFormat(QImage::Format_ARGB32_Premultiplied); // what QGIS renders into
    printf("%s %dx%d\n", argv[f], img.width(), img.height());
    for (int c : {-1, 0, 10, 20, 50, 80, 100}) {
      QByteArray ba; qint64 best = 1LL << 60;
      for (int r = 0; r < 5; ++r) {
        ba.clear(); QBuffer buf(&ba); buf.open(QIODevice::WriteOnly);
        QElapsedTimer t; t.start();
        if (c < 0) img.save(&buf, "PNG");                 // what QGIS does today
        else { QImageWriter w(&buf, "PNG"); w.setCompression(c); w.write(img); }
        best = std::min(best, t.nsecsElapsed());
      }
      printf("  %-26s %6.1f ms  %8lld bytes\n", c < 0 ? "save() default (QGIS now)" : qPrintable(QString("compression %1 (zlib %2)").arg(c).arg(c * 9 / 91)), best / 1e6, (long long)ba.size());
    }
  }
}
