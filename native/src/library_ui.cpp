#include "app.h"
#include <QtConcurrent>
#include <optional>

using namespace Platinum;

namespace {
QString bytesText(qint64 bytes) {
  if (bytes >= 1073741824)
    return QString::number(double(bytes) / 1073741824, 'f', 1) + " GB";
  if (bytes >= 1048576)
    return QString::number(double(bytes) / 1048576, 'f', 1) + " MB";
  return QString::number(std::max<qint64>(0, bytes) / 1024) + " KB";
}
QString familyName(const QJsonObject &entry) {
  const auto family = entry.value("machineFamily").toString("powerMacG4");
  return family == "quadra800" ? "Quadra 800"
       : family == "powerMac7500" ? "Power Mac 7500" : "Power Mac G4";
}
QString systemName(const QJsonObject &entry) {
  auto version = entry["osVersion"].toString();
  if (!version.isEmpty() && version[0].isDigit())
    version.prepend(version.startsWith("7.") ? "System " : "Mac OS ");
  return version;
}
QString systemCategory(const QJsonObject &entry) {
  auto version = systemName(entry);
  return version.contains("Copland", Qt::CaseInsensitive) ? "Copland"
       : version.contains("7.") ? "System 7"
       : version.contains("8.") ? "Mac OS 8" : "Mac OS 9";
}
bool compatible(const QJsonObject &entry) {
  return entry.value("machineFamily").toString("powerMacG4") != "powerMac7500" &&
      Model::supportedVersion(entry["minimumAppVersion"].toString());
}
// The public catalog retains older image revisions for existing installations.
// Browsing should present one current edition instead of near-identical copies.
QJsonArray newestEditions(const QJsonArray &rows) {
  QMap<QString, QJsonObject> newest;
  QStringList order;
  for (auto row : rows) {
    auto entry = row.toObject();
    auto id = entry["id"].toString();
    auto match = QRegularExpression("^(.*)-v(\\d+)$").match(id);
    auto key = match.hasMatch() ? match.captured(1) : id;
    if (!newest.contains(key)) order << key;
    const int revision = match.hasMatch() ? match.captured(2).toInt() : 0;
    const int oldRevision = newest[key]["id"].toString().section("-v", -1).toInt();
    if (newest[key].isEmpty() || revision > oldRevision) newest[key] = entry;
  }
  QJsonArray result;
  for (bool available : {true, false})
    for (const auto &key : order)
      if (compatible(newest[key]) == available) result.append(newest[key]);
  return result;
}

class EditionDelegate : public QStyledItemDelegate {
public:
  using QStyledItemDelegate::QStyledItemDelegate;
  QSize sizeHint(const QStyleOptionViewItem &, const QModelIndex &) const override {
    return {250, 58};
  }
  void paint(QPainter *painter, const QStyleOptionViewItem &option,
             const QModelIndex &index) const override {
    auto entry = index.data(Qt::UserRole).toJsonObject();
    const bool selected = option.state & QStyle::State_Selected;
    Pixels p(option.rect.width(), option.rect.height(),
             selected ? 0xffccccff : 0xffffffff);
    const auto family = entry.value("machineFamily").toString("powerMacG4");
    const auto icon = family == "quadra800" ? "quadra_800"
                    : family == "powerMac7500" ? "powermac_7500" : "powermac_g4";
    QPainter(&p.image).drawImage(7, 11, QImage(":/" + QString(icon) + "-32.png"));
    text(p, elide(entry["name"].toString(), p.w() - 58), 47, 17);
    text(p, elide(familyName(entry), p.w() - 58, "small"), 47, 33,
         0xff555555, "small");
    const auto caption = compatible(entry)
        ? bytesText(qint64(entry["archiveBytes"].toDouble())) + " download"
        : entry.value("machineFamily").toString() == "powerMac7500"
            ? "macOS only" : "App update required";
    text(p, elide(caption, p.w() - 58, "small"), 47, 47, 0xff555555, "small");
    p.line(0, p.h() - 1, p.w(), selected ? 0xffaaaadd : 0xffeeeeee);
    if (selected && option.state & QStyle::State_HasFocus) {
      p.frame(1, 1, p.w() - 2, p.h() - 2, 0xff666699);
    }
    painter->save();
    painter->setRenderHint(QPainter::SmoothPixmapTransform, false);
    painter->drawImage(option.rect.topLeft(), p.image);
    painter->restore();
  }
};

class LibraryDialog : public PDialog {
public:
  QBoxLayout *columns = nullptr;
  explicit LibraryDialog(QWidget *parent)
      : PDialog("Mac OS Library", {790, 620}, parent) {
    growable = true;
    setMinimumSize(360, 300);
    setObjectName("libraryDialog");
  }
protected:
  void resizeEvent(QResizeEvent *event) override {
    PDialog::resizeEvent(event);
    if (columns)
      columns->setDirection(width() < 650 ? QBoxLayout::TopToBottom
                                         : QBoxLayout::LeftToRight);
  }
};

template <class T>
T libraryTask(QWidget *parent, const QString &title, const QString &name,
              const std::function<T(std::atomic_bool &, Library::Progress)> &job,
              bool download) {
  PDialog d(title, {500, download ? 280 : 190}, parent);
  d.setObjectName("libraryProgressDialog");
  auto v = dialogLayout(&d);
  v->addWidget(label(name, &d, "system", true));
  auto status = label(download ? "Connecting to the Library…" : "Loading editions…",
                      &d, "regular", true);
  status->setObjectName("downloadStatus");
  auto bar = new PProgress(&d);
  bar->setValue(-1);
  auto amount = label("", &d, "small", true);
  amount->setObjectName("downloadAmount");
  v->addWidget(status);
  v->addWidget(bar);
  v->addWidget(amount);
  if (download)
    v->addWidget(label("Pause at any time. Your download is saved and resumes "
                       "when you choose this edition again.", &d, "small", true));
  v->addStretch();
  std::atomic_bool cancel = false;
  bool finished = false;
  auto pause = button(download ? "Pause Download" : "Cancel", [&] { cancel = true; }, &d);
  pause->setObjectName("downloadPause");
  buttonRow(v, {pause});
  auto requestPause = [&] {
    cancel = true;
    pause->setEnabled(false);
    status->setText(download ? "Saving your progress…" : "Canceling…");
  };
  QObject::connect(pause, &PButton::clicked, &d, requestPause);
  d.canClose = [&] {
    if (!finished) requestPause();
    return finished;
  };
  struct Result { std::optional<T> value; QString error; };
  QFutureWatcher<Result> watcher;
  auto future = QtConcurrent::run([&]() -> Result {
    try {
      QElapsedTimer throttle;
      throttle.start();
      QString lastPhase;
      qint64 phaseStart = 0;
      QElapsedTimer phaseTime;
      phaseTime.start();
      auto progress = [&](QString phase, qint64 complete, qint64 total) {
        if (phase != lastPhase) {
          phaseStart = complete;
          phaseTime.restart();
        } else if (complete != total && throttle.elapsed() < 100) {
          return;
        }
        lastPhase = phase;
        throttle.restart();
        auto detail = total > 0 ? bytesText(complete) + " of " + bytesText(total) : QString();
        const double seconds = phaseTime.elapsed() / 1000.0;
        if (phase == "Downloading" && seconds >= 1 && complete > phaseStart) {
          const double speed = (complete - phaseStart) / seconds;
          detail += " · " + bytesText(qint64(speed)) + "/s";
          if (total > complete && speed > 0) {
            const auto left = qint64((total - complete) / speed);
            detail += left >= 60 ? QString(" · about %1 min left").arg((left + 59) / 60)
                                 : " · less than a minute left";
          }
        }
        const auto phaseText = phase == "Checking download" ? "Checking your download"
                             : phase == "Installing" ? "Preparing your Mac" : phase;
        QMetaObject::invokeMethod(&d, [=, &cancel] {
          if (cancel) return;
          status->setText(phaseText + (total > 0
              ? QString("… %1%").arg(complete * 100 / total) : "…"));
          amount->setText(detail);
          bar->setValue(total > 0 ? double(complete) / total : -1);
        }, Qt::QueuedConnection);
      };
      return {job(cancel, progress), {}};
    } catch (const std::exception &e) {
      return {{}, QString::fromUtf8(e.what())};
    } catch (...) {
      return {{}, "The operation could not complete. Please try again."};
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
  const auto result = future.result();
  if (!result.error.isEmpty()) Model::fail(result.error);
  return *result.value;
}
} // namespace

QJsonArray App::loadCatalog() {
  if (!catalogRows.isEmpty()) return catalogRows;
  auto owner = QApplication::activeWindow();
  catalogRows = libraryTask<QJsonArray>(owner ? owner : this, "Mac OS Library",
      "Finding ready-to-run Macs", [](std::atomic_bool &cancel, Library::Progress) {
        return Library::load(cancel);
      }, false);
  return catalogRows;
}

Model::Machine App::install(QJsonObject entry, const QString &name) {
  const auto destination = Model::documents();
  auto owner = QApplication::activeWindow();
  auto machine = libraryTask<Model::Machine>(owner ? owner : this,
      "Download Mac", name.isEmpty() ? entry["name"].toString() : name,
      [=](std::atomic_bool &cancel, Library::Progress progress) {
        return Library::install(entry, destination, name, cancel, progress);
      }, true);
  registerMachine(machine);
  return machine;
}

void App::showLibrary() {
  const auto destination = Model::documents();
  LibraryDialog d(this);
  auto outer = new QVBoxLayout(d.body);
  outer->setContentsMargins(14, 12, 14, 12);
  outer->setSpacing(10);
  outer->addWidget(label("Mac OS Library", &d));
  outer->addWidget(label("Choose a system to download. "
                         "Each download creates a separate Mac.", &d, "regular", true));
  auto searchRow = new QHBoxLayout;
  searchRow->setSpacing(8);
  searchRow->addWidget(label("Search:", &d));
  auto search = new PEdit({}, &d);
  search->setObjectName("librarySearch");
  search->setAccessibleName("Search the Library");
  search->setToolTip("Search by system, edition, model, game or application.");
  searchRow->addWidget(search, 1);
  auto clear = button("Clear", [&] { search->clear(); search->setFocus(); }, &d);
  clear->setEnabled(false);
  searchRow->addWidget(clear);
  outer->addLayout(searchRow);
  auto filterRow = new QHBoxLayout;
  filterRow->setSpacing(8);
  auto filter = new PCombo(&d);
  filter->setObjectName("libraryFilter");
  filter->setAccessibleName("Filter by Mac OS");
  filter->addItems({"All systems", "System 7", "Mac OS 8", "Mac OS 9", "Copland"});
  filterRow->addWidget(filter);
  auto count = label("Loading editions…", &d, "small");
  count->setObjectName("libraryCount");
  filterRow->addWidget(count, 1);
  auto refresh = new PButton("Refresh", &d);
  refresh->setObjectName("libraryRefresh");
  filterRow->addWidget(refresh);
  outer->addLayout(filterRow);

  d.columns = new QBoxLayout(QBoxLayout::LeftToRight);
  d.columns->setSpacing(14);
  auto editions = new QListWidget(&d);
  editions->setObjectName("libraryList");
  editions->setAccessibleName("Mac OS editions");
  editions->setItemDelegate(new EditionDelegate(editions));
  editions->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
  editions->setSelectionMode(QAbstractItemView::SingleSelection);
  editions->setMinimumSize(180, 88);
  editions->setFrameStyle(QFrame::Box);
  editions->setLineWidth(2);
  d.columns->addWidget(editions, 4);
  auto detailArea = new QScrollArea(&d);
  detailArea->setFrameShape(QFrame::NoFrame);
  detailArea->setWidgetResizable(true);
  detailArea->setMinimumWidth(180);
  detailArea->setMinimumHeight(80);
  detailArea->setAutoFillBackground(false);
  detailArea->viewport()->setAutoFillBackground(false);
  auto content = new QWidget;
  auto detail = new QVBoxLayout(content);
  detail->setContentsMargins(0, 0, 8, 0);
  detail->setSpacing(10);
  auto title = label("Choose an edition", content, "system", true);
  title->setObjectName("libraryEditionTitle");
  auto summary = label("The Library is loading. You can close this window at any time.",
                       content, "regular", true);
  auto specs = label("", content, "small", true);
  auto availability = label("", content, "small", true);
  availability->setObjectName("libraryAvailability");
  auto nameCaption = label("Name your new Mac:", content);
  auto name = new PEdit({}, content);
  name->setObjectName("libraryMachineName");
  name->setAccessibleName("Name your new Mac");
  auto diskHint = label("", content, "small", true);
  diskHint->setToolTip(QDir::toNativeSeparators(destination));
  detail->addWidget(title);
  detail->addWidget(summary);
  detail->addWidget(specs);
  detail->addWidget(availability);
  detail->addWidget(nameCaption);
  detail->addWidget(name);
  detail->addWidget(diskHint);
  detail->addStretch();
  detailArea->setWidget(content);
  d.columns->addWidget(detailArea, 5);
  outer->addLayout(d.columns, 1);
  auto feedback = label("", &d, "small", true);
  feedback->setObjectName("libraryFeedback");
  outer->addWidget(feedback);
  auto footer = new QHBoxLayout;
  footer->setSpacing(10);
  footer->addWidget(button("Close", [&] { d.done(); }, &d));
  footer->addStretch();
  auto showMacs = button("Show My Macs", [&] { d.done(); }, &d);
  showMacs->setObjectName("libraryShowMacs");
  showMacs->hide();
  footer->addWidget(showMacs);
  auto download = new PButton("Download Mac", &d, true);
  download->setObjectName("libraryDownload");
  download->setEnabled(false);
  footer->addWidget(download);
  outer->addLayout(footer);

  auto visibleRows = newestEditions(catalogRows);
  auto selectedEntry = [&]() {
    return editions->currentItem() ? editions->currentItem()->data(Qt::UserRole).toJsonObject()
                                   : QJsonObject();
  };
  QString detailID, readyMachineID, readyEditionID, readyName;
  auto updateDetails = [&] {
    auto entry = selectedEntry();
    const bool exists = !entry.isEmpty();
    const bool available = exists && compatible(entry);
    download->setEnabled(available);
    name->setVisible(available);
    nameCaption->setVisible(available);
    specs->setVisible(exists);
    diskHint->setVisible(available);
    availability->setVisible(exists);
    if (!exists) {
      detailID.clear();
      title->setText("No matching editions");
      summary->setText("Try another search or choose All systems.");
      download->setText("Download Mac");
      return;
    }
    title->setText(entry["name"].toString());
    summary->setText(entry["summary"].toString());
    specs->setText("System: " + systemName(entry) + "\nModel: " + familyName(entry) +
        "\nDownload: " + bytesText(qint64(entry["archiveBytes"].toDouble())) +
        "\nVirtual disk: " + bytesText(qint64(entry.value("diskCapacityBytes")
                                                   .toDouble(entry["installedBytes"].toDouble()))));
    const auto saved = Library::cachedBytes(entry);
    const auto archiveSize = qint64(entry["archiveBytes"].toDouble());
    download->setText(saved == archiveSize ? "Create Mac"
                      : saved ? "Resume Download" : "Download Mac");
    if (detailID != entry["id"].toString()) name->setText(entry["name"].toString());
    detailID = entry["id"].toString();
    if (readyEditionID == detailID && readyName == name->text())
      download->setText("Start Mac");
    availability->setText(!available
        ? entry.value("machineFamily").toString() == "powerMac7500"
            ? "Copland is available in ClassicMac for macOS. Choose another system for this computer."
            : "Requires ClassicMac " + entry["minimumAppVersion"].toString() + " or later."
        : readyEditionID == detailID && readyName == name->text()
            ? "Your Mac is ready to start."
        : saved == archiveSize ? "Download complete. Create a Mac from the saved image."
        : saved ? "Saved download: " + bytesText(saved) + ". Resume to continue where you left off."
                : "Ready to download. No installation CD needed.");
    diskHint->setText("The virtual disk grows as you add files. Your new Mac is saved in " +
                      QDir::toNativeSeparators(destination) + ".");
  };
  auto rebuild = [&] {
    auto previous = selectedEntry()["id"].toString();
    QSignalBlocker blocker(editions);
    editions->clear();
    int selectedRow = 0;
    const auto query = search->text().trimmed();
    for (auto row : visibleRows) {
      auto entry = row.toObject();
      const auto haystack = entry["name"].toString() + " " + entry["summary"].toString() +
                            " " + systemName(entry) + " " + familyName(entry);
      if ((!query.isEmpty() && !haystack.contains(query, Qt::CaseInsensitive)) ||
          (filter->currentIndex() > 0 && filter->currentText() != systemCategory(entry)))
        continue;
      auto item = new QListWidgetItem(entry["name"].toString(), editions);
      item->setData(Qt::UserRole, entry);
      item->setToolTip(entry["name"].toString() + "\n" + familyName(entry) + " · " +
                       bytesText(qint64(entry["archiveBytes"].toDouble())));
      if (entry["id"].toString() == previous) selectedRow = editions->count() - 1;
    }
    editions->setCurrentRow(editions->count() ? selectedRow : -1);
    count->setText(QString("%1 of %2 editions").arg(editions->count()).arg(visibleRows.size()));
    clear->setEnabled(!search->text().isEmpty());
    updateDetails();
  };
  connect(editions, &QListWidget::currentRowChanged, &d, [&](int) { updateDetails(); });
  connect(search, &QLineEdit::textChanged, &d, [&](const QString &) { rebuild(); });
  connect(filter, qOverload<int>(&QComboBox::currentIndexChanged), &d, [&](int) { rebuild(); });
  connect(download, &PButton::clicked, &d, [&] {
    auto entry = selectedEntry();
    if (entry.isEmpty() || !compatible(entry)) return;
    if (name->text().trimmed().isEmpty()) {
      feedback->setText("Give your new Mac a name before downloading.");
      name->setFocus();
      return;
    }
    const auto machineName = name->text().trimmed();
    try {
      if (readyEditionID == entry["id"].toString() && readyName == machineName) {
        for (int i = 0; i < machines.size(); ++i)
          if (machines[i].id() == readyMachineID) {
            select(i);
            startSelected();
            d.done();
            return;
          }
      }
      const auto machine = install(entry, machineName);
      readyMachineID = machine.id();
      readyEditionID = entry["id"].toString();
      readyName = machine.name();
      feedback->setText(machine.name() + " is ready. Choose Start Mac to open it, or Show My Macs to return home.");
      showMacs->show();
      updateDetails();
      name->setText(machineName);
    } catch (const std::exception &e) {
      feedback->setText(QString::fromUtf8(e.what()));
      updateDetails();
      name->setText(machineName);
    }
  });
  connect(name, &QLineEdit::textChanged, &d, [&] {
    auto entry = selectedEntry();
    if (entry.isEmpty()) return;
    const auto saved = Library::cachedBytes(entry);
    download->setText(readyEditionID == entry["id"].toString() && readyName == name->text().trimmed()
        ? "Start Mac" : saved == qint64(entry["archiveBytes"].toDouble()) ? "Create Mac"
        : saved ? "Resume Download" : "Download Mac");
  });
  struct CatalogResult { QJsonArray rows; QString error; };
  QFutureWatcher<CatalogResult> watcher;
  std::atomic_bool cancel = false;
  bool loading = false;
  auto refreshCatalog = [&] {
    if (loading) return;
    loading = true;
    cancel = false;
    refresh->setEnabled(false);
    refresh->setText("Loading…");
    feedback->setText("Connecting to the Library… You can keep browsing or close this window.");
    watcher.setFuture(QtConcurrent::run([&]() -> CatalogResult {
      try { return {Library::load(cancel), {}}; }
      catch (const std::exception &e) { return {{}, QString::fromUtf8(e.what())}; }
      catch (...) { return {{}, "The Library could not be loaded. Please try again."}; }
    }));
  };
  connect(&watcher, &QFutureWatcher<CatalogResult>::finished, &d, [&] {
    loading = false;
    refresh->setEnabled(true);
    refresh->setText("Refresh");
    if (cancel) return;
    auto result = watcher.result();
    if (!result.error.isEmpty()) {
      feedback->setText("Could not refresh the Library. Check your connection and choose Retry. " + result.error);
      refresh->setText("Retry");
      if (visibleRows.isEmpty()) {
        title->setText("The Library is unavailable");
        summary->setText("Your saved Macs are still available. Try loading the Library again when you are online.");
      }
      return;
    }
    catalogRows = result.rows;
    visibleRows = newestEditions(catalogRows);
    rebuild();
    if (visibleRows.isEmpty()) {
      title->setText("No editions are available yet");
      summary->setText("Choose Refresh to check the Library again later.");
    }
    feedback->setText(visibleRows.isEmpty() ? "No editions are available yet. Choose Refresh to check again."
                                          : "Latest editions shown. Downloads can be paused and resumed.");
  });
  connect(refresh, &PButton::clicked, &d, refreshCatalog);
  d.canClose = [&] { cancel = true; return true; };
  rebuild();
  if (visibleRows.isEmpty()) {
    title->setText("Loading the Library");
    summary->setText("Finding ready-to-run Macs. This window can be closed while loading.");
    QTimer::singleShot(0, &d, refreshCatalog);
  } else {
    feedback->setText("Latest editions shown. Downloads can be paused and resumed.");
  }
  search->setFocus();
  d.exec();
  cancel = true;
  if (watcher.isRunning()) watcher.waitForFinished();
}
