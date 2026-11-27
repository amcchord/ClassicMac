#include "app.h"
#include "transfer_meter.h"
#include <QtConcurrent>
#include <memory>
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
  return family == "quadra800"    ? "Quadra 800"
       : family == "powerMac7500" ? "Power Mac 7500"
       : family == "powerMacOSX"  ? "Power Mac G4 — Mac OS X"
                                  : "Power Mac G4";
}
QString systemName(const QJsonObject &entry) {
  auto version = entry["osVersion"].toString();
  if (!version.isEmpty() && version[0].isDigit())
    version.prepend(version.startsWith("10.") ? "Mac OS X "
                    : version.startsWith("7.") ? "System "
                                               : "Mac OS ");
  return version;
}
QString systemCategory(const QJsonObject &entry) {
  auto version = systemName(entry);
  return version.contains("Copland", Qt::CaseInsensitive) ? "Copland"
       : version.startsWith("Mac OS X ")                  ? "Mac OS X"
       : version.startsWith("System 7")                   ? "System 7"
       : version.startsWith("Mac OS 8")                   ? "Mac OS 8"
                                                          : "Mac OS 9";
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
  static const QRegularExpression revRe("^(.*)-v(\\d+)$");
  for (auto row : rows) {
    auto entry = row.toObject();
    auto id = entry["id"].toString();
    auto match = revRe.match(id);
    auto key = match.hasMatch() ? match.captured(1) : id;
    if (!newest.contains(key))
      order << key;
    const int revision = match.hasMatch() ? match.captured(2).toInt() : 0;
    const int oldRevision =
        newest[key]["id"].toString().section("-v", -1).toInt();
    if (newest[key].isEmpty() || revision > oldRevision)
      newest[key] = entry;
  }
  const int megaIdx = order.indexOf(QStringLiteral("os922-mega-deluxe"));
  const int superIdx = order.indexOf(QStringLiteral("os922-superdeluxe"));
  if (megaIdx > superIdx && superIdx >= 0) {
    order.move(megaIdx, superIdx);
  }
  QJsonArray result;
  for (bool available : {true, false})
    for (const auto &key : order)
      if (compatible(newest[key]) == available)
        result.append(newest[key]);
  return result;
}

