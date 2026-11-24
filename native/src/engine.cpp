#include "engine.h"
#include <QRandomGenerator>
#include <atomic>
#include <limits>
using Model::fail;
namespace {
bool readDiskBytes(QFile &f, quint64 offset, int count, quint64 fileSize,
                   QByteArray &out) {
  if (count <= 0 || offset > fileSize || quint64(count) > fileSize - offset ||
      offset > quint64(std::numeric_limits<qint64>::max()))
    return false;
  if (!f.seek(qint64(offset)))
    return false;
  out = f.read(count);
  return out.size() == count;
}
quint16 be16(const QByteArray &d, int off) {
  return (quint16(quint8(d[off])) << 8) | quint16(quint8(d[off + 1]));
}
quint32 be32(const QByteArray &d, int off) {
  return (quint32(quint8(d[off])) << 24) |
         (quint32(quint8(d[off + 1])) << 16) |
         (quint32(quint8(d[off + 2])) << 8) | quint32(quint8(d[off + 3]));
}
bool isPowerOfTwo(quint64 v) { return v != 0 && (v & (v - 1)) == 0; }
bool isBlessedVolume(QFile &f, quint64 volumeStart, quint64 byteCount,
                     quint64 fileSize) {
  if (volumeStart > std::numeric_limits<quint64>::max() - 1024)
    return false;
  QByteArray header;
  if (!readDiskBytes(f, volumeStart + 1024, 512, fileSize, header))
    return false;
  const quint16 sig = be16(header, 0);
  if (sig == 0x482B || sig == 0x4858)
    return be32(header, 80) != 0;
  if (sig != 0x4244)
    return false;
  const quint16 embeddedSig = be16(header, 124);
  if (embeddedSig == 0x482B || embeddedSig == 0x4858) {
    const quint32 allocBlockSize = be32(header, 20);
    const quint16 allocStart = be16(header, 28);
    const quint16 embeddedStartBlock = be16(header, 126);
    const quint16 embeddedBlockCount = be16(header, 128);
    if (allocBlockSize < 512 || !isPowerOfTwo(allocBlockSize) ||
        embeddedBlockCount == 0)
      return false;
    const quint64 allocBase = quint64(allocStart) * 512ULL;
    const quint64 embeddedWithin =
        quint64(embeddedStartBlock) * quint64(allocBlockSize);
    if (allocBase > std::numeric_limits<quint64>::max() - embeddedWithin)
      return false;
    const quint64 relativeStart = allocBase + embeddedWithin;
    const quint64 embeddedLen =
        quint64(embeddedBlockCount) * quint64(allocBlockSize);
    if (relativeStart > std::numeric_limits<quint64>::max() - embeddedLen ||
        relativeStart + embeddedLen > byteCount ||
        volumeStart > std::numeric_limits<quint64>::max() - relativeStart)
      return false;
    const quint64 embeddedStart = volumeStart + relativeStart;
    if (embeddedStart > std::numeric_limits<quint64>::max() - 1024)
      return false;
    QByteArray embeddedHeader;
    if (!readDiskBytes(f, embeddedStart + 1024, 512, fileSize, embeddedHeader))
      return false;
    const quint16 actualSig = be16(embeddedHeader, 0);
    if (actualSig != 0x482B && actualSig != 0x4858)
      return false;
    return be32(embeddedHeader, 80) != 0;
  }
  return be32(header, 92) != 0;
}
} // namespace
bool Engine::hasBlessedSystemFolder(const QString &diskPath) {
  QFile f(diskPath);
  if (!f.open(QFile::ReadOnly))
    return false;
  const qint64 rawSize = f.size();
  if (rawSize < 1536)
    return false;
  const quint64 fileSize = quint64(rawSize);
  QByteArray blockZero;
  if (!readDiskBytes(f, 0, 512, fileSize, blockZero))
    return false;
  quint64 blockSize = 512;
  if (be16(blockZero, 0) == 0x4552) {
    const quint64 candidate = be16(blockZero, 2);
    if (candidate >= 512 && candidate <= 4096 && isPowerOfTwo(candidate))
      blockSize = candidate;
  }
  QByteArray firstEntry;
  if (!readDiskBytes(f, blockSize, int(blockSize), fileSize, firstEntry) ||
      be16(firstEntry, 0) != 0x504D)
    return isBlessedVolume(f, 0, fileSize, fileSize);
  const quint32 declaredEntries = be32(firstEntry, 4);
  if (declaredEntries == 0)
    return false;
  const quint32 entryCount = qMin<quint32>(declaredEntries, 4096);
  for (quint32 index = 1; index <= entryCount; ++index) {
    QByteArray entry;
    if (!readDiskBytes(f, blockSize * quint64(index), int(blockSize), fileSize,
                       entry) ||
        be16(entry, 0) != 0x504D)
      continue;
    QByteArray rawType = entry.mid(48, 32);
    const int nul = rawType.indexOf('\0');
    if (nul >= 0)
      rawType.truncate(nul);
    const QString type = QString::fromLatin1(rawType);
    if (type != "Apple_HFS" && type != "Apple_HFSX")
      continue;
    const quint64 start = blockSize * quint64(be32(entry, 8));
    const quint64 length = blockSize * quint64(be32(entry, 12));
    if (start >= fileSize || length < 1536)
      continue;
    if (isBlessedVolume(f, start, length, fileSize))
      return true;
  }
  return false;
}
Engine::Engine(Model::Machine m, QObject *p)
    : QObject(p), machine(m), vnc(this) {
  bootHandoffTimer.setSingleShot(true);
  connect(&bootHandoffTimer, &QTimer::timeout, this, [this] {
    if (!running || stopping || forced ||
        process.state() != QProcess::Running)
      return;
    if (paused) {
      bootHandoffTimer.start(1000);
      return;
    }
    try {
      hmp("classicmac-boot-complete");
    } catch (...) {
    }
  });
  connect(&process, &QProcess::readyReadStandardOutput, this, &Engine::read);
  connect(&process, &QProcess::readyReadStandardError, this, [&] {
    log =
        (log + QString::fromUtf8(process.readAllStandardError())).right(65536);
  });
  connect(&process, qOverload<int, QProcess::ExitStatus>(&QProcess::finished),
          this, [&](int code, QProcess::ExitStatus) {
            bootHandoffTimer.stop();
            running = false;
            paused = false;
            vnc.close();
            if (!initializing && code == 0 && !forced &&
                machine.config["bootFromCD"].toBool() &&
                !machine.config["cdImagePath"].toString().isEmpty()) {
              try {
                auto saved = Model::readMachine(machine.folder);
                if (saved.config["bootFromCD"].toBool() &&
                    saved.config["cdImagePath"].toString() ==
                        machine.config["cdImagePath"].toString()) {
                  const QString diskPath = Model::machineFile(
                      machine.folder, saved.config["diskImageName"].toString());
                  if (hasBlessedSystemFolder(diskPath)) {
                    saved.config["bootFromCD"] = false;
                    saved.config = Model::validate(saved.config);
                    Model::writeJSON(machine.folder + "/config.json",
                                     saved.config);
                    machine = saved;
                  }
                }
              } catch (...) {
              }
            }
            emit changed();
            if (!initializing) {
              bool again = (restarting && reason == "guest-shutdown") ||
                           (!stopping && (reason == "guest-reset" ||
                                          reason == "host-qmp-system-reset"));
              emit exited(again, code && !stopping
                                     ? "The Mac stopped unexpectedly. " + log
                                     : QString());
            }
          });
}
Engine::~Engine() { // Application close is refused while a guest is mounted.
  // This path is reached only after a normal exit or explicit Force Off.
  bootHandoffTimer.stop();
  if (process.state() != QProcess::NotRunning) {
    process.kill();
    process.waitForFinished(5000);
  }
}
void Engine::read() {
  buffer += process.readAllStandardOutput();
  if (buffer.size() > 1048576) {
    process.kill();
    return;
  }
  for (;;) {
    int at = buffer.indexOf('\n');
    if (at < 0)
      break;
    auto line = buffer.left(at);
    buffer.remove(0, at + 1);
    auto o = QJsonDocument::fromJson(line).object();
    if (o.contains("QMP"))
      emit greeting();
    if (o["event"].toString() == "SHUTDOWN")
      reason = o["data"].toObject()["reason"].toString();
    if (o.contains("id"))
      emit response(o["id"].toInt(), o);
  }
}
void Engine::start() {
  machine = Model::readMachine(machine.folder);
  const QString r = Model::runtime();
  const QString family = machine.family();
  if (family == "powerMac7500")
    fail("Copland requires the macOS edition.");
  const bool cdBoot = machine.config["bootFromCD"].toBool() &&
                      !machine.config["cdImagePath"].toString().isEmpty();
  const bool ppc = family == "powerMacG4";
  const bool legacy = ppc && machine.config["useVirtioDisk"].toBool();
  const bool compatCD =
      cdBoot && (!machine.config["useG4CPU"].toBool() || legacy);
  const bool ati =
      ppc && !cdBoot && machine.config["graphicsCard"].toString() == "atiRadeon";
  const bool tools =
      machine.config["toolsCDInserted"].toBool() && (ppc ? !cdBoot : true);
  const bool sharing =
      !cdBoot && family != "powerMacOSX" &&
      !machine.config["sharedFolderPath"].toString().isEmpty();
  const bool needsNdrvLoader =
      ppc && (tools || machine.config["tabletInput"].toBool() || ati ||
              legacy || compatCD || sharing);

  auto executable = family == "quadra800"    ? "qemu-system-m68k"
                    : family == "powerMacOSX" ? "qemu-system-ppc-osx"
                                              : "qemu-system-ppc";
  const QString binPath = Model::binary(executable);
  if (!QFileInfo(binPath).isExecutable())
    fail("ClassicMac's emulation engine is missing or damaged. Reinstall ClassicMac.");

  auto requireRuntimeFile = [&](const QString &rel) {
    if (!QFileInfo(r + "/" + rel).isFile())
      fail("Part of ClassicMac's emulation engine is missing (" + rel +
           "). Reinstall ClassicMac.");
  };
  if (family == "quadra800") {
    requireRuntimeFile("Quadra800.rom");
    requireRuntimeFile("declrom");
    if (machine.config["useEnhancedFramebuffer"].toBool())
      requireRuntimeFile("qemu/pc-bios/mac_qfb.rom");
    auto pram =
        machine.folder + "/" + machine.config["pramImageName"].toString();
    if (!QFileInfo::exists(pram)) {
      requireRuntimeFile("pram-seed.img");
      if (!QFile::copy(r + "/pram-seed.img", pram))
        fail("Cannot prepare parameter memory.");
      QFile::setPermissions(pram, QFile::permissions(pram) |
                                      QFileDevice::ReadOwner |
                                      QFileDevice::WriteOwner |
                                      QFileDevice::ReadUser |
                                      QFileDevice::WriteUser);
    }
    Model::machineFile(machine.folder,
                       machine.config["pramImageName"].toString());
  } else if (family == "powerMacOSX") {
    requireRuntimeFile("qemu-osx/openbios-ppc-osx");
    requireRuntimeFile("qemu-osx/qemu_vga.ndrv");
    const auto flash = machine.folder + "/nvram.img";
    if (!QFileInfo::exists(flash)) {
      requireRuntimeFile("qemu-osx/macosx-flash-seed.img");
      if (!QFile::copy(r + "/qemu-osx/macosx-flash-seed.img", flash))
        fail("Cannot prepare Mac OS X startup memory.");
      QFile::setPermissions(flash, QFile::permissions(flash) |
                                       QFileDevice::ReadOwner |
                                       QFileDevice::WriteOwner |
                                       QFileDevice::ReadUser |
                                       QFileDevice::WriteUser);
    }
    if (QFileInfo(Model::machineFile(machine.folder, "nvram.img")).size() !=
        16384)
      fail("Mac OS X startup memory must be a 16 KB file named nvram.img.");
  } else {
    requireRuntimeFile("qemu/pc-bios/openbios-ppc");
    requireRuntimeFile("qemu/pc-bios/vgabios-stdvga.bin");
    requireRuntimeFile("qemu/pc-bios/qemu_vga.ndrv");
    if (needsNdrvLoader)
      requireRuntimeFile("ndrvloader");
  }
  if (tools)
    requireRuntimeFile("ClassicMacTools.iso");
#ifndef Q_OS_WIN
  if (sharing) {
    const QFileInfo sf(machine.config["sharedFolderPath"].toString());
    if (!sf.exists() || !sf.isDir())
      fail("The shared folder could not be found. Choose it again in Media.");
  }
#endif
  for (auto key : {"cdImagePath", "floppyImagePath"})
    if (!machine.config[key].toString().isEmpty() &&
        !QFileInfo(machine.config[key].toString()).isFile())
      fail("The attached media image is missing. Choose it again in Media.");

  initializing = true;
  auto guard = qScopeGuard([&] { initializing = false; });
  silentAudioFallback = false;
  audioUnavailable = false;
  QString audioOverride;
  for (int attempt = 0; attempt < 6; ++attempt) {
    port = 0;
    for (int p = 0; p < 16; ++p) {
      QTcpServer probe;
      const quint16 candidate =
          p < 2 ? 0
                : quint16(QRandomGenerator::system()->bounded(5900, 65000));
      if (probe.listen(QHostAddress::LocalHost, candidate)) {
        const int bound = probe.serverPort();
        probe.close();
        if (bound >= 5900) {
          port = bound;
          break;
        }
      }
    }
    if (port < 5900)
      fail("Cannot reserve the guest display port.");
    password = QByteArray::number(QRandomGenerator::system()->generate64(), 16)
                   .left(8);
    log.clear();
    buffer.clear();
    reason.clear();
    stopping = restarting = forced = false;
    auto args = Model::qemuArguments(machine, port, audioOverride);
    process.setProgram(binPath);
    process.setArguments(args);
    process.setWorkingDirectory(r + "/bin");
    process.setProcessEnvironment(Model::hostEnvironment());
#ifdef Q_OS_WIN
    process.setCreateProcessArgumentsModifier(
        [](QProcess::CreateProcessArguments *a) { a->flags |= 0x08000000; });
#endif
    QEventLoop loop;
    QTimer timer;
    timer.setSingleShot(true);
    bool ready = false;
    auto g = connect(this, &Engine::greeting, &loop, [&] {
      ready = true;
      loop.quit();
    });
    auto f = connect(&process,
                     qOverload<int, QProcess::ExitStatus>(&QProcess::finished),
                     &loop, &QEventLoop::quit);
    connect(&timer, &QTimer::timeout, &loop, &QEventLoop::quit);
    process.start();
    if (!process.waitForStarted(5000)) {
      disconnect(g);
      disconnect(f);
      fail("The emulation engine could not start: " + process.errorString());
    }
    timer.start(15000);
    loop.exec();
    disconnect(g);
    disconnect(f);
    if (!ready) {
      process.kill();
      process.waitForFinished(5000);
      log = (log + QString::fromUtf8(process.readAllStandardError()))
                .right(65536);
      if (log.contains("Address already in use") && attempt < 5)
        continue;
      static const QRegularExpression audioRe(
          "XDG_RUNTIME_DIR not set|Wrong context state|audio:|dsound:|"
          "pulseaudio:|DirectSound|cooperative level|"
          "could not stat pidfile .*pulse.*pid|pa_context_connect.*failed",
          QRegularExpression::CaseInsensitiveOption);
      if (machine.config["sound"].toBool() && audioRe.match(log).hasMatch()) {
#ifdef Q_OS_LINUX
        if (audioOverride.isEmpty() || audioOverride == "pa") {
          audioOverride = "alsa";
          continue;
        }
#endif
        if (!silentAudioFallback) {
          audioOverride = "none";
          silentAudioFallback = true;
          audioUnavailable = true;
          continue;
        }
      }
      fail("The emulation engine did not start. " + log);
    }
    running = true;
    try {
      command("qmp_capabilities");
      command("set_password", {{"protocol", "vnc"},
                               {"password", QString::fromLatin1(password)}});
    } catch (...) {
      process.kill();
      process.waitForFinished(5000);
      throw;
    }
    vnc.open(port, password);
    bootHandoffTimer.stop();
    if (args.contains("-icount"))
      bootHandoffTimer.start(15000);
    emit changed();
    return;
  }
  fail("The emulation engine could not start after retrying.");
}
QJsonValue Engine::command(QString name, QJsonObject args) {
  if (process.state() != QProcess::Running)
    fail("The Mac is not running.");
  int id = ++sequence;
  QJsonObject result;
  bool received = false;
  QEventLoop loop;
  QTimer timer;
  timer.setSingleShot(true);
  auto c = connect(this, &Engine::response, &loop, [&](int n, QJsonObject o) {
    if (n == id) {
      result = o;
      received = true;
      loop.quit();
    }
  });
  auto f = connect(&process,
                   qOverload<int, QProcess::ExitStatus>(&QProcess::finished),
                   &loop, &QEventLoop::quit);
  connect(&timer, &QTimer::timeout, &loop, &QEventLoop::quit);
  QJsonObject request = {{"execute", name}, {"arguments", args}, {"id", id}};
  process.write(QJsonDocument(request).toJson(QJsonDocument::Compact) + '\n');
  timer.start(5000);
  loop.exec();
  disconnect(c);
  disconnect(f);
  if (!received)
    fail("The Mac did not respond. " + log);
  if (result.contains("error"))
    fail(result["error"].toObject()["desc"].toString());
  return result["return"];
}
QJsonValue Engine::hmp(const QString &s) {
  return command("human-monitor-command", {{"command-line", s}});
}
void Engine::pause() {
  command("stop");
  paused = true;
  emit changed();
}
void Engine::resume() {
  command("cont");
  paused = false;
  emit changed();
}
void Engine::shutdown(bool restart) {
  if (forced)
    return;
  bootHandoffTimer.stop();
  restarting = restart;
  stopping = true;
  emit changed();
  // Ask the classic OS to quit applications and shut down. Sending Power
  // followed by a timed Return races the confirmation dialog on busy guests
  // and can instead accept an unrelated document dialog.
  // Leave time for ADB to poll all four key-downs on a busy emulated Mac.
  try {
    if (paused)
      resume();
    hmp("sendkey ctrl-alt-meta_l-power 500");
  } catch (...) {
    // A Force Stop can be chosen while QMP is waiting in its event loop.
    // That deliberate process exit is not another failure to show the user.
    if (running && !forced)
      throw;
  }
}
void Engine::forceOff() {
  bootHandoffTimer.stop();
  restarting = false;
  stopping = true;
  forced = true;
  // A hung engine may never service QMP. Kill the owned process directly and
  // let QProcess::finished complete normal cleanup without blocking the UI.
  process.kill();
  emit changed();
}
QList<QStringList> Engine::pastePlan(QString s) {
  s.replace("\r\n", "\n");
  s.replace('\r', '\n');
  s = s.normalized(QString::NormalizationForm_C);
  if (s.isEmpty() || s.size() > 8192)
    fail("Paste between 1 and 8,192 characters at a time.");
  QMap<QChar, QStringList> map;
  QString lower = "abcdefghijklmnopqrstuvwxyz0123456789";
  for (auto ch : lower)
    map[ch] = {QString(ch)};
  for (auto ch : QString("ABCDEFGHIJKLMNOPQRSTUVWXYZ"))
    map[ch] = {"shift-" + QString(ch.toLower())};
  QStringList plain = {"`", "-", "=", "[", "]", "\\", ";", "'", ",", ".", "/",
                       "1", "2", "3", "4", "5", "6",  "7", "8", "9", "0"},
              shift = {"~", "_", "+", "{", "}", "|", ":", "\"", "<", ">", "?",
                       "!", "@", "#", "$", "%", "^", "&", "*",  "(", ")"},
              names = {"grave_accent",
                       "minus",
                       "equal",
                       "bracket_left",
                       "bracket_right",
                       "backslash",
                       "semicolon",
                       "apostrophe",
                       "comma",
                       "dot",
                       "slash",
                       "1",
                       "2",
                       "3",
                       "4",
                       "5",
                       "6",
                       "7",
                       "8",
                       "9",
                       "0"};
  for (int i = 0; i < plain.size(); ++i) {
    map[plain[i][0]] = {names[i]};
    map[shift[i][0]] = {"shift-" + names[i]};
  }
  map[' '] = {"spc"};
  map['\t'] = {"tab"};
  map['\n'] = {"ret"};
  QStringList accents = {"e|áéíóú|ÁÉÍÓÚ|aeiou", "u|äëïöüÿ|ÄËÏÖÜŸ|aeiouy",
                         "i|âêîôû|ÂÊÎÔÛ|aeiou",
                         "grave_accent|àèìòù|ÀÈÌÒÙ|aeiou", "n|ãñõ|ÃÑÕ|ano"};
  for (auto a : accents) {
    auto p = a.split('|');
    for (int i = 0; i < p[1].size(); ++i) {
      map[p[1][i]] = {"alt-" + p[0], QString(p[3][i])};
      map[p[2][i]] = {"alt-" + p[0], "shift-" + QString(p[3][i])};
    }
  }
  for (auto item :
       QStringList{"ç|alt-c", "Ç|alt-shift-c", "å|alt-a", "Å|alt-shift-a",
                   "ø|alt-o", "Ø|alt-shift-o", "ß|alt-s"}) {
    auto a = item.split('|');
    map[a[0][0]] = {a[1]};
  }
  QList<QStringList> result;
  for (auto ch : s) {
    if (!map.contains(ch))
      fail(QString("Unsupported character U+%1. Nothing has been pasted.")
               .arg(uint(ch.unicode()), 4, 16, QChar('0')));
    result << map[ch];
  }
  return result;
}
void Engine::paste(QString text, bool slow, std::atomic_bool &cancel,
                   const std::function<void(int, int)> &progress) {
  auto plan = pastePlan(text);
  int complete = 0;
  for (auto sequence : plan) {
    if (cancel || !running || paused)
      fail("Pasting stopped. Text already entered remains in the Mac.");
    for (auto key : sequence) {
      auto response = hmp("sendkey " + key + " 1").toString();
      if (QRegularExpression("unknown key|error:|unknown command",
                             QRegularExpression::CaseInsensitiveOption)
              .match(response)
              .hasMatch())
        fail("The Mac rejected a key chord.");
      QEventLoop loop;
      QTimer::singleShot(slow ? 125 : 50, &loop, &QEventLoop::quit);
      loop.exec();
    }
    progress(++complete, plan.size());
  }
}
