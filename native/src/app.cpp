#include "app.h"
#include <QtConcurrent>
#include <optional>
#ifdef Q_OS_WIN
#include <windows.h>
#endif
using Model::fail;
using namespace Platinum;
static QString iconName(const QString &f) {
  return f == "quadra800"      ? "quadra_800"
         : f == "powerMac7500" ? "powermac_7500"
                               : "powermac_g4";
}
static QString modelName(const QString &f) {
  return f == "quadra800"      ? "Quadra 800"
         : f == "powerMac7500" ? "Power Mac 7500"
                               : "Power Mac G4";
}
class MachineDelegate : public QStyledItemDelegate {
public:
  App *app;
  explicit MachineDelegate(App *a) : QStyledItemDelegate(a), app(a) {}
  QSize sizeHint(const QStyleOptionViewItem &,
                 const QModelIndex &) const override {
    return {240, 40};
  }
  void paint(QPainter *q, const QStyleOptionViewItem &o,
             const QModelIndex &index) const override {
    auto m = app->machines.value(index.row());
    Pixels p(o.rect.width(), 40,
             o.state & QStyle::State_Selected ? 0xffccccff : 0xffffffff);
    text(p, elide(m.name(), p.w() - 52), 45, 17);
    auto e = app->engines.value(m.id());
    text(p, e ? (e->paused ? "Paused" : "Running now") : modelName(m.family()),
         45, 31, 0xff555555, "small");
    QPainter painter(&p.image);
    painter.drawImage(5, 4, QImage(":/" + iconName(m.family()) + "-32.png"));
    if (e) {
      painter.fillRect(31, 29, 7, 7, Qt::black);
      painter.fillRect(32, 30, 5, 5,
                       e->paused ? QColor(0xff8600) : QColor(0x00a600));
      painter.fillRect(33, 31, 1, 1, Qt::white);
    }
    q->save();
    q->setRenderHint(QPainter::SmoothPixmapTransform, false);
    q->drawImage(o.rect.topLeft(), p.image);
    q->restore();
  }
};
class Preview : public QWidget {
public:
  App *app;
  explicit Preview(App *a) : QWidget(a), app(a) {
    setAccessibleName("Mac preview");
    setFixedSize(400, 300);
  }
  void paintEvent(QPaintEvent *) override {
    Pixels p(width(), height(), 0xff63639c);
    sunken(p);
    auto m = app->current();
    QImage image;
    if (auto e = app->currentEngine())
      image = e->vnc.frame;
    else if (m) {
      try {
        auto path = Model::machineFile(m->folder, "preview.png");
        if (QFileInfo(path).size() <= 16 * 1048576)
          image.load(path);
      } catch (const std::exception &) { /* A preview is optional. */
      }
    }
    QPainter q(&p.image);
    if (!image.isNull())
      q.drawImage(QRect(2, 2, width() - 4, height() - 4), image);
    else {
      q.drawImage(
          (width() - 32) / 2, height() / 2 - 30,
          QImage(":/" + iconName(m ? m->family() : "powerMacG4") + "-32.png"));
      text(p, "Start up this Mac to see its screen.", 65, height() / 2 + 22,
           0xffffffff, "small");
    }
    QPainter(this).drawImage(0, 0, p.image);
  }
  void mouseDoubleClickEvent(QMouseEvent *) override { app->startSelected(); }
};
template <class T>
static T
background(QWidget *parent, const QString &title,
           const std::function<T(std::atomic_bool &, Library::Progress)> &job,
           bool cancellable = true) {
  PDialog d(title, {480, 190}, parent);
  auto v = dialogLayout(&d);
  auto status = label("Preparing…", &d, "regular", true);
  auto bar = new PProgress(&d);
  v->addWidget(status);
  v->addWidget(bar);
  v->addStretch();
  std::atomic_bool cancel = false;
  bool finished = false;
  auto pause = button("Pause", [&] { cancel = true; }, &d);
  pause->setVisible(cancellable);
  buttonRow(v, {pause});
  d.canClose = [&] {
    if (!finished)
      cancel = true;
    return finished;
  };
  struct Result {
    std::optional<T> value;
    QString error;
  };
  QFutureWatcher<Result> watcher;
  auto progress = [&](QString phase, qint64 complete, qint64 total) {
    QMetaObject::invokeMethod(
        &d,
        [=] {
          status->setText(
              phase + QString("… %1%").arg(total ? complete * 100 / total : 0));
          bar->setValue(total ? double(complete) / total : -1);
        },
        Qt::QueuedConnection);
  };
  auto future = QtConcurrent::run([&]() -> Result {
    try {
      return {job(cancel, progress), {}};
    } catch (const std::exception &e) {
      return {{}, QString::fromUtf8(e.what())};
    } catch (...) {
      return {{}, "The operation could not complete."};
    }
  });
  QObject::connect(&watcher, &QFutureWatcher<Result>::finished, &d, [&] {
    finished = true;
    d.done();
  });
  watcher.setFuture(future);
  d.exec();
  if (!finished) {
    cancel = true;
    future.waitForFinished();
  }
  auto result = future.result();
  if (!result.error.isEmpty())
    fail(result.error);
  return *result.value;
}
App::App() : PWindow("ClassicMac", {940, 700}) {
  setMinimumSize(360, 240);
  setWindowIcon(QIcon(":/app.png"));
  QFile f(Model::libraryFile());
  if (f.open(QFile::ReadOnly) && f.size() <= 1048576) {
    auto paths = QJsonDocument::fromJson(f.readAll()).array();
    for (auto p : paths) {
      try {
        auto m = Model::readMachine(p.toString());
        bool duplicate = false;
        for (auto old : machines)
          duplicate |= old.id() == m.id();
        if (!duplicate)
          machines << m;
      } catch (const std::exception &e) {
        unavailablePaths << p.toString();
        qWarning("Skipped unavailable library machine: %s", e.what());
      }
    }
  }
  buildHome();
  canClose = [&] {
    if (!engines.isEmpty()) {
      if (question(this, "Macs are running",
                   "Shut down your running Macs before quitting ClassicMac. "
                   "Send Shut Down to every running Mac?",
                   "Shut Down")) {
        for (auto e : engines.values())
          if (e->running)
            e->shutdown();
      }
      return false;
    }
    for (auto w : guests)
      w->close();
    QTimer::singleShot(0, qApp, &QApplication::quit);
    return true;
  };
  render();
  if (machines.isEmpty())
    QTimer::singleShot(0, this, [&] {
      if (qEnvironmentVariableIsEmpty("CLASSICMAC_QUALIFY"))
        showSetup();
    });
}
Model::Machine *App::current() {
  return selected >= 0 && selected < machines.size() ? &machines[selected]
                                                     : nullptr;
}
Engine *App::currentEngine() const {
  return selected >= 0 && selected < machines.size()
             ? engines.value(machines[selected].id())
             : nullptr;
}
void App::saveLibrary() {
  QJsonArray paths;
  for (const auto &path : unavailablePaths)
    paths.append(path);
  for (auto m : machines)
    paths.append(m.folder);
  Model::writeJSON(Model::libraryFile(), paths);
}
void App::registerMachine(const Model::Machine &m) {
  unavailablePaths.removeAll(m.folder);
  for (int i = 0; i < machines.size(); ++i)
    if (machines[i].id() == m.id()) {
      if (machines[i].folder != m.folder)
        fail("A different folder already uses this Mac's identity.");
      machines[i] = m;
      select(i);
      saveLibrary();
      return;
    }
  machines << m;
  selected = machines.size() - 1;
  saveLibrary();
  render();
}
void App::addMachine(const QString &folder) {
  registerMachine(Model::readMachine(folder));
}
void App::select(int i) {
  selected = i;
  render();
}
void App::importMachine() {
  auto path = QFileDialog::getExistingDirectory(
      this, "Open a ClassicMac machine folder", Model::documents());
  if (!path.isEmpty())
    addMachine(path);
}
void App::buildHome() {
  auto outer = new QVBoxLayout(body);
  outer->setContentsMargins(12, 12, 12, 12);
  outer->setSpacing(10);
  home = new QWidget(body);
  auto columns = new QHBoxLayout(home);
  columns->setContentsMargins(0, 0, 0, 0);
  columns->setSpacing(14);
  auto left = new QWidget(home);
  left->setFixedWidth(262);
  auto l = new QVBoxLayout(left);
  l->setContentsMargins(0, 0, 0, 0);
  l->setSpacing(8);
  l->addWidget(label("Macs on this computer:", left));
  list = new QListWidget(left);
  list->setItemDelegate(new MachineDelegate(this));
  list->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
  list->setFrameStyle(QFrame::Box);
  list->setLineWidth(2);
  list->setSpacing(0);
  list->setSelectionMode(QAbstractItemView::SingleSelection);
  l->addWidget(list, 1);
  auto actions = new QHBoxLayout;
  actions->setSpacing(10);
  actions->addWidget(button("New Mac…", [&] { showEditor(true); }, left));
  actions->addWidget(button("Library…", [&] { showLibrary(); }, left));
  actions->addStretch();
  l->addLayout(actions);
  columns->addWidget(left);
  auto right = new QWidget(home);
  auto panel = new QVBoxLayout(right);
  panel->setContentsMargins(0, 0, 0, 0);
  panel->setSpacing(10);
  preview = new Preview(this);
  panel->addWidget(preview, 0, Qt::AlignHCenter);
  auto heading = new QHBoxLayout;
  nameLabel = label("", right);
  statusLabel = label("", right, "small");
  statusLabel->ink = 0xff555555;
  heading->addWidget(nameLabel, 1);
  heading->addWidget(statusLabel);
  panel->addLayout(heading);
  auto group = new PGroup("About This Mac", right);
  auto g = new QHBoxLayout(group);
  g->setContentsMargins(12, 23, 12, 12);
  g->setSpacing(12);
  details = label("", group, "small");
  g->addWidget(details, 1);
  auto controls = new QVBoxLayout;
  controls->setSpacing(10);
  infoButton = button("Get Info…", [&] { showEditor(); }, group);
  mediaButton = button("Media…", [&] { showMedia(); }, group);
  controls->addWidget(infoButton);
  controls->addWidget(mediaButton);
  controls->addWidget(button(
      "Show Files",
      [&] {
        if (auto m = current())
          QDesktopServices::openUrl(QUrl::fromLocalFile(m->folder));
      },
      group));
  controls->addStretch();
  g->addLayout(controls);
  panel->addWidget(group);
  notice = label("", right, "small", true);
  notice->ink = 0xff555555;
  panel->addWidget(notice);
  panel->addStretch();
  auto power = new QHBoxLayout;
  power->setSpacing(12);
  power->addWidget(button("Open Mac…", [&] { importMachine(); }, right));
  power->addWidget(button(
      "About…",
      [&] {
        problem(this,
                "ClassicMac " CLASSICMAC_VERSION
                "\n\nNative C++ application with QEMU emulation and the "
                "measured Platinum appearance.\n\nSystem icons © Apple "
                "Computer, Inc. Machine artwork © The Iconfactory, included "
                "with permission.\n\nCopland requires the macOS edition.");
      },
      right));
  power->addStretch();
  startButton = button("Start Up", [&] { startSelected(); }, right, true);
  power->addWidget(startButton);
  panel->addLayout(power);
  columns->addWidget(right, 1);
  auto scroller = new QScrollArea(body);
  scroller->setFrameShape(QFrame::NoFrame);
  scroller->setWidgetResizable(true);
  scroller->setWidget(home);
  scroller->setAutoFillBackground(false);
  scroller->viewport()->setAutoFillBackground(false);
  outer->addWidget(scroller, 1);
  connect(list, &QListWidget::currentRowChanged, this, [&](int i) {
    if (i >= 0 && selected != i)
      select(i);
  });
  connect(list, &QListWidget::itemDoubleClicked, this, [&] {
    try {
      startSelected();
    } catch (const std::exception &e) {
      problem(this, e.what());
    }
  });
}
void App::render() {
  if (!home)
    return;
  QSignalBlocker block(list);
  list->clear();
  for (auto m : machines)
    list->addItem(m.name());
  list->setCurrentRow(selected);
  auto m = current();
  auto e = currentEngine();
  nameLabel->setText(m ? m->name() : "Your classic Macs, on your PC.");
  statusLabel->setText(e ? (e->paused ? "Paused" : "Running now")
                         : "Shut down");
  QString detail;
  if (m) {
    auto c = m->config;
    detail = "Model:     " + modelName(m->family()) +
             "\nMemory:    " + QString::number(c["ramMB"].toInt()) +
             " MB\nHard disk:  " + QString::number(c["diskSizeGB"].toInt()) +
             " GB\nScreen:     " + QString::number(c["width"].toInt()) + " × " +
             QString::number(c["height"].toInt()) + "\nGraphics:  " +
             (c["graphicsCard"].toString() == "atiRadeon" ? "ATI Radeon"
                                                          : "Standard") +
             "\nSharing:    " +
             (c["sharedFolderPath"].toString().isEmpty() ? "Off" : "On");
  }
  details->setText(detail);
  infoButton->setEnabled(m && !e);
  mediaButton->setEnabled(m);
  startButton->setEnabled(m && m->family() != "powerMac7500");
  startButton->setText(e ? (e->paused ? "Resume Mac" : "Show Mac")
                         : "Start Up");
  notice->setText(
      e   ? (e->audioUnavailable
                 ? "Sound is unavailable on this computer. This session is "
                   "running without sound."
                 : "Use Shut Down inside Mac OS to keep its disk healthy.")
      : m ? "Choose Start Up, or use Media to select an installation CD."
          : "Open a .classic folder or choose New Mac to get started.");
  preview->update();
}
PWindow *App::guest(Engine *e) {
  if (guests.contains(e->machine.id()))
    return guests[e->machine.id()];
  auto w =
      new PWindow(e->machine.name(),
                  {std::max(653, e->machine.config["width"].toInt() + 13),
                   std::max(550, e->machine.config["height"].toInt() + 84)},
                  this);
  w->setMinimumSize(480, 360);
  auto v = new QVBoxLayout(w->body);
  v->setContentsMargins(0, 0, 0, 0);
  v->setSpacing(6);
  auto screen = new GuestView(&e->vnc, w->body);
  screen->helpers = e->machine.config["classicInputHelpers"].toBool();
  const auto config = e->machine.config;
  const bool ppc = e->machine.family() == "powerMacG4";
  const bool cdBoot = config["bootFromCD"].toBool() && !config["cdImagePath"].toString().isEmpty();
  screen->followsWindow = ppc ? (cdBoot || config["graphicsCard"].toString() != "atiRadeon")
                              : config["useEnhancedFramebuffer"].toBool();
  screen->widthAlignment = ppc ? 8 : 1;
  v->addWidget(screen, 1);
  auto displayRow = new QHBoxLayout;
  displayRow->setContentsMargins(8, 0, 24, 0);
  auto status = label("Starting up…", w, "small");
  status->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Fixed);
  displayRow->addWidget(status, 1);
  auto scale = new PCombo(w);
  scale->setAccessibleName("Guest display scale");
  scale->addItems({"Sharp Pixels", "1× Pixels", "2× Pixels", "Fit Window"});
  scale->setFixedWidth(126);
  displayRow->addWidget(scale);
  v->addLayout(displayRow);
  auto updateDisplayStatus = [=] {
    QString s = e->vnc.frame.isNull() ? "Starting up…" :
        QString("%1 × %2").arg(e->vnc.frame.width()).arg(e->vnc.frame.height());
    if (!screen->followsWindow)
      s += " · Fixed resolution";
    if (!screen->resizeNotice.isEmpty())
      s += " · " + screen->resizeNotice;
    else if (e->vnc.relative)
      s += screen->captured ? " · Escape releases mouse" : " · Click to capture mouse";
    if (status->value != s)
      status->setText(s);
  };
  connect(scale, qOverload<int>(&QComboBox::currentIndexChanged), screen, [=](int mode) {
    screen->fitWindow = mode == 3;
    screen->pixelZoom = mode == 1 ? 1 : mode == 2 ? 2 : 0;
    screen->scheduleResize();
    screen->update();
  });
  connect(screen, &GuestView::displayChanged, w, updateDisplayStatus);
  connect(screen, &GuestView::captureChanged, w, updateDisplayStatus);
  connect(&e->vnc, &VNC::changed, w, updateDisplayStatus);
  auto controls = new QHBoxLayout;
  controls->setContentsMargins(8, 0, 24, 6);
  controls->setSpacing(10);
  auto pause = button(
      "Pause",
      [e, p = QPointer<PWindow>(w)] {
        if (e->paused)
          e->resume();
        else
          e->pause();
        if (p)
          p->update();
      },
      w);
  controls->addWidget(pause);
  controls->addWidget(button("Restart", [e] { e->shutdown(true); }, w));
  controls->addWidget(button("Shut Down", [e] { e->shutdown(); }, w));
  controls->addWidget(button("Paste…", [this, e] { showPaste(e); }, w));
  controls->addStretch();
  auto fullScreen = button("Full Screen", [] {}, w);
  auto toggleFullScreen = [w, screen, fullScreen] {
        screen->release();
        if (w->isFullScreen())
          w->showNormal();
        else
          w->showFullScreen();
        fullScreen->setText(w->isFullScreen() ? "Window" : "Full Screen");
        screen->setFocus();
      };
  connect(fullScreen, &PButton::clicked, w, toggleFullScreen);
  connect(screen, &GuestView::toggleFullScreen, w, toggleFullScreen);
  auto shortcut = new QShortcut(QKeySequence("Alt+Return"), w);
  connect(shortcut, &QShortcut::activated, w, toggleFullScreen);
  controls->addWidget(fullScreen);
  v->addLayout(controls);
  guests[e->machine.id()] = w;
  connect(e, &Engine::changed, w, [=] {
    pause->setText(e->paused ? "Resume" : "Pause");
    bool wasPaused = screen->paused;
    screen->paused = e->paused;
    if (wasPaused && !screen->paused)
      screen->scheduleResize();
  });
  connect(&e->vnc, &VNC::problem, this, [this](QString s) {
    QTimer::singleShot(0, this, [this, s] { problem(this, s); });
  });
  auto previewClock = std::make_shared<QElapsedTimer>();
  previewClock->start();
  connect(&e->vnc, &VNC::changed, this, [this, e, previewClock] {
    preview->update();
    if (previewClock->elapsed() > 1000 && !e->vnc.frame.isNull()) {
      previewClock->restart();
      QSaveFile file(e->machine.folder + "/preview.png");
      if (file.open(QFile::WriteOnly) &&
          e->vnc.frame
              .scaled(400, 300, Qt::KeepAspectRatio, Qt::FastTransformation)
              .save(&file, "PNG"))
        file.commit();
    }
  });
  return w;
}
void App::startSelected() {
  auto m = current();
  if (!m)
    return;
  auto e = currentEngine();
  if (e) {
    if (e->paused)
      e->resume();
    auto w = guest(e);
    w->show();
    w->raise();
    w->activateWindow();
    return;
  }
  auto id = m->id();
  e = new Engine(*m, this);
  engines[id] = e;
  connect(e, &Engine::changed, this, &App::render);
  connect(e, &Engine::exited, this, [this, id, e](bool restart, QString error) {
    engines.remove(id);
    if (auto w = guests.take(id)) {
      w->hide();
      w->deleteLater();
    }
    e->deleteLater();
    render();
    if (!error.isEmpty())
      problem(this, error);
    if (restart)
      QTimer::singleShot(0, this, [this, id] {
        for (int i = 0; i < machines.size(); ++i)
          if (machines[i].id() == id) {
            select(i);
            try {
              startSelected();
            } catch (const std::exception &e) {
              problem(this, e.what());
            }
            break;
          }
      });
  });
  try {
    e->start();
  } catch (...) {
    engines.remove(id);
    e->deleteLater();
    render();
    throw;
  }
  auto w = guest(e);
  w->show();
  w->raise();
  w->activateWindow();
  render();
}
QJsonArray App::loadCatalog() {
  catalogRows = background<QJsonArray>(
      this, "Loading Mac OS Library",
      [](std::atomic_bool &, Library::Progress) { return Library::load(); },
      false);
  return catalogRows;
}
Model::Machine App::install(QJsonObject entry, const QString &name) {
  auto parent = Model::documents();
  auto m = background<Model::Machine>(
      this, "Download Mac",
      [=](std::atomic_bool &cancel, Library::Progress progress) {
        return Library::install(entry, parent, name, cancel, progress);
      });
  registerMachine(m);
  return m;
}
void App::showLibrary() {
  auto rows = loadCatalog();
  PDialog d("Mac OS Library", {650, 610}, this);
  auto v = dialogLayout(&d);
  v->addWidget(label("Download a ready-to-run Mac. Each edition creates its "
                     "own independent machine.",
                     &d, "regular", true));
  auto area = new QScrollArea(&d);
  area->setWidgetResizable(true);
  area->setFrameShape(QFrame::NoFrame);
  auto content = new QWidget;
  auto items = new QVBoxLayout(content);
  items->setContentsMargins(0, 0, 8, 0);
  items->setSpacing(14);
  for (auto row : rows) {
    auto entry = row.toObject();
    items->addWidget(label(entry["name"].toString(), content));
    items->addWidget(
        label(entry["summary"].toString(), content, "regular", true));
    bool available =
        entry.value("machineFamily").toString("powerMacG4") != "powerMac7500" &&
        Model::supportedVersion(entry["minimumAppVersion"].toString());
    auto b = button(
        available ? "Download…" : "Requires macOS Copland engine",
        [&, entry] {
          install(entry, entry["name"].toString());
          problem(&d, "Installed. Choose Start Up to begin.");
        },
        content);
    b->setEnabled(available);
    items->addWidget(b, 0, Qt::AlignLeft);
  }
  items->addStretch();
  area->setWidget(content);
  v->addWidget(area, 1);
  buttonRow(v, {button("Close", [&] { d.done(); }, &d, true)});
  d.exec();
}
void App::settingsSaved(QJsonObject c) {
  auto m = current();
  if (!m || currentEngine())
    fail("Shut down this Mac before changing its settings.");
  c = Model::validate(c);
  Model::writeJSON(m->folder + "/config.json", c);
  m->config = c;
  render();
}
void App::showEditor(bool create) {
  auto m = current();
  if (!create && !m)
    return;
  if (!create && currentEngine())
    fail("Shut down this Mac before changing its settings.");
  PDialog d(create ? "New Mac" : "Get Info", {590, create ? 300 : 620}, this);
  auto v = dialogLayout(&d);
  auto form = new QGridLayout;
  form->setHorizontalSpacing(12);
  form->setVerticalSpacing(10);
  auto c = create ? Model::freshConfig("Mac OS 9") : m->config;
  auto name = new PEdit(c["name"].toString(), &d);
  form->addWidget(label("Name:", &d), 0, 0);
  form->addWidget(name, 0, 1);
  auto family = new PCombo(&d);
  family->addItem("Power Mac G4", "powerMacG4");
  family->addItem("Quadra 800", "quadra800");
  auto capacity = new PCombo(&d);
  for (int n : {1, 2, 4, 8, 16, 32, 64, 120})
    capacity->addItem(QString::number(n) + " GB", n);
  capacity->setCurrentIndex(5);
  auto ram = new PEdit(QString::number(c["ramMB"].toInt()), &d);
  auto resolution = new PCombo(&d);
  for (auto s :
       {"640x480", "800x600", "1024x768", "1280x960", "1600x1200", "1920x1080"})
    resolution->addItem(s);
  resolution->setCurrentText(
      QString("%1x%2").arg(c["width"].toInt()).arg(c["height"].toInt()));
  auto depth = new PCombo(&d);
  for (int n : {1, 2, 4, 8, 16, 32})
    depth->addItem(n == 1    ? "Black & White"
                   : n == 16 ? "Thousands"
                   : n == 32 ? "Millions"
                             : QString::number(1 << n),
                   n);
  depth->setCurrentIndex(depth->findData(c["depth"].toInt()));
  auto graphics = new PCombo(&d);
  graphics->addItem("Standard", "standard");
  graphics->addItem("ATI Radeon (3D acceleration)", "atiRadeon");
  graphics->setCurrentIndex(graphics->findData(c["graphicsCard"].toString()));
  if (create) {
    ram->hide();
    resolution->hide();
    depth->hide();
    graphics->hide();
    form->addWidget(label("Model:", &d), 1, 0);
    form->addWidget(family, 1, 1);
    form->addWidget(label("Hard disk:", &d), 2, 0);
    form->addWidget(capacity, 2, 1);
  } else {
    family->hide();
    capacity->hide();
    form->addWidget(label("Memory (MB):", &d), 1, 0);
    form->addWidget(ram, 1, 1);
    form->addWidget(label("Screen:", &d), 2, 0);
    form->addWidget(resolution, 2, 1);
    form->addWidget(label("Colors:", &d), 3, 0);
    form->addWidget(depth, 3, 1);
    form->addWidget(label("Graphics:", &d), 4, 0);
    form->addWidget(graphics, 4, 1);
  }
  v->addLayout(form);
  if (!create && m->family() == "powerMacG4")
    v->addWidget(label("Standard graphics follows the window. ATI Radeon keeps the Mac's selected resolution; use Fit Window to fill the view.",
                        &d, "small", true));
  QMap<QString, PButton *> checks;
  if (!create) {
    for (auto pair : QList<QPair<QString, QString>>{
             {"toolsCDInserted", "ClassicMac Tools CD"},
             {"useG4CPU", "PowerPC G4 CPU"},

             {"useEnhancedFramebuffer", "Enhanced Quadra framebuffer"},
             {"sound", "Sound"},
             {"networking", "Networking"},
             {"tabletInput", "Tablet pointer"},
             {"classicInputHelpers",
              "Right click as Control-click; scroll as arrow keys"},
             {"bootFromCD", "Start from inserted CD"}}) {
      auto check = new PButton(pair.second, &d);
      check->toggleButton = true;
      check->setCheckable(true);
      check->setChecked(c[pair.first].toBool());
      checks[pair.first] = check;
      v->addWidget(check);
    }
    v->addWidget(
        label("Shut down the Mac before changing hardware or startup media.",
              &d, "small", true));
  }
  v->addStretch();
  buttonRow(v, {button(
                    "Cancel", [&] { d.done(0); }, &d),
                button(
                    create ? "Create" : "Save",
                    [&] {
                      if (create) {
                        const auto newName = name->text();
                        const auto newFamily = family->currentData().toString();
                        const auto diskSize = capacity->currentData().toInt();
                        auto machine = background<Model::Machine>(
                            &d, "Creating Mac",
                            [=](std::atomic_bool &, Library::Progress) {
                              return Model::createMachine(newName, newFamily,
                                                          diskSize,
                                                          Model::documents());
                            },
                            false);
                        registerMachine(machine);
                      } else {
                        auto next = c;
                        next["name"] = name->text();
                        bool ok;
                        int n = ram->text().toInt(&ok);
                        if (!ok)
                          fail("Enter a whole number for memory.");
                        next["ramMB"] = n;
                        auto r = resolution->currentText().split('x');
                        next["width"] = r[0].toInt();
                        next["height"] = r[1].toInt();
                        next["depth"] = depth->currentData().toInt();
                        next["graphicsCard"] =
                            graphics->currentData().toString();
                        for (auto it = checks.begin(); it != checks.end(); ++it)
                          next[it.key()] = it.value()->isChecked();
                        settingsSaved(next);
                      }
                      d.done();
                    },
                    &d, true)});
  d.exec();
}
void App::showMedia() {
  auto m = current();
  if (!m)
    return;
  PDialog d("Media", {540, 430}, this);
  auto v = dialogLayout(&d);
  v->addWidget(label("Shut down this Mac before changing its media.", &d,
                     "regular", true));
  auto choose = [&](QString key, QString title, QString filters) {
    auto path = QFileDialog::getOpenFileName(&d, title, m->folder, filters);
    if (!path.isEmpty()) {
      auto c = m->config;
      c[key] = path;
      if (key == "cdImagePath")
        c["bootFromCD"] = true;
      settingsSaved(c);
    }
  };
  for (auto key :
       QStringList{"cdImagePath", "floppyImagePath", "sharedFolderPath"}) {
#ifdef Q_OS_WIN
    if (key == "sharedFolderPath" && m->config[key].toString().isEmpty())
      continue;
#endif
    QString title = key == "cdImagePath"       ? "CD image"
                    : key == "floppyImagePath" ? "Floppy image"
                                               : "Shared folder";
    v->addWidget(
        label(title + ": " + QFileInfo(m->config[key].toString()).fileName(),
              &d, "small"));
    auto row = new QHBoxLayout;
    auto chooseButton = button(
        "Choose " + title + "…",
        [&, key] {
          if (key == "sharedFolderPath") {
#ifdef Q_OS_WIN
            fail("Host folder sharing requires Linux or macOS.");
#else
            auto path = QFileDialog::getExistingDirectory(
                &d, "Share a Folder with This Mac");
            if (!path.isEmpty()) {
              auto c = m->config;
              c[key] = path;
              c["sharedVolumeName"] = "Shared Files";
              settingsSaved(c);
            }
#endif
          } else
            choose(key, "Choose " + title,
                   key == "cdImagePath"
                       ? "CD images (*.iso *.img *.toast *.cdr)"
                       : "Floppy images (*.img *.dsk *.ima *.raw)");
        },
        &d);
    chooseButton->setEnabled(!currentEngine() && (key != "floppyImagePath" ||
                                                  m->family() == "quadra800"));
    row->addWidget(chooseButton);
#ifdef Q_OS_WIN
    if (key == "sharedFolderPath")
      chooseButton->hide();
#endif
    auto eject = button(
        key == "sharedFolderPath" ? "Turn Off" : "Eject",
        [&, key] {
          auto c = m->config;
          c.remove(key);
          if (key == "cdImagePath")
            c["bootFromCD"] = false;
          settingsSaved(c);
        },
        &d);
    eject->setEnabled(!currentEngine() && !m->config[key].toString().isEmpty());
    row->addWidget(eject);
    row->addStretch();
    v->addLayout(row);
  }
  v->addStretch();
  auto force = button(
      "Force Off…",
      [&] {
        auto e = currentEngine();
        if (e && question(&d, "Force Off",
                          "Unsaved changes will be lost. Use Shut Down in Mac "
                          "OS whenever possible.",
                          "Force Off")) {
          e->forceOff();
          d.done();
        }
      },
      &d);
  force->setEnabled(currentEngine());
  buttonRow(v, {force, button("Done", [&] { d.done(); }, &d, true)});
  d.exec();
}
void App::showPaste(Engine *e) {
  PDialog d("Paste Text into Mac", {590, 400}, this);
  auto v = dialogLayout(&d);
  v->addWidget(label("Focus the destination in Mac OS first. The Mac must use "
                     "its US keyboard layout.",
                     &d, "regular", true));
  auto edit = new QPlainTextEdit(&d);
  QFont bitmapFont("RetroBoot Regular");
  bitmapFont.setPixelSize(12);
  bitmapFont.setStyleStrategy(QFont::NoAntialias);
  edit->setFont(bitmapFont);
  edit->setPlainText(QApplication::clipboard()->text().left(8192));
  v->addWidget(edit, 1);
  auto slow = new PButton("Slow typing", &d);
  slow->toggleButton = true;
  slow->setCheckable(true);
  v->addWidget(slow);
  auto status = label("", &d, "small");
  v->addWidget(status);
  std::atomic_bool cancel = false;
  bool busy = false;
  d.canClose = [&] {
    cancel = true;
    return !busy;
  };
  auto send = button(
      "Paste Text",
      [&] {
        busy = true;
        cancel = false;
        auto guard = qScopeGuard([&] { busy = false; });
        e->paste(edit->toPlainText(), slow->isChecked(), cancel,
                 [&](int n, int total) {
                   status->setText(
                       QString("Typed %1 of %2 characters.").arg(n).arg(total));
                 });
        status->setText("Finished.");
      },
      &d, true);
  buttonRow(v, {button(
                    "Cancel",
                    [&] {
                      cancel = true;
                      if (!busy)
                        d.done();
                    },
                    &d),
                send});
  d.exec();
}
void App::showSetup() {
  if (settingUp)
    return;
  settingUp = true;
  auto guard = qScopeGuard([&] { settingUp = false; });
  PDialog d("Welcome to ClassicMac", {660, 540}, this);
  auto v = dialogLayout(&d);
  auto heading = label("Your first classic Mac", &d);
  v->addWidget(heading);
  auto stack = new QStackedWidget(&d);
  v->addWidget(stack, 1);
  QList<QWidget *> pages;
  for (int i = 0; i < 4; ++i) {
    auto p = new QWidget;
    stack->addWidget(p);
    pages << p;
  }
  auto welcome = new QVBoxLayout(pages[0]);
  welcome->setSpacing(18);
  welcome->addWidget(
      label("A classic Macintosh, ready on your computer.", pages[0]));
  welcome->addWidget(
      label("Download a prepared Mac from the Library, bring your own "
            "installation CD, or open a .classic machine you already have.",
            pages[0], "regular", true));
  auto license = new PButton(
      "I have the right to use the Mac OS software I choose.", pages[0]);
  license->toggleButton = true;
  license->setCheckable(true);
  welcome->addWidget(license);
  welcome->addWidget(button(
      "Open an existing Mac…",
      [&] {
        importMachine();
        if (current())
          d.done();
      },
      pages[0]));
  welcome->addStretch();
  auto sourceLayout = new QVBoxLayout(pages[1]);
  sourceLayout->addWidget(label("How would you like to start?", pages[1]));
  auto download = new PButton("Download a pre-installed Mac", pages[1]);
  download->toggleButton = download->radio = true;
  download->setCheckable(true);
  download->setAutoExclusive(true);
  download->setChecked(true);
  auto installer = new PButton("Use my own Mac OS installation CD", pages[1]);
  installer->toggleButton = installer->radio = true;
  installer->setCheckable(true);
  installer->setAutoExclusive(true);
  sourceLayout->addWidget(download);
  sourceLayout->addWidget(
      label("Choose System 7.5.5, Mac OS 8.1, 8.6 or Mac OS 9.", pages[1],
            "regular", true));
  sourceLayout->addWidget(installer);
  sourceLayout->addWidget(label(
      "Create a blank Mac and install from your ISO, IMG, Toast or CDR image.",
      pages[1], "regular", true));
  sourceLayout->addStretch();
  auto fields = new QVBoxLayout(pages[2]);
  auto editions = new PCombo(pages[2]);
  auto model = new PCombo(pages[2]);
  model->addItem("Power Mac G4", "powerMacG4");
  model->addItem("Quadra 800", "quadra800");
  auto summary = label("", pages[2], "regular", true);
  auto name = new PEdit("Mac OS 9", pages[2]);
  QString cd;
  auto chooseCD = button(
      "Choose Installation CD…",
      [&] {
        auto path = QFileDialog::getOpenFileName(
            &d, "Choose your Mac OS installation CD", {},
            "CD images (*.iso *.img *.toast *.cdr)");
        if (!path.isEmpty()) {
          cd = path;
          summary->setText(QFileInfo(path).fileName());
        }
      },
      pages[2]);
  fields->addWidget(label("Name your Mac:", pages[2]));
  fields->addWidget(name);
  fields->addWidget(editions);
  fields->addWidget(model);
  fields->addWidget(chooseCD);
  fields->addWidget(summary);
  fields->addStretch();
  auto ready = new QVBoxLayout(pages[3]);
  ready->addWidget(label("Your Mac is ready", pages[3]));
  auto help = label("", pages[3], "regular", true);
  ready->addWidget(help);
  ready->addStretch();
  auto next = new PButton("Continue", &d, true);
  auto back = button(
      "Back",
      [&] { stack->setCurrentIndex(std::max(0, stack->currentIndex() - 1)); },
      &d);
  auto later = button("Start Later", [&] { d.done(); }, &d);
  auto cancel = button("Cancel", [&] { d.done(); }, &d);
  auto update = [&] {
    int step = stack->currentIndex();
    back->setVisible(step == 1 || step == 2);
    later->setVisible(step == 3);
    cancel->setVisible(!machines.isEmpty());
    next->setEnabled(step != 0 || license->isChecked());
    next->setText(
        step < 2    ? "Continue"
        : step == 3 ? (installer->isChecked() ? "Start Installer" : "Start Mac")
                    : (installer->isChecked() ? "Create Mac" : "Download Mac"));
    heading->setText(
        QString("Step %1 of 4 · Your first classic Mac").arg(step + 1));
  };
  buttonRow(v, {cancel, back, later, next});
  connect(license, &PButton::toggled, &d, update);
  connect(stack, &QStackedWidget::currentChanged, &d, update);
  connect(editions, qOverload<int>(&QComboBox::currentIndexChanged), &d,
          [&](int i) {
            if (i < 0)
              return;
            auto entry = editions->itemData(i).toJsonObject();
            name->setText(entry["name"].toString());
            summary->setText(entry["summary"].toString());
          });
  connect(next, &PButton::clicked, &d, [&] {
    try {
      int step = stack->currentIndex();
      if (step == 0) {
        if (license->isChecked())
          stack->setCurrentIndex(1);
      } else if (step == 1) {
        bool useInstaller = installer->isChecked();
        editions->setVisible(!useInstaller);
        model->setVisible(useInstaller);
        chooseCD->setVisible(useInstaller);
        summary->setText("");
        if (!useInstaller) {
          auto rows = loadCatalog();
          editions->clear();
          QMap<QString, QJsonObject> newest;
          for (auto row : rows) {
            auto e = row.toObject();
            if (e.value("machineFamily").toString("powerMacG4") ==
                    "powerMac7500" ||
                !Model::supportedVersion(e["minimumAppVersion"].toString()))
              continue;
            auto key = e["id"].toString();
            auto match = QRegularExpression("^(.*)-v(\\d+)$").match(key);
            auto base = match.hasMatch() ? match.captured(1) : key;
            if (!newest.contains(base) ||
                match.captured(2).toInt() >
                    newest[base]["id"].toString().section("-v", -1).toInt())
              newest[base] = e;
          }
          for (auto e : newest)
            editions->addItem(e["name"].toString(), QVariant::fromValue(e));
          if (editions->count() == 0)
            fail("The Library has no compatible editions.");
          auto entry = editions->currentData().toJsonObject();
          name->setText(entry["name"].toString());
          summary->setText(entry["summary"].toString());
        }
        stack->setCurrentIndex(2);
      } else if (step == 2) {
        if (installer->isChecked()) {
          if (cd.isEmpty())
            fail("Choose your Mac OS installation CD image first.");
          const auto newName = name->text();
          const auto newFamily = model->currentData().toString();
          auto m = background<Model::Machine>(
              &d, "Creating your Mac",
              [=](std::atomic_bool &, Library::Progress) {
                return Model::createMachine(newName, newFamily,
                                            newFamily == "quadra800" ? 2 : 32,
                                            Model::documents(), cd);
              },
              false);
          registerMachine(m);
          help->setText("Start the installer. Inside Mac OS, initialize the "
                        "blank hard disk with the CD's disk utility, then run "
                        "Installer. On Power Mac, turn off Update Apple Hard "
                        "Disk Drivers in Options. After installation, shut "
                        "down, eject the CD in Media, then start your Mac.");
        } else {
          install(editions->currentData().toJsonObject(), name->text());
          help->setText("Everything is in place. Start your Mac to go straight "
                        "to the classic desktop.");
        }
        stack->setCurrentIndex(3);
      } else {
        startSelected();
        d.done();
      }
    } catch (const std::exception &e) {
      problem(&d, e.what());
    }
  });
  update();
  d.exec();
  render();
}
QJsonObject App::evidence() const {
  QJsonArray rows;
  for (auto m : machines)
    rows.append(QJsonObject{{"id", m.id()},
                            {"name", m.name()},
                            {"folder", m.folder},
                            {"config", m.config},
                            {"running", engines.contains(m.id())}});
  QJsonArray displays;
  for (auto e : engines) {
    auto w = guests.value(e->machine.id());
    if (!w)
      continue;
    auto view = w->findChild<GuestView *>();
    if (!view)
      continue;
    const auto image = view->screenRect();
    displays.append(QJsonObject{
        {"name", e->machine.name()}, {"windowWidth", w->width()}, {"windowHeight", w->height()},
        {"viewportWidth", view->width()}, {"viewportHeight", view->height()},
        {"frameWidth", e->vnc.frame.width()}, {"frameHeight", e->vnc.frame.height()},
        {"desiredWidth", view->desiredSize().width()}, {"desiredHeight", view->desiredSize().height()},
        {"requestedWidth", e->vnc.lastRequestedSize.width()}, {"requestedHeight", e->vnc.lastRequestedSize.height()},
        {"scale", w->devicePixelRatioF()}, {"followsWindow", view->followsWindow},
        {"resizeSupported", e->vnc.desktopResizeSupported}, {"resizeResult", e->vnc.desktopResizeResult},
        {"fitWindow", view->fitWindow}, {"pixelZoom", view->pixelZoom},
        {"imageX", image.x()}, {"imageY", image.y()}, {"imageWidth", image.width()}, {"imageHeight", image.height()},
        {"captured", view->captured}, {"relative", e->vnc.relative},
        {"resizeNotice", view->resizeNotice}});
  }
  QJsonObject result = {{"frontend", "native-qt-cpp"},
          {"version", CLASSICMAC_VERSION},
          {"machines", rows},
          {"width", width()},
          {"height", height()},
          {"shaded", shaded},
          {"scale", devicePixelRatioF()},
          {"electron", false},
          {"webview", false}, {"displays", displays}};
#ifdef Q_OS_WIN
  const auto hwnd = reinterpret_cast<HWND>(winId());
  result["windowsDpi"] = int(GetDpiForWindow(hwnd));
  result["perMonitorV2"] = bool(AreDpiAwarenessContextsEqual(
      GetWindowDpiAwarenessContext(hwnd), DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2));
  RECT client;
  if (GetClientRect(hwnd, &client)) {
    result["windowsClientWidth"] = int(client.right - client.left);
    result["windowsClientHeight"] = int(client.bottom - client.top);
  }
#endif
  return result;
}