class EditionDelegate : public QStyledItemDelegate {
public:
  using QStyledItemDelegate::QStyledItemDelegate;
  QSize sizeHint(const QStyleOptionViewItem &,
                 const QModelIndex &) const override {
    return {250, 58};
  }
  void paint(QPainter *painter, const QStyleOptionViewItem &option,
             const QModelIndex &index) const override {
    auto entry = index.data(Qt::UserRole).toJsonObject();
    const bool selected = option.state & QStyle::State_Selected;
    Pixels p(option.rect.width(), option.rect.height(),
             selected ? 0xffccccff : 0xffffffff);
    const auto family = entry.value("machineFamily").toString("powerMacG4");
    const auto icon = family == "quadra800"    ? "quadra_800"
                    : family == "powerMac7500" ? "powermac_7500"
                                               : "powermac_g4";
    QPainter(&p.image).drawImage(7, 11,
                                 cachedIcon(":/" + QString(icon) + "-32.png"));
    const QString title = Library::displayTitle(entry);
    text(p, elide(title, p.w() - 58), 47, 17);
    text(p, elide(familyName(entry), p.w() - 58, "small"), 47, 33,
         0xff555555, "small");
    const auto caption =
        compatible(entry)
            ? bytesText(qint64(entry["archiveBytes"].toDouble())) + " download"
        : entry.value("machineFamily").toString() == "powerMac7500"
            ? "macOS only"
            : "App update required";
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
      : PDialog("Mac OS Library", {800, 620}, parent) {
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
              bool download, bool cachedComplete = false) {
  PDialog d(title, {500, download ? 280 : 190}, parent);
  d.setObjectName("libraryProgressDialog");
  auto v = dialogLayout(&d);
  v->addWidget(label(name, &d, "system", true));
  const QString initialStatus = !download         ? "Loading editions…"
                                : cachedComplete  ? "Checking your saved download…"
                                                  : "Connecting to the Library…";
  auto status = label(initialStatus, &d, "regular", true);
  status->setObjectName("downloadStatus");
  auto bar = new PProgress(&d);
  bar->setValue(-1);
  auto amount = label("", &d, "small", true);
  amount->setObjectName("downloadAmount");
  v->addWidget(status);
  v->addWidget(bar);
  v->addWidget(amount);
  PLabel *hint = nullptr;
  if (download) {
    hint = label(
        cachedComplete
            ? "Creating your Mac from the saved image. You can cancel at any time."
            : "Pause at any time. Your download is saved and resumes when you choose this edition again.",
        &d, "small", true);
    v->addWidget(hint);
  }
  v->addStretch();
  std::atomic_bool cancel = false;
  bool finished = false;
  QString currentPhase = (download && !cachedComplete) ? "Downloading" : "";
  auto pause = button((download && !cachedComplete) ? "Pause Download" : "Cancel",
                      [&] { cancel = true; }, &d);
  pause->setObjectName("downloadPause");
  buttonRow(v, {pause});
  auto requestPause = [&] {
    cancel = true;
    pause->setEnabled(false);
    status->setText((download && currentPhase == "Downloading")
                        ? "Saving your progress…"
                        : "Canceling…");
  };
  QObject::connect(pause, &PButton::clicked, &d, requestPause);
  d.canClose = [&] {
    if (!finished)
      requestPause();
    return finished;
  };
  struct Result {
    std::optional<T> value;
    QString error;
  };
  QFutureWatcher<Result> watcher;
  auto future = QtConcurrent::run([&]() -> Result {
    try {
      QElapsedTimer throttle;
      throttle.start();
      QString lastPhase;
      QElapsedTimer phaseTime;
      phaseTime.start();
      Transfer::Meter meter;
      auto progress = [&](QString phase, qint64 complete, qint64 total) {
        if (phase != lastPhase) {
          phaseTime.restart();
          meter.reset(total, complete);
        } else if (complete != total && throttle.elapsed() < 100) {
          return;
        }
        lastPhase = phase;
        throttle.restart();
        auto detail = total > 0 ? bytesText(complete) + " of " + bytesText(total)
                                : QString();
        if (phase == "Downloading" || phase == "Checking download" ||
            phase == "Installing") {
          double speedBps = 0.0;
          double etaSec = -1.0;
          meter.update(complete, phaseTime.elapsed() / 1000.0, speedBps,
                       etaSec);
          const QString speedText = Transfer::Meter::formatSpeed(speedBps);
          if (!speedText.isEmpty())
            detail += (detail.isEmpty() ? "" : " · ") + speedText;
          const QString etaText = Transfer::Meter::formatEta(etaSec);
          if (!etaText.isEmpty())
            detail += (detail.isEmpty() ? "" : " · ") + etaText;
        }
        const auto phaseText = phase == "Checking download" ? "Checking your download"
                             : phase == "Installing"        ? "Preparing your Mac"
                                                            : phase;
        QMetaObject::invokeMethod(
            &d,
            [=, &cancel, &currentPhase] {
              if (cancel)
                return;
              currentPhase = phase;
              if (download) {
                const bool downloading = (phase == "Downloading");
                pause->setText(downloading ? "Pause Download" : "Cancel");
                if (hint)
                  hint->setText(
                      downloading
                          ? "Pause at any time. Your download is saved and resumes when you choose this edition again."
                          : "Your downloaded image stays saved if you cancel.");
              }
              status->setText(
                  phaseText +
                  (total > 0 ? QString("… %1%").arg(complete * 100 / total)
                             : "…"));
              amount->setText(detail);
              bar->setValue(total > 0 ? double(complete) / total : -1);
            },
            Qt::QueuedConnection);
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
  if (!result.error.isEmpty())
    Model::fail(result.error);
  return *result.value;
}
} // namespace

QJsonArray App::loadCatalog() {
  if (!catalogRows.isEmpty())
    return catalogRows;
  auto owner = QApplication::activeWindow();
  catalogRows = libraryTask<QJsonArray>(
      owner ? owner : this, "Mac OS Library", "Finding ready-to-run Macs",
      [](std::atomic_bool &cancel, Library::Progress) {
        return Library::load(cancel);
      },
      false);
  return catalogRows;
}

Model::Machine App::install(QJsonObject entry, const QString &name,
                            const QString &destination) {
  const auto target = destination.isEmpty() ? Model::documents() : destination;
  auto owner = QApplication::activeWindow();
  const auto title = name.isEmpty() ? Library::displayTitle(entry) : name;
  const auto archiveSize = qint64(entry["archiveBytes"].toDouble());
  const bool cachedComplete =
      archiveSize > 0 && Library::cachedBytes(entry) == archiveSize;
  auto machine = libraryTask<Model::Machine>(
      owner ? owner : this, cachedComplete ? "Create Mac" : "Download Mac", title,
      [=](std::atomic_bool &cancel, Library::Progress progress) {
        return Library::install(entry, target, name, cancel, progress);
      },
      true, cachedComplete);
  registerMachine(machine);
  return machine;
}

void App::showLibrary() {
  for (auto *existing : findChildren<PDialog *>()) {
    if (existing->objectName() == "libraryDialog" && existing->isVisible()) {
      existing->raise();
      existing->activateWindow();
      return;
    }
  }
  QString saveFolder = Model::documents();
  LibraryDialog d(this);
  auto outer = new QVBoxLayout(d.body);
  outer->setContentsMargins(14, 12, 14, 12);
  outer->setSpacing(10);
  outer->addWidget(label("Mac OS Library", &d));
  outer->addWidget(label("Choose a system to download. "
                         "Each download creates a separate Mac.",
                         &d, "regular", true));
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
  filter->addItems({"All systems", "System 7", "Mac OS 8", "Mac OS 9",
                    "Mac OS X", "Copland"});
  filterRow->addWidget(filter);
  auto count = label("Loading editions…", &d, "small");
  count->setObjectName("libraryCount");
  filterRow->addWidget(count, 1);
  auto refresh = new PButton("Refresh", &d);
  refresh->setObjectName("libraryRefresh");
  filterRow->addWidget(refresh);
  outer->addLayout(filterRow);

  auto partialBanner = label("", &d, "small", true);
  partialBanner->setObjectName("libraryPartialWarning");
  partialBanner->ink = 0xff880000;
  partialBanner->hide();
  outer->addWidget(partialBanner);

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
  auto summary = label(
      "The Library is loading. You can close this window at any time.",
      content, "regular", true);
  auto specs = label("", content, "small", true);
  auto availability = label("", content, "small", true);
  availability->setObjectName("libraryAvailability");
  auto nameCaption = label("Name your new Mac:", content);
  auto name = new PEdit({}, content);
  name->setObjectName("libraryMachineName");
  name->setAccessibleName("Name your new Mac");
  auto saveRow = new QHBoxLayout;
  saveRow->setSpacing(8);
  auto saveCaption = label("Save in:", content, "small");
  auto saveLocation = label(
      QFileInfo(saveFolder).fileName().isEmpty()
          ? QDir::toNativeSeparators(saveFolder)
          : QFileInfo(saveFolder).fileName(),
      content, "small");
  saveLocation->setToolTip(QDir::toNativeSeparators(saveFolder));
  auto chooseFolder = new PButton("Choose…", content);
  chooseFolder->setObjectName("librarySaveFolder");
  saveRow->addWidget(saveCaption);
  saveRow->addWidget(saveLocation, 1);
  saveRow->addWidget(chooseFolder);
  auto diskHint = label("", content, "small", true);
  detail->addWidget(title);
  detail->addWidget(summary);
  detail->addWidget(specs);
  detail->addWidget(availability);
  detail->addWidget(nameCaption);
  detail->addWidget(name);
  detail->addLayout(saveRow);
  detail->addWidget(diskHint);
  detail->addStretch();
  detailArea->setWidget(content);
  d.columns->addWidget(detailArea, 5);
  outer->addLayout(d.columns, 1);
  auto feedback = label("", &d, "small", true);
  feedback->setObjectName("libraryFeedback");
  outer->addWidget(feedback);
  auto footer = new QHBoxLayout;
  footer->setContentsMargins(0, 0, 16, 0);
  footer->setSpacing(10);
  footer->addWidget(button("Close", [&] { d.done(); }, &d));
  auto discard = new PButton("Discard Download", &d);
  discard->setObjectName("libraryDiscard");
  discard->hide();
  footer->addWidget(discard);
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
    return editions->currentItem()
               ? editions->currentItem()->data(Qt::UserRole).toJsonObject()
               : QJsonObject();
  };
  QString detailID, readyMachineID, readyEditionID, readyName;
  qint64 cachedSavedBytes = 0;
  bool loading = false;
  auto updateActionButtons = [&] {
    auto entry = selectedEntry();
    const bool available = !entry.isEmpty() && compatible(entry);
    const auto archiveSize = qint64(entry["archiveBytes"].toDouble());
    const bool isReady = readyEditionID == detailID &&
                         readyName == name->text().trimmed();
    download->setEnabled(!loading && available);
    download->setText(isReady                          ? "Start Mac"
                      : cachedSavedBytes == archiveSize && archiveSize > 0
                                                       ? "Create Mac"
                      : cachedSavedBytes > 0           ? "Resume Download"
                                                       : "Download Mac");
    discard->setVisible(available && cachedSavedBytes > 0 && !isReady);
    discard->setEnabled(!loading && available && cachedSavedBytes > 0);
    chooseFolder->setEnabled(!loading && available);
  };
  auto updateDetails = [&] {
    auto entry = selectedEntry();
    const bool exists = !entry.isEmpty();
    const bool available = exists && compatible(entry);
    name->setVisible(available);
    nameCaption->setVisible(available);
    saveCaption->setVisible(available);
    saveLocation->setVisible(available);
    chooseFolder->setVisible(available);
    specs->setVisible(exists);
    diskHint->setVisible(available);
    availability->setVisible(exists);
    if (!exists) {
      detailID.clear();
      cachedSavedBytes = 0;
      title->setText("No matching editions");
      summary->setText("Try another search or choose All systems.");
      updateActionButtons();
      return;
    }
    const QString display = Library::displayTitle(entry);
    title->setText(display);
    summary->setText(Library::cleanSummary(entry));
    const qint64 requiredFree = Library::requiredDiskBytes(entry, saveFolder);
    specs->setText("System: " + systemName(entry) +
                   "\nModel: " + familyName(entry) +
                   "\nDownload: " + bytesText(qint64(entry["archiveBytes"].toDouble())) +
                   "\nVirtual disk: " +
                   bytesText(qint64(entry.value("diskCapacityBytes")
                                        .toDouble(entry["installedBytes"].toDouble()))) +
                   "\nFree space needed: " + bytesText(requiredFree));
    cachedSavedBytes = Library::cachedBytes(entry);
    const auto archiveSize = qint64(entry["archiveBytes"].toDouble());
    if (detailID != entry["id"].toString())
      name->setText(display);
    detailID = entry["id"].toString();
    updateActionButtons();
    availability->setText(
        !available
            ? entry.value("machineFamily").toString() == "powerMac7500"
                  ? "Copland is available in ClassicMac for macOS. Choose another system for this computer."
                  : "Requires ClassicMac " + entry["minimumAppVersion"].toString() + " or later."
        : readyEditionID == detailID && readyName == name->text().trimmed()
            ? "Your Mac is ready to start."
        : cachedSavedBytes == archiveSize && archiveSize > 0
            ? "Download complete. Create a Mac from the saved image."
        : cachedSavedBytes > 0
            ? "Saved download: " + bytesText(cachedSavedBytes) +
                  ". Resume to continue where you left off."
            : "Ready to download. No installation CD needed.");
    diskHint->setText(
        "The virtual disk grows as you add files. Your new Mac is saved in " +
        QDir::toNativeSeparators(saveFolder) + ".");
  };
  QObject::connect(chooseFolder, &PButton::clicked, &d, [&] {
    auto picked = QFileDialog::getExistingDirectory(
        &d, "Choose where to save this Mac", saveFolder);
    if (!picked.isEmpty()) {
      saveFolder = picked;
      const auto folderName = QFileInfo(saveFolder).fileName();
      saveLocation->setText(folderName.isEmpty()
                                ? QDir::toNativeSeparators(saveFolder)
                                : folderName);
      saveLocation->setToolTip(QDir::toNativeSeparators(saveFolder));
      updateDetails();
    }
  });
  auto updatePartialBanner = [&] {
    const auto warn = Library::partialWarning();
    partialBanner->setText(warn);
    partialBanner->setVisible(!warn.isEmpty());
  };
  auto rebuild = [&] {
    auto previous = selectedEntry()["id"].toString();
    QSignalBlocker blocker(editions);
    editions->clear();
    int selectedRow = 0;
    const auto query = search->text().trimmed();
    for (auto row : visibleRows) {
      auto entry = row.toObject();
      const auto display = Library::displayTitle(entry);
      const auto clean = Library::cleanSummary(entry);
      const auto haystack = display + " " + clean + " " + systemName(entry) +
                            " " + familyName(entry);
      if ((!query.isEmpty() && !haystack.contains(query, Qt::CaseInsensitive)) ||
          (filter->currentIndex() > 0 &&
           filter->currentText() != systemCategory(entry)))
        continue;
      auto item = new QListWidgetItem(display, editions);
      item->setData(Qt::UserRole, entry);
      item->setToolTip(display + "\n" + familyName(entry) + " · " +
                       bytesText(qint64(entry["archiveBytes"].toDouble())));
      if (entry["id"].toString() == previous)
        selectedRow = editions->count() - 1;
    }
    editions->setCurrentRow(editions->count() ? selectedRow : -1);
    count->setText(QString("%1 of %2 editions")
                       .arg(editions->count())
                       .arg(visibleRows.size()));
    clear->setEnabled(!search->text().isEmpty());
    updateDetails();
  };
  connect(editions, &QListWidget::currentRowChanged, &d,
          [&](int) { updateDetails(); });
  connect(search, &QLineEdit::textChanged, &d,
          [&](const QString &) { rebuild(); });
  connect(filter, qOverload<int>(&QComboBox::currentIndexChanged), &d,
          [&](int) { rebuild(); });
  connect(discard, &PButton::clicked, &d, [&] {
    auto entry = selectedEntry();
    if (entry.isEmpty() || loading)
      return;
    if (Library::discardDownload(entry)) {
      feedback->setText("Saved download discarded.");
    } else {
      feedback->setText("Could not discard the saved download.");
    }
    updateDetails();
    editions->viewport()->update();
  });
  connect(download, &PButton::clicked, &d, [&] {
    auto entry = selectedEntry();
    if (loading || entry.isEmpty() || !compatible(entry))
      return;
    if (name->text().trimmed().isEmpty()) {
      feedback->setText("Give your new Mac a name before downloading.");
      name->setFocus();
      return;
    }
    QString machineName;
    try {
      machineName = Model::sanitizedName(name->text());
    } catch (const std::exception &e) {
      feedback->setText(QString::fromUtf8(e.what()));
      name->setFocus();
      return;
    }
    try {
      if (readyEditionID == entry["id"].toString() &&
          readyName == machineName) {
        for (int i = 0; i < machines.size(); ++i)
          if (machines[i].id() == readyMachineID) {
            select(i);
            startSelected();
            d.done();
            return;
          }
      }
      const auto machine = install(entry, machineName, saveFolder);
      readyMachineID = machine.id();
      readyEditionID = entry["id"].toString();
      readyName = machine.name();
      feedback->setText(
          machine.name() +
          " is ready. Choose Start Mac to open it, or Show My Macs to return home.");
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
    if (entry.isEmpty())
      return;
    updateActionButtons();
  });

  struct CatalogResult {
    QJsonArray rows;
    QString error;
  };
  auto refreshCancel = std::make_shared<std::atomic_bool>(false);
  auto refreshCatalog = [&] {
    if (loading)
      return;
    loading = true;
    *refreshCancel = false;
    refresh->setEnabled(false);
    refresh->setText("Loading…");
    updateActionButtons();
    feedback->setText(
        "Connecting to the Library… You can keep browsing or close this window.");
    auto *watcher = new QFutureWatcher<CatalogResult>(qApp);
    auto cancelFlag = refreshCancel;
    connect(watcher, &QFutureWatcher<CatalogResult>::finished, watcher,
            [watcher, cancelFlag, guard = QPointer<PDialog>(&d), this,
             &loading, refresh, feedback, title, summary, &visibleRows,
             &rebuild, &updateActionButtons, &updatePartialBanner] {
              const auto result = watcher->result();
              watcher->deleteLater();
              if (!guard)
                return;
              loading = false;
              refresh->setEnabled(true);
              refresh->setText("Refresh");
              if (*cancelFlag) {
                updateActionButtons();
                return;
              }
              if (!result.error.isEmpty()) {
                feedback->setText(
                    "Could not refresh the Library. Check your connection and choose Retry. " +
                    result.error);
                refresh->setText("Retry");
                if (visibleRows.isEmpty()) {
                  title->setText("The Library is unavailable");
                  summary->setText(
                      "Your saved Macs are still available. Try loading the Library again when you are online.");
                }
                updateActionButtons();
                return;
              }
              catalogRows = result.rows;
              visibleRows = newestEditions(catalogRows);
              updatePartialBanner();
              rebuild();
              if (visibleRows.isEmpty()) {
                title->setText("No editions are available yet");
                summary->setText(
                    "Choose Refresh to check the Library again later.");
              }
              feedback->setText(
                  visibleRows.isEmpty()
                      ? "No editions are available yet. Choose Refresh to check again."
                      : "Latest editions shown. Downloads can be paused and resumed.");
            });
    watcher->setFuture(QtConcurrent::run([cancelFlag]() -> CatalogResult {
      try {
        return {Library::load(*cancelFlag), {}};
      } catch (const std::exception &e) {
        return {{}, QString::fromUtf8(e.what())};
      } catch (...) {
        return {{}, "The Library could not be loaded. Please try again."};
      }
    }));
  };
  connect(refresh, &PButton::clicked, &d, refreshCatalog);
  d.canClose = [refreshCancel] {
    *refreshCancel = true;
    return true;
  };
  updatePartialBanner();
  rebuild();
  if (visibleRows.isEmpty()) {
    title->setText("Loading the Library");
    summary->setText(
        "Finding ready-to-run Macs. This window can be closed while loading.");
    QTimer::singleShot(0, &d, refreshCatalog);
  } else {
    feedback->setText(
        "Latest editions shown. Downloads can be paused and resumed.");
  }
  search->setFocus();
  d.exec();
  *refreshCancel = true;
}
