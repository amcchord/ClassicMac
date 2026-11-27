#include "checkpoint.h"
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QProcess>
#include <QRegularExpression>
#include <QStorageInfo>
#include <algorithm>
#include <cstring>
#ifdef Q_OS_WIN
#include <windows.h>
#else
#include <cerrno>
#include <cstdio>
#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>
#ifdef Q_OS_MACOS
#include <sys/clonefile.h>
#include <sys/mount.h>
#elif defined(Q_OS_LINUX)
#include <sys/ioctl.h>
#ifndef FICLONE
#define FICLONE _IOW(0x94, 9, int)
#endif
#endif
#endif

namespace Checkpoints {
namespace {
bool isValidId(const QString &id) {
  static const QRegularExpression re(
      QStringLiteral("^[A-Za-z0-9][A-Za-z0-9._-]{0,80}$"));
  return re.match(id).hasMatch() && id != QLatin1String(".") &&
         id != QLatin1String("..");
}

QString checkpointsRoot(const QString &machineFolder) {
  const QString canon = QFileInfo(machineFolder).canonicalFilePath();
  if (canon.isEmpty())
    Model::fail(QStringLiteral("Machine folder could not be found."));
  return canon + QStringLiteral("/Checkpoints");
}

QString checkpointDir(const QString &machineFolder,
                      const QString &checkpointId) {
  if (!isValidId(checkpointId))
    Model::fail(QStringLiteral("Invalid checkpoint identifier."));
  return checkpointsRoot(machineFolder) + QLatin1Char('/') + checkpointId;
}

qint64 allocatedBytesForFile(const QString &path) {
#ifdef Q_OS_WIN
  const std::wstring wpath = QDir::toNativeSeparators(path).toStdWString();
  DWORD high = 0;
  const DWORD low = GetCompressedFileSizeW(wpath.c_str(), &high);
  if (low != INVALID_FILE_SIZE || GetLastError() == NO_ERROR)
    return (qint64(high) << 32) | qint64(low);
  return QFileInfo(path).size();
#else
  struct stat st {};
  if (::stat(QFile::encodeName(path).constData(), &st) == 0)
    return qint64(st.st_blocks) * 512;
  return QFileInfo(path).size();
#endif
}

void ensureOwnerReadWrite(const QString &path) {
  QFile::setPermissions(path, QFile::permissions(path) |
                                  QFileDevice::ReadOwner |
                                  QFileDevice::WriteOwner |
                                  QFileDevice::ReadUser |
                                  QFileDevice::WriteUser);
}

bool isAllZero(const char *data, qint64 len) {
  for (qint64 i = 0; i < len; ++i) {
    if (data[i] != 0)
      return false;
  }
  return true;
}

QString sparseCopyFile(const QString &srcPath, const QString &dstPath) {
  QFile src(srcPath);
  if (!src.open(QFile::ReadOnly))
    Model::fail(QStringLiteral("Cannot open source file for checkpoint."));
  const qint64 totalSize = src.size();

  QFile::remove(dstPath);
  QFile dst(dstPath);
  if (!dst.open(QFile::WriteOnly | QFile::NewOnly))
    Model::fail(QStringLiteral("Cannot create checkpoint file."));
  const bool sparseSupported = Model::makeSparse(dst);

  constexpr qint64 kChunk = 1048576;
  QByteArray buf(int(kChunk), Qt::Uninitialized);
  qint64 offset = 0;
  bool skippedZeros = false;
  while (offset < totalSize) {
    const qint64 want = std::min(kChunk, totalSize - offset);
    const qint64 got = src.read(buf.data(), want);
    if (got <= 0)
      Model::fail(QStringLiteral("Failed while reading disk for checkpoint."));
    if (isAllZero(buf.constData(), got)) {
      skippedZeros = true;
    } else {
      if (!dst.seek(offset) || dst.write(buf.constData(), got) != got)
        Model::fail(QStringLiteral("Failed while writing checkpoint disk."));
    }
    offset += got;
  }
  if (!dst.resize(totalSize) || !dst.flush())
    Model::fail(QStringLiteral("Could not finish writing checkpoint disk."));
  dst.close();
  ensureOwnerReadWrite(dstPath);
  return (sparseSupported || skippedZeros) ? QStringLiteral("sparse")
                                           : QStringLiteral("copy");
}

QString copyFileCoW(const QString &srcPath, const QString &dstPath,
                    bool requireCoW = false, qint64 minSpaceIfNotCoW = 0) {
  QFile::remove(dstPath);
#ifdef Q_OS_MACOS
  if (::clonefile(QFile::encodeName(srcPath).constData(),
                  QFile::encodeName(dstPath).constData(), 0) == 0) {
    ensureOwnerReadWrite(dstPath);
    return QStringLiteral("clonefile");
  }
  QFile::remove(dstPath);
#elif defined(Q_OS_LINUX)
  {
    const int srcFd =
        ::open(QFile::encodeName(srcPath).constData(), O_RDONLY);
    if (srcFd >= 0) {
      const int dstFd = ::open(QFile::encodeName(dstPath).constData(),
                               O_WRONLY | O_CREAT | O_EXCL, 0660);
      if (dstFd >= 0) {
        const int rc = ::ioctl(dstFd, FICLONE, srcFd);
        ::close(dstFd);
        ::close(srcFd);
        if (rc == 0) {
          ensureOwnerReadWrite(dstPath);
          return QStringLiteral("reflink");
        }
        ::unlink(QFile::encodeName(dstPath).constData());
      } else {
        ::close(srcFd);
      }
    }
  }
#endif
  if (requireCoW && minSpaceIfNotCoW > 0) {
    const QStorageInfo vol(QFileInfo(dstPath).absolutePath());
    if (vol.isValid() && vol.bytesAvailable() < minSpaceIfNotCoW)
      Model::fail(
          QStringLiteral("Not enough free space for a checkpoint copy."));
  }
#ifdef Q_OS_LINUX
  {
    QProcess cp;
    cp.setProgram(QStringLiteral("cp"));
    cp.setArguments({QStringLiteral("--reflink=auto"),
                     QStringLiteral("--sparse=always"), srcPath, dstPath});
    cp.start();
    if (cp.waitForFinished(3600 * 1000) &&
        cp.exitStatus() == QProcess::NormalExit && cp.exitCode() == 0) {
      ensureOwnerReadWrite(dstPath);
      return QStringLiteral("sparse");
    }
    QFile::remove(dstPath);
  }
#endif
  return sparseCopyFile(srcPath, dstPath);
}

void replaceFileAtomically(const QString &srcTmp, const QString &dstLive) {
#ifdef Q_OS_WIN
  const std::wstring s = QDir::toNativeSeparators(srcTmp).toStdWString();
  const std::wstring d = QDir::toNativeSeparators(dstLive).toStdWString();
  if (!MoveFileExW(s.c_str(), d.c_str(),
                   MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH))
    Model::fail(QStringLiteral("Could not replace the machine file."));
#else
  if (::rename(QFile::encodeName(srcTmp).constData(),
               QFile::encodeName(dstLive).constData()) != 0)
    Model::fail(QStringLiteral("Could not replace the machine file."));
#endif
}

QString sessionStatePath(const QString &machineFolder) {
  const QString canon = QFileInfo(machineFolder).canonicalFilePath();
  return (canon.isEmpty() ? machineFolder : canon) +
         QStringLiteral("/.session-state.json");
}
} // namespace

QList<Entry> list(const QString &machineFolder) {
  QList<Entry> entries;
  const QString root = checkpointsRoot(machineFolder);
  QDir dir(root);
  if (!dir.exists())
    return entries;
  const QStringList subdirs =
      dir.entryList(QDir::Dirs | QDir::NoDotAndDotDot, QDir::Name);
  for (const QString &sub : subdirs) {
    if (!isValidId(sub))
      continue;
    const QString cpPath = root + QLatin1Char('/') + sub;
    const QFileInfo cpInfo(cpPath);
    if (cpInfo.isSymLink() || !cpInfo.isDir())
      continue;
    if (!QFileInfo(cpPath + QStringLiteral("/checkpoint.json")).isFile() ||
        (!QFileInfo(cpPath + QStringLiteral("/disk.img")).isFile() &&
         !QFileInfo(cpPath + QStringLiteral("/flash.img")).isFile()))
      continue;
    try {
      const QJsonObject meta =
          Model::readJSON(cpPath + QStringLiteral("/checkpoint.json"), 1048576);
      Entry e;
      e.id = meta.value(QStringLiteral("id")).toString(sub);
      if (e.id != sub && !isValidId(e.id))
        e.id = sub;
      e.label = meta.value(QStringLiteral("label")).toString();
      e.kind =
          meta.value(QStringLiteral("kind")).toString(QStringLiteral("manual"));
      e.created = meta.value(QStringLiteral("created")).toString();
      e.method = meta.value(QStringLiteral("method")).toString();
      e.diskBytes =
          qint64(meta.value(QStringLiteral("diskBytes")).toDouble(0));
      e.allocatedBytes =
          qint64(meta.value(QStringLiteral("allocatedBytes")).toDouble(0));
      entries.append(e);
    } catch (...) {
    }
  }
  std::sort(entries.begin(), entries.end(), [](const Entry &a, const Entry &b) {
    if (a.created != b.created)
      return a.created > b.created;
    return a.id > b.id;
  });
  return entries;
}

static Entry createImpl(const Model::Machine &machine, const QString &label,
                        const QString &kind, bool requireSpaceForFallback) {
  const QString root = checkpointsRoot(machine.folder);
  if (!QDir().mkpath(root))
    Model::fail(QStringLiteral("Cannot create the Checkpoints folder."));

  const bool isPippin = machine.family() == QLatin1String("pippin");
  const QString primaryName =
      isPippin ? Model::localName(machine.config[QStringLiteral("flashImageName")],
                                  QStringLiteral("flash.img"))
               : Model::localName(machine.config[QStringLiteral("diskImageName")],
                                  QStringLiteral("disk.img"));
  const QString diskPath = Model::machineFile(machine.folder, primaryName);
  const qint64 diskAlloc = allocatedBytesForFile(diskPath);

  const QString baseId =
      QDateTime::currentDateTimeUtc().toString(QStringLiteral("yyyyMMdd'T'HHmmss'Z'"));
  QString cpId = baseId;
  int suffix = 1;
  while (QFileInfo::exists(root + QLatin1Char('/') + cpId)) {
    ++suffix;
    cpId = baseId + QLatin1Char('-') + QString::number(suffix);
  }

  const QString staging =
      root + QStringLiteral("/.tmp-") + cpId + QStringLiteral("-") +
      QString::number(QDateTime::currentMSecsSinceEpoch());
  if (!QDir().mkpath(staging))
    Model::fail(QStringLiteral("Cannot create checkpoint staging folder."));
  bool committed = false;
  auto cleanup = qScopeGuard([&] {
    if (!committed)
      QDir(staging).removeRecursively();
  });

  const QString dstDisk = staging + QLatin1Char('/') +
                          (isPippin ? QStringLiteral("flash.img") : QStringLiteral("disk.img"));
  const QString method = copyFileCoW(
      diskPath, dstDisk, requireSpaceForFallback, diskAlloc + 256LL * 1048576);

  if (machine.family() == QLatin1String("powerMacOSX") || isPippin) {
    const QString nvramPath =
        QFileInfo(machine.folder).canonicalFilePath() + QStringLiteral("/nvram.img");
    if (QFileInfo(nvramPath).isFile() && !QFileInfo(nvramPath).isSymLink())
      copyFileCoW(nvramPath, staging + QStringLiteral("/nvram.img"));
  }
  if (machine.family() == QLatin1String("quadra800") || isPippin) {
    const QString pramName = Model::localName(
        machine.config[QStringLiteral("pramImageName")], QStringLiteral("pram.img"));
    const QString pramPath =
        QFileInfo(machine.folder).canonicalFilePath() + QLatin1Char('/') + pramName;
    if (QFileInfo(pramPath).isFile() && !QFileInfo(pramPath).isSymLink())
      copyFileCoW(pramPath, staging + QStringLiteral("/pram.img"));
  }

  const qint64 diskBytes = QFileInfo(dstDisk).size();
  const qint64 allocBytes = allocatedBytesForFile(dstDisk);
  const QString created =
      QDateTime::currentDateTimeUtc().toString(Qt::ISODate);
  const QString cleanLabel =
      label.trimmed().isEmpty() ? QStringLiteral("Checkpoint")
                                : label.trimmed().left(80);
  const QString cleanKind =
      kind.trimmed().isEmpty() ? QStringLiteral("manual")
                               : kind.trimmed().left(32);

  const QJsonObject meta{
      {QStringLiteral("schemaVersion"), 1},
      {QStringLiteral("id"), cpId},
      {QStringLiteral("label"), cleanLabel},
      {QStringLiteral("kind"), cleanKind},
      {QStringLiteral("created"), created},
      {QStringLiteral("method"), method},
      {QStringLiteral("machineId"), machine.id()},
      {QStringLiteral("machineFamily"), machine.family()},
      {QStringLiteral("diskBytes"), double(diskBytes)},
      {QStringLiteral("allocatedBytes"), double(allocBytes)},
  };
  Model::writeJSON(staging + QStringLiteral("/checkpoint.json"), meta);

  const QString finalDir = root + QLatin1Char('/') + cpId;
  if (!QDir().rename(staging, finalDir))
    Model::fail(QStringLiteral("Could not finalize the checkpoint."));
  committed = true;

  Entry e;
  e.id = cpId;
  e.label = cleanLabel;
  e.kind = cleanKind;
  e.created = created;
  e.method = method;
  e.diskBytes = diskBytes;
  e.allocatedBytes = allocBytes;
  return e;
}

Entry create(const Model::Machine &machine, const QString &label,
             const QString &kind) {
  return createImpl(machine, label, kind, false);
}

void createFreshInstallIfPossible(const QString &machineFolder) {
  try {
    const auto existing = list(machineFolder);
    for (const auto &e : existing) {
      if (e.kind == QLatin1String("fresh-install"))
        return;
    }
    const auto machine = Model::readMachine(machineFolder);
    const QStorageInfo vol(machine.folder);
    if (vol.isValid() && vol.bytesAvailable() < 64LL * 1048576)
      return;
    createImpl(machine, QStringLiteral("Fresh install"),
               QStringLiteral("fresh-install"), true);
  } catch (...) {
  }
}

void restore(const Model::Machine &machine, const QString &checkpointId) {
  const QString cpDir = checkpointDir(machine.folder, checkpointId);
  const QFileInfo cpInfo(cpDir);
  if (!cpInfo.exists() || cpInfo.isSymLink() || !cpInfo.isDir())
    Model::fail(QStringLiteral("That checkpoint does not exist."));
  const bool isPippin = machine.family() == QLatin1String("pippin");
  const QString cpDisk =
      cpDir + (isPippin ? QStringLiteral("/flash.img") : QStringLiteral("/disk.img"));
  if (!QFileInfo(cpDir + QStringLiteral("/checkpoint.json")).isFile() ||
      !QFileInfo(cpDisk).isFile())
    Model::fail(QStringLiteral("The checkpoint is incomplete."));

  const QString cpNvram = cpDir + QStringLiteral("/nvram.img");
  if (machine.family() == QLatin1String("powerMacOSX")) {
    QFile nv(cpNvram);
    if (!QFileInfo(cpNvram).isFile() || !nv.open(QFile::ReadOnly) ||
        nv.size() != 16384)
      Model::fail(
          QStringLiteral("This checkpoint's OS X startup memory is missing."));
    QString err;
    if (!Model::validateOsxNvram(nv.readAll(), &err))
      Model::fail(err);
  }

  const QString diskName =
      isPippin ? Model::localName(machine.config[QStringLiteral("flashImageName")],
                                  QStringLiteral("flash.img"))
               : Model::localName(machine.config[QStringLiteral("diskImageName")],
                                  QStringLiteral("disk.img"));
  const QString liveDisk = Model::machineFile(machine.folder, diskName);

  // Back up the current state before replacing live files.
  create(machine, QStringLiteral("Pre-restore backup"),
         QStringLiteral("pre-restore"));

  const QString canonFolder = QFileInfo(machine.folder).canonicalFilePath();
  const QString tmpDisk = canonFolder + QStringLiteral("/.") + diskName +
                          QStringLiteral(".restore");
  const QString tmpNvram = canonFolder + QStringLiteral("/.nvram.img.restore");
  const QString pramName = Model::localName(
      machine.config[QStringLiteral("pramImageName")], QStringLiteral("pram.img"));
  const QString tmpPram = canonFolder + QStringLiteral("/.") + pramName +
                          QStringLiteral(".restore");

  auto cleanup = qScopeGuard([&] {
    QFile::remove(tmpDisk);
    QFile::remove(tmpNvram);
    QFile::remove(tmpPram);
  });

  copyFileCoW(cpDisk, tmpDisk);
  bool hasNvramRestore = false;
  if ((machine.family() == QLatin1String("powerMacOSX") || isPippin) &&
      QFileInfo(cpNvram).isFile()) {
    copyFileCoW(cpNvram, tmpNvram);
    hasNvramRestore = true;
  }
  const QString cpPram = cpDir + QStringLiteral("/pram.img");
  bool hasPramRestore = false;
  if ((machine.family() == QLatin1String("quadra800") || isPippin) &&
      QFileInfo(cpPram).isFile()) {
    copyFileCoW(cpPram, tmpPram);
    hasPramRestore = true;
  }

  replaceFileAtomically(tmpDisk, liveDisk);
  if (hasNvramRestore)
    replaceFileAtomically(tmpNvram, canonFolder + QStringLiteral("/nvram.img"));
  if (hasPramRestore)
    replaceFileAtomically(tmpPram, canonFolder + QLatin1Char('/') + pramName);

  markSessionState(machine.folder, true, QStringLiteral("restored"));
}

void remove(const QString &machineFolder, const QString &checkpointId) {
  const QString cpDir = checkpointDir(machineFolder, checkpointId);
  const QFileInfo info(cpDir);
  if (!info.exists() && !info.isSymLink())
    return;
  if (info.isSymLink() || !info.isDir())
    Model::fail(QStringLiteral("Invalid checkpoint folder."));
  if (!QDir(cpDir).removeRecursively())
    Model::fail(QStringLiteral("Could not delete the checkpoint."));
}

bool lastSessionClean(const QString &machineFolder) {
  const QString path = sessionStatePath(machineFolder);
  if (!QFileInfo::exists(path))
    return true;
  try {
    const QJsonObject obj = Model::readJSON(path);
    return obj.value(QStringLiteral("clean")).toBool(false);
  } catch (...) {
    return false;
  }
}

void markSessionState(const QString &machineFolder, bool clean,
                      const QString &reason) {
  QJsonObject obj{
      {QStringLiteral("clean"), clean},
      {QStringLiteral("updatedAt"),
       QDateTime::currentDateTimeUtc().toString(Qt::ISODate)},
  };
  if (!reason.isEmpty())
    obj.insert(QStringLiteral("reason"), reason);
  Model::writeJSON(sessionStatePath(machineFolder), obj);
}
} // namespace Checkpoints
