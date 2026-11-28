#include "qualification.h"
#include "app.h"
#include "checkpoint.h"
#include "printer.h"
#include "transfer_meter.h"
#include "updater.h"
#include <QDirIterator>
#include <QElapsedTimer>
#include <QRegularExpression>
#include <cstdio>
#include <cstring>
#include <type_traits>
#include <zlib.h>
using namespace Platinum;
static void require(bool value, const QString &reason) {
  if (!value)
    Model::fail(reason);
}
int selfTest(const QString &fixtures, const QString &output) {
  QJsonArray reports;
  int count = 0;
  auto check = [&](QString name, int w, int h,
                   std::function<void(Pixels &)> render) {
    Pixels p(w, h);
    render(p);
    QImage expected(fixtures + "/" + name + ".png");
    require(!expected.isNull(), "Missing independent fixture " + name);
    expected = expected.convertToFormat(QImage::Format_ARGB32);
    require(expected.size() == p.image.size(), "Fixture size " + name);
    for (int scale : {1, 2}) {
      auto actual = p.image.scaled(w * scale, h * scale, Qt::IgnoreAspectRatio,
                                   Qt::FastTransformation),
           want = expected.scaled(w * scale, h * scale, Qt::IgnoreAspectRatio,
                                  Qt::FastTransformation);
      qint64 differing = 0;
      for (int y = 0; y < actual.height(); ++y)
        for (int x = 0; x < actual.width(); ++x)
          differing += actual.pixel(x, y) != want.pixel(x, y);
      if (differing && !output.isEmpty()) {
        QDir().mkpath(output);
        actual.save(output + "/" + name + "-actual-" + QString::number(scale) + "x.png");
      }
      require(differing == 0, name + QString(" at %1x: %2 differing pixels")
                                         .arg(scale)
                                         .arg(differing));
      reports.append(QJsonObject{
          {"name", name}, {"scale", scale}, {"differingPixels", differing}});
      ++count;
    }
    if (!output.isEmpty()) {
      QDir().mkpath(output);
      p.image.save(output + "/" + name + ".png");
    }
  };
  for (auto state : QStringList{"normal", "pressed", "disabled"}) {
    check("button-" + state, 80, 20, [=](Pixels &p) { button(p, state); });
    check("default-" + state, 86, 26,
          [=](Pixels &p) { button(p, state, true); });
    check("popup-" + state, 100, 20, [=](Pixels &p) { popup(p, state); });
    check("checkbox-mixed-" + state, 14, 12,
          [=](Pixels &p) { toggle(p, false, false, state, true); });
    check("os9-bevel-" + state, 100, 22, [=](Pixels &p) { bevel(p, state); });
    for (bool on : {false, true})
      for (bool radio : {false, true})
        check((radio ? QString("radio") : QString("checkbox")) + "-" +
                  (on ? "on" : "off") + "-" + state,
              radio ? 12 : 14, 12,
              [=](Pixels &p) { toggle(p, radio, on, state); });
  }
  check("group", 250, 100, [](Pixels &p) { group(p); });
  check("sunken", 104, 26, sunken);
  check("focus", 256, 56, focus);
  check("progress-0", 250, 14, [](Pixels &p) { progress(p, 0.3); });
  check("progress-1", 250, 14, [](Pixels &p) { progress(p, 0.7); });
  for (auto kind : QStringList{"close", "zoom", "collapse"}) {
    check("os8-" + kind, 13, 13, [=](Pixels &p) { titleBox(p, kind); });
    check("os9-" + kind + "-pressed", 13, 13,
          [=](Pixels &p) { titleBox(p, kind, true); });
  }
  for (auto state : QStringList{"active", "inactive", "collapsed"})
    check("os9-window-" + state, 333, state == "collapsed" ? 23 : 169,
          [=](Pixels &p) {
            windowFrame(p, state != "inactive", state == "collapsed");
          });
  check("system-ascii", 256, 120, [](Pixels &p) {
    p.fill(0, 0, p.w(), p.h(), 0xffffffff);
    for (int c = 32; c < 127; ++c) {
      int i = c - 32;
      text(p, QString(QChar(c)), i % 16 * 16, i / 16 * 20 + 14);
    }
  });
  QFile fontManifest(fixtures + "/native-fonts.json");
  require(fontManifest.open(QFile::ReadOnly), "Missing native font provenance");
  const auto fontCodes = QJsonDocument::fromJson(fontManifest.readAll()).object()["codes"].toArray();
  for (const auto &font : QStringList{"system", "regular", "small"})
    check("native-font-" + font, 520, 336, [font, fontCodes](Pixels &p) {
      p.image.fill(0xffffffff);
      for (int i = 0; i < fontCodes.size(); ++i) {
        const auto ch = QString(QChar(fontCodes[i].toInt()));
        const int x = 4 + i % 16 * 32, y = i / 16 * 24;
        text(p, ch, x, y + 16, 0xff000000, font);
        const int advance = textWidth(ch, font);
        for (int bit = 0; bit < 8; ++bit)
          if (advance & (1 << bit)) p.pixel(x + 22 + bit, y + 21, 0xff000000);
      }
    });
  check("os9-window-movable", 267, 178, [](Pixels &p) { movableDialog(p); });
  check("os9-window-movable-inactive", 267, 178, [](Pixels &p) { movableDialog(p, 70, false); });
  check("os9-menu-panel", 153, 123, [](Pixels &p) {
    menuFrame(p);
    p.fill(1, 20, 150, 16, 0xff333399);
    p.vertical(1, 20, 16, 0xff6666cc);
    p.vertical(150, 20, 16, 0xff000088);
    p.line(1, 54, 150, 0xff888888);
    p.line(1, 55, 150, 0xffffffff);
  });
  check("os9-field-normal", 254, 24, [](Pixels &p) {
    sunken(p);
    p.fill(2, 2, 250, 20, 0xffffffff);
  });
  check("os9-field-focused", 256, 26, [](Pixels &p) {
    p.stretch(measured()["FIELD_FOCUS"].toArray(), 3);
    p.fill(3, 3, 250, 20, 0xffffffff);
  });
  check("os9-button-focused", 84, 24, [](Pixels &p) {
    p.stretch(measured()["BUTTON_FOCUS"].toArray(), 6);
  });
  check("os9-checkbox-focused", 16, 16, [](Pixels &p) {
    p.fill(0, 0, 16, 16, 0xff6666cc);
    Pixels indicator(14, 12, 0);
    toggle(indicator, false, false);
    QPainter(&p.image).drawImage(2, 2, indicator.image);
  });
  for (int phase = 0; phase < 4; ++phase)
    check("os9-progress-phase-" + QString::number(phase), 250, 14,
           [phase](Pixels &p) { progress(p, -1, phase); });
  for (const auto &state : QStringList{"normal", "up-pressed", "down-pressed", "disabled"})
    check("os9-scroll-" + state, 16, 200, [state](Pixels &p) {
      scrollBar(p, state == "disabled" ? -1 : 46,
                 state == "up-pressed" ? -1 : state == "down-pressed" ? 1 : 0);
    });
  check("os9-scroll-horizontal", 200, 16, [](Pixels &p) {
    Pixels vertical(16, 200);
    scrollBar(vertical, 46);
    p.image = vertical.image.transformed(QTransform(0, 1, 1, 0, 0, 0));
  });
  QFile advances(fixtures + "/system-advances.json");
  require(advances.open(QFile::ReadOnly), "Missing font advances");
  auto widths = QJsonDocument::fromJson(advances.readAll()).array();
  for (int i = 0; i < widths.size(); ++i)
    require(textWidth(QString(QChar(i + 32))) == widths[i].toInt(),
            "Font advance differs");
  auto config = Model::freshConfig("Test");
  require(config["ramMB"].toInt() == 512, "Power Mac defaults");
  int rejected = 0;
  for (auto bad : QStringList{"../disk.img", "a/b", "a\\b", ".", "..", ""})
    try {
      Model::localName(bad);
    } catch (...) {
      ++rejected;
    }
  require(rejected == 6, "Unsafe machine paths accepted");
  for (auto key : QStringList{"ramMB", "width", "depth"}) {
    auto c = config;
    c[key] = -1;
    bool invalid = false;
    try {
      Model::validate(c);
    } catch (...) {
      invalid = true;
    }
    require(invalid, "Invalid hardware accepted");
  }
  require(Engine::pastePlan("Aé\n").size() == 3, "MacRoman paste mapping");
  bool invalid = false;
  try {
    Engine::pastePlan(QString::fromUtf8("hello😀"));
  } catch (...) {
    invalid = true;
  }
  require(invalid, "Unsupported paste must fail before sending keys");
  require(
      VNC::authenticate(QByteArray::fromHex("000102030405060708090a0b0c0d0e0f"),
                        "password")
              .toHex() == "b866924125c8eebb9debc1db61c538e2",
      "VNC authentication vector");
  QFile golden(":/argument-golden.json");
  require(golden.open(QFile::ReadOnly), "Missing argument oracle");
  auto profiles = QJsonDocument::fromJson(golden.readAll()).array();
  for (auto row : profiles) {
    auto profile = row.toObject();
    const auto platform = profile["platform"].toString();
    if (!platform.isEmpty()) {
#if defined(Q_OS_LINUX)
      if (platform != "linux")
        continue;
#elif defined(Q_OS_WIN)
      if (platform != "windows")
        continue;
#elif defined(Q_OS_MACOS)
      if (platform != "macos" && platform != "darwin")
        continue;
#else
      continue;
#endif
    }
    auto actual = Model::argumentsFor(
        {"/fixture/Test.classic", profile["config"].toObject()}, 6000, true);
    for (auto &s : actual)
      s.replace(Model::runtime(), "/runtime");
    QStringList expected;
    for (auto v : profile["arguments"].toArray())
      expected << v.toString();
    require(actual == expected, "QEMU argument parity differs for " +
                                    QJsonDocument(profile["config"].toObject())
                                        .toJson(QJsonDocument::Compact));
  }
  auto legacy = config;
  legacy["useVirtioDisk"] = true;
  legacy["useG4CPU"] = false;
  legacy["tabletInput"] = false;
  legacy["toolsCDInserted"] = false;
  auto args =
      Model::argumentsFor({"/fixture/Test.classic", legacy}, 6000, true);
  require(args.contains("cuda.idle-irq-ms=50") &&
              args.contains("rtl8139.tx-delay-ns=1000000") &&
              !args.contains("shift=4,sleep=off") &&
              args.contains("virtio-blk-pci,drive=startup0,addr=0xf"),
          "8.6 startup profile lost");
  auto legacyAll = legacy;
  legacyAll["toolsCDInserted"] = true;
  legacyAll["cdImagePath"] = "/fixture/Game.iso";
  if (Model::supportsHostSharing())
    legacyAll["sharedFolderPath"] = "/fixture/Shared";
  legacyAll["tabletInput"] = true;
  auto legacyAllArgs =
      Model::argumentsFor({"/fixture/Test.classic", legacyAll}, 6000, true);
  require(legacyAllArgs.contains("virtio-blk-pci,drive=startup0,addr=0xf") &&
              legacyAllArgs.contains(
                  "virtio-blk-pci,drive=classicmac-tools,addr=0x10") &&
              legacyAllArgs.contains(
                  "virtio-blk-pci,drive=classicmac-cd,addr=0x11") &&
              (!Model::supportsHostSharing() ||
               legacyAllArgs.contains(
                   "virtio-9p-pci,addr=0x13,fsdev=share0,mount_tag=Shared Files")) &&
              legacyAllArgs.contains("virtio-tablet-pci,addr=0x14"),
          "8.6 multi-virtio PCI slot allocation");
  QTemporaryDir pramTmp;
  const QString pramPath = pramTmp.path() + "/pram.img";
  QFile pramFile(pramPath);
  require(pramFile.open(QFile::WriteOnly) &&
              pramFile.write(QByteArray(512, '\0')) == 512,
          "Quadra PRAM test file creation");
  pramFile.close();
  auto qBase = Model::freshConfig("QuadraPRAM", "quadra800");
  qBase["cdImagePath"] = "/fixture/os75.iso";
  qBase["floppyImagePath"] = "/fixture/disktools.img";
  qBase["bootFromCD"] = false;
  qBase["bootFromFloppy"] = false;
  Engine::applyQuadraBootPRAM(pramPath, Model::validate(qBase));
  auto readBootRec = [&] {
    QFile f(pramPath);
    require(f.open(QFile::ReadOnly) && f.seek(0x78), "Read PRAM boot rec");
    return f.read(4);
  };
  require(readBootRec() == QByteArray("\xff\xff\xff\xdf", 4),
          "Quadra PRAM HD boot bytes");
  auto qCd = qBase;
  qCd["bootFromCD"] = true;
  qCd["bootFromFloppy"] = true;
  auto qCdValid = Model::validate(qCd);
  require(!qCdValid["bootFromFloppy"].toBool(),
          "bootFromCD clears bootFromFloppy");
  Engine::applyQuadraBootPRAM(pramPath, qCdValid);
  require(readBootRec() == QByteArray("\xff\xff\xff\xdc", 4),
          "Quadra PRAM CD boot bytes");
  auto qFd = qBase;
  qFd["bootFromFloppy"] = true;
  Engine::applyQuadraBootPRAM(pramPath, Model::validate(qFd));
  require(readBootRec() == QByteArray("\x00\x00\x0c\xa0", 4),
          "Quadra PRAM Floppy boot bytes");
  QTemporaryDir tmp;
  QFile sparse(tmp.path() + "/sparse");
  require(sparse.open(QFile::WriteOnly) && Model::makeSparse(sparse) &&
              sparse.resize(32LL * 1073741824),
          "Sparse disk creation failed");
  sparse.close();
  require(QFileInfo(sparse).size() == 32LL * 1073741824,
          "Sparse disk logical size");
  // The same parser used for public images must reject traversal, duplicate
  // members and non-regular tar entries before they can touch another folder.
  auto member = [](QByteArray name, QByteArray data, char type = '0') {
    QByteArray h(512, 0);
    std::copy(name.begin(), name.end(), h.begin());
    auto octal = [](quint64 n, int width) {
      return QByteArray::number(n, 8).rightJustified(width - 1, '0') + '\0';
    };
    h.replace(100, 8, octal(0600, 8));
    h.replace(124, 12, octal(data.size(), 12));
    h[156] = type;
    h.replace(148, 8, QByteArray(8, ' '));
    quint64 sum = 0;
    for (auto c : h)
      sum += quint8(c);
    h.replace(148, 8, octal(sum, 7) + ' ');
    return h + data + QByteArray((512 - data.size() % 512) % 512, 0);
  };
  const QByteArray configBytes = "{}";
  QByteArray good = member("config.json", configBytes) +
                    member("disk.img", QByteArray(512, 0));
  int archives = 0;
  for (auto payload :
       QList<QByteArray>{good, member("../outside", QByteArray(512, 0)),
                         member("disk.img", QByteArray(512, 0), '2'),
                         member("disk.img", QByteArray(512, 0)) +
                             member("disk.img", QByteArray(512, 0))}) {
    auto archive = tmp.path() + QString("/archive%1.gz").arg(archives);
    auto bytes = payload + QByteArray(1024, 0);
    auto gz = gzopen(QFile::encodeName(archive).constData(), "wb");
    require(gz != nullptr, "Test gzip open");
    require(gzwrite(gz, bytes.constData(), bytes.size()) == bytes.size(),
            "Test gzip write");
    gzclose(gz);
    auto folder = tmp.path() + QString("/extract%1").arg(archives);
    QDir().mkdir(folder);
    std::atomic_bool cancelled = false;
    bool accepted = false;
    try {
      Library::extract(archive, folder,
                       {{"installedBytes", archives == 0 ? 514 : 1024},
                        {"diskCapacityBytes", 512}},
                       cancelled, [](QString, qint64, qint64) {});
      accepted = true;
    } catch (const std::exception &) {
    }
    require(accepted == (archives == 0),
            "Unsafe or incomplete archive handling");
    ++archives;
  }
  require(!QFileInfo::exists(tmp.path() + "/outside"),
          "Archive escaped destination");
  {
    QTcpServer server;
    require(server.listen(QHostAddress::LocalHost, 0),
            "RFB close regression listener");
    VNC client;
    QEventLoop loop;
    bool warned = false;
    QObject::connect(&server, &QTcpServer::newConnection, &loop, [&] {
      auto peer = server.nextPendingConnection();
      peer->disconnectFromHost();
      peer->deleteLater();
    });
    QObject::connect(&client, &VNC::problem, &loop, [&](QString) {
      warned = true;
      loop.quit();
    });
    client.open(server.serverPort(), "password");
    QTimer::singleShot(150, &loop, &QEventLoop::quit);
    loop.exec();
    require(!warned, "Normal RFB disconnect opened an error dialog");
  }
  // Render actual native button widgets; independent fixture comparison checks
  // that Qt compositing did not introduce platform styling or interpolation.
  for (int scale : {1, 2}) {
    PButton native("", nullptr);
    native.resize(84, 24);
    native.setFocusPolicy(Qt::NoFocus);
    QImage capture(84 * scale, 24 * scale, QImage::Format_ARGB32);
    capture.setDevicePixelRatio(scale);
    capture.fill(face);
    native.render(&capture);
    QImage expected(84, 24, QImage::Format_ARGB32);
    expected.fill(face);
    QPainter(&expected).drawImage(2, 2, QImage(fixtures + "/button-normal.png"));
    expected = expected.convertToFormat(QImage::Format_ARGB32)
                   .scaled(capture.size(), Qt::IgnoreAspectRatio,
                           Qt::FastTransformation);
    require(capture == expected,
            "Actual native button differs at scale " + QString::number(scale));
  }
  for (int scale : {1, 2}) {
    for (const auto &kind : QStringList{"push", "checkbox", "radio"}) {
      const bool checkbox = kind != "push";
      PButton native("", nullptr);
      native.toggleButton = checkbox;
      native.radio = kind == "radio";
      native.setCheckable(checkbox);
      native.resize(checkbox ? QSize(16, 16) : QSize(84, 24));
      native.show();
      native.activateWindow();
      native.setFocus();
      QApplication::processEvents();
      // X11 window-manager activation is asynchronous. Wait for actual focus
      // before comparing its artwork; offscreen platforms complete at once.
      QElapsedTimer focusDeadline;
      focusDeadline.start();
      while (!native.hasFocus() && focusDeadline.elapsed() < 1500) {
        QEventLoop focusWait;
        QTimer::singleShot(10, &focusWait, &QEventLoop::quit);
        focusWait.exec();
      }
      require(native.hasFocus(), "Keyboard focus did not reach the test button");
      QImage capture(native.size() * scale, QImage::Format_ARGB32);
      capture.setDevicePixelRatio(scale);
      capture.fill(face);
      native.render(&capture);
      QImage expected(fixtures + (kind == "radio" ? "/os9-radio-focused.png" :
                         checkbox ? "/os9-checkbox-focused.png" : "/os9-button-focused.png"));
      expected = expected.convertToFormat(QImage::Format_ARGB32).scaled(
          capture.size(), Qt::IgnoreAspectRatio, Qt::FastTransformation);
      require(capture == expected, "Focused native widget differs from the Mac");
    }
    Pixels panel(153, 123);
    menuFrame(panel);
    QPainter painter(&panel.image);
    QStyleOptionMenuItem item;
    item.rect = QRect(2, 20, 149, 16);
    item.state = QStyle::State_Enabled | QStyle::State_Selected;
    qApp->style()->drawControl(QStyle::CE_MenuItem, &item, &painter);
    item.rect = QRect(2, 52, 149, 6);
    item.menuItemType = QStyleOptionMenuItem::Separator;
    qApp->style()->drawControl(QStyle::CE_MenuItem, &item, &painter);
    painter.end();
    if (!output.isEmpty()) panel.image.save(output + "/qt-menu-panel.png");
    require(panel.image == QImage(fixtures + "/os9-menu-panel.png").convertToFormat(QImage::Format_ARGB32),
            "The Qt menu delegate changed the native selected row or separator");
  }
  {
    PEdit field(QString(100, 'W'));
    field.resize(100, field.height());
    field.selectAll();
    QImage capture(field.size(), QImage::Format_ARGB32);
    capture.fill(face);
    field.render(&capture);
    Pixels frame(field.width(), field.height());
    sunken(frame);
    for (int y = 0; y < field.height(); ++y)
      for (int x = 0; x < field.width(); ++x)
        if (x < 2 || x >= field.width() - 2 || y < 2 || y >= field.height() - 2)
          require(capture.pixel(x, y) == frame.image.pixel(x, y),
                  "A long field selection painted over its frame");
  }
  for (int scale : {1, 2}) {
    PWindow native("", {333, 169});
    native.show();
    QApplication::setActiveWindow(&native);
    QApplication::processEvents();
    QImage capture(333 * scale, 169 * scale, QImage::Format_ARGB32);
    capture.setDevicePixelRatio(scale);
    capture.fill(face);
    native.render(&capture);
    QImage expected(fixtures + "/os9-window-active.png");
    expected = expected.convertToFormat(QImage::Format_ARGB32)
                   .scaled(capture.size(), Qt::IgnoreAspectRatio,
                           Qt::FastTransformation);
    qint64 differences = 0;
    for (int y = 0; y < capture.height(); ++y)
      for (int x = 0; x < capture.width(); ++x) {
        if (y >= scale && y < 21 * scale && x >= scale && x < 331 * scale)
          continue;
        differences += capture.pixel(x, y) != expected.pixel(x, y);
      }
    require(
        differences == 0,
        QString(
            "Actual native window/content/grow frame differs at %1x: %2 pixels")
            .arg(scale)
            .arg(differences));
    native.setMinimumSize(320, 160);
    const auto minimum = native.minimumSize();
    native.shade();
    require(native.height() == 23, "Native window does not shade to 23 pixels");
    native.shade();
    require(native.minimumSize() == minimum,
            "WindowShade changed the resize minimum");
    native.close();
  }
  // A real style grip must use the Mac's measured pixels, including when Qt
  // creates one in a constrained scroll area. Fusion's diagonal strokes are
  // cosmetic device-pixel lines and break the UI's 2x pixel grid.
  for (int scale : {1, 2}) {
    QImage capture(17 * scale, 17 * scale, QImage::Format_ARGB32);
    capture.setDevicePixelRatio(scale);
    capture.fill(face);
    QPainter painter(&capture);
    QStyleOptionSizeGrip option;
    option.rect = QRect(0, 0, 17, 17);
    option.state = QStyle::State_Enabled | QStyle::State_Active;
    qApp->style()->drawControl(QStyle::CE_SizeGrip, &option, &painter);
    painter.end();
    QImage expected(fixtures + "/os9-window-active.png");
    expected = expected.convertToFormat(QImage::Format_ARGB32)
                   .copy(311, 147, 17, 17)
                   .scaled(capture.size(), Qt::IgnoreAspectRatio, Qt::FastTransformation);
    require(capture == expected, "Native size grip differs from the Mac pixels");
  }
  {
    auto oldHome = qgetenv("CLASSICMAC_TEST_HOME"),
         oldQualification = qgetenv("CLASSICMAC_QUALIFY");
    qputenv("CLASSICMAC_TEST_HOME", (tmp.path() + "/ui").toUtf8());
    qputenv("CLASSICMAC_QUALIFY", "1");
    {
      App native;
      native.machines.append({tmp.path(), Model::freshConfig("Pixel Mac")});
      native.render();
      native.show();
      QApplication::setActiveWindow(&native);
      QApplication::processEvents();
      QImage one(native.size(), QImage::Format_ARGB32),
          two(native.size() * 2, QImage::Format_ARGB32);
      two.setDevicePixelRatio(2);
      one.fill(face);
      two.fill(face);
      native.QWidget::render(&one);
      native.QWidget::render(&two);
      auto expected =
          one.scaled(two.size(), Qt::IgnoreAspectRatio, Qt::FastTransformation);
      if (!output.isEmpty()) {
        one.save(output + "/home-1x.png");
        two.save(output + "/home-2x.png");
      }
      require(two == expected,
              "Composed home is not an exact 2x pixel expansion");
      native.canClose = {};
      native.close();
    }
    if (oldHome.isNull())
      qunsetenv("CLASSICMAC_TEST_HOME");
    else
      qputenv("CLASSICMAC_TEST_HOME", oldHome);
    if (oldQualification.isNull())
      qunsetenv("CLASSICMAC_QUALIFY");
    else
      qputenv("CLASSICMAC_QUALIFY", oldQualification);
  }
  displayTests(fixtures, output);
  // Global DPI affects child backing-store clips, so render in real separate
  // QApplication instances as well as onto synthetic high-DPI paint devices.
  for (auto scale : {"1.25", "1.5", "1.75"}) {
    QProcess child;
    auto env = QProcessEnvironment::systemEnvironment();
    env.insert("QT_SCALE_FACTOR", scale);
    env.insert("QT_QPA_PLATFORM", "offscreen");
    child.setProcessEnvironment(env);
    const auto folder = output.isEmpty() ? QString() : output + "/dpi-" + scale;
    if (!folder.isEmpty()) QDir().mkpath(folder);
    child.start(QCoreApplication::applicationFilePath(), {"--display-test", fixtures, folder});
    require(child.waitForFinished(20000) && child.exitStatus() == QProcess::NormalExit && child.exitCode() == 0,
            "Fractional backing-store test failed at " + QString(scale) + ": " +
                QString::fromUtf8(child.readAllStandardError()));
  }

  // OS X NVRAM validation and RTC base tests.
  {
    auto buildBank = [](quint32 generation, qint32 xpramOffset = 0) {
      QByteArray bank(8192, '\0');
      auto writePart = [&](int off, quint8 sig, quint16 units, const char *name) {
        bank[off] = char(sig);
        bank[off + 2] = char((units >> 8) & 0xff);
        bank[off + 3] = char(units & 0xff);
        std::memcpy(bank.data() + off + 4, name, 12);
        quint32 sum = 0;
        for (int i = 0; i < 16; ++i)
          if (i != 1)
            sum += quint8(bank[off + i]);
        while (sum > 255)
          sum = (sum & 255U) + (sum >> 8);
        bank[off + 1] = char(sum & 0xff);
      };
      writePart(0, 0x5a, 2, "wwwwwwwwwwww");
      bank[20] = char((generation >> 24) & 0xff);
      bank[21] = char((generation >> 16) & 0xff);
      bank[22] = char((generation >> 8) & 0xff);
      bank[23] = char(generation & 0xff);
      writePart(32, 0x70, 193, "common\0\0\0\0\0\0");
      writePart(3120, 0xa0, 81, "APL,MacOS75\0");
      if (xpramOffset != 0) {
        const quint32 raw = quint32(xpramOffset) & 0x00ffffffu;
        const int xoff = 3120 + 16 + 0xec;
        bank[xoff + 1] = char((raw >> 16) & 0xff);
        bank[xoff + 2] = char((raw >> 8) & 0xff);
        bank[xoff + 3] = char(raw & 0xff);
      }
      writePart(4416, 0x7f, 236, "wwwwwwwwwwww");
      const uLong crc = adler32(
          1L, reinterpret_cast<const Bytef *>(bank.constData() + 20), 8192 - 20);
      bank[16] = char((crc >> 24) & 0xff);
      bank[17] = char((crc >> 16) & 0xff);
      bank[18] = char((crc >> 8) & 0xff);
      bank[19] = char(crc & 0xff);
      return bank;
    };
    const QByteArray validNvram = buildBank(1) + buildBank(2);
    QString nvramErr;
    require(Model::validateOsxNvram(validNvram, &nvramErr),
            "Valid OS X NVRAM rejected: " + nvramErr);
    nvramErr.clear();
    require(!Model::validateOsxNvram(QByteArray(8192, '\0'), &nvramErr) &&
                !nvramErr.isEmpty(),
            "Short OS X NVRAM accepted");
    nvramErr.clear();
    require(!Model::validateOsxNvram(QByteArray(16384, '\0'), &nvramErr) &&
                !nvramErr.isEmpty(),
            "Zeroed OS X NVRAM accepted");
    QByteArray corruptNvram = validNvram;
    corruptNvram[100] = char(corruptNvram[100] ^ 0x55);
    corruptNvram[8192 + 100] = char(corruptNvram[8192 + 100] ^ 0x55);
    nvramErr.clear();
    require(!Model::validateOsxNvram(corruptNvram, &nvramErr) &&
                !nvramErr.isEmpty(),
            "Corrupted OS X NVRAM accepted");

    const QString nvramPath = tmp.path() + "/test-nvram.img";
    QFile nvf(nvramPath);
    require(nvf.open(QFile::WriteOnly | QFile::Truncate) &&
                nvf.write(validNvram) == validNvram.size(),
            "Write test OS X NVRAM");
    nvf.close();
    require(Model::osxRtcBase(tmp.path() + "/missing-nvram.img", 1700000000LL) ==
                "base=utc",
            "Missing OS X NVRAM must fall back to base=utc");
    require(Model::osxRtcBase(nvramPath, 1700000000LL) == "base=utc",
            "Zero-offset OS X NVRAM must return base=utc");
    const QByteArray offsetNvram = buildBank(1, 0) + buildBank(3, -18000);
    const QString offsetPath = tmp.path() + "/offset-nvram.img";
    QFile ovf(offsetPath);
    require(ovf.open(QFile::WriteOnly | QFile::Truncate) &&
                ovf.write(offsetNvram) == offsetNvram.size(),
            "Write offset OS X NVRAM");
    ovf.close();
    const auto rtcA = Model::osxRtcBase(offsetPath, 1700000000LL);
    const auto rtcB = Model::osxRtcBase(offsetPath, 1700003600LL);
    require(rtcA != rtcB && rtcA.startsWith("base=") && rtcA != "base=utc",
            "osxRtcBase must advance with nowUtcSeconds");
  }

  // OS X UI state detection tests (menu open, selection move, confirmation dialog).
  {
    const int w = 1024, h = 768;
    auto makeRgb = [&](quint8 r, quint8 g, quint8 b) {
      QByteArray rgb(w * h * 3, '\0');
      for (int i = 0; i < w * h; ++i) {
        rgb[i * 3] = char(r);
        rgb[i * 3 + 1] = char(g);
        rgb[i * 3 + 2] = char(b);
      }
      return rgb;
    };
    auto paintRect = [&](QByteArray &rgb, int rx, int ry, int rw, int rh,
                         quint8 r, quint8 g, quint8 b) {
      for (int y = ry; y < ry + rh && y < h; ++y) {
        for (int x = rx; x < rx + rw && x < w; ++x) {
          const int off = (y * w + x) * 3;
          rgb[off] = char(r);
          rgb[off + 1] = char(g);
          rgb[off + 2] = char(b);
        }
      }
    };
    QByteArray base = makeRgb(58, 110, 165);
    paintRect(base, 0, 0, w, 22, 238, 238, 238);

    QByteArray menuOpen = base;
    paintRect(menuOpen, 6, 22, 210, 260, 248, 248, 248);
    for (int y = 32; y <= 240; y += 22)
      paintRect(menuOpen, 14, y, 140, 10, 30, 30, 30);
    require(!Engine::detectOsxMenuOpened(w, h, base, base),
            "Unchanged frame falsely triggered detectOsxMenuOpened");
    require(!Engine::detectOsxMenuOpened(w, h, {}, {}),
            "Empty frame falsely triggered detectOsxMenuOpened");
    require(Engine::detectOsxMenuOpened(w, h, base, menuOpen),
            "detectOsxMenuOpened failed to detect Apple menu dropdown");

    QByteArray menuSel1 = menuOpen;
    paintRect(menuSel1, 8, 180, 206, 20, 45, 115, 225);
    QByteArray menuSel2 = menuOpen;
    paintRect(menuSel2, 8, 202, 206, 20, 45, 115, 225);
    require(!Engine::detectOsxMenuSelectionMoved(w, h, menuSel1, menuSel1),
            "Unchanged menu selection falsely triggered");
    require(Engine::detectOsxMenuSelectionMoved(w, h, menuSel1, menuSel2),
            "detectOsxMenuSelectionMoved failed to detect selection change");

    QByteArray confirmDlg = base;
    paintRect(confirmDlg, 312, 240, 400, 180, 236, 236, 236);
    paintRect(confirmDlg, 340, 270, 280, 14, 20, 20, 20);
    paintRect(confirmDlg, 340, 296, 240, 12, 60, 60, 60);
    paintRect(confirmDlg, 580, 376, 104, 24, 50, 125, 230);
    require(!Engine::detectOsxConfirmationDialog(w, h, base, base),
            "Unchanged frame falsely triggered detectOsxConfirmationDialog");
    require(Engine::detectOsxConfirmationDialog(w, h, base, confirmDlg),
            "detectOsxConfirmationDialog failed to detect centered dialog");
  }

  // Native relative mouse scaling tests.
  {
    double rx = 0.0, ry = 0.0;
    int ox = 0, oy = 0;
    GuestView::scaleNativeRelative(rx, ry, 0.0, 0.0, 800, 1.0, false, ox, oy);
    require(ox == 0 && oy == 0, "Zero mouse delta must produce zero output");
    GuestView::scaleNativeRelative(rx, ry, 16.0, -12.0, 800, 1.0, false, ox, oy);
    require(ox > 0 && oy < 0, "Mouse delta sign must be preserved");
    rx = 0.0;
    ry = 0.0;
    int totalSmallX = 0;
    for (int i = 0; i < 16; ++i) {
      GuestView::scaleNativeRelative(rx, ry, 0.25, 0.0, 800, 1.0, false, ox, oy);
      totalSmallX += ox;
    }
    require(totalSmallX > 0, "Subpixel relative mouse deltas must accumulate");
    rx = 0.0;
    ry = 0.0;
    GuestView::scaleNativeRelative(rx, ry, 12.0, 12.0, 1600, 1.0, true, ox, oy);
    require(ox > 0 && oy > 0, "Legacy native mouse scaling must preserve movement");
  }

  // Transfer::Meter speed & ETA tests.
  {
    Transfer::Meter meter(10000, 0, 4.0);
    double speed = 0.0, eta = -1.0;
    meter.update(0, 0.0, speed, eta);
    meter.update(2000, 1.0, speed, eta);
    meter.update(4000, 2.0, speed, eta);
    require(speed > 1500.0 && speed < 2500.0,
            "Transfer::Meter speed estimate out of range");
    require(eta > 2.0 && eta < 4.5,
            "Transfer::Meter ETA estimate out of range");
    require(!Transfer::Meter::formatSpeed(speed).isEmpty(),
            "Transfer::Meter::formatSpeed returned empty");
    require(!Transfer::Meter::formatEta(eta).isEmpty(),
            "Transfer::Meter::formatEta returned empty");
    meter.reset(20000, 5000);
    meter.update(5000, 0.0, speed, eta);
    meter.update(10000, 1.0, speed, eta);
    require(speed > 0.0 && eta > 0.0, "Transfer::Meter reset failed");
  }

  // Checkpoints create/list/restore/remove & session state tests.
  {
    const QString cpFolder = tmp.path() + "/CheckpointTest.classic";
    QDir().mkpath(cpFolder);
    QJsonObject cpCfg = Model::freshConfig("Checkpoint Test", "powerMacG4");
    cpCfg["diskSizeGB"] = 1;
    cpCfg["ramMB"] = 512;
    Model::writeJSON(cpFolder + "/config.json", cpCfg);
    {
      QFile disk(cpFolder + "/disk.img");
      require(disk.open(QFile::WriteOnly | QFile::Truncate) &&
                  disk.write(QByteArray(65536, 'A')) == 65536,
              "Create test checkpoint disk");
    }
    const Model::Machine cpMachine = Model::readMachine(cpFolder);
    require(Checkpoints::list(cpFolder).isEmpty(),
            "Fresh machine must have no checkpoints");
    const auto cpEntry = Checkpoints::create(cpMachine, "Initial", "manual");
    require(!cpEntry.id.isEmpty() && cpEntry.label == "Initial" &&
                cpEntry.kind == "manual",
            "Checkpoint create metadata invalid");
    const auto cpListed = Checkpoints::list(cpFolder);
    require(cpListed.size() == 1 && cpListed[0].id == cpEntry.id,
            "Checkpoint list did not return created entry");
    {
      QFile disk(cpFolder + "/disk.img");
      require(disk.open(QFile::WriteOnly | QFile::Truncate) &&
                  disk.write(QByteArray(65536, 'B')) == 65536,
              "Mutate test checkpoint disk");
    }
    cpCfg["ramMB"] = 256;
    Model::writeJSON(cpFolder + "/config.json", Model::validate(cpCfg));
    Checkpoints::restore(cpMachine, cpEntry.id);
    {
      QFile disk(cpFolder + "/disk.img");
      require(disk.open(QFile::ReadOnly) && disk.read(4) == QByteArray("AAAA"),
              "Checkpoint restore did not restore disk contents");
    }
    require(Model::readMachine(cpFolder).config["ramMB"].toInt() == 256,
            "Checkpoint restore should preserve config.json");
    const auto afterRestore = Checkpoints::list(cpFolder);
    require(afterRestore.size() == 2,
            "Checkpoint restore should create a pre-restore backup");
    Checkpoints::markSessionState(cpFolder, false, "unclean");
    require(!Checkpoints::lastSessionClean(cpFolder),
            "markSessionState(false) failed");
    Checkpoints::markSessionState(cpFolder, true);
    require(Checkpoints::lastSessionClean(cpFolder),
            "markSessionState(true) failed");
    for (const auto &e : afterRestore)
      Checkpoints::remove(cpFolder, e.id);
    require(Checkpoints::list(cpFolder).isEmpty(),
            "Checkpoint remove did not delete checkpoints");
  }

  // Updater parsing & version comparison tests.
  {
    require(Updater::compareVersions("4.8.1", "4.8.2") < 0, "Version lt");
    require(Updater::compareVersions("4.8.9", "4.8.10") < 0, "Version numeric");
    require(Updater::compareVersions("v4.8.0", "4.8") == 0, "Version eq");
    require(Updater::compareVersions("4.9.0", "4.8.1") > 0, "Version gt");

    const QByteArray releaseJson = QJsonDocument(QJsonObject{
        {"tag_name", "v99.1.0"},
        {"body", "Bug fixes"},
        {"html_url", "https://github.com/amcchord/ClassicMac/releases/tag/v99.1.0"},
        {"draft", false},
        {"prerelease", false},
        {"assets", QJsonArray{
            QJsonObject{
                {"name", "ClassicMac-99.1.0-linux-x64.AppImage"},
                {"browser_download_url",
                 "https://github.com/amcchord/ClassicMac/releases/download/v99.1.0/ClassicMac-99.1.0-linux-x64.AppImage"}},
            QJsonObject{
                {"name", "ClassicMac-99.1.0-windows-x64.exe"},
                {"browser_download_url",
                 "https://objects.githubusercontent.com/amcchord/ClassicMac/ClassicMac-99.1.0-windows-x64.exe"}},
            QJsonObject{
                {"name", "SHA256SUMS"},
                {"browser_download_url",
                 "https://github.com/amcchord/ClassicMac/releases/download/v99.1.0/SHA256SUMS"}}}}})
                                       .toJson();
    const auto linuxRel =
        Updater::parseLatestRelease(releaseJson, "4.8.1", "linux");
    require(linuxRel.has_value() && linuxRel->version == "99.1.0" &&
                linuxRel->assetName == "ClassicMac-99.1.0-linux-x64.AppImage" &&
                !linuxRel->sumsUrl.isEmpty(),
            "Updater parseLatestRelease linux");
    const auto winRel =
        Updater::parseLatestRelease(releaseJson, "4.8.1", "windows");
    require(winRel.has_value() &&
                winRel->assetName == "ClassicMac-99.1.0-windows-x64.exe",
            "Updater parseLatestRelease windows");
    require(!Updater::parseLatestRelease(releaseJson, "99.1.0", "linux").has_value(),
            "Updater same version must return nullopt");

    const QByteArray preJson = QJsonDocument(QJsonObject{
        {"tag_name", "v99.2.0"},
        {"prerelease", true},
        {"assets", QJsonArray{}}})
                                   .toJson();
    require(!Updater::parseLatestRelease(preJson, "4.8.1", "linux").has_value(),
            "Updater prerelease must be ignored");

    const QByteArray evilJson = QJsonDocument(QJsonObject{
        {"tag_name", "v99.2.0"},
        {"assets", QJsonArray{
            QJsonObject{
                {"name", "ClassicMac-99.2.0-linux-x64.AppImage"},
                {"browser_download_url", "https://evil.example.com/bad.AppImage"}},
            QJsonObject{
                {"name", "SHA256SUMS"},
                {"browser_download_url", "https://github.com/amcchord/ClassicMac/SHA256SUMS"}}}}})
                                    .toJson();
    bool evilRejected = false;
    try {
      Updater::parseLatestRelease(evilJson, "4.8.1", "linux");
    } catch (const std::exception &) {
      evilRejected = true;
    }
    require(evilRejected, "Updater must reject disallowed asset hosts");

    const QString sums =
        "0123456789ABCDEF0123456789ABCDEF0123456789ABCDEF0123456789ABCDEF  ClassicMac-99.1.0-linux-x64.AppImage\n"
        "fedcba9876543210fedcba9876543210fedcba9876543210fedcba9876543210 *./ClassicMac-99.1.0-windows-x64.exe\n";
    require(Updater::parseSha256Sums(sums, "ClassicMac-99.1.0-linux-x64.AppImage") ==
                "0123456789abcdef0123456789abcdef0123456789abcdef0123456789abcdef",
            "Updater parseSha256Sums linux");
    require(Updater::parseSha256Sums(sums, "ClassicMac-99.1.0-windows-x64.exe") ==
                "fedcba9876543210fedcba9876543210fedcba9876543210fedcba9876543210",
            "Updater parseSha256Sums windows");
    require(Updater::parseSha256Sums(sums, "missing.exe").isEmpty(),
            "Updater parseSha256Sums missing");
  }

  // Library recommendedSettings tests (ATI Radeon + Tools CD per edition).
  {
    const auto oldTestHome = qgetenv("CLASSICMAC_TEST_HOME");
    qputenv("CLASSICMAC_TEST_HOME", (tmp.path() + "/lib-home").toUtf8());
    const auto restoreHome = qScopeGuard([&] {
      if (oldTestHome.isNull())
        qunsetenv("CLASSICMAC_TEST_HOME");
      else
        qputenv("CLASSICMAC_TEST_HOME", oldTestHome);
    });
    QJsonObject tplCfg = Model::freshConfig("Template", "powerMacG4");
    tplCfg.remove("toolsCDInserted");
    const QByteArray tplCfgBytes = QJsonDocument(tplCfg).toJson();
    const QByteArray tplArchivePayload =
        member("config.json", tplCfgBytes) +
        member("disk.img", QByteArray(512, 0)) + QByteArray(1024, 0);
    const QString tplGzPath = tmp.path() + "/lib-recommended.tar.gz";
    {
      auto gz = gzopen(QFile::encodeName(tplGzPath).constData(), "wb");
      require(gz != nullptr, "Library test gzip open");
      require(gzwrite(gz, tplArchivePayload.constData(),
                      tplArchivePayload.size()) == tplArchivePayload.size(),
              "Library test gzip write");
      gzclose(gz);
    }
    QFile gzFile(tplGzPath);
    require(gzFile.open(QFile::ReadOnly), "Read Library test archive");
    const QByteArray gzBytes = gzFile.readAll();
    const QString gzSha =
        QString::fromLatin1(QCryptographicHash::hash(gzBytes, QCryptographicHash::Sha256).toHex());
    auto makeEntry = [&](const QString &id) {
      return QJsonObject{
          {"id", id},
          {"name", id},
          {"summary", "Test summary"},
          {"osVersion", "9.2.2"},
          {"minimumAppVersion", "1.0.0"},
          {"machineFamily", "powerMacG4"},
          {"archiveURL", "https://mcchord.net/test.tar.gz"},
          {"archiveBytes", gzBytes.size()},
          {"installedBytes", tplCfgBytes.size() + 512},
          {"diskCapacityBytes", 512},
          {"sha256", gzSha}};
    };
    const QJsonObject atiEntry = makeEntry("os922-deluxe-v6");
    const QJsonObject full921Entry = makeEntry("os921-full-v5");
    QJsonObject stdEntry = makeEntry("mac-os-8.6-base-v1");
    stdEntry["recommendedSettings"] = QJsonObject{{"ram_mb", 384}, {"cpu", "g3"}};
    const QString cachePath = Library::cacheFile(atiEntry);
    require(!cachePath.isEmpty() &&
                QDir().mkpath(QFileInfo(cachePath).absolutePath()),
            "Create Library cache dir");
    {
      QFile cf(cachePath);
      require(cf.open(QFile::WriteOnly | QFile::Truncate) &&
                  cf.write(gzBytes) == gzBytes.size(),
              "Write Library cache file");
    }
    const QString full921CachePath = Library::cacheFile(full921Entry);
    require(QFile::copy(cachePath, full921CachePath) ||
                QFileInfo::exists(full921CachePath),
            "Copy cache for os921-full");
    const QString libDest = tmp.path() + "/lib-dest";
    std::atomic_bool libCancel = false;
    const Model::Machine atiMac = Library::install(
        atiEntry, libDest, "ATI Edition", libCancel, [](QString, qint64, qint64) {});
    require(atiMac.config["graphicsCard"].toString() == "atiRadeon" &&
                atiMac.config["toolsCDInserted"].toBool(),
            "Library ATI edition recommendedSettings");
    const Model::Machine full921Mac = Library::install(
        full921Entry, libDest, "OS 9.2.1 Full", libCancel, [](QString, qint64, qint64) {});
    require(full921Mac.config["graphicsCard"].toString() == "atiRadeon",
            "Library os921-full ATI default");
    const Model::Machine stdMac = Library::install(
        stdEntry, libDest, "Base Edition", libCancel, [](QString, qint64, qint64) {});
    require(stdMac.config["graphicsCard"].toString() == "standard" &&
                stdMac.config["ramMB"].toInt() == 384 &&
                !stdMac.config["useG4CPU"].toBool(),
            "Library base edition recommendedSettings");
  }

  // Virtual printing config validation, QEMU arguments, test page, PDF passthrough, and protocol server tests.
  {
    auto pCfg = Model::freshConfig("Print Test");
    require(!pCfg["printingEnabled"].toBool(),
            "printingEnabled must default to false");
    require(!pCfg.contains("printerName"),
            "printerName must be absent by default");

    auto coplandCfg = Model::freshConfig("Copland Print", "powerMac7500");
    coplandCfg["printingEnabled"] = true;
    coplandCfg["printerName"] = "HP_LaserJet";
    auto coplandValid = Model::validate(coplandCfg);
    require(!coplandValid["printingEnabled"].toBool() &&
                !coplandValid.contains("printerName"),
            "Copland must clear printingEnabled and printerName");

    auto roundTripCfg = pCfg;
    roundTripCfg["printerName"] = "  Office Printer  ";
    auto rtValid = Model::validate(roundTripCfg);
    require(rtValid["printerName"].toString() == "Office Printer",
            "printerName must trim surrounding whitespace");

    auto emptyPrinterCfg = pCfg;
    emptyPrinterCfg["printerName"] = "   ";
    require(!Model::validate(emptyPrinterCfg).contains("printerName"),
            "Empty trimmed printerName must be removed");

    auto badPrinterCfg = pCfg;
    badPrinterCfg["printerName"] = QString("Bad\nPrinter");
    bool badPrinterRejected = false;
    try {
      Model::validate(badPrinterCfg);
    } catch (const std::exception &) {
      badPrinterRejected = true;
    }
    require(badPrinterRejected, "Control characters in printerName must fail");

    const Model::Machine printOffMac{"/fixture/Print.classic", pCfg};
    const auto argsOff = Model::argumentsFor(printOffMac, 6000, true);
    const auto argsWithEmptyPorts =
        Model::qemuArguments(printOffMac, 6000, QStringLiteral("none"), {});
    require(argsOff == argsWithEmptyPorts,
            "Printing off must preserve exact QEMU arguments");

    auto pOnCfg = pCfg;
    pOnCfg["printingEnabled"] = true;
    pOnCfg["networking"] = false;
    const Model::Machine printOnMac{"/fixture/Print.classic", pOnCfg};
    const Model::PrintPorts ports{5150, 6310, 9101, "/tmp/cm-bridge-test"};
    const auto argsOn =
        Model::qemuArguments(printOnMac, 6000, QStringLiteral("none"), ports);
    const int nicIdx = argsOn.indexOf(QStringLiteral("-nic"));
    require(nicIdx >= 0 && nicIdx + 1 < argsOn.size(),
            "Missing -nic argument when printing is enabled");
    const QString nicVal = argsOn[nicIdx + 1];
    require(nicVal.contains("restrict=on") &&
                nicVal.contains("guestfwd=tcp:10.0.2.4:515-cmd:/tmp/cm-bridge-test --tcp-bridge 127.0.0.1 5150") &&
                nicVal.contains("guestfwd=tcp:10.0.2.4:631-cmd:/tmp/cm-bridge-test --tcp-bridge 127.0.0.1 6310") &&
                nicVal.contains("guestfwd=tcp:10.0.2.4:9100-cmd:/tmp/cm-bridge-test --tcp-bridge 127.0.0.1 9101"),
            "Printing on with networking off must emit restrict=on and 515/631/9100 guestfwd rules");
    require(argsOn.contains("cuda.idle-irq-ms=50"),
            "Standard VGA Power Mac must pass cuda.idle-irq-ms=50");

    if (Model::supportsHostSharing()) {
      auto atiPrintCfg = pOnCfg;
      atiPrintCfg["graphicsCard"] = "atiRadeon";
      atiPrintCfg["toolsCDInserted"] = true;
      atiPrintCfg["tabletInput"] = true;
      atiPrintCfg["sharedFolderPath"] = "/tmp/ShareTest";
      atiPrintCfg["sharedVolumeName"] = "ShareTest";
      const Model::Machine atiPrintMac{"/fixture/ATIPrint.classic", atiPrintCfg};
      const auto atiPrintArgs = Model::qemuArguments(atiPrintMac, 6000, QStringLiteral("none"), ports);
      require(atiPrintArgs.contains("virtio-tablet-pci,addr=0x13") &&
                  atiPrintArgs.contains("virtio-9p-pci,addr=0x14,fsdev=share0,mount_tag=ShareTest") &&
                  !atiPrintArgs.contains("cuda.idle-irq-ms=50"),
              "ATI with printing + sharing must isolate virtio PCI slots and omit cuda.idle-irq-ms");
    }

    const QByteArray testPs =
        Printer::generateTestPostScript("LaserWriter", "9.9.9");
    require(testPs.startsWith("%!PS-Adobe-3.0") &&
                testPs.contains("ClassicMac 9.9.9") &&
                testPs.contains("Target: LaserWriter") &&
                testPs.contains("showpage"),
            "generateTestPostScript output invalid");

    const QString pdfOutDir = tmp.path() + "/Printed PDFs";
    const QByteArray samplePdf("%PDF-1.4\n%sample\n%%EOF\n");
    QString pdfErr, savedPdfPath;
    require(Printer::submitJob(samplePdf, "Quarterly Report.ps", "pdf",
                               pdfOutDir, &pdfErr, &savedPdfPath),
            "PDF passthrough submitJob failed: " + pdfErr);
    require(!savedPdfPath.isEmpty() && QFileInfo(savedPdfPath).isFile() &&
                savedPdfPath.endsWith(" - Quarterly Report.pdf"),
            "PDF passthrough saved unexpected path: " + savedPdfPath);
    {
      QFile pf(savedPdfPath);
      require(pf.open(QFile::ReadOnly) && pf.readAll() == samplePdf,
              "PDF passthrough contents mismatch");
    }

    // Live loopback Printer::Server tests for LPD, IPP, and Raw 9100.
    Printer::Server server("pdf", pdfOutDir, true);
    QList<QPair<QString, QByteArray>> capturedJobs;
    server.testHandler = [&](const QByteArray &data, const QString &jobName,
                             QString *) {
      capturedJobs.append({jobName, data});
      return true;
    };
    require(server.start() && server.lpdPort() > 0 && server.ippPort() > 0 &&
                server.rawPort() > 0,
            "Printer::Server failed to start on ephemeral loopback ports");

    auto pumpUntil = [&](const std::function<bool()> &pred, int timeoutMs = 4000) {
      QElapsedTimer t;
      t.start();
      while (!pred() && t.elapsed() < timeoutMs) {
        QCoreApplication::processEvents(QEventLoop::AllEvents, 20);
      }
      return pred();
    };

    // 1. LPD with OS 9 NUL padding and duplicate subcommand header re-send.
    {
      capturedJobs.clear();
      QTcpSocket sock;
      sock.connectToHost(QHostAddress::LocalHost, server.lpdPort());
      require(sock.waitForConnected(2000), "LPD connect failed");
      auto readAck = [&](int count = 1) -> QByteArray {
        require(pumpUntil([&] { return sock.bytesAvailable() >= count; }),
                "LPD ACK timeout");
        return sock.read(count);
      };
      sock.write("\x02" "classicmac\n");
      sock.flush();
      require(readAck() == QByteArray("\x00", 1), "LPD queue ACK failed");

      const QByteArray ctrl = "Hhost\nPuser\nJMy LPD Doc\n";
      const QByteArray ctrlHdr =
          QByteArray("\x02") + QByteArray::number(ctrl.size()) + " cfA001host\n";
      // Leading NUL padding before control subcommand (OS 9 behavior)
      sock.write(QByteArray("\x00\x00", 2) + ctrlHdr);
      sock.flush();
      require(readAck() == QByteArray("\x00", 1), "LPD ctrl header ACK failed");
      sock.write(ctrl + QByteArray("\x00", 1));
      sock.flush();
      require(readAck() == QByteArray("\x00", 1), "LPD ctrl data ACK failed");

      const QByteArray psPayload("%!PS-Adobe-3.0\nshowpage\n");
      const QByteArray dataHdr =
          QByteArray("\x03") + QByteArray::number(psPayload.size()) + " dfA001host\n";
      sock.write(dataHdr);
      sock.flush();
      require(readAck() == QByteArray("\x00", 1), "LPD data header ACK 1 failed");
      // Re-send subcommand header (OS 9 Desktop PrintMonitor quirk)
      sock.write(dataHdr);
      sock.flush();
      require(readAck() == QByteArray("\x00", 1), "LPD duplicate header ACK failed");
      sock.write(psPayload + QByteArray("\x00", 1));
      sock.flush();
      require(readAck() == QByteArray("\x00", 1), "LPD data payload ACK failed");
      sock.disconnectFromHost();
      require(pumpUntil([&] { return !capturedJobs.isEmpty(); }),
              "LPD job was not dispatched");
      require(capturedJobs.size() == 1 &&
                  capturedJobs[0].first == "My LPD Doc" &&
                  capturedJobs[0].second == psPayload,
              "LPD captured job mismatch");
    }

    // 2. IPP: Get-Printer-Attributes, Create-Job + Send-Document, Print-Job.
    {
      auto makeIppAttr = [](quint8 tag, const QByteArray &name,
                            const QByteArray &val) {
        QByteArray b;
        b.append(char(tag));
        b.append(char((name.size() >> 8) & 0xff));
        b.append(char(name.size() & 0xff));
        b.append(name);
        b.append(char((val.size() >> 8) & 0xff));
        b.append(char(val.size() & 0xff));
        b.append(val);
        return b;
      };
      auto makeIppInt = [&](quint8 tag, const QByteArray &name, qint32 val) {
        QByteArray v(4, '\0');
        v[0] = char((val >> 24) & 0xff);
        v[1] = char((val >> 16) & 0xff);
        v[2] = char((val >> 8) & 0xff);
        v[3] = char(val & 0xff);
        return makeIppAttr(tag, name, v);
      };
      auto makeIppReq = [&](quint16 opId, quint32 reqId,
                            const QByteArray &attrs,
                            const QByteArray &payload = {}) {
        QByteArray b;
        b.append('\x01');
        b.append('\x01');
        b.append(char((opId >> 8) & 0xff));
        b.append(char(opId & 0xff));
        b.append(char((reqId >> 24) & 0xff));
        b.append(char((reqId >> 16) & 0xff));
        b.append(char((reqId >> 8) & 0xff));
        b.append(char(reqId & 0xff));
        b.append('\x01');
        b.append(makeIppAttr(0x47, "attributes-charset", "utf-8"));
        b.append(makeIppAttr(0x48, "attributes-natural-language", "en"));
        b.append(attrs);
        b.append('\x03');
        b.append(payload);
        return b;
      };
      auto sendIppHttp = [&](const QByteArray &ippBody,
                             bool expectContinue = false) -> QByteArray {
        QTcpSocket sock;
        sock.connectToHost(QHostAddress::LocalHost, server.ippPort());
        require(sock.waitForConnected(2000), "IPP connect failed");
        QByteArray hdr =
            QStringLiteral(
                "POST /printers/classicmac HTTP/1.1\r\n"
                "Host: 127.0.0.1\r\n"
                "Content-Type: application/ipp\r\n"
                "Content-Length: %1\r\n"
                "%2"
                "Connection: close\r\n\r\n")
                .arg(ippBody.size())
                .arg(expectContinue ? QStringLiteral("Expect: 100-continue\r\n")
                                    : QString())
                .toLatin1();
        sock.write(hdr);
        sock.flush();
        if (expectContinue) {
          require(pumpUntil([&] { return sock.bytesAvailable() > 0; }),
                  "IPP 100-continue timeout");
          const QByteArray cont = sock.readAll();
          require(cont.startsWith("HTTP/1.1 100 Continue\r\n\r\n"),
                  "Missing HTTP 100 Continue");
        }
        sock.write(ippBody);
        sock.flush();
        QByteArray resp;
        require(pumpUntil([&] {
          resp.append(sock.readAll());
          return sock.state() == QAbstractSocket::UnconnectedState &&
                 sock.bytesAvailable() == 0;
        }),
                "IPP response timeout");
        resp.append(sock.readAll());
        const int sep = resp.indexOf("\r\n\r\n");
        require(sep > 0 && resp.startsWith("HTTP/1.1 200 OK"),
                "IPP HTTP 200 header missing");
        return resp.mid(sep + 4);
      };

      const QByteArray gpaResp = sendIppHttp(makeIppReq(0x000B, 101, {}), true);
      require(gpaResp.size() >= 8 && quint8(gpaResp[2]) == 0 &&
                  quint8(gpaResp[3]) == 0 &&
                  gpaResp.contains("application/postscript"),
              "IPP Get-Printer-Attributes failed");

      const QByteArray createResp = sendIppHttp(
          makeIppReq(0x0005, 102, makeIppAttr(0x42, "job-name", "CreateSend")));
      require(createResp.size() >= 8 && quint8(createResp[2]) == 0 &&
                  quint8(createResp[3]) == 0 && createResp.contains("job-id"),
              "IPP Create-Job failed");

      capturedJobs.clear();
      const QByteArray ippPayload("%!PS-Adobe-3.0\n%ipp\n");
      const QByteArray sendResp = sendIppHttp(makeIppReq(
          0x0006, 103,
          makeIppInt(0x21, "job-id", 1) +
              makeIppAttr(0x22, "last-document", QByteArray("\x01", 1)),
          ippPayload));
      require(sendResp.size() >= 8 && quint8(sendResp[2]) == 0 &&
                  quint8(sendResp[3]) == 0 &&
                  capturedJobs.size() == 1 &&
                  capturedJobs[0].first == "CreateSend" &&
                  capturedJobs[0].second == ippPayload,
              "IPP Send-Document failed");

      capturedJobs.clear();
      const QByteArray printResp = sendIppHttp(makeIppReq(
          0x0002, 104, makeIppAttr(0x42, "job-name", "Direct PrintJob"),
          ippPayload));
      require(printResp.size() >= 8 && quint8(printResp[2]) == 0 &&
                  quint8(printResp[3]) == 0 &&
                  capturedJobs.size() == 1 &&
                  capturedJobs[0].first == "Direct PrintJob" &&
                  capturedJobs[0].second == ippPayload,
              "IPP Print-Job failed");
    }

    // 3. Raw 9100 (AppSocket / JetDirect) with %%Title: and EOT stripping.
    {
      capturedJobs.clear();
      QTcpSocket sock;
      sock.connectToHost(QHostAddress::LocalHost, server.rawPort());
      require(sock.waitForConnected(2000), "Raw 9100 connect failed");
      const QByteArray rawPs("%!PS-Adobe-3.0\n%%Title: (Raw Test Page)\nshowpage\n");
      sock.write(rawPs + QByteArray("\x04", 1));
      sock.flush();
      sock.disconnectFromHost();
      require(pumpUntil([&] { return !capturedJobs.isEmpty(); }),
              "Raw 9100 job was not dispatched");
      require(capturedJobs.size() == 1 &&
                  capturedJobs[0].first == "Raw Test Page" &&
                  capturedJobs[0].second == rawPs,
              "Raw 9100 captured job mismatch");
    }

    server.stop();
  }
  if (!output.isEmpty())
    Model::writeJSON(output + "/self-test.json",
                     QJsonObject{{"checks", count},
                                 {"nativeWidgetGroups", 7},
                                 {"nativeFontGlyphs", 672},
                                 {"dpiScales", QJsonArray{1, 1.25, 1.5, 1.75, 2, 2.5, 3}},
                                 {"displayProtocolAndInput", "passed"},
                                 {"pixels", reports},
                                 {"result", "passed"}});
  std::printf("Passed %d independent pixel captures, 95 glyph advances, native "
              "widget composites, configuration, sparse disk, paste, VNC "
              "authentication and Mac OS 8.6 profile checks.\n",
              count);
  return 0;
}
class Qualification : public QObject {
public:
  App *app;
  qint64 markedEnginePid = 0;
  QJsonArray steps, results;
  QString output;
  int index = 0;
  int announced = -1;
  QElapsedTimer elapsed;
  QElapsedTimer interactionClock;
  QTimer timer;
  bool runningStep = false;
  QWidget *scope(const QJsonObject &step) const {
    if (step["window"].toString() == "home") return app;
    if (step["window"].toString() == "guest" && app->currentEngine())
      return app->guest(app->currentEngine());
    auto w = QApplication::activeModalWidget();
    return w ? w : QApplication::activeWindow();
  }
  QWidget *namedWidget(const QJsonObject &step) const {
    const auto name = step["object"].toString();
    require(!name.isEmpty(), "A qualification widget requires an object name");
    QList<QWidget *> matches;
    auto root = scope(step);
    for (auto w : QApplication::allWidgets())
      if (w->objectName() == name && w->isVisible() &&
          (!root || w == root || w->window() == root))
        matches << w;
    require(matches.size() == 1,
            QString("Expected one visible widget '%1', found %2")
                .arg(name).arg(matches.size()));
    return matches.first();
  }
  QString path(QString value) const {
    value.replace("${documents}", Model::documents());
    value.replace("${profile}", Model::dataDirectory());
    value.replace("${output}", output);
    return value;
  }
  Qualification(App *a, const QJsonObject &recipe)
      : QObject(a), app(a), steps(recipe["steps"].toArray()),
        output(recipe["output"].toString()) {
    require(!output.isEmpty() && steps.size() <= 200,
            "Invalid qualification recipe");
    QDir().mkpath(output);
    timer.setInterval(100);
    connect(&timer, &QTimer::timeout, this, [&] { tick(); });
    elapsed.start();
    timer.start();
  }
  void finish(bool passed, QString error = {}) {
    timer.stop();
    if (!passed) {
      auto w = QApplication::activeModalWidget();
      if (!w)
        w = QApplication::activeWindow();
      if (w)
        w->grab().save(output + "/failure.png");
    }
    Model::writeJSON(output + "/result.json",
                     QJsonObject{{"passed", passed},
                                 {"error", error},
                                 {"steps", results},
                                 {"state", app->evidence()}});
    for (auto e : app->engines.values())
      if (e->running)
        e->forceOff();
    QTimer::singleShot(0, qApp, [passed] { qApp->exit(passed ? 0 : 1); });
  }
  void tick() {
    if (runningStep)
      return;
    if (index >= steps.size()) {
      finish(true);
      return;
    }
    auto step = steps[index].toObject();
    auto action = step["action"].toString();
    if (announced != index) {
      announced = index;
      qInfo("QA step %d: %s", index, action.toUtf8().constData());
    }
    qint64 wait = step.value("timeout").toInt(60000);
    if (elapsed.elapsed() > wait) {
      finish(false, "Timed out at " + QString::number(index) + ": " + action);
      return;
    }
    bool done = false;
    runningStep = true;
    try {
      if (action == "wait")
        done = elapsed.elapsed() >= step["milliseconds"].toInt();
      else if (action == "waitFile")
        done = QFileInfo(path(step["path"].toString())).isFile();
      else if (action == "fixtureCatalog") {
        require(!qEnvironmentVariableIsEmpty("CLASSICMAC_QUALIFY") &&
                    !qEnvironmentVariableIsEmpty("CLASSICMAC_TEST_HOME") &&
                    !qEnvironmentVariableIsEmpty("CLASSICMAC_TEST_DOCUMENTS"),
                "Catalog fixtures require an isolated qualification session");
        auto catalog = QJsonObject{{"schemaVersion", 1},
                                  {"machines", step["entries"].toArray()}};
        app->catalogRows = Library::parse(QJsonDocument(catalog).toJson());
        done = true;
      } else if (action == "fixtureDownload") {
        require(!qEnvironmentVariableIsEmpty("CLASSICMAC_QUALIFY") &&
                    !qEnvironmentVariableIsEmpty("CLASSICMAC_TEST_HOME") &&
                    !qEnvironmentVariableIsEmpty("CLASSICMAC_TEST_DOCUMENTS"),
                "Download fixtures require an isolated qualification session");
        int found = -1;
        for (int i = 0; i < app->catalogRows.size(); ++i)
          if (app->catalogRows[i].toObject()["id"] == step["id"]) found = i;
        require(found >= 0, "The cached-download fixture is not in the catalog");
        auto entry = app->catalogRows[found].toObject();
        require(entry.value("machineFamily").toString("powerMacG4") == "powerMacG4",
                "The tiny download fixture must use a Power Mac");
        const auto config = QJsonDocument(Model::freshConfig(entry["name"].toString())).toJson();
        auto member = [](const QByteArray &name, const QByteArray &data) {
          QByteArray header(512, 0);
          std::copy(name.begin(), name.end(), header.begin());
          auto octal = [](quint64 n, int width) {
            return QByteArray::number(n, 8).rightJustified(width - 1, '0') + '\0';
          };
          header.replace(100, 8, octal(0600, 8));
          header.replace(124, 12, octal(data.size(), 12));
          header[156] = '0';
          header.replace(148, 8, QByteArray(8, ' '));
          quint64 sum = 0;
          for (auto byte : header) sum += quint8(byte);
          header.replace(148, 8, octal(sum, 7) + ' ');
          return header + data + QByteArray((512 - data.size() % 512) % 512, 0);
        };
        const auto tar = member("config.json", config) + member("disk.img", QByteArray(512, 0)) +
                         QByteArray(1024, 0);
        QTemporaryDir temporary;
        require(temporary.isValid(), "Cannot prepare the tiny download fixture");
        const auto archivePath = temporary.path() + "/fixture.gz";
        auto gzip = gzopen(QFile::encodeName(archivePath).constData(), "wb");
        require(gzip, "Cannot open the tiny fixture archive");
        const auto written = gzwrite(gzip, tar.constData(), tar.size());
        const auto closed = gzclose(gzip);
        require(written == tar.size() && closed == Z_OK, "Cannot write fixture archive");
        QFile archive(archivePath);
        require(archive.open(QFile::ReadOnly), "Cannot read fixture archive");
        auto bytes = archive.readAll();
        const auto sha = QCryptographicHash::hash(bytes, QCryptographicHash::Sha256).toHex();
        entry["sha256"] = QString::fromLatin1(sha);
        entry["archiveBytes"] = bytes.size();
        entry["installedBytes"] = config.size() + 512;
        entry["diskCapacityBytes"] = 512;
        app->catalogRows[found] = entry;
        if (step["corrupt"].toBool()) bytes[bytes.size() / 2] = char(bytes[bytes.size() / 2] ^ 1);
        const auto cachePath = Library::cacheFile(entry);
        require(!cachePath.isEmpty() && QDir().mkpath(QFileInfo(cachePath).absolutePath()),
                "Cannot create fixture cache");
        QFile cache(cachePath);
        require(cache.open(QFile::WriteOnly) && cache.write(bytes) == bytes.size(),
                "Cannot populate fixture cache");
        done = true;
      } else if (action == "fixtureEngine") {
        require(!qEnvironmentVariableIsEmpty("CLASSICMAC_QUALIFY") &&
                    !qEnvironmentVariableIsEmpty("CLASSICMAC_TEST_HOME") &&
                    !qEnvironmentVariableIsEmpty("CLASSICMAC_TEST_DOCUMENTS"),
                "Engine fixtures require an isolated qualification session");
        auto m = app->current();
        require(m && !app->currentEngine(), "Select a stopped fixture Mac first");
        auto e = new Engine(*m, app);
        const auto id = m->id();
        app->engines[id] = e;
        connect(e, &Engine::changed, app, &App::render);
        connect(e, &Engine::exited, app, [a = app, id, e](bool, QString) {
          a->engines.remove(id);
          if (auto w = a->guests.take(id)) {
            w->hide();
            w->deleteLater();
          }
          e->deleteLater();
          a->render();
        });
        e->process.setProgram(QCoreApplication::applicationFilePath());
        e->process.setArguments({"--qualification-engine"});
        e->process.start();
        require(e->process.waitForStarted(2000),
                "The unresponsive engine fixture could not start");
        e->running = true;
        e->paused = step["paused"].toBool();
        if (step["guest"].toBool()) {
          Pixels frame(640, 480, 0xffcccccc);
          text(frame, "Qualification fixture: unresponsive engine", 50, 230);
          e->vnc.frame = frame.image;
          app->guest(e)->show();
        }
        app->render();
        done = true;
      } else if (action == "assertState") {
        if (step.contains("machines"))
          require(app->machines.size() == step["machines"].toInt(),
                  "Unexpected number of machines");
        if (step.contains("running"))
          require(app->engines.size() == step["running"].toInt(),
                  "Unexpected number of running engines");
        if (step.contains("selected"))
          require(app->current() && app->current()->name() == step["selected"].toString(),
                  "The expected Mac is not selected");
        if (step.contains("paused"))
          require(app->currentEngine() &&
                      app->currentEngine()->paused == step["paused"].toBool(),
                  "Unexpected pause state");
        done = true;
      } else if (action == "assertPath") {
        const auto location = path(step["path"].toString());
        require(QFileInfo::exists(location) == step.value("exists").toBool(true),
                "Unexpected path existence: " + location);
        if (step.contains("bytes"))
          require(QFileInfo(location).size() == step["bytes"].toDouble(),
                  "Fixture file size changed: " + location);
        done = true;
      } else if (action == "assertUI") {
        auto w = namedWidget(step);
        if (step.contains("readOnly")) {
          auto field = qobject_cast<QLineEdit *>(w);
          require(field && field->isReadOnly() == step["readOnly"].toBool(),
                  "Unexpected editable state: " + w->objectName());
        }
        if (step.contains("enabled"))
          require(w->isEnabled() == step["enabled"].toBool(),
                  "Unexpected enabled state: " + w->objectName());
        if (step.contains("text") || step.contains("contains")) {
          QString text = w->accessibleName();
          if (auto b = qobject_cast<QAbstractButton *>(w)) text = b->text();
          if (auto l = qobject_cast<PLabel *>(w)) text = l->value;
          if (auto e = qobject_cast<QLineEdit *>(w)) text = e->text();
          if (step.contains("text"))
            require(text == step["text"].toString(),
                    "Unexpected widget text: " + w->objectName() + " = " + text);
          if (step.contains("contains"))
            require(text.contains(step["contains"].toString()),
                    "Missing widget text: " + w->objectName() + " = " + text);
        }
        if (step["withinWindow"].toBool()) {
          auto root = w->window();
          require(root->rect().contains(QRect(w->mapTo(root, QPoint()), w->size())),
                  "A visible action is clipped: " + w->objectName());
        }
        done = true;
      } else if (action == "assertFooter") {
        auto root = scope(step);
        require(root, "No window with footer actions");
        for (const auto &text : step["buttons"].toArray()) {
          QList<QAbstractButton *> matches;
          for (auto b : root->findChildren<QAbstractButton *>())
            if (b->window() == root && b->isVisible() && b->text() == text.toString())
              matches << b;
          require(matches.size() == 1, "Expected one footer button: " + text.toString());
          auto b = matches.first();
          require(root->rect().contains(QRect(b->mapTo(root, QPoint()), b->size())),
                  "A footer action is outside the window: " + b->text());
          for (auto parent = b->parentWidget(); parent && parent != root;
               parent = parent->parentWidget())
            require(!qobject_cast<QAbstractScrollArea *>(parent),
                    "A footer action scrolls out of reach: " + b->text());
        }
        done = true;
      } else if (action == "assertSafeDefault") {
        auto dialog = QApplication::activeModalWidget();
        require(dialog, "No destructive confirmation to inspect");
        int defaults = 0;
        for (auto b : dialog->findChildren<PButton *>())
          if (b->window() == dialog && b->isVisible() && b->defaultButton) {
            ++defaults;
            require(b->text() == "Cancel", "A destructive action owns the default ring");
            require(b->hasFocus(), "The safe Cancel action does not have keyboard focus");
          }
        require(defaults == 1, "A confirmation requires exactly one safe default");
        done = true;
      } else if (action == "assertList" || action == "listSelect") {
        auto list = qobject_cast<QListWidget *>(namedWidget(step));
        require(list, "The qualification target is not a list");
        QList<QListWidgetItem *> visible;
        for (int i = 0; i < list->count(); ++i)
          if (!list->item(i)->isHidden()) visible << list->item(i);
        if (step.contains("count"))
          require(visible.size() == step["count"].toInt(),
                  QString("Expected %1 list entries, found %2")
                      .arg(step["count"].toInt()).arg(visible.size()));
        if (action == "listSelect") {
          QList<QListWidgetItem *> matches;
          for (auto item : visible)
            if ((!step.contains("text") || item->text() == step["text"].toString()) &&
                (!step.contains("id") || item->data(Qt::UserRole).toJsonObject()["id"] == step["id"]))
              matches << item;
          require(matches.size() == 1, "The requested list entry is not unique");
          list->setCurrentItem(matches.first());
          list->scrollToItem(matches.first());
        }
        if (step.contains("selectedId"))
          require(list->currentItem() &&
                      list->currentItem()->data(Qt::UserRole).toJsonObject()["id"] == step["selectedId"],
                  "Unexpected selected Library edition");
        done = true;
      } else if (action == "markTime") {
        interactionClock.start();
        done = true;
      } else if (action == "elapsedUnder") {
        require(interactionClock.isValid() &&
                    interactionClock.elapsed() < step["milliseconds"].toInt(),
                QString("The interaction blocked for %1 ms").arg(interactionClock.elapsed()));
        done = true;
      } else if (action == "closeWindow") {
        auto w = scope(step);
        require(w, "No qualification window to close");
        QTimer::singleShot(0, w, [w] { w->close(); });
        done = true;
      } else if (action == "scrollTop") {
        auto w = scope(step);
        require(w, "No qualification window to scroll");
        for (auto area : w->findChildren<QScrollArea *>())
          area->verticalScrollBar()->setValue(0);
        done = true;
      } else if (action == "open") {
        app->addMachine(path(step["folder"].toString()));
        done = true;
      } else if (action == "resize") {
        PWindow *target = app;
        if (step["window"].toString() == "modal")
          target = qobject_cast<PWindow *>(QApplication::activeModalWidget());
        if (step["window"].toString() == "guest") {
          require(app->currentEngine(), "No running guest to resize");
          target = app->guest(app->currentEngine());
        }
        require(target, "No qualification window to resize");
        target->resize(step["width"].toInt(), step["height"].toInt());
        QApplication::processEvents();
        for (auto b : target->body->findChildren<PButton *>()) {
          if (b->isVisible()) {
            if (target == app) {
              for (auto area : app->body->findChildren<QScrollArea *>())
                area->ensureWidgetVisible(b);
            } else {
              bool scrollable = false;
              for (auto parent = b->parentWidget(); parent && parent != target;
                   parent = parent->parentWidget())
                scrollable |= qobject_cast<QAbstractScrollArea *>(parent) != nullptr;
              if (!scrollable)
                require(target->rect().contains(QRect(b->mapTo(target, QPoint()), b->size())),
                        "A window action is clipped: " + b->text());
            }
          }
        }
        done = true;
      } else if (action == "fitScreen") {
        auto w = step["window"].toString() == "guest" && app->currentEngine() ?
                   app->guest(app->currentEngine()) : app;
        if (step["window"].toString() == "modal")
          w = qobject_cast<PWindow *>(QApplication::activeModalWidget());
        require(w, "No window to fit");
        w->fitToScreen();
        QApplication::processEvents();
        require(w->screen()->availableGeometry().contains(w->frameGeometry()),
                "A window extends outside the Windows work area");
        for (auto b : w->body->findChildren<PButton *>())
          if (b->isVisible() && b->parentWidget() == w->body)
            require(w->rect().contains(QRect(b->mapTo(w, QPoint()), b->size())),
                    "A dialog/guest footer action is clipped: " + b->text());
        done = true;
      } else if (action == "maximize" || action == "restore") {
        auto w = step["window"].toString() == "guest" && app->currentEngine() ?
                   app->guest(app->currentEngine()) : app;
        if (action == "maximize") w->showMaximized();
        else w->showNormal();
        done = true;
      } else if (action == "assertDpi") {
        const auto scale = step["scale"].toDouble();
        const auto pixelScale = step["pixelScale"].toDouble(scale);
        require(qAbs(app->devicePixelRatioF() - pixelScale) < 0.02,
                "The UI did not use the requested whole-pixel zoom");
#ifdef Q_OS_WIN
        auto state = app->evidence();
        require(state["perMonitorV2"].toBool(), "The Windows window is not Per-Monitor V2 aware");
        if (step["native"].toBool())
          require(qAbs(state["windowsDpi"].toInt() / 96.0 - scale) < 0.02,
                  "The Windows OS DPI does not match this test");
#endif
        done = true;
      } else if (action == "guestSize") {
        if (auto e = app->currentEngine(); e && e->vnc.ready) {
          auto view = app->guest(e)->findChild<GuestView *>();
          const auto expected = step["automatic"].toBool() ? view->desiredSize() :
                                 QSize(step["width"].toInt(), step["height"].toInt());
          done = e->vnc.frame.size() == expected;
          if (done && step["sharp"].toBool()) {
            const auto r = view->screenRect();
            auto zoom = r.width() * view->devicePixelRatioF() / expected.width();
            require(qAbs(zoom - qRound(zoom)) < 0.001 && zoom >= 1,
                    "Guest pixels do not map to integral physical pixels");
          }
        }
      } else if (action == "guestEscape") {
        if (auto e = app->currentEngine()) {
          auto w = app->guest(e);
          auto view = w->findChild<GuestView *>();
          if (w->isFullScreen()) {
            QKeyEvent event(QEvent::KeyPress, Qt::Key_F,
                            Qt::ControlModifier | Qt::AltModifier);
            QApplication::sendEvent(view, &event);
          } else {
            QKeyEvent event(QEvent::KeyPress, Qt::Key_Escape, Qt::NoModifier);
            QApplication::sendEvent(view, &event);
          }
          done = true;
        }
      } else if (action == "setup") {
        QTimer::singleShot(0, app, [a = app] { a->showSetup(); });
        done = true;
      } else if (action == "create") {
        auto m =
            Model::createMachine(step["name"].toString(),
                                 step.value("family").toString("powerMacG4"),
                                 step.value("size").toInt(2),
                                 Model::documents(), step["cd"].toString());
        app->addMachine(m.folder);
        done = true;
      } else if (action == "select") {
        for (int i = 0; i < app->machines.size(); ++i)
          if (app->machines[i].name() == step["name"].toString()) {
            app->selected = i;
            app->render();
            done = true;
            break;
          }
      } else if (action == "download") {
        auto id = step["id"].toString();
        auto rows = Library::load();
        QJsonObject found;
        for (auto row : rows)
          if (row.toObject()["id"].toString() == id)
            found = row.toObject();
        require(!found.isEmpty(), "Catalog entry missing: " + id);
        std::atomic_bool cancel = false;
        auto m =
            Library::install(found, Model::documents(), step["name"].toString(),
                             cancel, [](QString, qint64, qint64) {});
        app->addMachine(m.folder);
        done = true;
      } else if (action == "start") {
        app->startSelected();
        done = true;
      } else if (action == "nativeKey") {
        if (auto e = app->currentEngine(); e && e->vnc.ready) {
          auto w = app->guest(e);
          auto view = w->findChild<GuestView *>();
          require(view, "Native guest input view missing");
          QString chord = step["key"].toString();
          bool meta = chord.startsWith("meta-");
          QString name = meta ? chord.mid(5) : chord;
          require(name.size() == 1,
                  "Qualification key must be a single character");
          if (meta) {
            QKeyEvent event(QEvent::KeyPress, Qt::Key_Meta, Qt::MetaModifier);
            QApplication::sendEvent(view, &event);
          }
          int key = Qt::Key_A + name[0].toUpper().unicode() - 'A';
          QKeyEvent down(QEvent::KeyPress, key,
                         meta ? Qt::MetaModifier : Qt::NoModifier, name);
          QApplication::sendEvent(view, &down);
          QKeyEvent up(QEvent::KeyRelease, key,
                       meta ? Qt::MetaModifier : Qt::NoModifier, name);
          QApplication::sendEvent(view, &up);
          if (meta) {
            QKeyEvent event(QEvent::KeyRelease, Qt::Key_Meta, Qt::NoModifier);
            QApplication::sendEvent(view, &event);
          }
          done = true;
        }
      } else if (action == "nativePointer") {
        if (auto e = app->currentEngine(); e && e->vnc.ready) {
          auto view = app->guest(e)->findChild<GuestView *>();
          require(view, "Native guest pointer view missing");
          const auto image = view->screenRect();
          const QPointF p(image.x() + (step["x"].toInt() + 0.5) * image.width() /
                                          e->vnc.frame.width(),
                          image.y() + (step["y"].toInt() + 0.5) * image.height() /
                                          e->vnc.frame.height());
          require(QRectF(view->displayViewport()).contains(p),
                  "The requested guest input point is outside the visible screen");
          QMouseEvent move(QEvent::MouseMove, p, view->mapToGlobal(p.toPoint()),
                           Qt::NoButton, Qt::NoButton, Qt::NoModifier);
          QApplication::sendEvent(view, &move);
          QMouseEvent press(QEvent::MouseButtonPress, p,
                            view->mapToGlobal(p.toPoint()), Qt::LeftButton,
                            Qt::LeftButton, Qt::NoModifier);
          QApplication::sendEvent(view, &press);
          QMouseEvent release(QEvent::MouseButtonRelease, p,
                              view->mapToGlobal(p.toPoint()), Qt::LeftButton,
                              Qt::NoButton, Qt::NoModifier);
          QApplication::sendEvent(view, &release);
          done = true;
        }
      } else if (action == "guestReady") {
        auto e = app->currentEngine();
        done = e && e->vnc.ready && !e->vnc.frame.isNull();
      } else if (action == "markEngine") {
        auto e = app->currentEngine();
        require(e && e->processId() > 0, "No engine process to mark");
        markedEnginePid = e->processId();
        done = true;
      } else if (action == "engineReplaced") {
        auto e = app->currentEngine();
        done = e && e->processId() > 0 &&
            e->processId() != markedEnginePid && e->vnc.ready;
      } else if (action == "window") {
        for (auto w : QApplication::topLevelWidgets())
          if (w->isVisible() && w->windowTitle() == step["title"].toString())
            done = true;
      } else if (action == "popup") {
        auto w = QApplication::activeModalWidget();
        if (!w) w = QApplication::activeWindow();
        if (w) {
          auto combos = w->findChildren<PCombo *>();
          combos.erase(std::remove_if(combos.begin(), combos.end(),
              [](PCombo *combo) { return !combo->isVisible(); }), combos.end());
          const int i = step.value("index").toInt();
          if (i >= 0 && i < combos.size()) {
            combos[i]->showPopup();
            done = true;
          }
        }
      } else if (action == "uiKey") {
        auto w = QApplication::activePopupWidget();
        if (!w) w = QApplication::focusWidget();
        if (!w) w = QApplication::activeModalWidget();
        if (w) {
          auto sequence = QKeySequence::fromString(step["key"].toString());
          require(sequence.count() == 1, "Qualification UI key must be one chord");
          auto chord = sequence[0];
          QKeyEvent down(QEvent::KeyPress, chord.key(), chord.keyboardModifiers());
          QKeyEvent up(QEvent::KeyRelease, chord.key(), chord.keyboardModifiers());
          QApplication::sendEvent(w, &down);
          QApplication::sendEvent(w, &up);
          done = true;
        }
      } else if (action == "capture") {
        auto w = step["window"].toString() == "home"
                     ? static_cast<QWidget *>(app)
                     : QApplication::activePopupWidget() ? QApplication::activePopupWidget()
                     : (QApplication::activeModalWidget()
                            ? QApplication::activeModalWidget()
                            : QApplication::activeWindow());
        if (!w)
          w = app;
        if (step["window"].toString() == "guest" && app->currentEngine())
          w = app->guest(app->currentEngine());
        if (!qEnvironmentVariableIsEmpty("CLASSICMAC_QUALIFY") && w->screen()) {
          // Keep the real Windows pointer and its accessibility halo outside
          // the captured app while comparing widget and desktop pixels. The
          // step remains guarded by runningStep during this short event loop.
          QPointer<QWidget> target(w);
          QCursor::setPos(w->screen()->availableGeometry().bottomRight());
          QEventLoop settle;
          QTimer::singleShot(150, &settle, &QEventLoop::quit);
          settle.exec();
          require(target && target->isVisible(),
                  "The qualification window closed before its capture");
        }
        w->grab().save(output + "/" + step["name"].toString() + ".png");
        if (step["nativeScreen"].toBool() && w->screen()) {
          const auto origin = w->mapToGlobal(QPoint());
          w->screen()->grabWindow(0, origin.x(), origin.y(), w->width(), w->height())
              .save(output + "/" + step["name"].toString() + "-desktop.png");
        }
        auto state = app->evidence();
        state["captureKind"] = w == app ? "home" : qobject_cast<PDialog *>(w) ? "dialog" :
            qobject_cast<QMenu *>(w) ? "menu" : "guest";
        state["captureWidth"] = w->width();
        state["captureHeight"] = w->height();
        state["captureTitle"] = w->windowTitle();
        state["captureFullScreen"] = w->isFullScreen();
        QJsonArray controls;
        for (auto control : w->findChildren<QWidget *>()) {
          if (!control->isVisible() || control->window() != w) continue;
          const auto position = control->mapTo(w, QPoint());
          QJsonObject row{{"class", control->metaObject()->className()},
                          {"object", control->objectName()},
                          {"accessibleName", control->accessibleName()},
                          {"enabled", control->isEnabled()},
                          {"x", position.x()}, {"y", position.y()},
                          {"width", control->width()}, {"height", control->height()}};
          if (auto b = qobject_cast<QAbstractButton *>(control)) row["text"] = b->text();
          if (auto l = qobject_cast<PLabel *>(control)) row["text"] = l->value;
          if (auto e = qobject_cast<QLineEdit *>(control)) row["text"] = e->text();
          if (auto c = qobject_cast<QComboBox *>(control)) row["text"] = c->currentText();
          if (auto l = qobject_cast<QListWidget *>(control)) {
            QJsonArray items;
            for (int i = 0; i < l->count(); ++i)
              if (!l->item(i)->isHidden())
                items.append(QJsonObject{{"text", l->item(i)->text()},
                                         {"selected", l->item(i)->isSelected()},
                                         {"data", l->item(i)->data(Qt::UserRole).toJsonObject()}});
            row["items"] = items;
          }
          controls.append(row);
        }
        state["controls"] = controls;
        Model::writeJSON(output + "/" + step["name"].toString() + ".json", state);
        if (auto e = app->currentEngine(); e && !e->vnc.frame.isNull())
          e->vnc.frame.save(output + "/" + step["name"].toString() +
                            "-guest.png");
        done = true;
      } else if (action == "shade") {
        auto w = step["window"].toString() == "guest" && app->currentEngine() ?
                   app->guest(app->currentEngine()) : app;
        w->shade();
        require(w->height() == (w->shaded ? 23 : w->expanded.height()),
                "WindowShade height");
        done = true;
      } else if (action == "pause" || action == "resume" ||
                 action == "shutdown" || action == "restart" ||
                 action == "key") {
        if (auto e = app->currentEngine()) {
          if (action == "pause") {
            e->pause();
            require(e->paused, "Pause state");
          }
          if (action == "resume") {
            e->resume();
            require(!e->paused, "Resume state");
          }
          if (action == "shutdown")
            e->shutdown();
          if (action == "restart")
            e->shutdown(true);
          if (action == "key") {
            auto key = step["key"].toString();
            require(
                QRegularExpression("^[a-z0-9_-]{1,80}$").match(key).hasMatch(),
                "Invalid qualification key");
            e->hmp("sendkey " + key);
          }
          done = true;
        }
      } else if (action == "stopped")
        done = !app->currentEngine();
      else if (action == "click") {
        QString target = step["text"].toString();
        auto root = scope(step);
        QList<QAbstractButton *> matches;
        const auto widgets = step.contains("object")
                                 ? QList<QWidget *>{namedWidget(step)}
                                 : QApplication::allWidgets();
        for (auto w : widgets)
          if (auto b = qobject_cast<QAbstractButton *>(w);
              b && b->isVisible() && b->isEnabled() &&
              (step.contains("object") || b->text() == target)) {
            if (root && b->window() != root)
              continue;
            matches << b;
          }
        require(matches.size() <= 1, "Ambiguous qualification button: " + target);
        if (matches.size() == 1) {
          auto b = matches.first();
          QTimer::singleShot(0, b, [b] { b->click(); });
          done = true;
        }
      } else if (action == "edit") {
        auto w = scope(step);
        if (w) {
          auto edits = step.contains("object")
                           ? QList<PEdit *>{qobject_cast<PEdit *>(namedWidget(step))}
                           : w->findChildren<PEdit *>();
          edits.erase(std::remove_if(edits.begin(), edits.end(),
                      [](PEdit *edit) { return !edit || !edit->isVisible(); }), edits.end());
          int i = step.value("index").toInt();
          if (i >= 0 && i < edits.size()) {
            edits[i]->setText(step["text"].toString());
            done = true;
          }
        }
      } else if (action == "combo") {
        auto w = scope(step);
        if (step["window"].toString() == "guest" && app->currentEngine())
          w = app->guest(app->currentEngine());
        if (w) {
          auto combos = step.contains("object")
                            ? QList<PCombo *>{qobject_cast<PCombo *>(namedWidget(step))}
                            : w->findChildren<PCombo *>();
          combos.erase(std::remove_if(combos.begin(), combos.end(),
              [](PCombo *combo) { return !combo || !combo->isVisible(); }), combos.end());
          int i = step.value("index").toInt();
          if (i >= 0 && i < combos.size()) {
            int j = combos[i]->findText(step["text"].toString());
            if (j >= 0) {
              combos[i]->setCurrentIndex(j);
              done = true;
            }
          }
        }
      } else if (action == "file") {
        for (auto w : QApplication::topLevelWidgets())
          if (auto d = qobject_cast<QFileDialog *>(w); d && d->isVisible()) {
            QFileInfo file(step["path"].toString());
            d->setDirectory(file.absolutePath());
            QTimer::singleShot(500, d, [d, file] {
              d->selectFile(file.absoluteFilePath());
              if (auto edit = d->findChild<QLineEdit *>("fileNameEdit"))
                edit->setText(file.absoluteFilePath());
              QTimer::singleShot(
                  100, d, [d] { QMetaObject::invokeMethod(d, "accept"); });
            });
            done = true;
            break;
          }
      } else
        Model::fail("Unknown qualification action: " + action);
      if (done) {
        results.append(QJsonObject{{"step", index},
                                   {"action", action},
                                   {"passed", true},
                                   {"elapsed", elapsed.elapsed()}});
        ++index;
        elapsed.restart();
      }
    } catch (const std::exception &e) {
      finish(false, e.what());
    }
    runningStep = false;
  }
};
int qualificationEngine() {
  require(!qEnvironmentVariableIsEmpty("CLASSICMAC_QUALIFY") &&
              !qEnvironmentVariableIsEmpty("CLASSICMAC_TEST_HOME") &&
              !qEnvironmentVariableIsEmpty("CLASSICMAC_TEST_DOCUMENTS"),
          "The unresponsive engine fixture requires isolated qualification data");
  // No QMP responses: this child reproduces an engine whose control pipe is
  // hung. Its parent must stop it without waiting for a QMP command timeout.
  // A bounded lifetime prevents an interrupted qualification from leaking it.
  QTimer::singleShot(120000, qApp, &QCoreApplication::quit);
  return QCoreApplication::exec();
}
void qualify(App *app, const QString &recipe) {
  try {
    new Qualification(app, Model::readJSON(recipe, 1048576));
  } catch (const std::exception &e) {
    std::fprintf(stderr, "Qualification: %s\n", e.what());
    qApp->exit(1);
  }
}

