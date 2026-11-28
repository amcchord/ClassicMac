#include "app.h"
#include "checkpoint.h"
#include <QDirIterator>
#include <QtConcurrent>
#include <optional>

namespace {
qint64 folderSizeBytes(const QString &folder) {
  if (folder.isEmpty() || !QFileInfo(folder).isDir())
    return 0;
  qint64 total = 0;
  QDirIterator it(folder,
                  QDir::Files | QDir::Hidden | QDir::System |
                      QDir::NoDotAndDotDot,
                  QDirIterator::Subdirectories);
  while (it.hasNext()) {
    it.next();
    total += it.fileInfo().size();
  }
  return total;
}

QString formatDiskSize(qint64 bytes) {
  if (bytes >= 1073741824LL)
    return QString::number(double(bytes) / 1073741824.0, 'f', 1) + " GB";
  if (bytes >= 1048576LL)
    return QString::number(double(bytes) / 1048576.0, 'f', 1) + " MB";
  return QString::number(std::max<qint64>(0, bytes) / 1024) + " KB";
}

QString checkpointSizeText(qint64 bytes) {
  return formatDiskSize(bytes);
}

template <class Fn>
void runCheckpointTask(QWidget *parent, const QString &title,
                       const QString &message, Fn &&fn) {
  PDialog waitDlg(title, {420, 150}, parent);
  waitDlg.setObjectName("checkpointBusyDialog");
  auto lv = dialogLayout(&waitDlg);
  lv->addWidget(label(message, &waitDlg, "regular", true));
  auto bar = new PProgress(&waitDlg);
  bar->setValue(-1);
  lv->addWidget(bar);
  lv->addStretch();
  bool finished = false;
  waitDlg.canClose = [&] { return finished; };
  struct Result {
    QString error;
  };
  QFutureWatcher<Result> watcher;
  auto future = QtConcurrent::run([fn = std::forward<Fn>(fn)]() -> Result {
    try {
      fn();
      return {};
    } catch (const std::exception &e) {
      return {QString::fromUtf8(e.what())};
    } catch (...) {
      return {"The checkpoint operation could not complete."};
    }
  });
  QObject::connect(&watcher, &QFutureWatcher<Result>::finished, &waitDlg, [&] {
    finished = true;
    waitDlg.done();
  });
  watcher.setFuture(future);
  waitDlg.exec();
  if (!finished)
    future.waitForFinished();
  const auto res = future.result();
  if (!res.error.isEmpty())
    Model::fail(res.error);
}

QString powerState(Engine *engine) {
  if (!engine || !engine->running)
    return "Shut down";
  if (engine->forcedStopRequested())
    return "Stopping the emulation engine…";
  if (engine->stoppingRequested())
    return "Waiting for Mac OS to shut down…";
  return engine->paused ? "Paused" : "Running";
}

PEdit *folderField(const Model::Machine &machine, QWidget *parent) {
  auto path = new PEdit({}, parent);
  path->setMaxLength(32767);
  path->setText(QDir::toNativeSeparators(machine.folder));
  path->setObjectName("machineFolderPath");
  path->setReadOnly(true);
  path->setAccessibleName("Machine folder");
  path->setToolTip(path->text());
  path->setCursorPosition(0);
  return path;
}

// Cancel owns the default ring in destructive confirmations. Pressing Return
// accidentally should never discard a running guest's work or its disk.
bool confirmAction(QWidget *parent, const QString &title, const QString &message,
                   const QString &accept, const QString &objectName) {
  PDialog dialog(title, {530, 250}, parent);
  dialog.setObjectName(objectName);
  auto layout = dialogLayout(&dialog);
  layout->addWidget(label(message, &dialog, "regular", true), 1);
  auto cancel = button("Cancel", [&] { dialog.done(0); }, &dialog, true);
  auto confirm = button(accept, [&] { dialog.done(1); }, &dialog);
  confirm->setObjectName(objectName == "deleteMachineConfirmDialog"
                            ? "confirmDeleteMachine"
                            : "confirmDestructiveAction");
  cancel->setObjectName("cancelDestructiveAction");
  if (auto owner = qobject_cast<PWindow *>(parent))
    QObject::connect(owner, &PWindow::closed, &dialog,
                     [&] { dialog.done(0); });
  buttonRow(layout, {cancel, confirm});
  cancel->setFocus();
  return dialog.exec() == 1;
}

QString trashName() {
#ifdef Q_OS_WIN
  return "Recycle Bin";
#else
  return "Trash";
#endif
}

QString validatedDeleteFolder(const Model::Machine &machine,
                              const QList<Model::Machine> &machines) {
  const QString cleaned = QDir::cleanPath(machine.folder);
  const QFileInfo info(cleaned);
  const QString folder = info.canonicalFilePath();
  const QString parentCanon = QFileInfo(info.path()).canonicalFilePath();
  const QString expected =
      parentCanon.isEmpty()
          ? QString()
          : QDir::cleanPath(parentCanon + "/" + info.fileName());
#ifdef Q_OS_WIN
  const Qt::CaseSensitivity pathCase = Qt::CaseInsensitive;
#else
  const Qt::CaseSensitivity pathCase = Qt::CaseSensitive;
#endif
  if (folder.isEmpty() || !info.isDir() || info.isSymLink() ||
      !info.fileName().endsWith(".classic", Qt::CaseInsensitive) ||
      QDir(folder).isRoot() || expected.isEmpty() ||
      folder.compare(expected, pathCase) != 0)
    Model::fail("To protect your files, only a regular .classic machine folder "
                "can be deleted here. Use Show Files to inspect this folder, "
                "or Remove from List to keep its files.");
  // Re-read the actual folder immediately before moving it. A folder replaced
  // since the Mac was opened must not be deleted under a stale identity.
  if (Model::readMachine(folder).id() != machine.id())
    Model::fail("This folder now belongs to a different Mac. Its files were kept.");
  for (const auto &other : machines) {
    if (other.id() == machine.id())
      continue;
    const QString otherFolder = QFileInfo(other.folder).canonicalFilePath();
    if (otherFolder.compare(folder, pathCase) == 0 ||
        otherFolder.startsWith(folder + "/", pathCase))
      Model::fail("This folder contains another Mac in your list. Move that "
                  "Mac out first, or use Remove from List to keep all files.");
  }
  return folder;
}
} // namespace

