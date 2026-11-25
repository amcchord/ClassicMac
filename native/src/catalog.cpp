#include "catalog.h"
#include "CMachineArchive.h"
#include "checkpoint.h"
#include <QElapsedTimer>
#include <QMutex>
#include <QMutexLocker>
#include <QtNetwork>
#include <algorithm>
#include <cstdlib>
#include <cstring>
#include <memory>
#include <zlib.h>
#ifdef Q_OS_WIN
#include <io.h>
#include <windows.h>
#else
#include <fcntl.h>
#include <sys/file.h>
#include <unistd.h>
#ifdef Q_OS_LINUX
#include <sys/sysinfo.h>
#include <sys/vfs.h>
#elif defined(Q_OS_MACOS)
#include <sys/mount.h>
#include <sys/sysctl.h>
#endif
#endif
using Model::fail;
namespace Library {
static constexpr qint64 limit = 140LL * 1073741824;
static QMutex gPartialWarningMutex;
static QString gPartialWarning;
static void checkCancel(std::atomic_bool &cancel) {
  if (cancel)
    fail("Paused. Your download is saved. Choose Download again to resume.");
}
static bool isValidSecureURL(const QString &s) {
  QUrl u(s);
  return u.isValid() && u.scheme() == "https" && u.userInfo().isEmpty() &&
         !u.hasFragment() && !u.host().isEmpty();
}
static QUrl secureURL(const QString &s) {
  QUrl u(s);
  if (!isValidSecureURL(s))
    fail("Library downloads require HTTPS.");
  return u;
}
static QJsonObject resolvedArchiveEntry(QJsonObject m) {
  if (m.contains("thinArchive") && m["thinArchive"].isObject()) {
    const auto thin = m["thinArchive"].toObject();
    if (thin["format"].toString() == "retroboot-thin-1") {
      const double thinBytes = thin["archiveBytes"].toDouble();
      const QString thinSha = thin["sha256"].toString();
      const QString thinUrl = thin["archiveURL"].toString();
      if (isValidSecureURL(thinUrl) && thin["archiveBytes"].isDouble() &&
          thinBytes > 0 && thinBytes <= limit &&
          thinBytes == qint64(thinBytes) &&
          QRegularExpression("^[a-f0-9]{64}$").match(thinSha).hasMatch()) {
        m["archiveURL"] = thinUrl;
        m["archiveBytes"] = thinBytes;
        m["sha256"] = thinSha;
      }
    }
  }
  return m;
}
static QString legacyCacheDirectory() {
  return Model::dataDirectory() + "/Downloads";
}
static QString cacheDirectory() {
  if (!qEnvironmentVariableIsEmpty("CLASSICMAC_TEST_HOME"))
    return legacyCacheDirectory();
  const QString base =
      QStandardPaths::writableLocation(QStandardPaths::CacheLocation);
  return base.isEmpty() ? legacyCacheDirectory() : base + "/Downloads";
}
static QString existingAncestor(const QString &path) {
  QString probe = QDir::cleanPath(path);
  while (!probe.isEmpty() && !QFileInfo::exists(probe)) {
    const QString up = QFileInfo(probe).path();
    if (up == probe)
      break;
    probe = up;
  }
  return probe;
}
class DownloadLock {
#ifdef Q_OS_WIN
  HANDLE handle = INVALID_HANDLE_VALUE;
#else
  int fd = -1;
#endif
public:
  DownloadLock() = default;
  ~DownloadLock() { unlock(); }
  DownloadLock(const DownloadLock &) = delete;
  DownloadLock &operator=(const DownloadLock &) = delete;
  bool tryLock(const QString &lockPath) {
    unlock();
#ifdef Q_OS_WIN
    handle = CreateFileW(reinterpret_cast<LPCWSTR>(lockPath.utf16()),
                         GENERIC_READ | GENERIC_WRITE,
                         FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                         nullptr, OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (handle == INVALID_HANDLE_VALUE)
      return false;
    OVERLAPPED ov{};
    if (!LockFileEx(handle,
                    LOCKFILE_EXCLUSIVE_LOCK | LOCKFILE_FAIL_IMMEDIATELY, 0,
                    MAXDWORD, MAXDWORD, &ov)) {
      CloseHandle(handle);
      handle = INVALID_HANDLE_VALUE;
      return false;
    }
    return true;
#else
    fd = ::open(QFile::encodeName(lockPath).constData(),
                O_RDWR | O_CREAT | O_NOFOLLOW, 0600);
    if (fd < 0)
      return false;
    if (::flock(fd, LOCK_EX | LOCK_NB) != 0) {
      ::close(fd);
      fd = -1;
      return false;
    }
    return true;
#endif
  }
  void unlock() {
#ifdef Q_OS_WIN
    if (handle != INVALID_HANDLE_VALUE) {
      OVERLAPPED ov{};
      UnlockFileEx(handle, 0, MAXDWORD, MAXDWORD, &ov);
      CloseHandle(handle);
      handle = INVALID_HANDLE_VALUE;
    }
#else
    if (fd >= 0) {
      ::flock(fd, LOCK_UN);
      ::close(fd);
      fd = -1;
    }
#endif
  }
};
QJsonArray parse(const QByteArray &data) {
  if (data.size() > 1048576)
    fail("The Library catalog is too large.");
  QJsonParseError error;
  auto doc = QJsonDocument::fromJson(data, &error);
  auto o = doc.object();
  if (error.error != QJsonParseError::NoError ||
      o["schemaVersion"].toInt() != 1 || !o["machines"].isArray() ||
      o["machines"].toArray().size() > 50)
    fail("Unsupported Library catalog.");
  QSet<QString> seen;
  QJsonArray result;
  for (auto row : o["machines"].toArray()) {
    auto m = row.toObject();
    auto id = m["id"].toString();
    const QString name = m["name"].toString();
    const QString summary = m["summary"].toString();
    const QString osVersion = m["osVersion"].toString();
    if (!QRegularExpression("^[a-z0-9][a-z0-9._-]{0,99}$")
             .match(id)
             .hasMatch() ||
        seen.contains(id) || name.trimmed().isEmpty() || name.size() > 100 ||
        name.contains(QRegularExpression("[\\x00-\\x1f\\x7f]")) ||
        !m["summary"].isString() || summary.size() > 1000 ||
        summary.contains(QRegularExpression("[\\x00-\\x09\\x0b-\\x1f\\x7f]")) ||
        !m["osVersion"].isString() || osVersion.trimmed().isEmpty() ||
        osVersion.size() > 80 ||
        osVersion.contains(QRegularExpression("[\\x00-\\x1f\\x7f]")) ||
        !QRegularExpression("^\\d{1,6}\\.\\d{1,6}(\\.\\d{1,6})?$")
             .match(m["minimumAppVersion"].toString())
             .hasMatch() ||
        !QRegularExpression("^[a-f0-9]{64}$")
             .match(m["sha256"].toString())
             .hasMatch())
      fail("Invalid Library entry.");
    for (auto key : {"archiveBytes", "installedBytes"}) {
      double n = m[key].toDouble();
      if (!m[key].isDouble() || n <= 0 || n > limit || n != qint64(n))
        fail("Invalid Library size.");
    }
    if (m["installedBytes"].toDouble() < 512)
      fail("Invalid installed size.");
    auto family = m.value("machineFamily").toString("powerMacG4");
    if (!QStringList{"powerMacG4", "quadra800", "powerMac7500", "powerMacOSX"}.contains(
            family))
      fail("Unknown Library Mac model.");
    const auto installed = m["installedBytes"].toDouble();
    const auto capacity = m["diskCapacityBytes"].toDouble();
    if (m.contains("diskCapacityBytes") &&
        (capacity < 512 || capacity >= installed || capacity != qint64(capacity) ||
         qint64(capacity) % 512))
      fail("Invalid Library disk capacity.");
    if (m.contains("requiredStorageBytes")) {
      const auto storage = m["requiredStorageBytes"].toDouble();
      if (!capacity || storage < 1048576 || storage > installed + 5 * 1048576 ||
          storage != qint64(storage) || qint64(storage) % 1048576)
        fail("Invalid Library storage requirement.");
    }
    secureURL(m["archiveURL"].toString());
    seen.insert(id);
    result.append(resolvedArchiveEntry(m));
  }
  return result;
}
static qint64 hostMemoryMB() {
#ifdef Q_OS_MACOS
  uint64_t mem = 0;
  size_t len = sizeof(mem);
  if (::sysctlbyname("hw.memsize", &mem, &len, nullptr, 0) == 0 && mem > 0)
    return qint64(mem / (1024ULL * 1024ULL));
#elif defined(Q_OS_LINUX)
  struct sysinfo si {};
  if (::sysinfo(&si) == 0 && si.totalram > 0)
    return qint64((quint64(si.totalram) * quint64(si.mem_unit)) /
                  (1024ULL * 1024ULL));
#elif defined(Q_OS_WIN)
  MEMORYSTATUSEX ms{};
  ms.dwLength = sizeof(ms);
  if (GlobalMemoryStatusEx(&ms) && ms.ullTotalPhys > 0)
    return qint64(ms.ullTotalPhys / (1024ULL * 1024ULL));
#endif
  return 0;
}

// One request per worker thread; no network objects cross Qt thread boundaries.
static void request(const QUrl &initial, const QByteArray &range,
                    qint64 maximum, std::atomic_bool &cancel,
                    const std::function<void(QNetworkReply *)> &headers,
                    const std::function<void(QByteArray)> &chunk,
                    int totalTimeoutMs = 0) {
  QNetworkAccessManager manager;
  QUrl u = initial;
  QElapsedTimer totalTimer;
  if (totalTimeoutMs > 0)
    totalTimer.start();
  for (int redirect = 0; redirect < 4; ++redirect) {
    if (totalTimeoutMs > 0 && totalTimer.elapsed() >= totalTimeoutMs)
      fail("The request timed out.");
    QNetworkRequest req(u);
    req.setAttribute(QNetworkRequest::RedirectPolicyAttribute,
                     QNetworkRequest::ManualRedirectPolicy);
    req.setTransferTimeout(
        totalTimeoutMs > 0
            ? int(std::max<qint64>(1, totalTimeoutMs - totalTimer.elapsed()))
            : 30000);
    req.setRawHeader("Accept-Encoding", "identity");
    req.setRawHeader("User-Agent", "ClassicMac/" CLASSICMAC_VERSION " Native");
    if (!range.isEmpty())
      req.setRawHeader("Range", range);
    auto reply = manager.get(req);
    QEventLoop loop;
    QTimer poll;
    poll.setInterval(80);
    QString failure;
    bool checked = false;
    bool redirecting = false;
    qint64 total = 0;
    auto consume = [&] {
      if (!failure.isEmpty())
        return;
      try {
        checkCancel(cancel);
        if (totalTimeoutMs > 0 && totalTimer.elapsed() >= totalTimeoutMs)
          fail("The request timed out.");
        if (!checked) {
          if (!reply->attribute(QNetworkRequest::HttpStatusCodeAttribute)
                   .isValid())
            return;
          int status =
              reply->attribute(QNetworkRequest::HttpStatusCodeAttribute)
                  .toInt();
          redirecting = status == 301 || status == 302 || status == 303 ||
                        status == 307 || status == 308;
          if (!redirecting)
            headers(reply);
          checked = true;
        }
        if (redirecting) {
          reply->readAll();
          return;
        }
        auto bytes = reply->readAll();
        total += bytes.size();
        if (total > maximum)
          fail("The server sent more data than declared.");
        if (!bytes.isEmpty())
          chunk(bytes);
      } catch (const std::exception &e) {
        failure = QString::fromUtf8(e.what());
        reply->abort();
      }
    };
    QObject::connect(reply, &QNetworkReply::readyRead, &loop, consume);
    QObject::connect(reply, &QNetworkReply::finished, &loop, &QEventLoop::quit);
    QObject::connect(&poll, &QTimer::timeout, &loop, [&] {
      if (cancel) {
        failure =
            "Paused. Your download is saved. Choose Download again to resume.";
        reply->abort();
      } else if (totalTimeoutMs > 0 &&
                 totalTimer.elapsed() >= totalTimeoutMs) {
        failure = "The request timed out.";
        reply->abort();
      }
    });
    poll.start();
    loop.exec();
    poll.stop();
    if (failure.isEmpty())
      consume();
    auto status =
        reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
    auto next =
        reply->attribute(QNetworkRequest::RedirectionTargetAttribute).toUrl();
    auto netError = reply->error();
    auto reason = reply->errorString();
    delete reply;
    if (!failure.isEmpty())
      fail(failure);
    if (redirecting) {
      auto target = secureURL(u.resolved(next).toString());
      if (target.host() != initial.host() ||
          target.port(443) != initial.port(443))
        fail("The download redirected to another site.");
      u = target;
      continue;
    }
    if (netError != QNetworkReply::NoError || status < 200 || status >= 300)
      fail("The download could not complete: " + reason);
    return;
  }
  fail("Too many download redirects.");
}
QString partialWarning() {
  QMutexLocker locker(&gPartialWarningMutex);
  return gPartialWarning;
}
QJsonArray load(std::atomic_bool &cancel) {
  {
    QMutexLocker locker(&gPartialWarningMutex);
    gPartialWarning.clear();
  }
  QJsonArray rows;
  QSet<QString> ids;
  bool loaded = false;
  QString failure;
  for (const auto *url : {Library::url, Library::retroBootURL}) {
    QByteArray data;
    try {
      request(
          secureURL(url), {}, 1048576, cancel,
          [](QNetworkReply *r) {
            if (r->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt() != 200)
              fail("The Library catalog could not be downloaded.");
          },
          [&](QByteArray bytes) { data += bytes; }, 30000);
      for (const auto &row : parse(data)) {
        const auto id = row.toObject()["id"].toString();
        if (!ids.contains(id)) {
          rows.append(row);
          ids.insert(id);
        }
      }
      loaded = true;
    } catch (const std::exception &error) {
      if (cancel)
        fail("Loading canceled. Your saved Macs are still available.");
      if (failure.isEmpty())
        failure = QString::fromUtf8(error.what());
    }
  }
  if (!loaded)
    fail(failure);
  if (!failure.isEmpty()) {
    QMutexLocker locker(&gPartialWarningMutex);
    gPartialWarning = "One catalog could not be loaded (" + failure +
                      "). Some editions may not be listed.";
  }
  return rows;
}
QJsonArray load() {
  std::atomic_bool cancel = false;
  return load(cancel);
}
QString cacheFile(const QJsonObject &entry) {
  const auto resolved = resolvedArchiveEntry(entry);
  const auto hash = resolved["sha256"].toString();
  if (!QRegularExpression("^[a-f0-9]{64}$").match(hash).hasMatch())
    return {};
  const QString fileName = hash + ".tar.gz.part";
  const QString primary = cacheDirectory() + "/" + fileName;
  const QString legacy = legacyCacheDirectory() + "/" + fileName;
  if (primary != legacy) {
    const QFileInfo primaryInfo(primary);
    const QFileInfo legacyInfo(legacy);
    if (!primaryInfo.exists() && legacyInfo.exists() &&
        !legacyInfo.isSymLink() && legacyInfo.isFile())
      return legacy;
  }
  return primary;
}
bool discardDownload(const QJsonObject &entry) {
  const auto resolved = resolvedArchiveEntry(entry);
  const auto hash = resolved["sha256"].toString();
  if (!QRegularExpression("^[a-f0-9]{64}$").match(hash).hasMatch())
    return false;
  const QString fileName = hash + ".tar.gz.part";
  QStringList candidates = {cacheDirectory() + "/" + fileName};
  const QString legacy = legacyCacheDirectory() + "/" + fileName;
  if (legacy != candidates.first())
    candidates << legacy;
  for (const QString &path : candidates) {
    const QFileInfo info(path);
    if (!info.exists() && !info.isSymLink())
      continue;
    if (info.isSymLink() || !info.isFile())
      return false;
    DownloadLock lock;
    if (!lock.tryLock(info.absolutePath() + "/" + hash + ".lock"))
      return false;
    if (QFileInfo::exists(path) && !QFile::remove(path))
      return false;
  }
  return true;
}
qint64 cachedBytes(const QJsonObject &m) {
  const auto resolved = resolvedArchiveEntry(m);
  const QString path = cacheFile(resolved);
  if (path.isEmpty())
    return 0;
  const QFileInfo info(path);
  if (!info.exists() || info.isSymLink() || !info.isFile())
    return 0;
  const auto size = qint64(resolved["archiveBytes"].toDouble());
  const auto saved = info.size();
  return saved > 0 && saved <= size ? saved : 0;
}
bool supportsSparse(const QString &dir) {
  const QString probe = existingAncestor(dir.isEmpty() ? Model::documents() : dir);
  if (probe.isEmpty())
    return false;
#ifdef Q_OS_WIN
  std::wstring native = QDir::toNativeSeparators(probe).toStdWString();
  wchar_t rootPath[MAX_PATH] = {};
  if (!GetVolumePathNameW(native.c_str(), rootPath, MAX_PATH))
    return false;
  DWORD flags = 0;
  if (!GetVolumeInformationW(rootPath, nullptr, 0, nullptr, nullptr, &flags,
                             nullptr, 0))
    return false;
  return (flags & FILE_SUPPORTS_SPARSE_FILES) != 0;
#elif defined(Q_OS_LINUX)
  struct statfs st{};
  if (::statfs(QFile::encodeName(probe).constData(), &st) != 0)
    return false;
  constexpr unsigned long kMsdosSuperMagic = 0x4d44;
  constexpr unsigned long kExfatSuperMagic = 0x2011BAB0UL;
  const auto fsType = static_cast<unsigned long>(st.f_type);
  return fsType != kMsdosSuperMagic && fsType != kExfatSuperMagic;
#elif defined(Q_OS_MACOS)
  struct statfs st{};
  if (::statfs(QFile::encodeName(probe).constData(), &st) != 0)
    return false;
  return std::strcmp(st.f_fstypename, "apfs") == 0;
#else
  return false;
#endif
}
qint64 requiredDiskBytes(const QJsonObject &entry, const QString &targetDir) {
  const auto resolved = resolvedArchiveEntry(entry);
  const qint64 archiveSize = qint64(resolved["archiveBytes"].toDouble());
  const qint64 saved = cachedBytes(resolved);
  const qint64 remaining = qMax<qint64>(0, archiveSize - saved);
  const QString dest = targetDir.isEmpty() ? Model::documents() : targetDir;
  const qint64 installed = qint64(resolved["installedBytes"].toDouble());
  const qint64 requiredStorage =
      (supportsSparse(dest) && resolved.contains("requiredStorageBytes"))
          ? qint64(resolved["requiredStorageBytes"].toDouble(installed))
          : installed;
  const QString part = cacheFile(resolved);
  const QString cacheDir =
      part.isEmpty() ? cacheDirectory() : QFileInfo(part).absolutePath();
  const QStorageInfo cacheVol(existingAncestor(cacheDir));
  const QStorageInfo targetVol(existingAncestor(dest));
  const bool sameVolume = !cacheVol.isValid() || !targetVol.isValid() ||
                          (cacheVol.rootPath() == targetVol.rootPath() &&
                           cacheVol.device() == targetVol.device());
  constexpr qint64 reserve = 256LL * 1048576;
  return (sameVolume ? remaining : 0) + requiredStorage + reserve;
}
static QString download(const QJsonObject &entry, std::atomic_bool &cancel,
                        const Progress &progress) {
  const auto m = resolvedArchiveEntry(entry);
  const QString path = cacheFile(m);
  if (path.isEmpty())
    fail("Invalid Library archive checksum.");
  const QString cache = QFileInfo(path).absolutePath();
  if (!QDir().mkpath(cache))
    fail("Cannot create the download cache.");
  if (QFileInfo(cache).isSymLink())
    fail("The download cache folder is unavailable.");
  const QString hashStr = m["sha256"].toString();
  DownloadLock lock;
  if (!lock.tryLock(cache + "/" + hashStr + ".lock"))
    fail("This edition is already downloading in another window.");
  const QFileInfo partInfo(path);
  if (partInfo.isSymLink())
    fail("The cached download is not a regular file.");
  qint64 size = qint64(m["archiveBytes"].toDouble());
  QFile f(path);
  qint64 offset = f.exists() ? f.size() : 0;
  if (offset > size) {
    if (!f.remove())
      fail("Cannot reset an invalid partial download.");
    offset = 0;
  }
  if (!f.open(QFile::ReadWrite))
    fail("Cannot open the download cache.");
  if (!f.seek(offset))
    fail("Cannot resume the download.");
  progress("Downloading", offset, size);
  if (offset < size) {
    auto range =
        offset ? "bytes=" + QByteArray::number(offset) + "-" : QByteArray();
    request(
        secureURL(m["archiveURL"].toString()), range, size, cancel,
        [&](QNetworkReply *r) {
          const int status =
              r->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
          const QByteArray encoding =
              r->rawHeader("Content-Encoding").trimmed().toLower();
          if (!encoding.isEmpty() && encoding != "identity")
            fail("The server changed the download encoding.");
          qint64 nextOffset = offset;
          if (offset && status == 200) {
            nextOffset = 0;
          } else if (offset) {
            auto expected = "bytes " + QByteArray::number(offset) + "-" +
                            QByteArray::number(size - 1) + "/" +
                            QByteArray::number(size);
            if (status != 206 || r->rawHeader("Content-Range") != expected)
              fail("The server could not resume this download safely.");
          } else if (status != 200) {
            fail("The archive could not be downloaded.");
          }
          bool ok = false;
          auto n = r->rawHeader("Content-Length").toLongLong(&ok);
          if (!ok || n != size - nextOffset)
            fail("The archive size does not match the catalog.");
          if (nextOffset != offset) {
            offset = nextOffset;
            if (!f.resize(0) || !f.seek(0))
              fail("Cannot restart the download.");
          }
        },
        [&](const QByteArray &bytes) {
          if (offset + bytes.size() > size)
            fail("Archive exceeds its catalog size.");
          if (f.write(bytes) != bytes.size())
            fail("The disk stopped accepting the download.");
          offset += bytes.size();
          progress("Downloading", offset, size);
        });
  }
  if (offset != size || !f.flush() || !f.seek(0))
    fail("The download ended early. Retry to resume.");
  QCryptographicHash hash(QCryptographicHash::Sha256);
  qint64 checked = 0;
  while (!f.atEnd()) {
    checkCancel(cancel);
    auto bytes = f.read(1048576);
    if (bytes.isEmpty() && f.error() != QFile::NoError)
      fail("Could not read the downloaded archive.");
    hash.addData(bytes);
    checked += bytes.size();
    progress("Checking download", checked, size);
  }
  f.close();
  if (hash.result().toHex() != hashStr.toLatin1()) {
    QFile::remove(path);
    fail("The archive failed its SHA-256 check. Retry the download.");
  }
  return path;
}
static qint64 number(const char *p, int len) {
  quint64 n = 0;
  if (quint8(p[0]) & 128) {
    for (int i = 0; i < len; ++i) {
      if (n > quint64(limit) / 256)
        fail("Archive member is too large.");
      n = n * 256 + (quint8(p[i]) & (i ? 255 : 127));
    }
  } else {
    auto s = QByteArray(p, len).split('\0').first().trimmed();
    if (s.isEmpty() ||
        QString::fromLatin1(s).contains(QRegularExpression("[^0-7]")))
      fail("Invalid archive number.");
    bool ok;
    n = s.toULongLong(&ok, 8);
    if (!ok)
      fail("Invalid archive number.");
  }
  if (n > quint64(limit))
    fail("Archive member is too large.");
  return qint64(n);
}
class Reader {
  gzFile gzip;
  qint64 readBytes = 0, max;

public:
  Reader(const QString &path, qint64 bound) : max(bound) {
    QFile f(path);
    if (!f.open(QFile::ReadOnly))
      fail("Cannot read archive.");
#ifdef Q_OS_WIN
    int fd = _dup(f.handle());
#else
    int fd = dup(f.handle());
#endif
    if (fd < 0)
      fail("Cannot open compressed archive.");
    gzip = gzdopen(fd, "rb");
    if (!gzip) {
#ifdef Q_OS_WIN
      _close(fd);
#else
      close(fd);
#endif
      fail("Cannot open compressed archive.");
    }
    gzbuffer(gzip, 1048576);
  }
  ~Reader() {
    if (gzip)
      gzclose(gzip);
  }
  QByteArray read(int n, bool optional = false) {
    QByteArray b(n, 0);
    int at = 0;
    while (at < n) {
      int got = gzread(gzip, b.data() + at, n - at);
      if (got <= 0) {
        if (optional && at == 0 && got == 0 && gzeof(gzip))
          return {};
        fail("Truncated or invalid compressed archive.");
      }
      at += got;
      readBytes += got;
      if (readBytes > max)
        fail("Archive expands beyond its declared size.");
    }
    return b;
  }
};
static void writeSparseDisk(Reader &reader, QFile &file, qint64 size,
                            qint64 memberSize, qint64 storage,
                            std::atomic_bool &cancel, qint64 total,
                            qint64 installed, const Progress &progress) {
  size_t count = 0;
  const auto readBlock = [](void *context, uint8_t *block) {
    try {
      const auto bytes = static_cast<Reader *>(context)->read(CM_TAR_BLOCK);
      memcpy(block, bytes.constData(), CM_TAR_BLOCK);
      return true;
    } catch (const std::exception &) {
      return false;
    }
  };
  std::unique_ptr<CMSparseExtent, decltype(&std::free)> extents(
      cm_sparse_read_map(readBlock, &reader, size, memberSize, &count), &std::free);
  if (!extents)
    fail("Invalid sparse machine disk.");
  qint64 stored = 0;
  for (size_t i = 0; i < count; ++i)
    stored += extents.get()[i].length;
  if (stored > storage)
    fail("The machine needs more storage than its catalog allows.");
  if (!file.resize(size))
    fail("Could not size the imported machine disk.");
  for (size_t i = 0; i < count; ++i) {
    const auto &extent = extents.get()[i];
    if (!file.seek(extent.offset))
      fail("Could not seek within the imported machine disk.");
    for (qint64 written = 0; written < extent.length;) {
      checkCancel(cancel);
      const auto bytes = reader.read(int(std::min<qint64>(1048576, extent.length - written)));
      if (file.write(bytes) != bytes.size())
        fail("The disk stopped accepting the imported machine.");
      written += bytes.size();
      progress("Installing", total + extent.offset + written, installed);
    }
  }
  if (!file.flush())
    fail("Could not finish the imported machine disk.");
}

void extract(const QString &archive, const QString &folder,
             const QJsonObject &m, std::atomic_bool &cancel,
             const Progress &progress) {
  qint64 installed = qint64(m["installedBytes"].toDouble()), total = 0;
  Reader reader(archive, installed + CM_SPARSE_HEADER_LIMIT + CM_SPARSE_MAP_LIMIT + 65536);
  int64_t sparseDiskSize = 0;
  QSet<QString> seen;
  QByteArray zero(1048576, 0);
  for (;;) {
    checkCancel(cancel);
    auto h = reader.read(512);
    if (h == zero.left(512)) {
      if (sparseDiskSize || reader.read(512) != zero.left(512))
        fail("Invalid archive end.");
      int padding = 0;
      for (;;) {
        auto tail = reader.read(512, true);
        if (tail.isEmpty())
          break;
        padding += 512;
        if (padding > 10240 || tail != zero.left(512))
          fail("Invalid archive padding.");
      }
      break;
    }
    auto checksum = number(h.data() + 148, 8);
    qint64 sum = 0;
    for (int i = 0; i < 512; ++i)
      sum += i >= 148 && i < 156 ? 32 : quint8(h[i]);
    if (sum != checksum || h.mid(157, 100) != zero.left(100) ||
        h.mid(345, 155) != zero.left(155))
      fail("Unsafe machine archive member.");
    const auto memberSize = number(h.data() + 124, 12);
    const int pad = int((512 - memberSize % 512) % 512);
    if (h[156] == 'x') {
      if (sparseDiskSize || seen.contains("disk.img") || memberSize <= 0 ||
          memberSize > CM_SPARSE_HEADER_LIMIT)
        fail("Invalid sparse machine disk header.");
      const auto pax = reader.read(int(memberSize));
      if (!cm_sparse_disk_size(reinterpret_cast<const uint8_t *>(pax.constData()),
                               pax.size(), &sparseDiskSize) ||
          (pad && reader.read(pad) != zero.left(pad)))
        fail("Invalid sparse machine disk header.");
      continue;
    }
    const auto name = sparseDiskSize ? QString("disk.img")
        : QString::fromUtf8(h.left(100).split('\0').first());
    QStringList allowed = {"config.json", "disk.img", "preview.png"};
    const auto family = m["machineFamily"].toString();
    if (family == "quadra800")
      allowed << "Quadra800.rom" << "pram.img";
    if (family == "powerMacOSX")
      allowed << "nvram.img";
    if ((h[156] != 0 && h[156] != '0') || !allowed.contains(name) || seen.contains(name))
      fail("Unsafe machine archive member.");
    seen.insert(name);
    const qint64 size = sparseDiskSize ? sparseDiskSize : memberSize;
    if (size <= 0 || size > installed - total ||
        (name == "config.json" && size > 65536) ||
        (name == "preview.png" && size > 16 * 1048576) ||
        (name == "disk.img" &&
         (size < 512 || size % 512 ||
          (m.contains("diskCapacityBytes") &&
           size != qint64(m["diskCapacityBytes"].toDouble())))) ||
        (name == "nvram.img" && size != 16384) ||
        (name == "Quadra800.rom" && size != 1048576) ||
        (name == "pram.img" && size != 256 && size != 512))
      fail("Archive member size does not match its catalog.");
    QFile f(folder + "/" + name);
    if (!f.open(QFile::WriteOnly | QFile::NewOnly))
      fail("Cannot create imported machine file.");
    if (name == "disk.img") {
      if (supportsSparse(folder) && !Model::makeSparse(f))
        fail("The destination does not support sparse virtual disks.");
      else if (!supportsSparse(folder))
        Model::makeSparse(f);
    }
    if (sparseDiskSize) {
      const auto storage = qint64(m["requiredStorageBytes"].toDouble(installed));
      writeSparseDisk(reader, f, size, memberSize, storage, cancel, total, installed, progress);
      sparseDiskSize = 0;
      total += size;
      if (pad && reader.read(pad) != zero.left(pad))
        fail("Invalid archive member padding.");
      progress("Installing", total, installed);
      continue;
    }
    qint64 pos = 0;
    while (pos < size) {
      checkCancel(cancel);
      int n = int(std::min(qint64(1048576), size - pos));
      auto bytes = reader.read(n);
      if (name != "disk.img" ||
          memcmp(bytes.constData(), zero.constData(), n)) {
        if (!f.seek(pos) || f.write(bytes) != n)
          fail("The disk stopped accepting the imported machine.");
      }
      pos += n;
      total += n;
      progress("Installing", total, installed);
    }
    if (!f.resize(size) || !f.flush())
      fail("Could not finish the imported machine file.");
    if (pad && reader.read(pad) != zero.left(pad))
      fail("Invalid archive member padding.");
  }
  if (total != installed || !seen.contains("disk.img") ||
      !seen.contains("config.json"))
    fail("Incomplete machine archive.");
}
QString displayTitle(const QJsonObject &entry) {
  const QString id = entry["id"].toString();
  const auto match = QRegularExpression("^(.*)-v(\\d+)$").match(id);
  const QString base = match.hasMatch() ? match.captured(1) : id;
  if (base == "system-7.5.5-base")
    return QStringLiteral("System 7.5.5 (Clean)");
  if (base == "mac-os-7.5.5")
    return QStringLiteral("System 7.5.5 (RetroBoot)");
  if (base == "mac-os-8.1-base")
    return QStringLiteral("Mac OS 8.1 (Clean)");
  if (base == "mac-os-8.1")
    return QStringLiteral("Mac OS 8.1 (Full)");
  if (base == "mac-os-9.2.1-ati-32gb")
    return QStringLiteral("Mac OS 9.2.1 (Clean)");
  if (base == "os921-full")
    return QStringLiteral("Mac OS 9.2.1 (Full)");
  return entry["name"].toString();
}
QString cleanSummary(const QJsonObject &entry) {
  const QString original = entry["summary"].toString().trimmed();
  QString s = original;
  s.replace("Select 2048 MB of Mac OS X memory and the ATI Radeon for the Deluxe setup.",
            "Runs on ClassicMac's standard Mac OS X display; 1024 MB RAM recommended.");
  s.remove(QRegularExpression("\\s+from the user's CD(?=[\\s:.,;]|$)"));
  s.remove(QRegularExpression(
      "\\s*Includes the Quadra 800 ROM this Mac needs \\(Apple's; kept private\\)\\."));
  s.remove(QRegularExpression("\\s*(?:Needs|Requires) RetroBoot .*?\\.(?=\\s|$)",
                              QRegularExpression::CaseInsensitiveOption |
                                  QRegularExpression::DotMatchesEverythingOption));
  s.remove(QRegularExpression("\\s*RetroBoot lab build\\.",
                              QRegularExpression::CaseInsensitiveOption));
  s.replace(QRegularExpression("\\s+([:.,;])"), "\\1");
  s.replace(QRegularExpression(" {2,}"), " ");
  s = s.trimmed();
  return s.isEmpty() ? original : s;
}
Model::Machine install(QJsonObject entry, const QString &parent,
                       const QString &name, std::atomic_bool &cancel,
                       const Progress &progress) {
  const auto m = resolvedArchiveEntry(entry);
  if (!Model::supportedVersion(m["minimumAppVersion"].toString()))
    fail("Update ClassicMac to install this edition.");
  auto family = m.value("machineFamily").toString("powerMacG4");
  if (!QStringList{"powerMacG4", "quadra800", "powerMacOSX"}.contains(family))
    fail("This edition needs the macOS Copland engine.");
  const QString dest = parent.isEmpty() ? Model::documents() : parent;
  if (!QDir().mkpath(dest))
    fail("Cannot create the machine destination.");
  const qint64 archiveSize = qint64(m["archiveBytes"].toDouble());
  const qint64 saved = cachedBytes(m);
  const qint64 remaining = qMax<qint64>(0, archiveSize - saved);
  const qint64 installed = qint64(m["installedBytes"].toDouble());
  const qint64 required =
      (supportsSparse(dest) && m.contains("requiredStorageBytes"))
          ? qint64(m["requiredStorageBytes"].toDouble(installed))
          : installed;
  constexpr qint64 reserve = 256LL * 1048576;
  const QString partPath = cacheFile(m);
  const QString cacheDir =
      partPath.isEmpty() ? cacheDirectory() : QFileInfo(partPath).absolutePath();
  const QStorageInfo cacheVol(existingAncestor(cacheDir));
  const QStorageInfo targetVol(existingAncestor(dest));
  const bool sameVolume = !cacheVol.isValid() || !targetVol.isValid() ||
                          (cacheVol.rootPath() == targetVol.rootPath() &&
                           cacheVol.device() == targetVol.device());
  if (targetVol.isValid() &&
      targetVol.bytesAvailable() <
          required + reserve + (sameVolume ? remaining : 0))
    fail("There is not enough free space to install this Mac.");
  if (!sameVolume && cacheVol.isValid() &&
      cacheVol.bytesAvailable() < remaining + reserve)
    fail("There is not enough free space in the download cache.");
  auto archive = download(m, cancel, progress);
  const QString machineName = name.isEmpty() ? displayTitle(m) : name;
  auto folder = Model::uniqueFolder(dest, machineName);
  bool complete = false;
  auto cleanup = qScopeGuard([&] {
    if (!complete)
      QDir(folder).removeRecursively();
  });
  extract(archive, folder, m, cancel, progress);
  auto source = Model::readJSON(folder + "/config.json");
  if (source.value("machineFamily").toString("powerMacG4") != family ||
      source.value("diskImageName").toString("disk.img") != "disk.img" ||
      source.value("pramImageName").toString("pram.img") != "pram.img")
    fail("The template does not match its Library entry.");
  if (family == "quadra800" && QFileInfo::exists(folder + "/Quadra800.rom")) {
    QFile rom(folder + "/Quadra800.rom");
    if (!rom.open(QFile::ReadOnly) ||
        QCryptographicHash::hash(rom.readAll(), QCryptographicHash::Sha256)
                .toHex() !=
            "05ad753fb594e656cf078023ec189e09e2a7655a780de993b75b8c51ed6b09ca")
      fail("The Quadra ROM failed its integrity check.");
  }
  auto c = Model::freshConfig(machineName, family);
  for (auto key : {"ramMB", "width", "height", "depth", "networking", "sound",
                   "useG4CPU", "useVirtioDisk", "tabletInput",
                   "classicInputHelpers", "useEnhancedFramebuffer",
                   "graphicsCard"})
    if (source.contains(key))
      c[key] = source[key];
  c["diskSizeGB"] =
      int((QFileInfo(folder + "/disk.img").size() + 1073741823) / 1073741824);
  c["toolsCDInserted"] =
      source.value("toolsCDInserted").toBool(family == "powerMacG4");
  if (family == "powerMacG4" &&
      QRegularExpression(
          "^(os922-(superdeluxe|deluxe|mega-deluxe|full)|mac-os-9\\.2\\.1-ati-32gb)-v\\d+$")
          .match(m["id"].toString())
          .hasMatch())
    c["graphicsCard"] = "atiRadeon";

  QJsonObject recommended;
  if (source.contains("recommendedSettings") &&
      source["recommendedSettings"].isObject())
    recommended = source["recommendedSettings"].toObject();
  if (m.contains("recommendedSettings") && m["recommendedSettings"].isObject()) {
    const QJsonObject entryRec = m["recommendedSettings"].toObject();
    for (auto it = entryRec.constBegin(); it != entryRec.constEnd(); ++it)
      recommended.insert(it.key(), it.value());
  }
  if (!recommended.isEmpty()) {
    const bool quad = family == "quadra800";
    const bool osx = family == "powerMacOSX";
    const int minRam = quad ? 16 : osx ? 256 : 32;
    int maxRam = quad ? 1000 : osx ? 2048 : 896;
    const qint64 hostMB = hostMemoryMB();
    if (hostMB > 0)
      maxRam = std::min(maxRam, int(std::max<qint64>(minRam, hostMB / 2)));
    const QJsonValue ramVal =
        (osx && recommended.contains("osx_ram_mb"))
            ? recommended.value("osx_ram_mb")
            : recommended.value("ram_mb");
    if (ramVal.isDouble()) {
      const double d = ramVal.toDouble();
      if (d > 0 && d == qint64(d))
        c["ramMB"] = std::clamp(int(d), minRam, maxRam);
    }
    if (family == "powerMacG4" && recommended.value("cpu").isString()) {
      const QString cpu = recommended.value("cpu").toString().trimmed().toLower();
      if (cpu == "g3")
        c["useG4CPU"] = false;
      else if (cpu == "7400" || cpu == "g4")
        c["useG4CPU"] = true;
    }
  }

  Model::writeJSON(folder + "/config.json", Model::validate(c));
  Model::writeJSON(
      folder + "/template-info.json",
      QJsonObject{{"schemaVersion", 1},
                  {"templateID", m["id"]},
                  {"osVersion", m["osVersion"]},
                  {"archiveSHA256", m["sha256"]},
                  {"importedAt",
                   QDateTime::currentDateTimeUtc().toString(Qt::ISODate)}});
  complete = true;
  Checkpoints::createFreshInstallIfPossible(folder);
  return Model::readMachine(folder);
}
QString install(const QJsonObject &entry, const QString &name,
                std::atomic_bool &cancel, Progress progress,
                const QString &destinationDir) {
  const QString dest =
      destinationDir.isEmpty() ? Model::documents() : destinationDir;
  return install(entry, dest, name, cancel, progress).folder;
}
} // namespace Library