void qualifyOSX(const QString &folder) {
  auto machine = Model::readMachine(folder);
  require(Model::isOSXFamily(machine.family()), "Expected a disposable OS X test Mac.");
  Engine engine(machine);
  engine.start();
  auto pump = [](int ms) {
    QEventLoop loop;
    QTimer::singleShot(ms, &loop, &QEventLoop::quit);
    loop.exec();
  };
  auto waitFor = [&](const auto &condition, const QString &failure, int timeout) {
    QElapsedTimer deadline;
    deadline.start();
    while (engine.running && !condition() && deadline.elapsed() < timeout)
      pump(100);
    require(condition(), failure + " " + engine.log);
  };
  auto finder = [&] {
    const auto &frame = engine.vnc.frame;
    if (!engine.vnc.ready || frame.size() != QSize(1024, 768))
      return false;
    QByteArray pixels;
    for (int y = 2; y < 19; ++y)
      for (int x = 45; x < 405; ++x) {
        QRgb pixel = frame.pixel(x, y);
        pixels.append(char(qRed(pixel)));
        pixels.append(char(qGreen(pixel)));
        pixels.append(char(qBlue(pixel)));
      }
    return QCryptographicHash::hash(pixels, QCryptographicHash::Sha256).toHex() ==
        "89f979ef71b80b76f11ca94687b9b2aae55b1d3eb71019ca362e426f896a5602";
  };
  waitFor(finder, "Tiger Finder did not appear in the native guest display.", 180000);
  engine.pause();
  require(!engine.command("query-status").toObject()["running"].toBool(), "Tiger did not pause.");
  engine.resume();
  require(engine.command("query-status").toObject()["running"].toBool(), "Tiger did not resume.");

  // Use the real native client's keyboard transport to open a Finder window.
  const auto desktop = engine.vnc.frame.copy(QRect(300, 80, 64, 64));
  engine.vnc.key(0xffeb, true);
  engine.vnc.key(0xffe1, true);
  pump(100);
  engine.vnc.key('h', true);
  pump(120);
  engine.vnc.key('h', false);
  engine.vnc.key(0xffe1, false);
  engine.vnc.key(0xffeb, false);
  waitFor([&] { return engine.vnc.frame.copy(QRect(300, 80, 64, 64)) != desktop; },
          "Tiger did not open the Home folder after keyboard input.", 10000);
  std::puts("Tiger Finder pixels, pause/resume and native keyboard input passed.");
  engine.forceOff(); // Disposable boot fixture, discarded after the check.
  waitFor([&] { return !engine.running; }, "Test guest process did not stop.", 10000);
}