void App::showMachineActions() {
  const auto selectedMachine = current();
  if (!selectedMachine)
    return;
  const auto machine = *selectedMachine;
  PDialog dialog("Manage Mac", {540, 445}, this);
  dialog.setObjectName("machineActionsDialog");
  auto layout = dialogLayout(&dialog);
  layout->addWidget(label(machine.name(), &dialog));
  auto state = label({}, &dialog, "small", true);
  layout->addWidget(state);
  layout->addWidget(label("Machine folder:", &dialog, "small"));
  layout->addWidget(folderField(machine, &dialog));
  layout->addWidget(label("Your virtual hard disk, settings and preview are "
                          "stored together in this folder.",
                          &dialog, "regular", true));
  auto actions = new QVBoxLayout;
  auto showFiles = button("Show Files", [&, machine] {
    bool opened = false;
#ifdef Q_OS_LINUX
    {
      QProcess p;
      p.setProgram("xdg-open");
      p.setArguments({machine.folder});
      p.setProcessEnvironment(Model::hostEnvironment());
      opened = p.startDetached();
    }
    if (!opened)
      opened = QDesktopServices::openUrl(QUrl::fromLocalFile(machine.folder));
#else
    opened = QDesktopServices::openUrl(QUrl::fromLocalFile(machine.folder));
#endif
    if (!opened)
      Model::fail("The folder could not be opened. Copy the path above "
                  "and open it in your file manager.");
  }, &dialog);
  showFiles->setObjectName("showMachineFiles");
  actions->addWidget(showFiles, 0, Qt::AlignLeft);
  auto power = button("Power…", [&] {
    if (auto engine = engines.value(machine.id()))
      showPowerActions(engine);
  }, &dialog);
  power->setObjectName("manageMachinePower");
  actions->addWidget(power, 0, Qt::AlignLeft);
  auto checkpoints = button("Checkpoints…", [&, machine] {
    showCheckpoints(machine);
  }, &dialog);
  checkpoints->setObjectName("manageMachineCheckpoints");
  actions->addWidget(checkpoints, 0, Qt::AlignLeft);
  auto remove = button("Delete Mac…", [&] {
    for (int i = 0; i < machines.size(); ++i)
      if (machines[i].id() == machine.id()) {
        select(i);
        removeSelected();
        if (!current() || current()->id() != machine.id())
          dialog.done();
        return;
      }
  }, &dialog);
  remove->setObjectName("removeMachine");
  actions->addWidget(remove, 0, Qt::AlignLeft);
  layout->addLayout(actions);
  auto explanation = label({}, &dialog, "small", true);
  layout->addWidget(explanation);
  layout->addStretch();
  auto refresh = [&] {
    auto engine = engines.value(machine.id());
    state->setText(powerState(engine));
    power->setEnabled(engine && engine->running);
    remove->setEnabled(true);
    explanation->setText(engine
        ? "Delete Mac can force stop this running Mac and move its folder "
          "to the " + trashName() + "."
        : "Delete Mac lets you keep the files or move the entire machine "
          "folder to the " + trashName() + ".");
  };
  QTimer timer;
  connect(&timer, &QTimer::timeout, &dialog, refresh);
  timer.start(250);
  refresh();
  buttonRow(layout, {button("Done", [&] { dialog.done(); }, &dialog, true)});
  dialog.exec();
}

