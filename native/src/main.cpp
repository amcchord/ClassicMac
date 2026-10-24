#include "app.h"
#include "qualification.h"
#include <cstdio>
int main(int argc, char **argv) {
  QGuiApplication::setHighDpiScaleFactorRoundingPolicy(
      Qt::HighDpiScaleFactorRoundingPolicy::RoundPreferFloor);
  QApplication app(argc, argv);
  app.setApplicationName("ClassicMac");
  app.setApplicationVersion(CLASSICMAC_VERSION);
  app.setOrganizationName("ClassicMac");
  app.setQuitOnLastWindowClosed(false);
  installPlatinumStyle();
  auto args = app.arguments();
  try {
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
