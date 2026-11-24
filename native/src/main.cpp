#include "app.h"
#include "qualification.h"
#include <QMessageBox>
#include <QSessionManager>
#include <cstdio>
int main(int argc, char **argv) {
  // The Platinum artwork is pixel art: fractional UI zoom makes alternate
  // stripes, glyph strokes and bevels different widths. Keep OS DPI awareness
  // while snapping the interface to the nearest whole physical-pixel zoom.
  QGuiApplication::setHighDpiScaleFactorRoundingPolicy(
      Qt::HighDpiScaleFactorRoundingPolicy::Round);
  QApplication app(argc, argv);
  app.setApplicationName("ClassicMac");
  app.setApplicationVersion(CLASSICMAC_VERSION);
  app.setOrganizationName("ClassicMac");
  QGuiApplication::setDesktopFileName("classicmac");
  app.setQuitOnLastWindowClosed(false);
  installPlatinumStyle();
  auto args = app.arguments();
  try {
    if (args.contains("--qualification-engine"))
      return qualificationEngine();
    if (args.contains("--display-test")) {
      int i = args.indexOf("--display-test");
      displayTests(args.value(i + 1), args.value(i + 2));
      return 0;
    }
    if (args.contains("--catalog-check")) {
      auto rows = Library::load();
      QSet<QString> ids;
      for (auto row : rows)
        ids.insert(row.toObject()["id"].toString());
      for (auto id :
           {"system-7.5.5-base-v1", "mac-os-8.1-base-v1", "mac-os-8.6-base-v1",
            "mac-os-7.5.5-v1", "os921-full-v4", "mac-os-x-10-4-11-tiger-period-v1"})
        if (!ids.contains(id))
          Model::fail("Missing ClassicMac or RetroBoot Library image.");
      std::printf("Native HTTPS catalog check passed: %lld editions, including "
                  "ClassicMac and RetroBoot images.\n",
                  static_cast<long long>(rows.size()));
      return 0;
    }
    if (args.contains("--qualify-osx")) {
      qualifyOSX(args.value(args.indexOf("--qualify-osx") + 1));
      return 0;
    }
    if (args.contains("--self-test")) {
      int i = args.indexOf("--self-test");
      return selfTest(args.value(i + 1), args.value(i + 2));
    }
    App window;
    window.show();
#ifndef QT_NO_SESSIONMANAGER
    QObject::connect(&app, &QGuiApplication::commitDataRequest, &window,
                     [&window](QSessionManager &sm) {
                       if (window.engines.isEmpty())
                         return;
                       sm.setRestartHint(QSessionManager::RestartNever);
                       if (sm.allowsInteraction()) {
                         if (!window.requestQuit())
                           sm.cancel();
                         sm.release();
                         return;
                       }
                       for (auto *e : window.engines.values())
                         if (e && e->running && !e->stoppingRequested() &&
                             e->machine.family() != "powerMacOSX") {
                           try {
                             e->shutdown();
                           } catch (...) {
                           }
                         }
                       QElapsedTimer wait;
                       wait.start();
                       while (!window.engines.isEmpty() && wait.elapsed() < 5000)
                         QCoreApplication::processEvents(QEventLoop::AllEvents,
                                                         50);
                     });
#endif
    if (!args.contains("--qualify")) {
      for (int i = 1; i < args.size(); ++i) {
        const QString &arg = args.at(i);
        if (arg.startsWith("--"))
          continue;
        QString path = arg;
        if (path.startsWith("file://", Qt::CaseInsensitive))
          path = QUrl(path).toLocalFile();
        const QFileInfo info(path);
        if (path.endsWith(".classic", Qt::CaseInsensitive) && info.isDir()) {
          try {
            window.addMachine(path);
          } catch (const std::exception &e) {
            problem(&window, QString::fromUtf8(e.what()));
          }
          break;
        }
      }
    }
    if (args.contains("--qualify")) {
      if (qEnvironmentVariableIsEmpty("CLASSICMAC_TEST_HOME") ||
          qEnvironmentVariableIsEmpty("CLASSICMAC_TEST_DOCUMENTS") ||
          qEnvironmentVariableIsEmpty("CLASSICMAC_QUALIFY"))
        Model::fail("Qualification requires isolated test data and documents "
                    "directories.");
      QApplication::setAttribute(Qt::AA_DontUseNativeDialogs);
      int i = args.indexOf("--qualify");
      QTimer::singleShot(100, &window, [&window, args, i] {
        qualify(&window, args.value(i + 1));
      });
    }
    return app.exec();
  } catch (const std::exception &e) {
    std::fprintf(stderr, "ClassicMac: %s\n", e.what());
#ifdef Q_OS_WIN
    const bool headless = args.contains("--self-test") ||
                          args.contains("--catalog-check") ||
                          args.contains("--display-test") ||
                          args.contains("--qualification-engine") ||
                          args.contains("--qualify") ||
                          args.contains("--qualify-osx");
    if (!headless)
      QMessageBox::critical(nullptr, "ClassicMac", QString::fromUtf8(e.what()));
#endif
    return 1;
  }
}