void App::showCheckpoints(const Model::Machine &initialMachine) {
  const QString machineId = initialMachine.id();
  const QString machineFolder = initialMachine.folder;
  PDialog dialog("Checkpoints", {580, 470}, this);
  dialog.setObjectName("checkpointsDialog");
  auto layout = dialogLayout(&dialog);
  layout->addWidget(label(initialMachine.name(), &dialog));
  layout->addWidget(label(
      "Checkpoints save this Mac's virtual disk and settings so you can "
      "restore an earlier state.",
      &dialog, "small", true));
  auto runningNote = label("", &dialog, "small", true);
  runningNote->setObjectName("checkpointsRunningNotice");
  layout->addWidget(runningNote);

  auto cpList = new QListWidget(&dialog);
  cpList->setObjectName("checkpointsList");
  cpList->setAccessibleName("Checkpoints");
  cpList->setFrameStyle(QFrame::Box);
  cpList->setLineWidth(2);
  cpList->setSelectionMode(QAbstractItemView::SingleSelection);
  layout->addWidget(cpList, 1);

  auto cpDetails = label("", &dialog, "small", true);
  cpDetails->setObjectName("checkpointDetails");
  layout->addWidget(cpDetails);

  QList<Checkpoints::Entry> entries;
  auto currentEntry = [&]() -> const Checkpoints::Entry * {
    const int row = cpList->currentRow();
    if (row < 0 || row >= entries.size())
      return nullptr;
    return &entries[row];
  };

  auto updateDetails = [&] {
    if (const auto *e = currentEntry()) {
      const qint64 bytes = e->allocatedBytes > 0 ? e->allocatedBytes : e->diskBytes;
      cpDetails->setText(QString("Kind: %1 · Created: %2 · Method: %3 · Size: %4")
                             .arg(e->kind, e->created, e->method,
                                  checkpointSizeText(bytes)));
    } else if (entries.isEmpty()) {
      cpDetails->setText("No checkpoints saved for this Mac yet.");
    } else {
      cpDetails->setText("Select a checkpoint.");
    }
  };

  auto reloadList = [&](const QString &selectId = {}) {
    QSignalBlocker block(cpList);
    const QString prevId =
        !selectId.isEmpty() ? selectId
                            : (currentEntry() ? currentEntry()->id : QString());
    entries = Checkpoints::list(machineFolder);
    cpList->clear();
    int selectRow = entries.isEmpty() ? -1 : 0;
    for (int i = 0; i < entries.size(); ++i) {
      const auto &e = entries[i];
      const qint64 bytes = e.allocatedBytes > 0 ? e.allocatedBytes : e.diskBytes;
      const QString line = QString("%1 — %2 (%3)")
                               .arg(e.label, e.created,
                                    checkpointSizeText(bytes));
      auto item = new QListWidgetItem(line, cpList);
      item->setData(Qt::UserRole, e.id);
      if (e.id == prevId)
        selectRow = i;
    }
    if (selectRow >= 0)
      cpList->setCurrentRow(selectRow);
    updateDetails();
  };

  auto createBtn = button(
      "Create Checkpoint…",
      [&] {
        if (engines.contains(machineId))
          Model::fail("Shut down this Mac before creating a checkpoint.");
        PDialog prompt("Create Checkpoint", {440, 190}, &dialog);
        prompt.setObjectName("createCheckpointDialog");
        auto pv = dialogLayout(&prompt);
        pv->addWidget(label("Checkpoint name:", &prompt));
        auto nameEdit = new PEdit(
            "Checkpoint " +
                QDateTime::currentDateTime().toString("yyyy-MM-dd HH:mm"),
            &prompt);
        nameEdit->setObjectName("checkpointLabelEdit");
        nameEdit->setAccessibleName("Checkpoint name");
        pv->addWidget(nameEdit);
        pv->addStretch();
        auto cancel = button("Cancel", [&] { prompt.done(0); }, &prompt);
        auto create = button("Create", [&] { prompt.done(1); }, &prompt, true);
        buttonRow(pv, {cancel, create});
        nameEdit->setFocus();
        nameEdit->selectAll();
        if (prompt.exec() != 1)
          return;
        const QString cpLabel = nameEdit->text().trimmed();
        if (cpLabel.isEmpty())
          Model::fail("Enter a name for the checkpoint.");
        if (engines.contains(machineId))
          Model::fail("Shut down this Mac before creating a checkpoint.");
        const Model::Machine latest = Model::readMachine(machineFolder);
        QString createdId;
        runCheckpointTask(&dialog, "Creating Checkpoint",
                          "Saving a checkpoint of “" + latest.name() + "”…",
                          [&] {
                            createdId = Checkpoints::create(latest, cpLabel).id;
                          });
        reloadList(createdId);
      },
      &dialog);
  createBtn->setObjectName("createCheckpointButton");

  auto restoreBtn = button(
      "Restore…",
      [&] {
        if (engines.contains(machineId))
          Model::fail("Shut down this Mac before restoring a checkpoint.");
        const auto *e = currentEntry();
        if (!e)
          return;
        const QString targetId = e->id;
        const QString targetLabel = e->label;
        if (!confirmAction(&dialog, "Restore Checkpoint",
                           "Restore “" + initialMachine.name() +
                               "” to checkpoint “" + targetLabel +
                               "”?\n\nChanges made since this checkpoint will be "
                               "replaced.",
                           "Restore", "restoreCheckpointConfirmDialog"))
          return;
        if (engines.contains(machineId))
          Model::fail("Shut down this Mac before restoring a checkpoint.");
        const Model::Machine latest = Model::readMachine(machineFolder);
        runCheckpointTask(&dialog, "Restoring Checkpoint",
                          "Restoring “" + targetLabel + "”…", [&] {
                            Checkpoints::restore(latest, targetId);
                          });
        try {
          const Model::Machine updated = Model::readMachine(machineFolder);
          for (auto &m : machines) {
            if (m.id() == machineId || m.folder == machineFolder) {
              m = updated;
              break;
            }
          }
          render();
        } catch (const std::exception &) {
        }
        reloadList(targetId);
      },
      &dialog);
  restoreBtn->setObjectName("restoreCheckpointButton");

  auto deleteBtn = button(
      "Delete",
      [&] {
        const auto *e = currentEntry();
        if (!e)
          return;
        const QString targetId = e->id;
        if (!confirmAction(&dialog, "Delete Checkpoint",
                           "Delete checkpoint “" + e->label +
                               "”?\n\nYour Mac's current disk and other "
                               "checkpoints are not changed.",
                           "Delete", "deleteCheckpointConfirmDialog"))
          return;
        Checkpoints::remove(machineFolder, targetId);
        reloadList();
      },
      &dialog);
  deleteBtn->setObjectName("deleteCheckpointButton");

  auto actionsRow = new QHBoxLayout;
  actionsRow->setSpacing(10);
  actionsRow->addWidget(createBtn);
  actionsRow->addWidget(restoreBtn);
  actionsRow->addWidget(deleteBtn);
  actionsRow->addStretch();
  layout->addLayout(actionsRow);

  auto syncButtons = [&] {
    const bool running = engines.contains(machineId);
    const bool hasSel = currentEntry() != nullptr;
    createBtn->setEnabled(!running);
    restoreBtn->setEnabled(!running && hasSel);
    deleteBtn->setEnabled(!running && hasSel);
    runningNote->setText(
        running
            ? "Shut down this Mac before creating, restoring, or deleting checkpoints."
            : QString());
  };

  connect(cpList, &QListWidget::currentRowChanged, &dialog, [&](int) {
    updateDetails();
    syncButtons();
  });

  QTimer timer;
  connect(&timer, &QTimer::timeout, &dialog, syncButtons);
  timer.start(250);

  reloadList();
  syncButtons();
  buttonRow(layout, {button("Done", [&] { dialog.done(); }, &dialog, true)});
  dialog.exec();
}

