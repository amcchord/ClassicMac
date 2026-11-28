#include "engine.h"
#include "checkpoint.h"
#include "printer.h"
#include <QDir>
#include <QMap>
#include <QPointer>
#include <QRandomGenerator>
#include <QSet>
#include <QTemporaryFile>
#include <algorithm>
#include <atomic>
#include <cmath>
#include <limits>
#include <utility>
#include <vector>
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

struct RgbDiffStats {
  int changed = 0;
  int total = 0;
};

RgbDiffStats boxDiffStats(int width, int height, const QByteArray &before,
                          const QByteArray &current, int x0, int y0, int x1,
                          int y1, int threshold = 24) {
  x0 = std::max(0, x0);
  y0 = std::max(0, y0);
  x1 = std::min(width, x1);
  y1 = std::min(height, y1);
  const qint64 expectedSize = qint64(width) * height * 3;
  if (x1 <= x0 || y1 <= y0 || before.size() != expectedSize ||
      current.size() != expectedSize)
    return {};
  int changed = 0;
  const int total = (x1 - x0) * (y1 - y0);
  const uchar *b = reinterpret_cast<const uchar *>(before.constData());
  const uchar *c = reinterpret_cast<const uchar *>(current.constData());
  for (int y = y0; y < y1; ++y) {
    const int row = y * width * 3;
    for (int x = x0; x < x1; ++x) {
      const int off = row + x * 3;
      const int diff = std::abs(int(b[off]) - int(c[off])) +
                       std::abs(int(b[off + 1]) - int(c[off + 1])) +
                       std::abs(int(b[off + 2]) - int(c[off + 2]));
      if (diff > threshold)
        ++changed;
    }
  }
  return {changed, total};
}

struct RgbPixel {
  uchar r = 0, g = 0, b = 0;
  bool operator==(const RgbPixel &o) const {
    return r == o.r && g == o.g && b == o.b;
  }
  bool operator<(const RgbPixel &o) const {
    if (r != o.r)
      return r < o.r;
    if (g != o.g)
      return g < o.g;
    return b < o.b;
  }
};

bool rgbFar(const uchar *a, const uchar *b) {
  return std::abs(int(a[0]) - int(b[0])) + std::abs(int(a[1]) - int(b[1])) +
             std::abs(int(a[2]) - int(b[2])) >
         60;
}

bool rgbFarPx(const uchar *a, const RgbPixel &b) {
  return std::abs(int(a[0]) - int(b.r)) + std::abs(int(a[1]) - int(b.g)) +
             std::abs(int(a[2]) - int(b.b)) >
         60;
}

bool menuStateChangedImpl(int w, int h, const QByteArray &before,
                          const QByteArray &after, bool wholeRow) {
  const int x0 = 30;
  const int x1 = w * 45 / 100;
  if (h < 22 || w < 200 || x1 <= x0)
    return false;
  int inkB = 0;
  int recolored = 0;
  QSet<int> colsB, colsA;
  const uchar *p = reinterpret_cast<const uchar *>(before.constData());
  const uchar *q = reinterpret_cast<const uchar *>(after.constData());
  for (int y = 2; y < 17; ++y) {
    auto mostCommon = [&](const uchar *buf, int startX, int endX, int step) {
      QMap<RgbPixel, int> counts;
      for (int x = startX; x < endX; x += step) {
        const uchar *px = buf + (y * w + x) * 3;
        counts[{px[0], px[1], px[2]}] += 1;
      }
      RgbPixel best{};
      int bestCount = -1;
      for (auto it = counts.constBegin(); it != counts.constEnd(); ++it) {
        if (it.value() > bestCount) {
          bestCount = it.value();
          best = it.key();
        }
      }
      return best;
    };
    const RgbPixel bgB =
        wholeRow ? mostCommon(p, 0, w, 2) : mostCommon(p, x0, x1, 1);
    const RgbPixel bgA =
        wholeRow ? mostCommon(q, 0, w, 2) : mostCommon(q, x0, x1, 1);
    for (int i = 0; i < x1 - x0; ++i) {
      const uchar *pb = p + (y * w + x0 + i) * 3;
      const uchar *pa = q + (y * w + x0 + i) * 3;
      if (rgbFarPx(pb, bgB)) {
        ++inkB;
        colsB.insert(i / 4);
        if (rgbFar(pb, pa))
          ++recolored;
      }
      if (rgbFarPx(pa, bgA))
        colsA.insert(i / 4);
    }
  }
  if (inkB < 40)
    return false;
  const int inter = (colsB & colsA).size();
  const int uni = std::max(1, int((colsB | colsA).size()));
  const double iou = double(inter) / double(uni);
  const double share = double(recolored) / double(inkB);
  return iou >= 0.7 && share >= 0.4;
}

