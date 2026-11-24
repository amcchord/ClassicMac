#include "app.h"

namespace {
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
  PDialog dialog("Manage Mac", {540, 410}, this);
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
  auto remove = button("Remove Mac…", [&] {
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
    remove->setEnabled(!engine);
    explanation->setText(engine
        ? "Shut down this Mac before removing it. If it stops responding, "
          "open Power to force it to stop."
        : "Remove Mac lets you keep the files or move the entire machine "
          "folder to the " + trashName() + ".");
  };
  QTimer timer;
  connect(&timer, &QTimer::timeout, &dialog, refresh);
  timer.start(250);
  refresh();
  buttonRow(layout, {button("Done", [&] { dialog.done(); }, &dialog, true)});
  dialog.exec();
}

void App::removeSelected() {
  if (!current())
    return;
  const auto machine = *current();
  if (engines.contains(machine.id())) {
    problem(this, "Shut down this Mac before removing it. Open Power to stop "
                  "a Mac that is not responding.");
    return;
  }
  PDialog dialog("Remove Mac", {560, 370}, this);
  dialog.setObjectName("removeMachineDialog");
  auto layout = dialogLayout(&dialog);
  layout->addWidget(label("Remove “" + machine.name() + "”?", &dialog));
  layout->addWidget(folderField(machine, &dialog));
  layout->addWidget(label("Remove from List keeps this folder and every file "
                          "inside it. Use Open Mac later to add it again.",
                          &dialog, "regular", true));
  auto keep = button("Remove from List", [&] { dialog.done(1); }, &dialog);
  keep->setObjectName("removeMachineKeepFiles");
  layout->addWidget(keep, 0, Qt::AlignLeft);
  layout->addWidget(label("Delete Files moves the entire .classic folder, "
                          "including its virtual hard disk, to the " +
                          trashName() + ". Attached media and shared folders "
                          "outside this folder stay where they are.",
                          &dialog, "regular", true));
  auto erase = button("Delete Files…", [&] {
    validatedDeleteFolder(machine, machines);
    if (confirmAction(&dialog, "Delete Mac Files",
                      "Move “" + machine.name() + "” and all files in its "
                      ".classic folder to the " + trashName() + "?\n\n" +
                      QDir::toNativeSeparators(machine.folder) +
                      "\n\nThis includes everything saved on this Mac's "
                      "virtual hard disk. You can restore the folder from "
                      "the " + trashName() + " until it is emptied.",
                      "Move to " + trashName(), "deleteMachineConfirmDialog"))
      dialog.done(2);
  }, &dialog);
  erase->setObjectName("deleteMachineFiles");
  layout->addWidget(erase, 0, Qt::AlignLeft);
  layout->addStretch();
  auto cancel = button("Cancel", [&] { dialog.done(0); }, &dialog, true);
  cancel->setObjectName("cancelRemoveMachine");
  buttonRow(layout, {cancel});
  cancel->setFocus();
  const auto result = dialog.exec();
  if (!result)
    return;
  if (engines.contains(machine.id()))
    Model::fail("The Mac is running. Shut it down before removing it.");
  int index = -1;
  for (int i = 0; i < machines.size(); ++i)
    if (machines[i].id() == machine.id() && machines[i].folder == machine.folder)
      index = i;
  if (index < 0)
    return;
  if (result == 2) {
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
  bool osx = target->machine.family() == "powerMacOSX";
  auto guidance = label(osx
      ? "Choose Shut Down or Restart from the Apple menu inside your Mac."
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
  if (osx) {
    shutDown->hide();
    restart->hide();
  } else {
    normal->addWidget(shutDown);
    normal->addWidget(restart);
  }
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
      guidance->setText("Mac OS has been asked to shut down. Use Back to Mac "
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
      if (engine && engine->running && !engine->stoppingRequested() &&
          engine->machine.family() != "powerMacOSX")
        try {
          engine->shutdown();
        } catch (const std::exception &error) {
          failures.append(QString::fromUtf8(error.what()));
        }
    const auto needsGuestShutdown = std::any_of(targets.begin(), targets.end(), [](const auto &engine) {
      return engine && engine->running && engine->machine.family() == "powerMacOSX";
    });
    guidance->setText(needsGuestShutdown
        ? "Choose Cancel to return to each Mac OS X screen and select Shut Down from its Apple menu."
        : failures.isEmpty()
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
      canShutDown |= !engine->stoppingRequested() && engine->machine.family() != "powerMacOSX";
      canForce |= !engine->forcedStopRequested();
    }
    states->setText(lines.join('\n'));
    if (!canShutDown && !engines.isEmpty() &&
        std::all_of(engines.begin(), engines.end(), [](const auto *engine) {
          return engine->machine.family() == "powerMacOSX";
        }))
      guidance->setText("Choose Cancel to return to each Mac OS X screen and select Shut Down from its Apple menu.");
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