void App::removeSelected() {
  if (!current())
    return;
  const auto machine = *current();
  const bool wasRunning = engines.contains(machine.id());
  const QString sizeText = formatDiskSize(folderSizeBytes(machine.folder));
  int result = 0;

  if (wasRunning) {
    PDialog dialog("Delete Running Mac", {560, 320}, this);
    dialog.setObjectName("removeMachineDialog");
    auto layout = dialogLayout(&dialog);
    layout->addWidget(label("Delete running Mac “" + machine.name() + "”?",
                            &dialog));
    layout->addWidget(folderField(machine, &dialog));
    layout->addWidget(label("Folder size on disk: " + sizeText, &dialog,
                            "small"));
    layout->addWidget(label(
        "Deleting a running Mac will stop the emulation engine immediately "
        "(unsaved work inside the Mac will be lost) and move its .classic "
        "folder (" +
            sizeText + ") to the " + trashName() +
            ". Attached media and shared folders outside this folder stay "
            "where they are.",
        &dialog, "regular", true));
    layout->addStretch();
    auto cancel = button("Cancel", [&] { dialog.done(0); }, &dialog, true);
    cancel->setObjectName("cancelRemoveMachine");
    auto erase = button("Force Stop & Move to " + trashName(),
                        [&] { dialog.done(2); }, &dialog);
    erase->setObjectName("deleteMachineFiles");
    buttonRow(layout, {cancel, erase});
    cancel->setFocus();
    result = dialog.exec();
  } else {
    PDialog dialog("Delete Mac", {560, 380}, this);
    dialog.setObjectName("removeMachineDialog");
    auto layout = dialogLayout(&dialog);
    layout->addWidget(label("Delete “" + machine.name() + "”?", &dialog));
    layout->addWidget(folderField(machine, &dialog));
    layout->addWidget(label("Folder size on disk: " + sizeText, &dialog,
                            "small"));
    layout->addWidget(label(
        "Remove from List keeps this folder and every file inside it. Use "
        "Open Mac later to add it again.",
        &dialog, "regular", true));
    auto keep = button("Remove from List", [&] { dialog.done(1); }, &dialog);
    keep->setObjectName("removeMachineKeepFiles");
    layout->addWidget(keep, 0, Qt::AlignLeft);
    layout->addWidget(label(
        "Move to " + trashName() +
            " moves the entire .classic folder (" + sizeText +
            "), including its virtual hard disk, to the " + trashName() +
            " immediately. Attached media and shared folders outside this "
            "folder stay where they are.",
        &dialog, "regular", true));
    auto erase = button("Move to " + trashName(),
                        [&] { dialog.done(2); }, &dialog);
    erase->setObjectName("deleteMachineFiles");
    layout->addWidget(erase, 0, Qt::AlignLeft);
    layout->addStretch();
    auto cancel = button("Cancel", [&] { dialog.done(0); }, &dialog, true);
    cancel->setObjectName("cancelRemoveMachine");
    buttonRow(layout, {cancel});
    cancel->setFocus();
    result = dialog.exec();
  }

  if (!result)
    return;

  if (wasRunning) {
    if (auto *eng = engines.value(machine.id())) {
      QEventLoop waitLoop;
      QTimer timeout;
      timeout.setSingleShot(true);
      auto conn =
          connect(eng, &Engine::exited, &waitLoop, &QEventLoop::quit);
      connect(&timeout, &QTimer::timeout, &waitLoop, &QEventLoop::quit);
      eng->forceOff();
      if (engines.contains(machine.id())) {
        timeout.start(5000);
        waitLoop.exec();
      }
      disconnect(conn);
      if (engines.contains(machine.id()))
        Model::fail("The running Mac could not be stopped. Try Force Stop in "
                    "Power, then delete it again.");
    }
  } else if (engines.contains(machine.id())) {
    Model::fail("The Mac started running. Stop it or delete it again.");
  }

  int index = -1;
  for (int i = 0; i < machines.size(); ++i)
    if (machines[i].id() == machine.id() &&
        machines[i].folder == machine.folder)
      index = i;
  if (index < 0)
    return;

  if (result == 2 && QFileInfo::exists(machine.folder)) {
    const auto folder = validatedDeleteFolder(machine, machines);
    if (!QFile::moveToTrash(folder))
      Model::fail("The machine could not be moved to the " + trashName() +
                  ". Its files were kept. Use Show Files to inspect the "
                  "folder or Remove from List to keep it.");
  }

  const auto oldMachines = machines;
  const auto oldSelection = selected;
  machines.removeAt(index);
  selected = machines.isEmpty() ? -1 : qMin(index, int(machines.size()) - 1);
  try {
    saveLibrary();
  } catch (...) {
    machines = oldMachines;
    selected = oldSelection;
    render();
    throw;
  }
  render();
}

