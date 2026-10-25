#include "qualification.h"
#include "app.h"
#include <cstdio>
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
    native.resize(80, 20);
    native.setFocusPolicy(Qt::NoFocus);
    QImage capture(80 * scale, 20 * scale, QImage::Format_ARGB32);
    capture.setDevicePixelRatio(scale);
    capture.fill(face);
    native.render(&capture);
    QImage expected(fixtures + "/button-normal.png");
    expected = expected.convertToFormat(QImage::Format_ARGB32)
                   .scaled(capture.size(), Qt::IgnoreAspectRatio,
                           Qt::FastTransformation);
    require(capture == expected,
            "Actual native button differs at scale " + QString::number(scale));
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
      QImage one(940, 700, QImage::Format_ARGB32),
          two(1880, 1400, QImage::Format_ARGB32);
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
  if (!output.isEmpty())
    Model::writeJSON(output + "/self-test.json",
                     QJsonObject{{"checks", count},
                                 {"nativeWidgets", 5},
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
  QJsonArray steps, results;
  QString output;
  int index = 0;
  int announced = -1;
  QElapsedTimer elapsed;
  QTimer timer;
  bool runningStep = false;
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
      else if (action == "open") {
        app->addMachine(step["folder"].toString());
        done = true;
      } else if (action == "resize") {
        PWindow *target = app;
        if (step["window"].toString() == "guest") {
          require(app->currentEngine(), "No running guest to resize");
          target = app->guest(app->currentEngine());
        }
        target->resize(step["width"].toInt(), step["height"].toInt());
        QApplication::processEvents();
        for (auto b : target->body->findChildren<PButton *>()) {
          if (b->isVisible()) {
            if (target == app) {
              for (auto area : app->body->findChildren<QScrollArea *>())
                area->ensureWidgetVisible(b);
            } else
              require(target->rect().contains(QRect(b->mapTo(target, QPoint()), b->size())),
                      "A guest action is clipped: " + b->text());
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
          auto view = app->guest(e)->findChild<GuestView *>();
          QKeyEvent event(QEvent::KeyPress, Qt::Key_Escape, Qt::NoModifier);
          QApplication::sendEvent(view, &event);
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
          auto size = e->vnc.frame.size();
          size.scale(view->size(), Qt::KeepAspectRatio);
          QPointF p((view->width() - size.width()) / 2.0 +
                        step["x"].toInt() * size.width() /
                            double(e->vnc.frame.width()),
                    (view->height() - size.height()) / 2.0 +
                        step["y"].toInt() * size.height() /
                            double(e->vnc.frame.height()));
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
      } else if (action == "capture") {
        auto w = step["window"].toString() == "home"
                     ? static_cast<QWidget *>(app)
                     : (QApplication::activeModalWidget()
                            ? QApplication::activeModalWidget()
                            : QApplication::activeWindow());
        if (!w)
          w = app;
        if (step["window"].toString() == "guest" && app->currentEngine())
          w = app->guest(app->currentEngine());
        w->grab().save(output + "/" + step["name"].toString() + ".png");
        if (step["nativeScreen"].toBool() && w->screen()) {
          const auto origin = w->mapToGlobal(QPoint());
          w->screen()->grabWindow(0, origin.x(), origin.y(), w->width(), w->height())
              .save(output + "/" + step["name"].toString() + "-desktop.png");
        }
        Model::writeJSON(output + "/" + step["name"].toString() + ".json", app->evidence());
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
        auto scope = QApplication::activeModalWidget();
        for (auto w : QApplication::allWidgets())
          if (auto b = qobject_cast<QAbstractButton *>(w);
              b && b->isVisible() && b->isEnabled() && b->text() == target) {
            if (scope && b->window() != scope)
              continue;
            QTimer::singleShot(0, b, [b] { b->click(); });
            done = true;
            break;
          }
      } else if (action == "edit") {
        auto w = (QApplication::activeModalWidget()
                      ? QApplication::activeModalWidget()
                      : QApplication::activeWindow());
        if (w) {
          auto edits = w->findChildren<PEdit *>();
          int i = step.value("index").toInt();
          if (i < edits.size()) {
            edits[i]->setText(step["text"].toString());
            done = true;
          }
        }
      } else if (action == "combo") {
        auto w = (QApplication::activeModalWidget()
                      ? QApplication::activeModalWidget()
                      : QApplication::activeWindow());
        if (step["window"].toString() == "guest" && app->currentEngine())
          w = app->guest(app->currentEngine());
        if (w) {
          auto combos = w->findChildren<PCombo *>();
          int i = step.value("index").toInt();
          if (i < combos.size()) {
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
void qualify(App *app, const QString &recipe) {
  try {
    new Qualification(app, Model::readJSON(recipe, 1048576));
  } catch (const std::exception &e) {
    std::fprintf(stderr, "Qualification: %s\n", e.what());
    qApp->exit(1);
  }
}
