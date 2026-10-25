#include "app.h"
#include "qualification.h"
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
  app.setQuitOnLastWindowClosed(false);
  installPlatinumStyle();
  auto args = app.arguments();
  try {
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
           {"system-7.5.5-base-v1", "mac-os-8.1-base-v1", "mac-os-8.6-base-v1"})
        if (!ids.contains(id))
          Model::fail("Missing public base image.");
      std::printf("Native HTTPS catalog check passed: %lld editions, including "
                  "all three classic bases.\n",
                  static_cast<long long>(rows.size()));
      return 0;
    }
    if (args.contains("--self-test")) {
      int i = args.indexOf("--self-test");
      return selfTest(args.value(i + 1), args.value(i + 2));
    }
    App window;
    window.show();
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
    return 1;
  }
}