void App::showPowerActions(Engine *engine) {
  QPointer<Engine> target(engine);
  if (!target || !target->running)
    return;
  for (auto existing : findChildren<PDialog *>())
    if (existing->objectName() == "machinePowerDialog" &&
        existing->property("machineId").toString() == target->machine.id()) {
      existing->raise();
      existing->activateWindow();
      return;
    }
  auto *owner = guests.value(target->machine.id());
  QWidget *parent = (owner && owner->isVisible() && owner->isActiveWindow())
                        ? static_cast<QWidget *>(owner)
                        : static_cast<QWidget *>(this);
  PDialog dialog("Mac Power", {570, 355}, parent);
  dialog.setObjectName("machinePowerDialog");
  dialog.setProperty("machineId", target->machine.id());
  auto layout = dialogLayout(&dialog);
  layout->addWidget(label(target->machine.name(), &dialog));
  auto status = label(powerState(target), &dialog, "small", true);
  status->setObjectName("machinePowerStatus");
  layout->addWidget(status);
  bool osx = Model::isOSXFamily(target->machine.family());
  auto guidance = label(osx
      ? "Shut Down asks Mac OS X to close its applications safely. Save your "
        "work and answer any dialogs on the Mac's screen, or use the Apple "
        "menu inside your Mac. Restart starts it again after shutdown."
      : "Shut Down asks Mac OS to close its applications safely. "
                         "Save your work and answer any dialogs on the Mac's "
                         "screen. Restart starts it again after shutdown.",
                         &dialog, "regular", true);
  layout->addWidget(guidance);
  auto normal = new QHBoxLayout;
  auto shutDown = button("Shut Down", [&] {
    if (target && target->running)
      target->shutdown();
  }, &dialog);
  shutDown->setObjectName("powerShutDown");
  auto restart = button("Restart", [&] {
    if (target && target->running)
      target->shutdown(true);
  }, &dialog);
  restart->setObjectName("powerRestart");
  normal->addWidget(shutDown);
  normal->addWidget(restart);
  normal->addStretch();
  layout->addLayout(normal);
  layout->addWidget(label("Mac not responding? Force Stop turns off the "
                          "emulation engine immediately. Unsaved changes "
                          "will be lost and the virtual disk may need repair.",
                          &dialog, "regular", true));
  auto force = button("Force Stop…", [&] {
    if (!target || !target->running)
      return;
    if (confirmAction(&dialog, "Force Stop Mac",
                      "Force stop “" + target->machine.name() + "” now?\n\n"
                      "Unsaved changes will be lost. Stopping during a disk "
                      "write can damage the virtual disk. Try Shut Down first "
                      "if the Mac is still responding.",
                      "Force Stop", "forceStopConfirmDialog"))
      if (target && target->running)
        target->forceOff();
  }, &dialog);
  force->setObjectName("powerForceStop");
  layout->addWidget(force, 0, Qt::AlignLeft);
  layout->addStretch();
  auto refresh = [&] {
    if (!target || !target->running) {
      dialog.done();
      return;
    }
    status->setText(powerState(target));
    shutDown->setEnabled(!target->stoppingRequested());
    restart->setEnabled(!target->stoppingRequested());
    force->setEnabled(!target->forcedStopRequested());
    if (target->stoppingRequested() && !target->forcedStopRequested())
      guidance->setText(
          osx ? "Mac OS X has been asked to shut down. Use Back to Mac "
                "to save work or answer its dialogs. If it stays "
                "stuck, Force Stop is available below."
              : "Mac OS has been asked to shut down. Use Back to Mac "
                "to save work or answer its dialogs. If it stays "
                "stuck, Force Stop is available below.");
  };
  connect(engine, &Engine::changed, &dialog, refresh);
  connect(engine, &Engine::exited, &dialog, [&] { dialog.done(); });
  buttonRow(layout, {button("Back to Mac", [&] {
    dialog.done();
    if (target && target->running) {
      auto window = guest(target);
      window->show();
      window->raise();
      window->activateWindow();
    }
  }, &dialog, true)});
  if (!target || !target->running)
    return;
  refresh();
  dialog.exec();
}

