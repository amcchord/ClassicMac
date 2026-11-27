#include "app.h"
#include "display.h"
#include "printer.h"
#include "transfer_meter.h"
#include <QDesktopServices>
#include <QtConcurrent>
#include <QtMath>
#include <optional>
#ifdef Q_OS_WIN
#include <windows.h>
#endif
using Model::fail;
using namespace Platinum;
static bool isUsefulPreview(const QImage &image) {
  if (image.isNull() || image.width() <= 0 || image.height() <= 0)
    return false;
  QRgb first = 0;
  bool hasFirst = false, nonBlack = false, varied = false;
  const int cols = std::min(9, image.width());
  const int rows = std::min(9, image.height());
  for (int r = 0; r < rows; ++r) {
    const int y = ((r * 2 + 1) * image.height()) / (2 * rows);
    for (int c = 0; c < cols; ++c) {
      const int x = ((c * 2 + 1) * image.width()) / (2 * cols);
      const QRgb px = image.pixel(x, y);
      if (!hasFirst) {
        first = px;
        hasFirst = true;
      } else if (px != first) {
        varied = true;
      }
      if (qRed(px) > 12 || qGreen(px) > 12 || qBlue(px) > 12)
        nonBlack = true;
      if (nonBlack && varied)
        return true;
    }
  }
  return false;
}
static void performAction(const std::function<void()> &action) {
  try { action(); }
  catch (const std::exception &error) {
    problem(QApplication::activeWindow(), QString::fromUtf8(error.what()));
  }
}
static QString iconName(const QString &f) {
  return f == "quadra800"      ? "quadra_800"
         : f == "powerMac7500" ? "powermac_7500"
                               : "powermac_g4";
}
static QString modelName(const QString &f) {
  return f == "quadra800"      ? "Quadra 800"
         : f == "powerMac7500" ? "Power Mac 7500"
         : f == "powerMacOSX"  ? "Power Mac G4 — Mac OS X"
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
    text(p, e ? (e->forcedStopRequested() ? "Stopping…" :
                 e->stoppingRequested() ? "Shutting down…" :
                 e->paused ? "Paused" : "Running now") : modelName(m.family()),
         45, 31, 0xff555555, "small");
    QPainter painter(&p.image);
    painter.drawImage(5, 4,
                      cachedIcon(":/" + iconName(m.family()) + "-32.png"));
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
  QString cachedPath;
  QDateTime cachedModified;
  qint64 cachedSize = -1;
  QImage cachedImage;
  explicit Preview(App *a) : QWidget(a), app(a) {
    setAccessibleName("Mac preview");
    setMinimumSize(200, 150);
    setMaximumHeight(300);
    setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);
  }
  QSize sizeHint() const override { return {400, 300}; }
  bool hasHeightForWidth() const override { return true; }
  int heightForWidth(int w) const override { return std::clamp(w * 3 / 4, 150, 300); }
  QImage loadCachedPreview(const QString &folder) {
    try {
      const auto path = Model::machineFile(folder, "preview.png");
      const QFileInfo info(path);
      if (!info.exists() || info.size() > 16 * 1048576) {
        if (cachedPath == path) {
          cachedPath.clear();
          cachedModified = {};
          cachedSize = -1;
          cachedImage = {};
        }
        return {};
      }
      if (cachedPath != path || cachedSize != info.size() ||
          cachedModified != info.lastModified()) {
        QImage loaded;
        loaded.load(path);
        cachedPath = path;
        cachedSize = info.size();
        cachedModified = info.lastModified();
        cachedImage = loaded;
      }
      return cachedImage;
    } catch (const std::exception &) {
      return {};
    }
  }
  void paintEvent(QPaintEvent *) override {
    Pixels p(width(), height(), 0xff63639c);
    sunken(p);
    auto m = app->current();
    QImage image;
    if (auto e = app->currentEngine()) {
      if (isUsefulPreview(e->vnc.frame))
        image = e->vnc.frame;
      else
        image = app->livePreviews.value(e->machine.id());
      if (image.isNull() && m)
        image = loadCachedPreview(m->folder);
    } else if (m) {
      image = loadCachedPreview(m->folder);
    }
    QPainter q(&p.image);
    q.setRenderHint(QPainter::SmoothPixmapTransform, false);
    if (!image.isNull()) {
      auto size = image.size();
      size.scale(width() - 4, height() - 4, Qt::KeepAspectRatio);
      q.drawImage(QRect(QPoint((width() - size.width()) / 2,
                               (height() - size.height()) / 2), size), image);
    }
    else {
      q.drawImage(
          (width() - 32) / 2, height() / 2 - 30,
          cachedIcon(":/" + iconName(m ? m->family() : "powerMacG4") + "-32.png"));
      const auto hint = m ? "Start up to see your Mac's screen."
                          : "Choose a Mac from the Library.";
      text(p, elide(hint, width() - 24, "small"),
           std::max(12, (width() - textWidth(hint, "small")) / 2),
           height() / 2 + 22, 0xffffffff, "small");
    }
    QPainter(this).drawImage(0, 0, p.image);
  }
  void mouseDoubleClickEvent(QMouseEvent *) override {
    performAction([this] { app->startSelected(); });
  }
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
  canClose = [this] { return requestQuit(); };
  render();
  QTimer::singleShot(0, this, [this] {
    if (machines.isEmpty() && qEnvironmentVariableIsEmpty("CLASSICMAC_QUALIFY"))
      showSetup();
  });
  QTimer::singleShot(1500, this, [this] {
    const auto settings = Updater::loadSettings();
    if (Updater::shouldAutoCheck(settings))
      checkForUpdates(false);
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
  auto toolbar = new QWidget(body);
  auto tools = new PFlowLayout(toolbar);
  tools->setContentsMargins(0, 0, 0, 0);
  tools->addWidget(button("Library…", [this] { showLibrary(); }, toolbar));
  tools->addWidget(button("New Mac…", [this] { showEditor(true); }, toolbar));
  tools->addWidget(button("Open Mac…", [this] { importMachine(); }, toolbar));
  auto checkUpdatesButton =
      button("Check for Updates…", [this] { checkForUpdates(true); }, toolbar);
  checkUpdatesButton->setObjectName("checkForUpdatesButton");
  tools->addWidget(checkUpdatesButton);
  tools->addWidget(button("About…", [this] { showAbout(); }, toolbar));
  outer->addWidget(toolbar);
  home = new QWidget(body);
  auto columns = new QBoxLayout(QBoxLayout::LeftToRight, home);
  homeColumns = columns;
  columns->setContentsMargins(0, 0, 0, 0);
  columns->setSpacing(14);
  auto left = new QWidget(home);
  machineSidebar = left;
  left->setMinimumWidth(210);
  auto l = new QVBoxLayout(left);
  l->setContentsMargins(0, 0, 0, 0);
  l->setSpacing(8);
  machineCount = label("Your Macs", left);
  l->addWidget(machineCount);
  list = new QListWidget(left);
  list->setObjectName("machineList");
  list->setAccessibleName("Your Macs");
  list->setItemDelegate(new MachineDelegate(this));
  list->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
  list->setFrameStyle(QFrame::Box);
  list->setLineWidth(2);
  list->setSpacing(0);
  list->setSelectionMode(QAbstractItemView::SingleSelection);
  list->setMinimumHeight(100);
  l->addWidget(list, 1);
  auto sidebarHelp = label("Double-click a Mac to open it.\nRight-click for more actions.", left, "small", true);
  sidebarHelp->setObjectName("machineListHelp");
  sidebarHelp->ink = 0xff555555;
  l->addWidget(sidebarHelp);
  columns->addWidget(left);
  auto right = new QWidget(home);
  right->setMinimumWidth(240);
  auto panel = new QVBoxLayout(right);
  panel->setContentsMargins(0, 0, 0, 0);
  panel->setSpacing(10);
  preview = new Preview(this);
  auto heading = new QHBoxLayout;
  nameLabel = label("", right);
  statusLabel = label("", right, "small");
  statusLabel->ink = 0xff555555;
  heading->addWidget(nameLabel, 1);
  heading->addWidget(statusLabel);
  panel->addLayout(heading);
  panel->addWidget(preview, 1);
  auto group = new PGroup("About This Mac", right);
  group->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Maximum);
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
  manageButton = button("Manage…", [this] { showMachineActions(); }, group);
  controls->addWidget(manageButton);
  deleteButton = button("Delete…", [this] { performAction([this] { removeSelected(); }); }, group);
  deleteButton->setObjectName("deleteMachineButton");
  controls->addWidget(deleteButton);
  controls->addStretch();
  g->addLayout(controls);
  panel->addWidget(group);
  notice = label("", right, "small", true);
  notice->ink = 0xff555555;
  panel->addWidget(notice);
  auto power = new QHBoxLayout;
  power->setSpacing(12);
  powerButton = button("Power…", [this] {
    if (auto e = currentEngine()) showPowerActions(e);
  }, right);
  power->addWidget(powerButton);
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
  list->setContextMenuPolicy(Qt::CustomContextMenu);
  connect(list, &QListWidget::customContextMenuRequested, this, [this](QPoint pos) {
    auto item = list->itemAt(pos);
    if (!item) return;
    list->setCurrentItem(item);
    QMenu menu(this);
    auto add = [&](const QString &text, std::function<void()> action) {
      return menu.addAction(text, this, [action] { performAction(action); });
    };
    add(currentEngine() ? "Show Mac" : "Start Up", [this] { startSelected(); });
    auto info = add("Get Info…", [this] { showEditor(); });
    info->setEnabled(!currentEngine());
    add("Media…", [this] { showMedia(); });
    add("Manage…", [this] { showMachineActions(); });
    if (currentEngine()) add("Power…", [this] { showPowerActions(currentEngine()); });
    menu.addSeparator();
    auto remove = add("Delete Mac…", [this] { removeSelected(); });
    remove->setEnabled(true);
    menu.exec(list->viewport()->mapToGlobal(pos));
  });
  auto shortcut = [this](const char *key, std::function<void()> action) {
    auto s = new QShortcut(QKeySequence(QString::fromLatin1(key)), this);
    connect(s, &QShortcut::activated, this, [action] {
      try { action(); } catch (const std::exception &e) { problem(QApplication::activeWindow(), e.what()); }
    });
  };
  shortcut("Ctrl+L", [this] { showLibrary(); });
  shortcut("Ctrl+N", [this] { showEditor(true); });
  shortcut("Ctrl+O", [this] { importMachine(); });
  shortcut("Ctrl+I", [this] { showEditor(); });
  auto removeShortcut = new QShortcut(QKeySequence(Qt::Key_Delete), list);
  removeShortcut->setContext(Qt::WidgetShortcut);
  connect(removeShortcut, &QShortcut::activated, this, [this] {
    performAction([this] { removeSelected(); });
  });
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
  reflowHome();
}
void App::reflowHome() {
  if (!homeColumns || !machineSidebar) return;
  const bool compact = body->width() < 660;
  homeColumns->setDirection(compact ? QBoxLayout::TopToBottom : QBoxLayout::LeftToRight);
  machineSidebar->setMinimumWidth(compact ? 0 : 210);
  machineSidebar->setMaximumWidth(compact ? QWIDGETSIZE_MAX : 240);
  machineSidebar->setMaximumHeight(compact ? 130 : QWIDGETSIZE_MAX);
  if (list) list->setMinimumHeight(compact ? 64 : 100);
  if (auto help = machineSidebar->findChild<PLabel *>("machineListHelp"))
    help->setVisible(!compact);
  if (preview) {
    preview->setMinimumHeight(compact ? 100 : 150);
    preview->setMaximumHeight(compact ? 150 : 300);
  }
}
void App::resizeEvent(QResizeEvent *event) {
  PWindow::resizeEvent(event);
  reflowHome();
}
void App::render() {
  if (!home)
    return;
  QSignalBlocker block(list);
  while (list->count() > machines.size()) delete list->takeItem(list->count() - 1);
  for (int i = 0; i < machines.size(); ++i) {
    if (i >= list->count()) list->addItem(machines[i].name());
    else list->item(i)->setText(machines[i].name());
  }
  list->viewport()->update();
  list->setCurrentRow(selected);
  machineCount->setText(QString("Your Macs · %1").arg(machines.size()));
  auto m = current();
  auto e = currentEngine();
  nameLabel->setText(m ? m->name() : "ClassicMac");
  statusLabel->setText(e ? (e->forcedStopRequested() ? "Stopping…" :
                            e->stoppingRequested() ? "Shutting down…" :
                            e->paused ? "Paused" : "Running")
                         : m ? "Shut down" : "");
  QString detail;
  if (m) {
    auto c = m->config;
    detail = "Model:     " + modelName(m->family()) +
             "\nMemory:    " + QString::number(c["ramMB"].toInt()) +
             " MB\nHard disk:  " + QString::number(c["diskSizeGB"].toInt()) +
             " GB\nScreen:     " + QString::number(c["width"].toInt()) + " × " +
             QString::number(c["height"].toInt()) + "\nGraphics:  " +
             (c["graphicsCard"].toString() == "atiRadeon" ? "ATI Radeon"
                                                          : "Standard");
    if (Model::supportsHostSharing() && m->family() != "powerMacOSX" &&
        m->family() != "powerMac7500") {
      const auto shared = c["sharedFolderPath"].toString();
      const auto sharedName = QFileInfo(shared).fileName();
      detail += "\nSharing:    " +
                (shared.isEmpty()
                     ? QString("Off")
                     : (sharedName.isEmpty()
                            ? QDir::toNativeSeparators(shared)
                            : sharedName));
    }
    if (m->family() != "powerMac7500") {
      const bool printOn = c["printingEnabled"].toBool();
      const QString pName = c["printerName"].toString();
      detail += "\nPrinting:    " +
                (!printOn          ? QString("Off")
                 : pName == "pdf"  ? QString("Save as PDF")
                 : pName.isEmpty() ? QString("System Default")
                                   : pName);
    }
  }
  details->setText(detail);
  infoButton->setEnabled(m && !e);
  mediaButton->setEnabled(m);
  manageButton->setEnabled(m);
  deleteButton->setEnabled(m != nullptr);
  powerButton->setVisible(e != nullptr);
  startButton->setEnabled(m && m->family() != "powerMac7500");
  startButton->setText(e ? (e->paused ? "Resume Mac" : "Show Mac")
                         : "Start Up");
  notice->setText(
      e   ? (e->audioUnavailable
                 ? "Sound is unavailable on this computer. This session is "
                   "running without sound."
                 : "Use Power for shutdown, restart, or a Mac that has stopped responding.")
      : m ? "Choose Start Up, or use Media to select an installation CD."
          : "Choose Library for a ready-to-run Mac, New Mac to use your own installation CD, or Open Mac for an existing .classic folder.");
  preview->update();
}
void App::showAbout() {
  static QPointer<PDialog> existing;
  if (existing) {
    existing->show();
    existing->raise();
    existing->activateWindow();
    return;
  }
  PDialog d("About ClassicMac", {540, 420}, this);
  existing = &d;
  auto v = dialogLayout(&d);
  auto header = new QHBoxLayout;
  header->setSpacing(14);
  auto iconLabel = new QLabel(&d);
  iconLabel->setFixedSize(72, 72);
  iconLabel->setPixmap(QPixmap::fromImage(
      QImage(":/app.png").scaled(72, 72, Qt::KeepAspectRatio,
                                 Qt::SmoothTransformation)));
  header->addWidget(iconLabel, 0, Qt::AlignTop);
  auto titleCol = new QVBoxLayout;
  titleCol->setSpacing(4);
  titleCol->addWidget(label("ClassicMac", &d));
  titleCol->addWidget(label("Version " CLASSICMAC_VERSION, &d, "small"));
  titleCol->addWidget(
      label("The whole classic Mac OS era on your computer.", &d, "regular", true));
  header->addLayout(titleCol, 1);
  v->addLayout(header);
  auto credits = new QGridLayout;
  credits->setHorizontalSpacing(10);
  credits->setVerticalSpacing(4);
  int row = 0;
  for (const auto &item : QList<QPair<QString, QString>>{
           {"Emulation:",
            "QEMU 11 (Power Mac & Quadra) and QEMU 10 (Mac OS X); Copland requires the macOS edition."},
           {"Graphics:",
            "ATI Radeon graphics and Platinum appearance shared with RetroBoot."},
           {"Firmware:", "OpenBIOS; classicvirtio host folder sharing."},
           {"System icons:",
            "Original classic Mac OS artwork © Apple Computer, Inc."},
           {"Mac icons:", "© The Iconfactory, included with permission."}}) {
    auto key = label(item.first, &d, "small");
    credits->addWidget(key, row, 0, Qt::AlignRight | Qt::AlignTop);
    credits->addWidget(label(item.second, &d, "small", true), row, 1);
    ++row;
  }
  credits->setColumnStretch(1, 1);
  v->addLayout(credits);
  v->addWidget(
      label("© 2026 Austin McChord. See Third-Party Notices for licenses.", &d,
            "small", true));
  v->addStretch();
  auto notices = button(
      "Third-Party Notices…",
      [&] {
        QString noticesPath;
        const auto dir = QCoreApplication::applicationDirPath();
        for (const auto &candidate : {
                 dir + "/ThirdPartyNotices.md",
                 dir + "/THIRD_PARTY_NOTICES.md",
                 dir + "/../ThirdPartyNotices.md",
                 dir + "/../share/doc/classicmac/ThirdPartyNotices.md",
                 dir + "/../../THIRD_PARTY_NOTICES.md",
                 dir + "/../../../THIRD_PARTY_NOTICES.md",
             }) {
          if (QFileInfo::exists(candidate)) {
            noticesPath = QFileInfo(candidate).canonicalFilePath();
            break;
          }
        }
        if (noticesPath.isEmpty()) {
          problem(&d,
                  "ThirdPartyNotices.md could not be found next to the application.");
          return;
        }
        if (!QDesktopServices::openUrl(QUrl::fromLocalFile(noticesPath))) {
          QFile file(noticesPath);
          if (file.open(QFile::ReadOnly)) {
            PDialog viewer("Third-Party Notices", {620, 480}, &d);
            auto lv = dialogLayout(&viewer);
            auto textView = new QPlainTextEdit(&viewer);
            textView->setReadOnly(true);
            textView->setPlainText(QString::fromUtf8(file.readAll()));
            lv->addWidget(textView, 1);
            buttonRow(lv,
                      {button("Close", [&] { viewer.done(); }, &viewer, true)});
            viewer.exec();
          } else {
            problem(&d, "Could not open ThirdPartyNotices.md.");
          }
        }
      },
      &d);
  auto checkUpdates = button(
      "Check for Updates…",
      [&] {
        d.done();
        checkForUpdates(true);
      },
      &d);
  checkUpdates->setObjectName("aboutCheckUpdates");
  buttonRow(v, {notices, checkUpdates,
                button("Close", [&] { d.done(); }, &d, true)});
  d.exec();
}