bool menuStateChanged(int w, int h, const QByteArray &before,
                      const QByteArray &after) {
  if (h < 22 || w < 200)
    return false;
  if (menuStateChangedImpl(w, h, before, after, false))
    return true;
  return menuStateChangedImpl(w, h, before, after, true);
}

struct DialogBox {
  int x0 = 0, y0 = 0, x1 = 0, y1 = 0;
  int area() const { return (x1 - x0) * (y1 - y0); }
};

QList<DialogBox> changedAlertBoxes(int w, int h, const QByteArray &before,
                                   const QByteArray &after) {
  constexpr int kMenuBar = 22;
  constexpr int cw = 8;
  constexpr int step = 2;
  if (w < 240 || h <= kMenuBar)
    return {};
  const int gw = (w + cw - 1) / cw;
  const int gh = (h + step - 1) / step;
  const uchar *p = reinterpret_cast<const uchar *>(before.constData());
  const uchar *q = reinterpret_cast<const uchar *>(after.constData());
  std::vector<quint8> grid(size_t(gw) * size_t(gh), 0);
  int cellCount = 0;
  for (int y = kMenuBar; y < h; y += step) {
    const int rowOff = y * w * 3;
    if (std::memcmp(p + rowOff, q + rowOff, size_t(w) * 3) == 0)
      continue;
    const int gy = y / step;
    for (int gx = 0; gx < gw; ++gx) {
      const int sx = gx * cw * 3;
      const int len = std::min(cw * 3, w * 3 - sx);
      if (len > 0 && std::memcmp(p + rowOff + sx, q + rowOff + sx, size_t(len)) != 0) {
        grid[size_t(gy) * size_t(gw) + size_t(gx)] = 1;
        ++cellCount;
      }
    }
  }
  if (cellCount == 0 || cellCount > gw * (h / step) / 3)
    return {};
  QList<DialogBox> out;
  std::vector<QPair<int, int>> stack;
  for (int gy = 0; gy < gh; ++gy) {
    for (int gx = 0; gx < gw; ++gx) {
      if (!grid[size_t(gy) * size_t(gw) + size_t(gx)])
        continue;
      grid[size_t(gy) * size_t(gw) + size_t(gx)] = 0;
      stack.clear();
      stack.push_back({gx, gy});
      int minX = gx, maxX = gx, minY = gy, maxY = gy;
      while (!stack.empty()) {
        const auto cur = stack.back();
        stack.pop_back();
        minX = std::min(minX, cur.first);
        maxX = std::max(maxX, cur.first);
        minY = std::min(minY, cur.second);
        maxY = std::max(maxY, cur.second);
        for (int dy = -1; dy <= 1; ++dy) {
          for (int dx = -1; dx <= 1; ++dx) {
            const int nx = cur.first + dx;
            const int ny = cur.second + dy;
            if (nx >= 0 && nx < gw && ny >= 0 && ny < gh &&
                grid[size_t(ny) * size_t(gw) + size_t(nx)]) {
              grid[size_t(ny) * size_t(gw) + size_t(nx)] = 0;
              stack.push_back({nx, ny});
            }
          }
        }
      }
      const DialogBox box{minX * cw, minY * step,
                          std::min(w, (maxX + 1) * cw),
                          std::min(h, (maxY + 1) * step)};
      const int bw = box.x1 - box.x0;
      const int bh = box.y1 - box.y0;
      if (bw >= 240 && bw <= 776 && bh >= 64 && bh <= 328)
        out.append(box);
    }
  }
  return out;
}