bool App::requestQuit() {
  if (engines.isEmpty()) {
    for (auto window : guests)
      window->close();
    QTimer::singleShot(0, qApp, &QApplication::quit);
    return true;
  }
  for (auto existing : findChildren<PDialog *>())
    if (existing->objectName() == "quitClassicMacDialog") {
      existing->raise();
      existing->activateWindow();
      return false;
    }
  PDialog dialog("Quit ClassicMac", {580, 360}, this);
  dialog.setObjectName("quitClassicMacDialog");
  auto layout = dialogLayout(&dialog);
  layout->addWidget(label("Some Macs are still running", &dialog));
  auto states = label({}, &dialog, "small", true);
  layout->addWidget(states);
  auto guidance = label("Shut Down All asks each Mac to close safely. Save "
                         "work and answer any dialogs on their screens. "
                         "ClassicMac will quit after every Mac shuts down.",
                         &dialog, "regular", true);
  layout->addWidget(guidance);
  layout->addWidget(label("If a Mac is stuck, Force Stop All turns off the "
                          "engines immediately. Unsaved work will be lost "
                          "and virtual disks may need repair.",
                          &dialog, "regular", true));
  auto force = button("Force Stop All…", [&] {
    if (confirmAction(&dialog, "Force Stop All Macs",
                      "Force stop every running Mac and quit ClassicMac?\n\n"
                      "Unsaved changes will be lost. Stopping during a disk "
                      "write can damage virtual disks.",
                      "Force Stop All", "forceStopAllConfirmDialog"))
      {
        QList<QPointer<Engine>> targets;
        for (auto engine : engines)
          targets.append(engine);
        for (auto engine : targets)
          if (engine && engine->running)
            engine->forceOff();
      }
  }, &dialog);
  force->setObjectName("quitForceStopAll");
  layout->addWidget(force, 0, Qt::AlignLeft);
  layout->addStretch();
  auto shutDown = button("Shut Down All", [&] {
    QStringList failures;
    const auto values = engines.values();
    QList<QPointer<Engine>> targets;
    for (auto engine : values)
      targets.append(engine);
    for (auto engine : targets)
      if (engine && engine->running && !engine->stoppingRequested())
        try {
          engine->shutdown();
        } catch (const std::exception &error) {
          failures.append(QString::fromUtf8(error.what()));
        }
    guidance->setText(failures.isEmpty()
        ? "Waiting for the Macs to shut down. Choose Cancel to return to "
          "their screens and save work. Force Stop All remains available "
          "if a Mac is stuck."
        : "A Mac did not respond to Shut Down. Choose Cancel to return to "
          "its screen, or Force Stop All if it is stuck.");
  }, &dialog);
  shutDown->setObjectName("quitShutDownAll");
  auto cancel = button("Cancel", [&] { dialog.done(0); }, &dialog, true);
  buttonRow(layout, {cancel, shutDown});
  auto refresh = [&] {
    if (engines.isEmpty()) {
      dialog.done(1);
      return;
    }
    QStringList lines;
    bool canShutDown = false, canForce = false;
    for (auto engine : engines) {
      lines.append(engine->machine.name() + " — " + powerState(engine));
      canShutDown |= !engine->stoppingRequested();
      canForce |= !engine->forcedStopRequested();
    }
    states->setText(lines.join('\n'));
    shutDown->setEnabled(canShutDown);
    force->setEnabled(canForce);
  };
  QTimer timer;
  connect(&timer, &QTimer::timeout, &dialog, refresh);
  timer.start(200);
  if (engines.isEmpty())
    return requestQuit();
  refresh();
  cancel->setFocus();
  if (dialog.exec() != 1)
    return false;
  return requestQuit();
}