void App::checkForUpdates(bool manual) {
  static bool checkInProgress = false;
  if (checkInProgress)
    return;
  checkInProgress = true;
  auto settings = Updater::loadSettings();
  settings.lastUpdateCheckUtc =
      QDateTime::currentDateTimeUtc().toString(Qt::ISODate);
  try {
    Updater::saveSettings(settings);
  } catch (const std::exception &) {
  }

  struct CheckResult {
    std::optional<Updater::ReleaseInfo> release;
    QString error;
  };

  if (!manual) {
    auto *watcher = new QFutureWatcher<CheckResult>(this);
    connect(watcher, &QFutureWatcher<CheckResult>::finished, this,
            [this, watcher, guard = QPointer<App>(this)] {
              checkInProgress = false;
              const auto res = watcher->result();
              watcher->deleteLater();
              if (!guard || !res.error.isEmpty() || !res.release)
                return;
              const auto curSettings = Updater::loadSettings();
              if (res.release->version == curSettings.skippedUpdateVersion)
                return;
              if (QApplication::activeModalWidget() || settingUp)
                return;
              showUpdateDialog(*res.release, false);
            });
    watcher->setFuture(QtConcurrent::run([]() -> CheckResult {
      try {
        std::atomic_bool cancel = false;
        return {Updater::checkLatest(cancel), {}};
      } catch (const std::exception &e) {
        return {std::nullopt, QString::fromUtf8(e.what())};
      } catch (...) {
        return {std::nullopt, QStringLiteral("Update check failed.")};
      }
    }));
    return;
  }

  PDialog waitDlg("Software Update", {460, 170}, this);
  waitDlg.setObjectName("updateCheckDialog");
  auto lv = dialogLayout(&waitDlg);
  lv->addWidget(label("Checking for updates…", &waitDlg, "regular", true));
  auto bar = new PProgress(&waitDlg);
  bar->setValue(-1);
  lv->addWidget(bar);
  lv->addStretch();
  std::atomic_bool cancel = false;
  bool finished = false;
  auto cancelBtn = button("Cancel", [&] { cancel = true; }, &waitDlg);
  buttonRow(lv, {cancelBtn});
  waitDlg.canClose = [&] {
    if (!finished)
      cancel = true;
    return finished;
  };
  QFutureWatcher<CheckResult> watcher;
  connect(&watcher, &QFutureWatcher<CheckResult>::finished, &waitDlg, [&] {
    finished = true;
    waitDlg.done();
  });
  auto future = QtConcurrent::run([&cancel]() -> CheckResult {
    try {
      return {Updater::checkLatest(cancel), {}};
    } catch (const std::exception &e) {
      return {std::nullopt, QString::fromUtf8(e.what())};
    } catch (...) {
      return {std::nullopt, QStringLiteral("Could not check for updates.")};
    }
  });
  watcher.setFuture(future);
  waitDlg.exec();
  if (!finished) {
    cancel = true;
    future.waitForFinished();
  }
  checkInProgress = false;
  const CheckResult res = future.result();
  if (cancel)
    return;
  if (!res.error.isEmpty()) {
    problem(this, res.error);
    return;
  }
  if (!res.release) {
    PDialog upToDate("Software Update", {460, 180}, this);
    upToDate.setObjectName("upToDateDialog");
    auto uv = dialogLayout(&upToDate);
    uv->addWidget(label("ClassicMac is up to date", &upToDate));
    uv->addWidget(label(
        "ClassicMac " CLASSICMAC_VERSION " is currently the newest version available.",
        &upToDate, "regular", true));
    uv->addStretch();
    buttonRow(uv, {button("OK", [&] { upToDate.done(); }, &upToDate, true)});
    upToDate.exec();
    return;
  }
  showUpdateDialog(*res.release, true);
}