QByteArray imageToPackedRgb888(const QImage &img, int &width, int &height) {
  if (img.isNull()) {
    width = 0;
    height = 0;
    return {};
  }
  const QImage rgb = img.format() == QImage::Format_RGB888
                         ? img
                         : img.convertToFormat(QImage::Format_RGB888);
  width = rgb.width();
  height = rgb.height();
  QByteArray out(width * height * 3, Qt::Uninitialized);
  for (int y = 0; y < height; ++y)
    std::memcpy(out.data() + y * width * 3, rgb.constScanLine(y),
                size_t(width) * 3);
  return out;
}
} // namespace
void Engine::applyQuadraBootPRAM(const QString &pramPath,
                                 const QJsonObject &config) {
  if (config["machineFamily"].toString() != "quadra800")
    return;
  QFile f(pramPath);
  if (!f.open(QIODevice::ReadWrite) || f.size() < 0x7C)
    return;
  QByteArray boot(4, '\0');
  if (config["bootFromCD"].toBool() &&
      !config["cdImagePath"].toString().isEmpty()) {
    boot = QByteArray("\xff\xff\xff\xdc", 4);
  } else if (config["bootFromFloppy"].toBool() &&
             !config["floppyImagePath"].toString().isEmpty()) {
    boot = QByteArray("\x00\x00\x0c\xa0", 4);
  } else {
    boot = QByteArray("\xff\xff\xff\xdf", 4);
  }
  if (f.seek(0x78))
    f.write(boot);
}
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
            stopPrintServer();
            running = false;
            paused = false;
            osxShuttingDown = false;
            vnc.close();
            if (!initializing) {
              const bool cleanExit =
                  !forced && code == 0 &&
                  (stopping || reason == "guest-shutdown" ||
                   reason == "guest-reset" || reason == "host-qmp-quit" ||
                   reason == "host-qmp-system-reset");
              const QString exitReason =
                  forced ? QStringLiteral("forced-off")
                         : !reason.isEmpty() ? reason
                         : code == 0 ? QStringLiteral("clean-exit")
                                     : QStringLiteral("unexpected-exit");
              try {
                Checkpoints::markSessionState(machine.folder, cleanExit,
                                              exitReason);
              } catch (...) {
              }
            }
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
              bool again =
                  (restarting &&
                   (reason == "guest-shutdown" || reason == "guest-reset")) ||
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
  stopPrintServer();
  if (process.state() != QProcess::NotRunning) {
    process.kill();
    process.waitForFinished(5000);
  }
}
void Engine::stopPrintServer() {
  if (printServer) {
    printServer->stop();
    printServer->deleteLater();
    printServer = nullptr;
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
    applyQuadraBootPRAM(pram, machine.config);
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
    const QString checkedFlash = Model::machineFile(machine.folder, "nvram.img");
    QFile nvFile(checkedFlash);
    if (!nvFile.open(QFile::ReadOnly) || nvFile.size() != 16384)
      fail("Mac OS X startup memory must be a 16 KB file named nvram.img.");
    QString nvErr;
    if (!Model::validateOsxNvram(nvFile.readAll(), &nvErr))
      fail(nvErr);
  } else if (family == "pippin") {
    const QString romPath = Model::resolvePippinROM(machine);
    const QFileInfo romInfo(romPath);
    if (romPath.isEmpty() || !romInfo.isFile() || romInfo.size() != 4194304)
      fail("Pippin requires a 4 MB Kinka boot ROM. Choose Download ROM or place pippin.rom inside ClassicMac's ROMs folder.");
    const QString flash = machine.folder + "/" +
                          Model::localName(machine.config["flashImageName"],
                                           QStringLiteral("flash.img"));
    if (!QFileInfo::exists(flash)) {
      QFile f(flash);
      if (!f.open(QFile::WriteOnly))
        fail("Cannot prepare Pippin Save Flash storage.");
      f.write(QByteArray(131072, char(0xff)));
    }
    const QString nvram = machine.folder + "/nvram.img";
    if (!QFileInfo::exists(nvram)) {
      QFile f(nvram);
      if (f.open(QFile::WriteOnly))
        f.write(QByteArray(8192, char(0x00)));
    }
    const QString pram = machine.folder + "/pram.img";
    if (!QFileInfo::exists(pram)) {
      QFile f(pram);
      if (f.open(QFile::WriteOnly))
        f.write(QByteArray(256, char(0x00)));
    }
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
  auto guard = qScopeGuard([&] {
    initializing = false;
    if (!running)
      stopPrintServer();
  });
  stopPrintServer();
  Model::PrintPorts printPorts;
  if (machine.config["printingEnabled"].toBool()) {
    printServer = new Printer::Server(
        machine.config["printerName"].toString(),
        Printer::defaultPdfDir(machine), false, this);
    if (!printServer->start())
      fail("Could not start the virtual printer service.");
#ifndef Q_OS_WIN
    printPorts.lpd = printServer->lpdPort();
    printPorts.ipp = printServer->ippPort();
    printPorts.raw = printServer->rawPort();
    printPorts.bridgePath = Printer::ensureBridgeSymlink();
#endif
  }
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
    stopping = restarting = forced = osxShuttingDown = false;
    auto args = Model::qemuArguments(machine, port, audioOverride, printPorts);
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
    try {
      Checkpoints::markSessionState(machine.folder, false,
                                    QStringLiteral("running"));
    } catch (...) {
    }
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
QImage Engine::currentFrame() const { return vnc.frame; }

bool Engine::detectOsxMenuOpened(int width, int height,
                                 const QByteArray &beforeRgb,
                                 const QByteArray &currentRgb) {
  const qint64 expectedSize = qint64(width) * height * 3;
  if (width < 640 || height < 480 || beforeRgb.size() != expectedSize ||
      currentRgb.size() != expectedSize)
    return false;
  if (beforeRgb == currentRgb)
    return false;
  const auto menu = boxDiffStats(width, height, beforeRgb, currentRgb, 0, 22,
                                 std::min(260, width), std::min(340, height));
  const auto rest = boxDiffStats(width, height, beforeRgb, currentRgb,
                                 std::min(300, width), 22, width, height);
  if (menu.total == 0 || menu.changed < 300)
    return false;
  return (double(menu.changed) / double(menu.total)) >= 0.03 &&
         (rest.total == 0 ||
          (double(rest.changed) / double(rest.total)) < 0.20);
}

bool Engine::detectOsxMenuSelectionMoved(int width, int height,
                                         const QByteArray &prevMenuRgb,
                                         const QByteArray &currentRgb) {
  const qint64 expectedSize = qint64(width) * height * 3;
  if (width < 640 || height < 480 || prevMenuRgb.size() != expectedSize ||
      currentRgb.size() != expectedSize)
    return false;
  if (prevMenuRgb == currentRgb)
    return false;
  const auto sel = boxDiffStats(width, height, prevMenuRgb, currentRgb, 4, 180,
                                std::min(250, width), std::min(340, height));
  const auto rest = boxDiffStats(width, height, prevMenuRgb, currentRgb,
                                 std::min(300, width), 22, width, height);
  return sel.total > 0 && sel.changed >= 80 &&
         (rest.total == 0 ||
          (double(rest.changed) / double(rest.total)) < 0.15);
}

bool Engine::detectOsxConfirmationDialog(int width, int height,
                                         const QByteArray &beforeRgb,
                                         const QByteArray &currentRgb) {
  const qint64 expectedSize = qint64(width) * height * 3;
  if (width < 640 || height < 480 || beforeRgb.size() != expectedSize ||
      currentRgb.size() != expectedSize)
    return false;
  if (beforeRgb == currentRgb)
    return false;
  if (menuStateChanged(width, height, beforeRgb, currentRgb)) {
    const auto boxes = changedAlertBoxes(width, height, beforeRgb, currentRgb);
    if (!boxes.isEmpty()) {
      DialogBox best = boxes.first();
      for (const auto &b : boxes) {
        if (b.area() > best.area())
          best = b;
      }
      const double cx = (best.x0 + best.x1) / 2.0;
      if ((best.x1 - best.x0) >= 160 && (best.y1 - best.y0) >= 60 &&
          cx >= width * 0.2 && cx <= width * 0.8)
        return true;
    }
  }
  const auto center =
      boxDiffStats(width, height, beforeRgb, currentRgb, width / 2 - 220, 40,
                   width / 2 + 220, height - 80);
  const auto edge = boxDiffStats(width, height, beforeRgb, currentRgb,
                                 width - 140, 22, width, height);
  return center.total > 0 && center.changed >= 2000 &&
         (edge.total == 0 ||
          (double(edge.changed) / double(edge.total)) < 0.25);
}

void Engine::shutdown(bool restart) {
  if (forced)
    return;
  if (machine.family() == QLatin1String("powerMacOSX")) {
    if (!running || process.state() != QProcess::Running || osxShuttingDown)
      return;
    bootHandoffTimer.stop();
    restarting = restart;
    stopping = true;
    osxShuttingDown = true;
    emit changed();

    QPointer<Engine> self(this);
    auto abortSequence = [&](const QString &msg) {
      if (self && running && !forced) {
        stopping = false;
        restarting = false;
        osxShuttingDown = false;
        emit changed();
        fail(msg);
      }
    };
    auto waitMs = [&](int ms) {
      if (!self || !running || forced ||
          process.state() != QProcess::Running)
        return;
      QEventLoop loop;
      QTimer timer;
      timer.setSingleShot(true);
      auto c = connect(this, &Engine::changed, &loop, [&] {
        if (!self || !running || forced)
          loop.quit();
      });
      auto f = connect(&process,
                       qOverload<int, QProcess::ExitStatus>(&QProcess::finished),
                       &loop, &QEventLoop::quit);
      connect(&timer, &QTimer::timeout, &loop, &QEventLoop::quit);
      timer.start(ms);
      loop.exec();
      disconnect(c);
      disconnect(f);
    };
    auto captureRgb = [&](int &w, int &h) -> QByteArray {
      if (!vnc.frame.isNull())
        return imageToPackedRgb888(vnc.frame, w, h);
      QTemporaryFile tmp(QDir::tempPath() +
                         QStringLiteral("/cm-osx-dump-XXXXXX.ppm"));
      if (!tmp.open())
        return {};
      const QString dumpPath = tmp.fileName();
      tmp.close();
      command(QStringLiteral("screendump"), {{QStringLiteral("filename"), dumpPath}});
      QImage img(dumpPath);
      return imageToPackedRgb888(img, w, h);
    };
    auto sendKey = [&](const QString &key) {
      hmp(QStringLiteral("sendkey ") + key + QStringLiteral(" 100"));
    };

    try {
      if (paused)
        resume();
      int bw = 0, bh = 0;
      const QByteArray beforeRgb = captureRgb(bw, bh);
      if (beforeRgb.isEmpty()) {
        abortSequence(QStringLiteral(
            "Mac OS X screen is not ready for clean shutdown."));
        return;
      }

      bool menuOpened = false;
      int mw = 0, mh = 0;
      QByteArray menuRgb;
      for (int attempt = 0; attempt < 2 && !menuOpened; ++attempt) {
        if (!self || !running || forced)
          return;
        sendKey(QStringLiteral("ctrl-f2"));
        waitMs(150);
        if (!self || !running || forced)
          return;
        sendKey(QStringLiteral("down"));
        for (int poll = 0; poll < 10; ++poll) {
          waitMs(200);
          if (!self || !running || forced)
            return;
          menuRgb = captureRgb(mw, mh);
          if (mw == bw && mh == bh &&
              detectOsxMenuOpened(bw, bh, beforeRgb, menuRgb)) {
            menuOpened = true;
            break;
          }
        }
        if (!menuOpened && attempt == 0) {
          sendKey(QStringLiteral("esc"));
          waitMs(150);
        }
      }
      if (!menuOpened) {
        abortSequence(QStringLiteral(
            "Could not open the Mac OS X Apple menu. Shut down from the guest "
            "screen or use Force Stop."));
        return;
      }

      sendKey(QStringLiteral("end"));
      waitMs(150);
      if (!self || !running || forced)
        return;
      sendKey(QStringLiteral("up"));

      bool selMoved = false;
      int sw = 0, sh = 0;
      QByteArray selRgb;
      for (int poll = 0; poll < 10; ++poll) {
        waitMs(200);
        if (!self || !running || forced)
          return;
        selRgb = captureRgb(sw, sh);
        if (sw == mw && sh == mh &&
            detectOsxMenuSelectionMoved(mw, mh, menuRgb, selRgb)) {
          selMoved = true;
          break;
        }
      }
      if (!selMoved) {
        sendKey(QStringLiteral("esc"));
        abortSequence(QStringLiteral(
            "Could not select Shut Down in the Mac OS X Apple menu. Shut down "
            "from the guest screen or use Force Stop."));
        return;
      }

      sendKey(QStringLiteral("ret"));
      bool dialogShown = false;
      int dw = 0, dh = 0;
      QByteArray dlgRgb;
      for (int poll = 0; poll < 15; ++poll) {
        waitMs(200);
        if (!self || !running || forced)
          return;
        dlgRgb = captureRgb(dw, dh);
        if ((dw == bw && dh == bh &&
             detectOsxConfirmationDialog(bw, bh, beforeRgb, dlgRgb)) ||
            (dw == mw && dh == mh &&
             detectOsxConfirmationDialog(mw, mh, menuRgb, dlgRgb))) {
          dialogShown = true;
          break;
        }
      }
      if (!dialogShown) {
        abortSequence(QStringLiteral(
            "Could not verify the Mac OS X Shut Down confirmation dialog. Shut "
            "down from the guest screen or use Force Stop."));
        return;
      }

      sendKey(QStringLiteral("ret"));
    } catch (...) {
      if (self && running && !forced) {
        stopping = false;
        restarting = false;
        osxShuttingDown = false;
        emit changed();
        throw;
      }
    }
    return;
  }
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
  osxShuttingDown = false;
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