void App::showUpdateDialog(const Updater::ReleaseInfo &info, bool manual) {
  PDialog d("Software Update", {540, info.notes.isEmpty() ? 270 : 420}, this);
  d.setObjectName("updateAvailableDialog");
  auto v = dialogLayout(&d);
  v->addWidget(label("ClassicMac " + info.version + " is available", &d));
  v->addWidget(label(
      "You are currently running version " CLASSICMAC_VERSION ".",
      &d, "small", true));
  if (!info.notes.isEmpty()) {
    auto notesView = new QPlainTextEdit(&d);
    notesView->setReadOnly(true);
    notesView->setFrameShape(QFrame::Box);
    notesView->setLineWidth(2);
    QFont bitmapFont("RetroBoot Regular");
    bitmapFont.setPixelSize(12);
    bitmapFont.setStyleStrategy(QFont::NoAntialias);
    notesView->setFont(bitmapFont);
    notesView->setPlainText(info.notes);
    v->addWidget(notesView, 1);
  } else {
    v->addStretch();
  }
  auto settings = Updater::loadSettings();
  auto autoCheck = new PButton("Automatically check for updates", &d);
  autoCheck->toggleButton = true;
  autoCheck->setCheckable(true);
  autoCheck->setChecked(settings.autoCheckUpdates);
  connect(autoCheck, &PButton::toggled, &d, [&](bool on) {
    auto s = Updater::loadSettings();
    s.autoCheckUpdates = on;
    try {
      Updater::saveSettings(s);
    } catch (const std::exception &) {
    }
  });
  v->addWidget(autoCheck);

  const bool canInPlace = Updater::canInstallInPlace();
  if (canInPlace && !engines.isEmpty())
    v->addWidget(label(
        "Shut down all running Macs before installing this update.",
        &d, "small", true));

  QList<QWidget *> actions;
  if (!manual) {
    auto skipBtn = button(
        "Skip This Version",
        [&] {
          auto s = Updater::loadSettings();
          s.skippedUpdateVersion = info.version;
          try {
            Updater::saveSettings(s);
          } catch (const std::exception &) {
          }
          d.done(0);
        },
        &d);
    skipBtn->setObjectName("skipUpdateButton");
    actions << skipBtn;
  }
  actions << button("Later", [&] { d.done(0); }, &d);
  if (canInPlace) {
    auto installBtn = button(
        "Install Update…",
        [&] {
          if (!engines.isEmpty()) {
            problem(&d, "Shut down all running Macs before installing an update.");
            return;
          }
          d.done(1);
        },
        &d, true);
    installBtn->setObjectName("installUpdateButton");
    actions << installBtn;
  } else {
    auto openBtn = button(
        "Download Page…",
        [&] {
          QDesktopServices::openUrl(QUrl(info.htmlUrl));
          d.done(0);
        },
        &d, true);
    openBtn->setObjectName("downloadUpdatePageButton");
    actions << openBtn;
  }
  buttonRow(v, actions);
  if (d.exec() == 1 && canInPlace) {
    performAction([this, info] { installUpdate(info); });
  }
}

void App::installUpdate(const Updater::ReleaseInfo &info) {
  if (!engines.isEmpty())
    fail("Shut down all running Macs before installing an update.");
  const QString targetPath = Updater::inPlaceTargetPath();
  if (targetPath.isEmpty())
    fail("In-place updates require a packaged AppImage or portable executable.");

  PDialog d("Installing Update", {500, 240}, this);
  d.setObjectName("updateProgressDialog");
  auto v = dialogLayout(&d);
  v->addWidget(label("ClassicMac " + info.version, &d, "system", true));
  auto status = label("Connecting to GitHub…", &d, "regular", true);
  auto bar = new PProgress(&d);
  bar->setValue(-1);
  auto amount = label("", &d, "small", true);
  v->addWidget(status);
  v->addWidget(bar);
  v->addWidget(amount);
  v->addStretch();
  std::atomic_bool cancel = false;
  bool finished = false;
  auto cancelBtn = button("Cancel", [&] {
    cancel = true;
    status->setText("Canceling…");
  }, &d);
  buttonRow(v, {cancelBtn});
  d.canClose = [&] {
    if (!finished)
      cancel = true;
    return finished;
  };

  struct InstallResult {
    QString tempPath;
    QString error;
  };
  QFutureWatcher<InstallResult> watcher;
  auto future = QtConcurrent::run([&]() -> InstallResult {
    try {
      QString lastPhase;
      QElapsedTimer phaseClock;
      phaseClock.start();
      Transfer::Meter meter;
      auto formatBytes = [](qint64 bytes) -> QString {
        if (bytes >= 1048576)
          return QString::number(double(bytes) / 1048576, 'f', 1) + " MB";
        return QString::number(std::max<qint64>(0, bytes) / 1024) + " KB";
      };
      auto progress = [&](QString phase, qint64 complete, qint64 total) {
        if (phase != lastPhase) {
          phaseClock.restart();
          meter.reset(total, complete);
          lastPhase = phase;
        }
        double speedBps = 0.0;
        double etaSec = -1.0;
        meter.update(complete, phaseClock.elapsed() / 1000.0, speedBps, etaSec);
        QString detail = total > 0
                             ? formatBytes(complete) + " of " + formatBytes(total)
                             : formatBytes(complete);
        const QString speedText = Transfer::Meter::formatSpeed(speedBps);
        if (!speedText.isEmpty())
          detail += (detail.isEmpty() ? "" : " · ") + speedText;
        const QString etaText = Transfer::Meter::formatEta(etaSec);
        if (!etaText.isEmpty())
          detail += (detail.isEmpty() ? "" : " · ") + etaText;
        const QString phaseText = phase == "Checking download"
                                      ? "Verifying SHA-256 checksum"
                                      : "Downloading update";
        QMetaObject::invokeMethod(
            &d,
            [=, &cancel] {
              if (cancel)
                return;
              status->setText(
                  phaseText +
                  (total > 0 ? QString("… %1%").arg(complete * 100 / total)
                             : "…"));
              amount->setText(detail);
              bar->setValue(total > 0 ? double(complete) / total : -1);
            },
            Qt::QueuedConnection);
      };
      return {Updater::downloadAndVerify(info, targetPath, cancel, progress), {}};
    } catch (const std::exception &e) {
      return {{}, QString::fromUtf8(e.what())};
    } catch (...) {
      return {{}, QStringLiteral("The update download failed.")};
    }
  });
  QObject::connect(&watcher, &QFutureWatcher<InstallResult>::finished, &d, [&] {
    finished = true;
    d.done();
  });
  watcher.setFuture(future);
  d.exec();
  if (!finished) {
    cancel = true;
    future.waitForFinished();
  }
  const InstallResult res = future.result();
  if (cancel)
    return;
  if (!res.error.isEmpty())
    fail(res.error);
  if (!engines.isEmpty()) {
    QFile::remove(res.tempPath);
    fail("A Mac was started while downloading. Shut down all Macs and try again.");
  }
  Updater::applyVerifiedUpdateAndRestart(res.tempPath, targetPath);
}
PWindow *App::guest(Engine *e) {
  if (guests.contains(e->machine.id()))
    return guests[e->machine.id()];
  const auto display = this->screen();
  const qreal dpi = display ? display->devicePixelRatio() : devicePixelRatioF();
  QSettings viewSettings(Model::dataDirectory() + "/view.ini", QSettings::IniFormat);
  const auto &config = e->machine.config;
  const bool ppc = e->machine.family() == "powerMacG4";
  const bool followsWindow =
      ppc || (e->machine.family() == "quadra800" &&
              config["useEnhancedFramebuffer"].toBool());
  const QString scaleKey = "display/" + e->machine.id() + "/scale";
  const int savedScale = std::clamp(viewSettings.value(scaleKey, followsWindow ? 0 : 3).toInt(), 0, 3);
  int zoom = savedScale == 1 ? 1 : savedScale == 2 ? 2 : NativeDisplay::preferredZoom(dpi);
  const QSize source(config["width"].toInt(), config["height"].toInt());
  auto contentSizeFor = [&](int factor) {
    if (savedScale == 3)
      return source;
    return QSize(qCeil(source.width() * factor / dpi),
                 qCeil(source.height() * factor / dpi));
  };
  const int chromeW = 13, chromeH = 29;
  while (zoom > 1 && display) {
    const auto cs = contentSizeFor(zoom);
    if (cs.width() + chromeW <= display->availableGeometry().width() - 8 &&
        cs.height() + chromeH + 92 <= display->availableGeometry().height() - 8)
      break;
    --zoom;
  }
  auto w = new PWindow(e->machine.name(),
                       contentSizeFor(zoom) + QSize(chromeW, chromeH + 63),
                       this);
  w->contentOnlyFullScreen = true;
  w->canClose = [this, guard = QPointer<Engine>(e)] {
    if (!guard || !guard->running) return true;
    QTimer::singleShot(0, this, [this, guard] {
      if (guard && guard->running) showPowerActions(guard);
    });
    return false;
  };
  w->setMinimumSize(480, 240);
  auto v = new QVBoxLayout(w->body);
  v->setContentsMargins(0, 0, 0, 0);
  v->setSpacing(0);
  auto screen = new GuestView(&e->vnc, w->body);
  screen->helpers = config["classicInputHelpers"].toBool();
  screen->swapAltCommand = config["swapAltCommand"].toBool();
  screen->legacyMouse = config["legacyMouseTracking"].toBool();
  screen->mouseDpi = config["mouseDPI"].toInt();
  screen->followsWindow = followsWindow;
  screen->widthAlignment = ppc ? 8 : 1;
  if (ppc && config["graphicsCard"].toString() == "atiRadeon" && !config["bootFromCD"].toBool()) {
    screen->minimumResolution = {640, 480};
    screen->maximumResolution = {1920, 1440};
  }
  screen->fitWindow = savedScale == 3;
  screen->pixelZoom = savedScale == 1 ? 1 : savedScale == 2 ? 2 : 0;
  v->addWidget(screen, 1);
  auto footer = new QWidget(w->body);
  auto footerLayout = new QVBoxLayout(footer);
  footerLayout->setContentsMargins(0, 6, 0, 0);
  footerLayout->setSpacing(6);
  auto displayRow = new QHBoxLayout;
  displayRow->setContentsMargins(8, 0, 24, 0);
  auto status = label("Starting up…", w, "small");
  status->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Fixed);
  displayRow->addWidget(status, 1);
  auto scale = new PCombo(w);
  scale->setAccessibleName("Guest display scale");
  scale->addItems({"Sharp Pixels", "1× Pixels", "2× Pixels", "Fit Window"});
  scale->setCurrentIndex(savedScale);
  scale->setFixedWidth(126);
  displayRow->addWidget(scale);
  footerLayout->addLayout(displayRow);
  auto updateDisplayStatus = [guard = QPointer<Engine>(e),
                              screen = QPointer<GuestView>(screen),
                              status = QPointer<PLabel>(status)] {
    if (!guard || !screen || !status) return;
    QString s = guard->vnc.frame.isNull()
                    ? "Starting up…"
                    : QString("%1 × %2")
                          .arg(guard->vnc.frame.width())
                          .arg(guard->vnc.frame.height());
    if (guard->paused)
      s += " · Paused";
    if (!screen->followsWindow)
      s += " · Fixed resolution";
    if (!screen->resizeNotice.isEmpty())
      s += " · " + screen->resizeNotice;
    else if (guard->vnc.relative && !guard->paused)
      s += screen->captured ? " · Escape or Ctrl+Alt releases mouse"
                            : " · Click to capture mouse";
    if (status->value != s)
      status->setText(s);
  };
  connect(scale, qOverload<int>(&QComboBox::currentIndexChanged), screen,
          [screen = QPointer<GuestView>(screen), scaleKey](int mode) {
            if (!screen) return;
            screen->fitWindow = mode == 3;
            screen->pixelZoom = mode == 1 ? 1 : mode == 2 ? 2 : 0;
            screen->scheduleResize();
            screen->update();
            QSettings settings(Model::dataDirectory() + "/view.ini",
                               QSettings::IniFormat);
            settings.setValue(scaleKey, mode);
          });
  connect(screen, &GuestView::displayChanged, w, updateDisplayStatus);
  connect(screen, &GuestView::captureChanged, w, updateDisplayStatus);
  connect(&e->vnc, &VNC::changed, w, updateDisplayStatus);
  auto toolbar = new QWidget(footer);
  auto controls = new PFlowLayout(toolbar);
  auto pause = button(
      "Pause",
      [guard = QPointer<Engine>(e), p = QPointer<PWindow>(w)] {
        if (!guard) return;
        if (guard->paused)
          guard->resume();
        else
          guard->pause();
        if (p)
          p->update();
      },
      w);
  controls->addWidget(pause);
  controls->addWidget(button(
      "Power…",
      [this, guard = QPointer<Engine>(e)] {
        if (guard && guard->running) showPowerActions(guard);
      },
      w));
  auto paste = button(
      "Paste…",
      [this, guard = QPointer<Engine>(e), pw = QPointer<PWindow>(w)] {
        if (guard && guard->running && !guard->paused) showPaste(guard, pw);
      },
      w);
  controls->addWidget(paste);
  controls->addWidget(button(
      "Media…",
      [this, guard = QPointer<Engine>(e), pw = QPointer<PWindow>(w)] {
        if (!guard) return;
        for (int i = 0; i < machines.size(); ++i)
          if (machines[i].id() == guard->machine.id()) { select(i); break; }
        showMedia(pw);
      },
      w));
  auto fullScreen = new PButton("Full Screen", w);
  auto syncFullScreen = [pw = QPointer<PWindow>(w),
                         screen = QPointer<GuestView>(screen),
                         fullScreen = QPointer<PButton>(fullScreen),
                         footer = QPointer<QWidget>(footer)] {
    if (!pw || !screen || !fullScreen || !footer) return;
    const bool fs = pw->isFullScreen();
    footer->setVisible(!fs);
    fullScreen->setText(fs ? "Window" : "Full Screen");
    if (!fs)
      screen->hideFullScreenHint();
  };
  auto toggleFullScreen = [pw = QPointer<PWindow>(w),
                           screen = QPointer<GuestView>(screen),
                           syncFullScreen] {
    if (!pw || !screen) return;
    screen->release();
    if (pw->isFullScreen()) {
      screen->hideFullScreenHint();
      pw->showNormal();
    } else {
      pw->showFullScreen();
      screen->showFullScreenHint();
    }
    syncFullScreen();
    screen->setFocus();
  };
  connect(w, &PWindow::windowStateChanged, w, syncFullScreen);
  connect(fullScreen, &PButton::clicked, w, toggleFullScreen);
  connect(screen, &GuestView::toggleFullScreen, w, toggleFullScreen);
  auto shortcut = new QShortcut(QKeySequence("Alt+Return"), w);
  connect(shortcut, &QShortcut::activated, w, toggleFullScreen);
  auto fsShortcut = new QShortcut(QKeySequence("Ctrl+Alt+F"), w);
  connect(fsShortcut, &QShortcut::activated, w, toggleFullScreen);
  controls->addWidget(fullScreen);
  footerLayout->addWidget(toolbar);
  v->addWidget(footer);
  w->ensurePolished();
  auto measureFooter = [&](int contentW) {
    footerLayout->activate();
    const int fh = footerLayout->hasHeightForWidth()
                       ? footerLayout->heightForWidth(contentW)
                       : footer->sizeHint().height();
    return std::max(fh, 63);
  };
  while (zoom > 1 && display) {
    const auto cs = contentSizeFor(zoom);
    const int fh = measureFooter(cs.width());
    if (cs.width() + chromeW <= display->availableGeometry().width() - 8 &&
        cs.height() + chromeH + fh <= display->availableGeometry().height() - 8)
      break;
    --zoom;
  }
  const auto finalContent = contentSizeFor(zoom);
  const QSize finalSize(finalContent.width() + chromeW,
                        finalContent.height() + chromeH +
                            measureFooter(finalContent.width()));
  w->resize(finalSize);
  w->expanded = finalSize;
  w->fitToScreen();
  guests[e->machine.id()] = w;
  connect(e, &Engine::changed, w,
          [guard = QPointer<Engine>(e), pause, paste,
           screen = QPointer<GuestView>(screen), updateDisplayStatus] {
            if (!guard) return;
            const bool stopping = guard->stoppingRequested();
            pause->setText(guard->paused ? "Resume" : "Pause");
            pause->setEnabled(!stopping);
            paste->setEnabled(!guard->paused && !stopping);
            if (screen) {
              bool wasPaused = screen->paused;
              screen->paused = guard->paused;
              if (screen->paused)
                screen->release();
              if (wasPaused && !screen->paused)
                screen->scheduleResize();
            }
            updateDisplayStatus();
          });
  connect(&e->vnc, &VNC::problem, this, [this](QString s) {
    QTimer::singleShot(0, this, [this, s] { problem(this, s); });
  });
  const QString machineId = e->machine.id();
  auto previewClock = std::make_shared<QElapsedTimer>();
  connect(&e->vnc, &VNC::changed, this,
          [this, guard = QPointer<Engine>(e), machineId, previewClock] {
            if (!guard) return;
            const bool firstFrame = !livePreviews.contains(machineId);
            if (isUsefulPreview(guard->vnc.frame) &&
                (firstFrame || !previewClock->isValid() ||
                 previewClock->elapsed() > 1000)) {
              previewClock->restart();
              livePreviews[machineId] = guard->vnc.frame.scaled(
                  400, 300, Qt::KeepAspectRatio, Qt::FastTransformation);
            }
            if (auto *cur = current(); cur && cur->id() == machineId)
              preview->update();
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
  connect(e, &Engine::exited, this,
          [this, id, guard = QPointer<Engine>(e)](bool restart, QString error) {
    QImage finalPreview;
    QString machineFolder;
    if (guard) {
      machineFolder = guard->machine.folder;
      if (isUsefulPreview(guard->vnc.frame))
        finalPreview = guard->vnc.frame.scaled(
            400, 300, Qt::KeepAspectRatio, Qt::FastTransformation);
      for (auto &machine : machines)
        if (machine.id() == id) {
          machine.config = guard->machine.config;
          break;
        }
    }
    if (finalPreview.isNull())
      finalPreview = livePreviews.take(id);
    else
      livePreviews.remove(id);
    if (!finalPreview.isNull() && !machineFolder.isEmpty() &&
        QDir(machineFolder).exists()) {
      QSaveFile file(machineFolder + "/preview.png");
      if (file.open(QFile::WriteOnly) && finalPreview.save(&file, "PNG"))
        file.commit();
    }
    engines.remove(id);
    if (auto w = guests.take(id)) {
      w->hide();
      w->deleteLater();
    }
    if (guard)
      guard->deleteLater();
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
void App::settingsSaved(QJsonObject c) {
  auto m = current();
  if (!m || currentEngine())
    fail("Shut down this Mac before changing its settings.");
  if (c["id"].toString() != m->id())
    fail("The selected Mac changed. Close this window and select the Mac again before saving.");
  c = Model::validate(c);
  Model::writeJSON(m->folder + "/config.json", c);
  m->config = c;
  render();
}
void App::showEditor(bool create) {
  static QPointer<PDialog> newMacDialog;
  static QMap<QString, QPointer<PDialog>> infoDialogs;
  if (create && newMacDialog) {
    newMacDialog->show();
    newMacDialog->raise();
    newMacDialog->activateWindow();
    return;
  }
  auto m = current();
  if (!create && !m)
    return;
  if (!create) {
    if (auto existing = infoDialogs.value(m->id())) {
      existing->show();
      existing->raise();
      existing->activateWindow();
      return;
    }
    if (currentEngine())
      fail("Shut down this Mac before changing its settings.");
  }
  const QString machineId = m ? m->id() : QString();
  PDialog d(create ? "New Mac" : "Get Info", {590, create ? 340 : 700}, this);
  if (create)
    newMacDialog = &d;
  else
    infoDialogs[machineId] = &d;
  auto v = dialogLayout(&d);
  auto form = new QGridLayout;
  form->setHorizontalSpacing(12);
  form->setVerticalSpacing(10);
  auto c = create ? Model::freshConfig("Mac OS 9") : m->config;
  const QString fam = !create ? m->family() : QString();
  const bool osx = !create && fam == "powerMacOSX";
  const bool ppc = !create && fam == "powerMacG4";
  const bool quadra = !create && fam == "quadra800";
  const bool copland = !create && fam == "powerMac7500";
  auto name = new PEdit(c["name"].toString(), &d);
  name->setAccessibleName("Mac name");
  name->setObjectName("machineName");
  form->addWidget(label("Name:", &d), 0, 0);
  form->addWidget(name, 0, 1);
  auto family = new PCombo(&d);
  auto capacity = new PCombo(&d);
  auto ram = new PEdit(QString::number(c["ramMB"].toInt()), &d);
  ram->setAccessibleName("Memory in megabytes");
  auto resolution = new PCombo(&d);
  auto customBox = new QWidget(&d);
  auto customRow = new QHBoxLayout(customBox);
  customRow->setContentsMargins(0, 0, 0, 0);
  customRow->setSpacing(6);
  auto customW = new PEdit(QString::number(c["width"].toInt()), customBox);
  customW->setAccessibleName("Custom screen width");
  customW->setMaximumWidth(80);
  auto customH = new PEdit(QString::number(c["height"].toInt()), customBox);
  customH->setAccessibleName("Custom screen height");
  customH->setMaximumWidth(80);
  customRow->addWidget(customW);
  customRow->addWidget(label("×", customBox));
  customRow->addWidget(customH);
  customRow->addStretch();
  customBox->hide();
  auto depth = new PCombo(&d);
  auto graphics = new PCombo(&d);
  QString cdPath;
  QString sharedPath = c["sharedFolderPath"].toString();
  const int minRam = quadra ? 16 : osx ? 256 : 32;
  const int maxRam = quadra ? 1000 : osx ? 2048 : 896;

  auto populateResolutions = [&](bool enhanced, int curW, int curH) {
    QSignalBlocker block(resolution);
    resolution->clear();
    const QStringList presets =
        (quadra && !enhanced)
            ? QStringList{"640x480", "800x600", "1152x870"}
            : QStringList{"640x480", "800x600", "1024x768", "1152x870",
                          "1280x960", "1600x1200", "1920x1080", "Custom…"};
    for (const auto &s : presets)
      resolution->addItem(s);
    const QString cur = QString("%1x%2").arg(curW).arg(curH);
    const int idx = resolution->findText(cur);
    if (idx >= 0 && cur != "Custom…") {
      resolution->setCurrentIndex(idx);
      customBox->hide();
    } else if (quadra && !enhanced) {
      resolution->setCurrentText("800x600");
      customBox->hide();
    } else {
      resolution->setCurrentText("Custom…");
      customW->setText(QString::number(curW));
      customH->setText(QString::number(curH));
      customBox->show();
    }
  };
  auto populateDepths = [&](bool enhanced, int width, int desired) {
    QSignalBlocker block(depth);
    depth->clear();
    QList<int> allowed;
    if (ppc)
      allowed = {16, 32};
    else if (quadra && !enhanced && width >= 1152)
      allowed = {8};
    else
      allowed = {1, 2, 4, 8, 16, 32};
    for (int n : allowed)
      depth->addItem(n == 1    ? "Black & White"
                     : n == 16 ? "Thousands"
                     : n == 32 ? "Millions"
                               : QString::number(1 << n),
                     n);
    int idx = depth->findData(desired);
    if (idx < 0)
      idx = depth->findData(allowed.contains(16) ? 16 : allowed.last());
    if (idx >= 0)
      depth->setCurrentIndex(idx);
  };

  QString lastAutoName = c["name"].toString();
  bool nameEdited = false;
  if (create) {
    ram->hide();
    resolution->hide();
    depth->hide();
    graphics->hide();
    family->addItem("Power Mac G4", "powerMacG4");
    family->addItem("Power Mac G4 — Mac OS X", "powerMacOSX");
    family->addItem("Quadra 800", "quadra800");
    for (int n : {1, 2, 4, 8, 16, 32, 64, 120})
      capacity->addItem(QString::number(n) + " GB", n);
    capacity->setCurrentIndex(capacity->findData(32));
    form->addWidget(label("Model:", &d), 1, 0);
    form->addWidget(family, 1, 1);
    form->addWidget(label("Hard disk:", &d), 2, 0);
    form->addWidget(capacity, 2, 1);
    auto cdRow = new QHBoxLayout;
    cdRow->setSpacing(6);
    auto cdLabel = label("None (blank hard disk)", &d, "small", true);
    auto clearCD = button(
        "Clear",
        [&] {
          cdPath.clear();
          cdLabel->setText("None (blank hard disk)");
          cdLabel->setToolTip({});
        },
        &d);
    clearCD->setEnabled(false);
    auto chooseCD = button(
        "Choose CD…",
        [&] {
          auto path = QFileDialog::getOpenFileName(
              &d, "Choose an installation CD image", {},
              "CD images (*.iso *.img *.toast *.cdr)");
          if (!path.isEmpty()) {
            cdPath = path;
            cdLabel->setText(QFileInfo(path).fileName());
            cdLabel->setToolTip(QDir::toNativeSeparators(path));
            clearCD->setEnabled(true);
          }
        },
        &d);
    connect(clearCD, &PButton::clicked, &d,
            [&] { clearCD->setEnabled(!cdPath.isEmpty()); });
    cdRow->addWidget(chooseCD);
    cdRow->addWidget(clearCD);
    cdRow->addWidget(cdLabel, 1);
    form->addWidget(label("Install CD:", &d), 3, 0);
    form->addLayout(cdRow, 3, 1);
    connect(name, &PEdit::textEdited, &d, [&](const QString &) {
      const auto trimmed = name->text().trimmed();
      nameEdited = !trimmed.isEmpty() && trimmed != lastAutoName;
    });
    connect(family, qOverload<int>(&QComboBox::currentIndexChanged), &d,
            [&](int) {
              const auto newFam = family->currentData().toString();
              const int defaultDisk = newFam == "quadra800" ? 2 : 32;
              const int idx = capacity->findData(defaultDisk);
              if (idx >= 0)
                capacity->setCurrentIndex(idx);
              const QString defName = newFam == "quadra800"    ? "Quadra 800"
                                      : newFam == "powerMacOSX" ? "Mac OS X"
                                                                : "Mac OS 9";
              if (!nameEdited || name->text().trimmed().isEmpty()) {
                lastAutoName = defName;
                name->setText(defName);
                nameEdited = false;
              }
            });
  } else {
    family->hide();
    capacity->hide();
    form->addWidget(label("Memory (MB):", &d), 1, 0);
    auto ramRow = new QHBoxLayout;
    ramRow->setSpacing(8);
    ramRow->addWidget(ram);
    ramRow->addWidget(
        label(QString("%1–%2 MB").arg(minRam).arg(maxRam), &d, "small"));
    ramRow->addStretch();
    form->addLayout(ramRow, 1, 1);
    auto screenCol = new QVBoxLayout;
    screenCol->setContentsMargins(0, 0, 0, 0);
    screenCol->setSpacing(6);
    screenCol->addWidget(resolution);
    screenCol->addWidget(customBox);
    form->addWidget(label("Screen:", &d), 2, 0, Qt::AlignTop);
    form->addLayout(screenCol, 2, 1);
    const bool enhanced = c["useEnhancedFramebuffer"].toBool();
    populateResolutions(enhanced, c["width"].toInt(), c["height"].toInt());
    graphics->setAccessibleName("Graphics card");
    graphics->addItem("Standard", "standard");
    graphics->addItem("ATI Radeon (3D acceleration)", "atiRadeon");
    graphics->setCurrentIndex(graphics->findData(c["graphicsCard"].toString()));
    graphics->setEnabled(ppc);
    if (osx) {
      depth->hide();
      graphics->hide();
    } else {
      populateDepths(enhanced, c["width"].toInt(), c["depth"].toInt());
      form->addWidget(label("Colors:", &d), 3, 0);
      form->addWidget(depth, 3, 1);
      form->addWidget(label("Graphics:", &d), 4, 0);
      form->addWidget(graphics, 4, 1);
    }
    if (Model::supportsHostSharing() && !osx && !copland) {
      auto shareRow = new QHBoxLayout;
      shareRow->setSpacing(6);
      auto shareLabel = label("", &d, "small", true);
      auto turnOffShare = button("Turn Off", nullptr, &d);
      auto syncShare = [&] {
        const auto folderName = QFileInfo(sharedPath).fileName();
        shareLabel->setText(
            sharedPath.isEmpty()
                ? "Off"
                : (folderName.isEmpty()
                       ? QDir::toNativeSeparators(sharedPath)
                       : folderName));
        shareLabel->setToolTip(QDir::toNativeSeparators(sharedPath));
        turnOffShare->setEnabled(!sharedPath.isEmpty());
      };
      connect(turnOffShare, &PButton::clicked, &d, [&] {
        sharedPath.clear();
        syncShare();
      });
      auto chooseShare = button(
          "Choose…",
          [&] {
            auto path = QFileDialog::getExistingDirectory(
                &d, "Share a Folder with This Mac",
                sharedPath.isEmpty() ? Model::documents() : sharedPath);
            if (!path.isEmpty()) {
              sharedPath = path;
              syncShare();
            }
          },
          &d);
      syncShare();
      shareRow->addWidget(chooseShare);
      shareRow->addWidget(turnOffShare);
      shareRow->addWidget(shareLabel, 1);
      form->addWidget(label("Sharing:", &d), osx ? 3 : 5, 0);
      form->addLayout(shareRow, osx ? 3 : 5, 1);
    }
  }
  v->addLayout(form);
  if (create)
    v->addWidget(label("This creates a blank Mac. Use Library for a ready-to-run system, "
                       "or choose an installation CD above or in Media later.",
                       &d, "regular", true));
  if (ppc)
    v->addWidget(label("Standard and ATI Radeon graphics change the Mac's resolution with the window. ATI requires the guest's ATI display software.",
                       &d, "small", true));
  if (quadra)
    v->addWidget(label("Enhanced Quadra video changes the Mac's resolution with the window. Sharp Pixels keeps complete pixels; built-in video uses a fixed resolution.",
                       &d, "small", true));
  QMap<QString, PButton *> checks;
  PCombo *printerCombo = nullptr;
  PEdit *mouseDpiEdit = nullptr;
  if (!create) {
    for (auto pair : QList<QPair<QString, QString>>{
             {"toolsCDInserted", "ClassicMac Tools CD"},
             {"useG4CPU", "PowerPC G4 CPU (off for Mac OS 8.5–8.6)"},
             {"useEnhancedFramebuffer", "Enhanced Quadra framebuffer"},
             {"sound", "Sound"},
             {"networking", "Networking"},
             {"printingEnabled", "Enable virtual printer (off by default)"},
             {"tabletInput", "Tablet pointer"},
             {"legacyMouseTracking", "Native classic mouse tracking"},
             {"classicInputHelpers",
              "Right click as Control-click; scroll as arrow keys"},
             {"swapAltCommand", "Swap Alt and Command keys"},
             {"bootFromCD", "Start from inserted CD"},
             {"bootFromFloppy", "Start from inserted floppy"}}) {
      auto check = new PButton(pair.second, &d);
      check->toggleButton = true;
      check->setCheckable(true);
      check->setChecked(c[pair.first].toBool());
      checks[pair.first] = check;
      if ((osx && (pair.first == "toolsCDInserted" || pair.first == "classicInputHelpers")) ||
          (pair.first == "useG4CPU" && !ppc) ||
          (pair.first == "printingEnabled" && copland) ||
          ((pair.first == "useEnhancedFramebuffer" || pair.first == "bootFromFloppy") && !quadra)) {
        check->hide();
        continue;
      }
      v->addWidget(check);
    }
    if (!copland) {
      auto printRow = new QHBoxLayout;
      printRow->setSpacing(6);
      printRow->addWidget(label("Printer:", &d));
      printerCombo = new PCombo(&d);
      printerCombo->setObjectName("printerCombo");
      printerCombo->setAccessibleName("Printer");
      auto populatePrinters = [printerCombo](const QString &desired) {
        QSignalBlocker block(printerCombo);
        printerCombo->clear();
        const auto printers = Printer::availablePrinters();
        QString defName;
        for (const auto &p : printers) {
          if (p.isDefault) {
            defName = p.displayName.isEmpty() ? p.queueName : p.displayName;
            break;
          }
        }
        if (defName.isEmpty())
          defName = Printer::defaultPrinterName();
        printerCombo->addItem(
            defName.isEmpty()
                ? QStringLiteral("System Default")
                : QStringLiteral("System Default (%1)").arg(defName),
            QString());
        for (const auto &p : printers) {
          const QString text =
              (p.displayName.isEmpty() || p.displayName == p.queueName)
                  ? p.queueName
                  : QStringLiteral("%1 (%2)").arg(p.displayName, p.queueName);
          printerCombo->addItem(text, p.queueName);
        }
        printerCombo->addItem(QStringLiteral("Save as PDF (Printed PDFs)"),
                              QStringLiteral("pdf"));
        if (!desired.isEmpty() && printerCombo->findData(desired) < 0)
          printerCombo->addItem(desired + QStringLiteral(" (saved)"), desired);
        const int idx = printerCombo->findData(desired);
        printerCombo->setCurrentIndex(idx >= 0 ? idx : 0);
      };
      populatePrinters(c["printerName"].toString());
      printRow->addWidget(printerCombo, 1);
      auto refreshPrinters = button(
          "Refresh",
          [printerCombo, populatePrinters] {
            populatePrinters(printerCombo->currentData().toString());
          },
          &d);
      refreshPrinters->setObjectName("refreshPrintersButton");
      printRow->addWidget(refreshPrinters);
      auto testPageBtn = button(
          "Test Page",
          [&, printerCombo] {
            const QString target = printerCombo->currentData().toString();
            const QString labelText =
                target == QStringLiteral("pdf")
                    ? QStringLiteral("Print to PDF")
                    : target.isEmpty()
                          ? (Printer::defaultPrinterName().isEmpty()
                                 ? QStringLiteral("System Default")
                                 : Printer::defaultPrinterName())
                          : target;
            const QByteArray ps = Printer::generateTestPostScript(
                labelText, QStringLiteral(CLASSICMAC_VERSION));
            QString err, savedPath;
            if (!Printer::submitJob(ps, QStringLiteral("ClassicMac Test Page"),
                                    target, Printer::defaultPdfDir(*m), &err,
                                    &savedPath)) {
              fail(err);
            }
            PDialog okDlg("Test Page", {440, 170}, &d);
            auto ov = dialogLayout(&okDlg);
            ov->addWidget(label(
                savedPath.isEmpty()
                    ? QStringLiteral("Test page sent to printer.")
                    : QStringLiteral("Test page saved to:\n") +
                          QDir::toNativeSeparators(savedPath),
                &okDlg, "regular", true));
            ov->addStretch();
            buttonRow(ov, {button("OK", [&] { okDlg.done(); }, &okDlg, true)});
            okDlg.exec();
          },
          &d);
      testPageBtn->setObjectName("testPrintPageButton");
      printRow->addWidget(testPageBtn);
      v->addLayout(printRow);
#ifdef Q_OS_WIN
      const QString guestAddr = QStringLiteral("10.0.2.2");
#else
      const QString guestAddr = QStringLiteral("10.0.2.4");
#endif
      v->addWidget(label(
          QStringLiteral("Guest printer address: %1 · LPD queue: classicmac · "
                         "IPP: 631 · JetDirect: 9100")
              .arg(guestAddr),
          &d, "small", true));
    }
    auto dpiRow = new QHBoxLayout;
    dpiRow->setSpacing(8);
    dpiRow->addWidget(label("Mouse DPI:", &d));
    mouseDpiEdit = new PEdit(
        c["mouseDPI"].toInt() > 0 ? QString::number(c["mouseDPI"].toInt())
                                   : QString(),
        &d);
    mouseDpiEdit->setObjectName("mouseDpiEdit");
    mouseDpiEdit->setAccessibleName("Mouse DPI");
    mouseDpiEdit->setPlaceholderText("Auto");
    mouseDpiEdit->setMaximumWidth(100);
    dpiRow->addWidget(mouseDpiEdit);
    dpiRow->addWidget(label("Auto or 100–100000", &d, "small"));
    dpiRow->addStretch();
    v->addLayout(dpiRow);
    v->addWidget(label(
        "Turn off Tablet pointer for relative mouse input (games, or USB "
        "Overdrive on Mac OS 8.5–9.2.2). Native classic mouse tracking scales "
        "raw host motion using Mouse DPI.",
        &d, "small", true));
    if (quadra && checks.contains("bootFromFloppy")) {
      checks["bootFromFloppy"]->setEnabled(
          !c["floppyImagePath"].toString().isEmpty());
      connect(checks["bootFromCD"], &PButton::toggled, &d, [&](bool on) {
        if (on)
          checks["bootFromFloppy"]->setChecked(false);
      });
      connect(checks["bootFromFloppy"], &PButton::toggled, &d, [&](bool on) {
        if (on)
          checks["bootFromCD"]->setChecked(false);
      });
    }
    auto selectedWidth = [&] {
      if (resolution->currentText() == "Custom…")
        return customW->text().toInt();
      return resolution->currentText().section('x', 0, 0).toInt();
    };
    connect(resolution, qOverload<int>(&QComboBox::currentIndexChanged), &d,
            [&](int) {
              const bool isCustom = resolution->currentText() == "Custom…";
              customBox->setVisible(isCustom);
              if (!isCustom) {
                auto parts = resolution->currentText().split('x');
                if (parts.size() == 2) {
                  customW->setText(parts[0]);
                  customH->setText(parts[1]);
                }
              }
              if (quadra && checks.contains("useEnhancedFramebuffer"))
                populateDepths(checks["useEnhancedFramebuffer"]->isChecked(),
                               selectedWidth(),
                               depth->currentData().toInt());
            });
    if (quadra && checks.contains("useEnhancedFramebuffer")) {
      connect(checks["useEnhancedFramebuffer"], &PButton::toggled, &d,
              [&](bool enh) {
                int w = selectedWidth();
                int h = resolution->currentText() == "Custom…"
                            ? customH->text().toInt()
                            : resolution->currentText().section('x', 1, 1).toInt();
                populateResolutions(enh, w, h);
                populateDepths(enh, selectedWidth(), depth->currentData().toInt());
              });
      connect(customW, &PEdit::textChanged, &d, [&](const QString &) {
        if (resolution->currentText() == "Custom…")
          populateDepths(checks["useEnhancedFramebuffer"]->isChecked(),
                         selectedWidth(), depth->currentData().toInt());
      });
    }
    v->addWidget(
        label("Shut down the Mac before changing hardware or startup media.",
              &d, "small", true));
  }
  v->addStretch();
  buttonRow(v, {button("Cancel", [&] { d.done(0); }, &d),
                button(
                    create ? "Create" : "Save",
                    [&] {
                      const auto cleanName = Model::sanitizedName(name->text());
                      if (cleanName.isEmpty())
                        fail("Enter a name for this Mac.");
                      if (create) {
                        const auto newFamily = family->currentData().toString();
                        const auto diskSize = capacity->currentData().toInt();
                        auto machine = background<Model::Machine>(
                            &d, "Creating Mac",
                            [=](std::atomic_bool &, Library::Progress) {
                              return Model::createMachine(cleanName, newFamily,
                                                          diskSize,
                                                          Model::documents(),
                                                          cdPath);
                            },
                            false);
                        registerMachine(machine);
                      } else {
                        auto next = c;
                        next["name"] = cleanName;
                        bool ok = false;
                        int n = ram->text().toInt(&ok);
                        if (!ok || n < minRam || n > maxRam)
                          fail(QString("Enter memory between %1 and %2 MB.")
                                   .arg(minRam)
                                   .arg(maxRam));
                        next["ramMB"] = n;
                        int nextW = 0, nextH = 0;
                        if (resolution->currentText() == "Custom…") {
                          bool okW = false, okH = false;
                          nextW = customW->text().toInt(&okW);
                          nextH = customH->text().toInt(&okH);
                          if (!okW || !okH || nextW < 480 || nextW > 3840 ||
                              nextH < 480 || nextH > 2160)
                            fail("Enter a custom resolution between 480×480 and 3840×2160.");
                        } else {
                          auto r = resolution->currentText().split('x');
                          nextW = r.value(0).toInt();
                          nextH = r.value(1).toInt();
                        }
                        next["width"] = nextW;
                        next["height"] = nextH;
                        if (!osx)
                          next["depth"] = depth->currentData().toInt();
                        next["graphicsCard"] =
                            graphics->currentData().toString();
                        for (auto it = checks.begin(); it != checks.end(); ++it)
                          next[it.key()] = it.value()->isChecked();
                        if (printerCombo && !copland) {
                          const QString chosenPrinter =
                              printerCombo->currentData().toString().trimmed();
                          if (chosenPrinter.isEmpty())
                            next.remove("printerName");
                          else
                            next["printerName"] = chosenPrinter;
                        }
                        if (mouseDpiEdit) {
                          const QString dpiText = mouseDpiEdit->text().trimmed();
                          if (dpiText.isEmpty() ||
                              dpiText.compare("Auto", Qt::CaseInsensitive) == 0) {
                            next.remove("mouseDPI");
                          } else {
                            bool okDpi = false;
                            const int dpiVal = dpiText.toInt(&okDpi);
                            if (!okDpi || dpiVal < 100 || dpiVal > 100000)
                              fail("Enter a mouse DPI between 100 and 100000, "
                                   "or leave blank for Auto.");
                            next["mouseDPI"] = dpiVal;
                          }
                        }
                        if (Model::supportsHostSharing() && !osx && !copland) {
                          if (sharedPath.isEmpty())
                            next.remove("sharedFolderPath");
                          else {
                            next["sharedFolderPath"] = sharedPath;
                            next["sharedVolumeName"] = "Shared Files";
                          }
                        }
                        settingsSaved(next);
                      }
                      d.done();
                    },
                    &d, true)});
  d.exec();
}
void App::showMedia(QWidget *parent) {
  static QMap<QString, QPointer<PDialog>> mediaDialogs;
  auto m = current();
  if (!m)
    return;
  const QString machineId = m->id();
  if (auto existing = mediaDialogs.value(machineId)) {
    existing->show();
    existing->raise();
    existing->activateWindow();
    return;
  }
  const QString family = m->family();
  const bool osx = family == "powerMacOSX";
  const bool copland = family == "powerMac7500";
  const bool quadra = family == "quadra800";
  PDialog d("Media", {540, 460}, parent ? parent : this);
  mediaDialogs[machineId] = &d;
  auto v = dialogLayout(&d);
  v->addWidget(label(m->name(), &d));
  v->addWidget(label(
      quadra
          ? "Choose CD, floppy, or startup media for this Mac. Shut down before changing media."
          : "Choose CD or startup media for this Mac. Shut down before changing media.",
      &d, "regular", true));
  auto findMachine = [&]() -> Model::Machine * {
    for (auto &item : machines)
      if (item.id() == machineId)
        return &item;
    return nullptr;
  };
  auto ensureEditable = [&]() -> Model::Machine * {
    if (engines.contains(machineId))
      fail("Shut down this Mac before changing media.");
    auto *cur = findMachine();
    if (!cur)
      fail("This Mac is no longer in your library.");
    for (int i = 0; i < machines.size(); ++i)
      if (machines[i].id() == machineId) {
        if (selected != i)
          select(i);
        break;
      }
    return findMachine();
  };
  QList<std::function<void()>> updates;
  auto refreshMedia = [&] {
    for (auto &update : updates)
      update();
  };
  auto choose = [&](QString key, QString title, QString filters) {
    auto *cur = ensureEditable();
    auto path = QFileDialog::getOpenFileName(&d, title, cur->folder, filters);
    if (!path.isEmpty()) {
      cur = ensureEditable();
      auto c = cur->config;
      c[key] = path;
      if (key == "cdImagePath") {
        c["bootFromCD"] = true;
        c["bootFromFloppy"] = false;
      }
      settingsSaved(c);
      refreshMedia();
    }
  };
  for (auto key :
       QStringList{"cdImagePath", "floppyImagePath", "sharedFolderPath"}) {
    if (key == "floppyImagePath" && family != "quadra800")
      continue;
    if (key == "sharedFolderPath" &&
        (!Model::supportsHostSharing() || osx || copland))
      continue;
    QString title = key == "cdImagePath"       ? "CD image"
                    : key == "floppyImagePath" ? "Floppy image"
                                               : "Shared folder";
    auto mediaPath = label("", &d, "small", true);
    v->addWidget(mediaPath);
    auto row = new QHBoxLayout;
    auto chooseButton = button(
        "Choose " + title + "…",
        [&, key, title] {
          if (key == "sharedFolderPath") {
            auto *cur = ensureEditable();
            auto path = QFileDialog::getExistingDirectory(
                &d, "Share a Folder with This Mac",
                cur->config[key].toString().isEmpty()
                    ? Model::documents()
                    : cur->config[key].toString());
            if (!path.isEmpty()) {
              cur = ensureEditable();
              auto c = cur->config;
              c[key] = path;
              c["sharedVolumeName"] = "Shared Files";
              settingsSaved(c);
              refreshMedia();
            }
          } else {
            choose(key, "Choose " + title,
                   key == "cdImagePath"
                       ? "CD images (*.iso *.img *.toast *.cdr)"
                       : "Floppy images (*.img *.dsk *.ima *.raw)");
          }
        },
        &d);
    row->addWidget(chooseButton);
    auto eject = button(
        key == "sharedFolderPath" ? "Turn Off" : "Eject",
        [&, key] {
          auto *cur = ensureEditable();
          auto c = cur->config;
          c.remove(key);
          if (key == "cdImagePath")
            c["bootFromCD"] = false;
          else if (key == "floppyImagePath")
            c["bootFromFloppy"] = false;
          settingsSaved(c);
          refreshMedia();
        },
        &d);
    row->addWidget(eject);
    row->addStretch();
    v->addLayout(row);
    updates.append([&, key, title, mediaPath, chooseButton, eject] {
      auto *cur = findMachine();
      const bool running = engines.contains(machineId);
      const auto path = cur ? cur->config[key].toString() : QString();
      mediaPath->setText(
          title + ": " +
          (path.isEmpty() ? "Not inserted"
                          : (QFileInfo(path).fileName().isEmpty()
                                 ? QDir::toNativeSeparators(path)
                                 : QFileInfo(path).fileName())));
      mediaPath->setToolTip(QDir::toNativeSeparators(path));
      chooseButton->setEnabled(!running && cur);
      eject->setEnabled(!running && cur && !path.isEmpty());
    });
  }
  bool syncing = false;
  auto toolsCheck = new PButton("ClassicMac Tools CD", &d);
  toolsCheck->toggleButton = true;
  toolsCheck->setCheckable(true);
  if (osx)
    toolsCheck->hide();
  else
    v->addWidget(toolsCheck);
  auto bootCheck = new PButton("Start from inserted CD", &d);
  bootCheck->toggleButton = true;
  bootCheck->setCheckable(true);
  v->addWidget(bootCheck);
  auto floppyBootCheck = new PButton("Start from inserted floppy", &d);
  floppyBootCheck->toggleButton = true;
  floppyBootCheck->setCheckable(true);
  if (quadra)
    v->addWidget(floppyBootCheck);
  else
    floppyBootCheck->hide();
  auto toggleOption = [&](const QString &key, bool on) {
    if (syncing)
      return;
    auto *cur = ensureEditable();
    auto c = cur->config;
    c[key] = on;
    if (on && key == "bootFromCD")
      c["bootFromFloppy"] = false;
    else if (on && key == "bootFromFloppy")
      c["bootFromCD"] = false;
    settingsSaved(c);
    refreshMedia();
  };
  connect(toolsCheck, &PButton::toggled, &d,
          [&](bool on) { toggleOption("toolsCDInserted", on); });
  connect(bootCheck, &PButton::toggled, &d,
          [&](bool on) { toggleOption("bootFromCD", on); });
  connect(floppyBootCheck, &PButton::toggled, &d,
          [&](bool on) { toggleOption("bootFromFloppy", on); });
  updates.append([&] {
    syncing = true;
    auto *cur = findMachine();
    const bool running = engines.contains(machineId);
    toolsCheck->setChecked(cur && cur->config["toolsCDInserted"].toBool());
    toolsCheck->setEnabled(!running && cur);
    bootCheck->setChecked(cur && cur->config["bootFromCD"].toBool());
    bootCheck->setEnabled(!running && cur);
    floppyBootCheck->setChecked(cur && cur->config["bootFromFloppy"].toBool());
    floppyBootCheck->setEnabled(
        !running && cur && !cur->config["floppyImagePath"].toString().isEmpty());
    syncing = false;
  });
  refreshMedia();
  v->addStretch();
  auto power = button("Power…", [&] {
    d.done();
    if (auto engine = engines.value(machineId))
      showPowerActions(engine);
  }, &d);
  power->setVisible(engines.contains(machineId));
  QTimer mediaTimer;
  connect(&mediaTimer, &QTimer::timeout, &d, [&] {
    refreshMedia();
    power->setVisible(engines.contains(machineId));
  });
  mediaTimer.start(250);
  buttonRow(v, {power, button("Done", [&] { d.done(); }, &d, true)});
  d.exec();
}
void App::showPaste(Engine *e, QWidget *parent) {
  static QMap<QString, QPointer<PDialog>> pasteDialogs;
  QPointer<Engine> target(e);
  if (!target)
    return;
  const QString machineId = target->machine.id();
  if (auto existing = pasteDialogs.value(machineId)) {
    existing->show();
    existing->raise();
    existing->activateWindow();
    return;
  }
  PDialog d("Paste Text into Mac", {590, 400}, parent ? parent : this);
  pasteDialogs[machineId] = &d;
  auto v = dialogLayout(&d);
  v->addWidget(label("Focus the destination in Mac OS first. The Mac must use "
                     "its US keyboard layout.",
                     &d, "regular", true));
  auto edit = new QPlainTextEdit(&d);
  edit->setFrameShape(QFrame::Box);
  edit->setLineWidth(2);
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
    if (busy) {
      cancel = true;
      status->setText("Cancelling…");
      return false;
    }
    return true;
  };
  PButton *send = nullptr;
  auto syncControls = [&] {
    const bool running = target && target->running;
    const bool paused = running && target->paused;
    const bool editable = running && !paused && !busy;
    if (send)
      send->setEnabled(editable);
    edit->setEnabled(editable);
    slow->setEnabled(editable);
    if (!running)
      status->setText("This Mac has shut down.");
    else if (paused)
      status->setText("Resume this Mac before pasting text.");
    else if (!busy && status->value == "Resume this Mac before pasting text.")
      status->setText("");
  };
  send = button(
      "Paste Text",
      [&] {
        if (!target || !target->running)
          fail("This Mac has shut down. Start it again before pasting text.");
        if (target->paused)
          fail("Resume this Mac before pasting text.");
        busy = true;
        cancel = false;
        syncControls();
        auto guard = qScopeGuard([&] {
          busy = false;
          syncControls();
        });
        const QString textPayload = edit->toPlainText();
        target->paste(textPayload, slow->isChecked(), cancel,
                      [&](int n, int total) {
                        status->setText(
                            QString("Typed %1 of %2 characters.")
                                .arg(n)
                                .arg(total));
                      });
        status->setText(cancel ? "Cancelled." : "Finished.");
      },
      &d, true);
  syncControls();
  connect(target, &Engine::changed, &d, syncControls);
  connect(target, &Engine::exited, &d, [&] {
    cancel = true;
    syncControls();
  });
  buttonRow(v, {button(
                    "Cancel",
                    [&] {
                      cancel = true;
                      if (!busy)
                        d.done();
                      else
                        status->setText("Cancelling…");
                    },
                    &d),
                send});
  d.exec();
}
void App::showSetup() {
  if (settingUp)
    return;
  settingUp = true;
  auto guard = qScopeGuard([this] { settingUp = false; });
  PDialog d("Welcome to ClassicMac", {660, 560}, this);
  auto v = dialogLayout(&d);
  auto headerRow = new QHBoxLayout;
  headerRow->setSpacing(12);
  auto appIcon = new QLabel(&d);
  appIcon->setFixedSize(40, 40);
  appIcon->setPixmap(QPixmap::fromImage(
      QImage(":/app.png").scaled(40, 40, Qt::KeepAspectRatio,
                                 Qt::SmoothTransformation)));
  headerRow->addWidget(appIcon);
  auto heading = label("Your first classic Mac", &d);
  headerRow->addWidget(heading, 1);
  v->addLayout(headerRow);
  auto stack = new QStackedWidget(&d);
  v->addWidget(stack, 1);
  QList<QWidget *> pages;
  for (int i = 0; i < 4; ++i) {
    auto p = new QWidget;
    stack->addWidget(p);
    pages << p;
  }
  auto welcome = new QVBoxLayout(pages[0]);
  welcome->setSpacing(16);
  welcome->addWidget(label("Set up a Mac", pages[0]));
  welcome->addWidget(
      label("Download a prepared Mac from the Library, bring your own "
            "installation CD, or open a .classic machine you already have.",
            pages[0], "regular", true));
  auto licenseGroup = new PGroup("Software License", pages[0]);
  auto licenseBox = new QVBoxLayout(licenseGroup);
  licenseBox->setContentsMargins(12, 24, 12, 12);
  auto license = new PButton(
      "I have the right to use the Mac OS software I choose.", licenseGroup);
  license->toggleButton = true;
  license->setCheckable(true);
  licenseBox->addWidget(license);
  welcome->addWidget(licenseGroup);
  auto openRow = new QHBoxLayout;
  openRow->addWidget(button(
      "Open an existing Mac…",
      [&] {
        importMachine();
        if (current())
          d.done();
      },
      pages[0]));
  openRow->addStretch();
  welcome->addLayout(openRow);
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
      label("Choose System 7.5.5, Mac OS 8.1, 8.6, Mac OS 9, or Mac OS X.",
            pages[1], "regular", true));
  sourceLayout->addWidget(installer);
  sourceLayout->addWidget(label(
      "Create a blank Mac and install from your ISO, IMG, Toast or CDR image.",
      pages[1], "regular", true));
  sourceLayout->addStretch();
  auto fields = new QVBoxLayout(pages[2]);
  auto editions = new PCombo(pages[2]);
  auto model = new PCombo(pages[2]);
  model->addItem("Power Mac G4", "powerMacG4");
  model->addItem("Power Mac G4 — Mac OS X", "powerMacOSX");
  model->addItem("Quadra 800", "quadra800");
  auto summary = label("", pages[2], "regular", true);
  auto name = new PEdit("Mac OS 9", pages[2]);
  QString lastAutoName = "Mac OS 9";
  bool nameEdited = false;
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
    QString downloadAction = "Download Mac";
    if (step == 2 && !installer->isChecked() && editions->currentIndex() >= 0) {
      const auto entry = editions->currentData().toJsonObject();
      if (QFileInfo(Library::cacheFile(entry)).size() > 0)
        downloadAction = "Resume Download";
    }
    next->setText(
        step < 2    ? "Continue"
        : step == 3 ? (installer->isChecked() ? "Start Installer" : "Start Mac")
                    : (installer->isChecked() ? "Create Mac" : downloadAction));
    heading->setText(
        QString("Step %1 of 4 · Your first classic Mac").arg(step + 1));
  };
  buttonRow(v, {cancel, back, later, next});
  connect(name, &PEdit::textEdited, &d, [&](const QString &) {
    const auto trimmed = name->text().trimmed();
    nameEdited = !trimmed.isEmpty() && trimmed != lastAutoName;
  });
  connect(license, &PButton::toggled, &d, update);
  connect(stack, &QStackedWidget::currentChanged, &d, update);
  connect(model, qOverload<int>(&QComboBox::currentIndexChanged), &d,
          [&](int) {
            if (!installer->isChecked())
              return;
            const auto fam = model->currentData().toString();
            const QString defName = fam == "quadra800"    ? "Quadra 800"
                                    : fam == "powerMacOSX" ? "Mac OS X"
                                                           : "Mac OS 9";
            if (!nameEdited || name->text().trimmed().isEmpty()) {
              lastAutoName = defName;
              name->setText(defName);
              nameEdited = false;
            }
          });
  connect(editions, qOverload<int>(&QComboBox::currentIndexChanged), &d,
          [&](int i) {
            if (i < 0)
              return;
            auto entry = editions->itemData(i).toJsonObject();
            const auto title = Library::displayTitle(entry);
            if (!nameEdited || name->text().trimmed().isEmpty()) {
              lastAutoName = title;
              name->setText(title);
              nameEdited = false;
            }
            summary->setText(Library::cleanSummary(entry));
            update();
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
          auto editionRank = [](const QJsonObject &e) {
            const auto fam = e.value("machineFamily").toString("powerMacG4");
            const auto id = e["id"].toString().toLower();
            const auto text =
                (id + " " + e["name"].toString()).toLower();
            if (id.startsWith("os922-mega-deluxe"))
              return 0;
            if (id.startsWith("os922-superdeluxe"))
              return 1;
            if (fam == "powerMacOSX" || text.contains("os x") ||
                text.contains("osx"))
              return 3;
            if (text.contains("os 9") || text.contains("os9"))
              return 2;
            if (text.contains("os 8") || text.contains("os8"))
              return 4;
            if (text.contains("system 7") || text.contains("7."))
              return 5;
            return 6;
          };
          auto ordered = newest.values();
          std::stable_sort(
              ordered.begin(), ordered.end(),
              [&](const QJsonObject &a, const QJsonObject &b) {
                const int ra = editionRank(a), rb = editionRank(b);
                if (ra != rb)
                  return ra < rb;
                return Library::displayTitle(a) < Library::displayTitle(b);
              });
          for (const auto &e : ordered)
            editions->addItem(Library::displayTitle(e), QVariant::fromValue(e));
          if (editions->count() == 0)
            fail("The Library has no compatible editions.");
          auto entry = editions->currentData().toJsonObject();
          const auto title = Library::displayTitle(entry);
          if (!nameEdited || name->text().trimmed().isEmpty()) {
            lastAutoName = title;
            name->setText(title);
            nameEdited = false;
          }
          summary->setText(Library::cleanSummary(entry));
        } else {
          if (!cd.isEmpty())
            summary->setText(QFileInfo(cd).fileName());
          const auto fam = model->currentData().toString();
          const QString defName = fam == "quadra800"    ? "Quadra 800"
                                  : fam == "powerMacOSX" ? "Mac OS X"
                                                         : "Mac OS 9";
          if (!nameEdited || name->text().trimmed().isEmpty()) {
            lastAutoName = defName;
            name->setText(defName);
            nameEdited = false;
          }
        }
        stack->setCurrentIndex(2);
      } else if (step == 2) {
        const auto cleanName = Model::sanitizedName(name->text());
        if (cleanName.isEmpty())
          fail("Enter a name for this Mac.");
        if (installer->isChecked()) {
          if (cd.isEmpty())
            fail("Choose your Mac OS installation CD image first.");
          const auto newFamily = model->currentData().toString();
          auto m = background<Model::Machine>(
              &d, "Creating your Mac",
              [=](std::atomic_bool &, Library::Progress) {
                return Model::createMachine(cleanName, newFamily,
                                            newFamily == "quadra800" ? 2 : 32,
                                            Model::documents(), cd);
              },
              false);
          registerMachine(m);
          if (newFamily == "powerMacOSX") {
            help->setText(
                "Start the installer. Inside Mac OS X Installer, open "
                "Utilities → Disk Utility, erase the blank hard disk as "
                "Mac OS Extended with an Apple Partition Map scheme, then "
                "install Mac OS X. After installation, shut down, eject the "
                "CD in Media, then start your Mac.");
          } else {
            help->setText("Start the installer. Inside Mac OS, initialize the "
                          "blank hard disk with the CD's disk utility, then run "
                          "Installer. On Power Mac, turn off Update Apple Hard "
                          "Disk Drivers in Options. After installation, shut "
                          "down, eject the CD in Media, then start your Mac.");
          }
        } else {
          install(editions->currentData().toJsonObject(), cleanName);
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
    const auto origin = view->mapTo(w, QPoint());
    displays.append(QJsonObject{
        {"engineProcessId", qint64(e->processId())},
        {"name", e->machine.name()}, {"windowWidth", w->width()}, {"windowHeight", w->height()},
        {"viewportWidth", view->width()}, {"viewportHeight", view->height()},
        {"displayViewportWidth", view->displayViewport().width()},
        {"displayViewportHeight", view->displayViewport().height()},
        {"viewX", origin.x()}, {"viewY", origin.y()},
        {"frameWidth", e->vnc.frame.width()}, {"frameHeight", e->vnc.frame.height()},
        {"desiredWidth", view->desiredSize().width()}, {"desiredHeight", view->desiredSize().height()},
        {"requestedWidth", e->vnc.lastRequestedSize.width()}, {"requestedHeight", e->vnc.lastRequestedSize.height()},
        {"scale", w->devicePixelRatioF()}, {"followsWindow", view->followsWindow},
        {"resizeSupported", e->vnc.desktopResizeSupported}, {"resizeResult", e->vnc.desktopResizeResult},
        {"fitWindow", view->fitWindow}, {"pixelZoom", view->pixelZoom},
        {"imageX", image.x()}, {"imageY", image.y()}, {"imageWidth", image.width()}, {"imageHeight", image.height()},
        {"captured", view->captured}, {"relative", e->vnc.relative},
        {"resizeNotice", view->resizeNotice}, {"engineLog", e->log}});
  }
  QJsonObject result = {{"frontend", "native-qt-cpp"},
          {"processId", qint64(QCoreApplication::applicationPid())},
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
  result["windowsScale"] = GetDpiForWindow(hwnd) / 96.0;
  result["uiPixelZoom"] = devicePixelRatioF();
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
