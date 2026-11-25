#include "updater.h"
#include "machine.h"
#include <QCryptographicHash>
#include <QDir>
#include <QElapsedTimer>
#include <QEventLoop>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QProcess>
#include <QRegularExpression>
#include <QSaveFile>
#include <QScopeGuard>
#include <QTimer>
#include <QUrl>
#include <QtNetwork>
#include <algorithm>
#include <cerrno>
#include <cstring>

#ifdef Q_OS_LINUX
#include <cstdio>
#include <sys/stat.h>
#endif

namespace Updater {
namespace {

constexpr int kTransferTimeoutMs = 20000;
constexpr qint64 kMaxMetadataBytes = 1048576;
constexpr qint64 kMaxSumsBytes = 262144;
constexpr qint64 kMaxAssetBytes = 1024LL * 1048576;

bool isAllowedHost(const QString &host) {
  const QString lower = host.toLower();
  return lower == "api.github.com" || lower == "github.com" ||
         lower == "objects.githubusercontent.com" ||
         lower == "release-assets.githubusercontent.com";
}

struct ParsedVersion {
  QList<int> parts;
  QString prerelease;
  bool valid = false;
};

ParsedVersion parseVersion(QString raw) {
  ParsedVersion out;
  raw = raw.trimmed();
  if (raw.startsWith('v', Qt::CaseInsensitive))
    raw.remove(0, 1);
  const int dash = raw.indexOf('-');
  if (dash >= 0) {
    out.prerelease = raw.mid(dash + 1).trimmed();
    raw = raw.left(dash);
  }
  const QStringList segments = raw.split('.');
  if (segments.isEmpty())
    return out;
  for (const QString &seg : segments) {
    if (seg.isEmpty() || seg.size() > 6)
      return out;
    bool ok = false;
    const int val = seg.toInt(&ok);
    if (!ok || val < 0)
      return out;
    out.parts.append(val);
  }
  out.valid = true;
  return out;
}

void fetchHttps(
    const QString &initialUrl, qint64 maxBytes, bool jsonApi,
    std::atomic_bool &cancel,
    const std::function<void(qint64 contentLength)> &onHeaders,
    const std::function<void(const QByteArray &chunk)> &onChunk) {
  if (!isAllowedUrl(initialUrl))
    Model::fail("Update URL is not allowed.");
  QNetworkAccessManager manager;
  QUrl current(initialUrl);
  for (int redirect = 0; redirect < 6; ++redirect) {
    if (cancel)
      Model::fail("Update check canceled.");
    QNetworkRequest req(current);
    req.setAttribute(QNetworkRequest::RedirectPolicyAttribute,
                     QNetworkRequest::ManualRedirectPolicy);
    req.setTransferTimeout(kTransferTimeoutMs);
    req.setRawHeader("Accept-Encoding", "identity");
    req.setRawHeader("User-Agent", "ClassicMac/" CLASSICMAC_VERSION " Native");
    if (jsonApi)
      req.setRawHeader("Accept", "application/vnd.github+json");
    QNetworkReply *reply = manager.get(req);
    QEventLoop loop;
    QTimer poll;
    poll.setInterval(80);
    QString failure;
    bool checkedHeaders = false;
    bool redirecting = false;
    qint64 received = 0;

    auto consume = [&] {
      if (!failure.isEmpty())
        return;
      try {
        if (cancel)
          Model::fail("Update canceled.");
        if (!checkedHeaders) {
          const QVariant statusVar =
              reply->attribute(QNetworkRequest::HttpStatusCodeAttribute);
          if (!statusVar.isValid())
            return;
          const int status = statusVar.toInt();
          redirecting = status == 301 || status == 302 || status == 303 ||
                        status == 307 || status == 308;
          if (!redirecting) {
            if (status < 200 || status >= 300)
              Model::fail(
                  QString("Update server returned HTTP %1.").arg(status));
            bool ok = false;
            const qint64 contentLength =
                reply->rawHeader("Content-Length").toLongLong(&ok);
            if (ok && contentLength > maxBytes)
              Model::fail("Update payload exceeds the size limit.");
            if (onHeaders)
              onHeaders(ok && contentLength > 0 ? contentLength : -1);
          }
          checkedHeaders = true;
        }
        if (redirecting) {
          reply->readAll();
          return;
        }
        const QByteArray bytes = reply->readAll();
        received += bytes.size();
        if (received > maxBytes)
          Model::fail("Update payload exceeds the size limit.");
        if (!bytes.isEmpty())
          onChunk(bytes);
      } catch (const std::exception &e) {
        failure = QString::fromUtf8(e.what());
        reply->abort();
      }
    };

    QObject::connect(reply, &QNetworkReply::readyRead, &loop, consume);
    QObject::connect(reply, &QNetworkReply::finished, &loop, &QEventLoop::quit);
    QObject::connect(&poll, &QTimer::timeout, &loop, [&] {
      if (cancel) {
        failure = "Update canceled.";
        reply->abort();
      }
    });
    poll.start();
    loop.exec();
    poll.stop();
    if (failure.isEmpty())
      consume();

    const int status =
        reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
    const QUrl next =
        reply->attribute(QNetworkRequest::RedirectionTargetAttribute).toUrl();
    const QNetworkReply::NetworkError netErr = reply->error();
    const QString errStr = reply->errorString();
    delete reply;

    if (!failure.isEmpty())
      Model::fail(failure);
    if (redirecting) {
      const QUrl resolved = current.resolved(next);
      const QString resolvedStr = resolved.toString();
      if (!isAllowedUrl(resolvedStr))
        Model::fail("Update redirected to an untrusted host.");
      current = resolved;
      continue;
    }
    if (netErr != QNetworkReply::NoError || status < 200 || status >= 300)
      Model::fail("Update request failed: " + errStr);
    return;
  }
  Model::fail("Too many redirects while checking for updates.");
}

} // namespace

QString currentPlatform() {
#if defined(Q_OS_LINUX)
  return QStringLiteral("linux");
#elif defined(Q_OS_WIN)
  return QStringLiteral("windows");
#else
  return QString();
#endif
}

int compareVersions(const QString &a, const QString &b) {
  const ParsedVersion pa = parseVersion(a);
  const ParsedVersion pb = parseVersion(b);
  if (!pa.valid || !pb.valid)
    return QString::compare(a.trimmed(), b.trimmed(), Qt::CaseInsensitive);
  const qsizetype count = std::max(pa.parts.size(), pb.parts.size());
  for (qsizetype i = 0; i < count; ++i) {
    const int va = i < pa.parts.size() ? pa.parts.at(i) : 0;
    const int vb = i < pb.parts.size() ? pb.parts.at(i) : 0;
    if (va != vb)
      return va < vb ? -1 : 1;
  }
  if (pa.prerelease.isEmpty() != pb.prerelease.isEmpty())
    return pa.prerelease.isEmpty() ? 1 : -1;
  if (!pa.prerelease.isEmpty()) {
    const int cmp =
        QString::compare(pa.prerelease, pb.prerelease, Qt::CaseInsensitive);
    return cmp < 0 ? -1 : cmp > 0 ? 1 : 0;
  }
  return 0;
}

bool isAllowedUrl(const QString &urlString) {
  const QUrl u(urlString.trimmed());
  return u.isValid() && u.scheme().compare("https", Qt::CaseInsensitive) == 0 &&
         u.userInfo().isEmpty() && !u.hasFragment() && !u.host().isEmpty() &&
         isAllowedHost(u.host());
}

std::optional<ReleaseInfo> parseLatestRelease(const QByteArray &json,
                                              const QString &currentVersion,
                                              const QString &platform) {
  if (json.size() > kMaxMetadataBytes)
    Model::fail("Update metadata is too large.");
  QJsonParseError err;
  const QJsonDocument doc = QJsonDocument::fromJson(json, &err);
  if (err.error != QJsonParseError::NoError || !doc.isObject())
    Model::fail("Invalid update metadata.");
  const QJsonObject obj = doc.object();
  if (obj["draft"].toBool() || obj["prerelease"].toBool())
    return std::nullopt;

  const QString tag = obj["tag_name"].toString().trimmed();
  QString version = tag;
  if (version.startsWith('v', Qt::CaseInsensitive))
    version.remove(0, 1);
  if (!QRegularExpression("^\\d{1,6}\\.\\d{1,6}(\\.\\d{1,6})?$")
           .match(version)
           .hasMatch())
    Model::fail("Invalid release version in update metadata.");
  if (compareVersions(version, currentVersion) <= 0)
    return std::nullopt;

  QString expectedAsset;
  if (platform == "linux")
    expectedAsset = QString("ClassicMac-%1-linux-x64.AppImage").arg(version);
  else if (platform == "windows")
    expectedAsset = QString("ClassicMac-%1-windows-x64.exe").arg(version);
  else
    return std::nullopt;

  QString assetUrl;
  QString sumsUrl;
  const QJsonArray assets = obj["assets"].toArray();
  for (const QJsonValue &val : assets) {
    const QJsonObject asset = val.toObject();
    const QString name = asset["name"].toString();
    const QString url = asset["browser_download_url"].toString();
    if (name == expectedAsset) {
      if (!isAllowedUrl(url))
        Model::fail("Release download URL is not allowed.");
      assetUrl = url;
    } else if (name == "SHA256SUMS") {
      if (!isAllowedUrl(url))
        Model::fail("Release checksum URL is not allowed.");
      sumsUrl = url;
    }
  }

  if (assetUrl.isEmpty() || sumsUrl.isEmpty())
    return std::nullopt;

  QString htmlUrl = obj["html_url"].toString().trimmed();
  if (!isAllowedUrl(htmlUrl))
    htmlUrl = QString::fromLatin1(releasesHtmlUrl);

  return ReleaseInfo{version,
                     tag,
                     obj["body"].toString(),
                     htmlUrl,
                     expectedAsset,
                     assetUrl,
                     sumsUrl};
}

QString parseSha256Sums(const QString &text, const QString &filename) {
  if (filename.isEmpty())
    return {};
  static const QRegularExpression lineRe(
      "^([0-9a-fA-F]{64})\\s+\\*?(.+)$");
  const QStringList lines = text.split('\n');
  for (QString line : lines) {
    line = line.trimmed();
    if (line.isEmpty() || line.startsWith('#'))
      continue;
    const QRegularExpressionMatch m = lineRe.match(line);
    if (!m.hasMatch())
      continue;
    QString entryName = m.captured(2).trimmed();
    if (entryName.startsWith("./"))
      entryName.remove(0, 2);
    if (entryName == filename ||
        (!entryName.contains("..") &&
         QFileInfo(entryName).fileName() == filename))
      return m.captured(1).toLower();
  }
  return {};
}

QString inPlaceTargetPath() {
#if defined(Q_OS_LINUX)
  const QString appImage = qEnvironmentVariable("APPIMAGE").trimmed();
  if (appImage.isEmpty())
    return {};
  const QFileInfo info(appImage);
  if (!info.exists() || !info.isFile())
    return {};
  return info.absoluteFilePath();
#elif defined(Q_OS_WIN)
  const QString portable =
      qEnvironmentVariable("CLASSICMAC_PORTABLE_EXE").trimmed();
  if (portable.isEmpty())
    return {};
  const QFileInfo info(portable);
  if (!info.exists() || !info.isFile())
    return {};
  return info.absoluteFilePath();
#else
  return {};
#endif
}

bool canInstallInPlace() { return !inPlaceTargetPath().isEmpty(); }

QString settingsPath() { return Model::dataDirectory() + "/settings.json"; }

Settings loadSettings() {
  Settings s;
  const QFileInfo info(settingsPath());
  if (!info.exists() || !info.isFile())
    return s;
  try {
    const QJsonObject obj = Model::readJSON(settingsPath());
    if (obj.contains("autoCheckUpdates") && obj["autoCheckUpdates"].isBool())
      s.autoCheckUpdates = obj["autoCheckUpdates"].toBool();
    if (obj.contains("lastUpdateCheckUtc") &&
        obj["lastUpdateCheckUtc"].isString())
      s.lastUpdateCheckUtc = obj["lastUpdateCheckUtc"].toString();
    if (obj.contains("skippedUpdateVersion") &&
        obj["skippedUpdateVersion"].isString())
      s.skippedUpdateVersion = obj["skippedUpdateVersion"].toString();
  } catch (const std::exception &) {
  }
  return s;
}

void saveSettings(const Settings &settings) {
  QJsonObject obj;
  if (QFileInfo::exists(settingsPath())) {
    try {
      obj = Model::readJSON(settingsPath());
    } catch (const std::exception &) {
      obj = {};
    }
  }
  obj["autoCheckUpdates"] = settings.autoCheckUpdates;
  obj["lastUpdateCheckUtc"] = settings.lastUpdateCheckUtc;
  obj["skippedUpdateVersion"] = settings.skippedUpdateVersion;
  Model::writeJSON(settingsPath(), obj);
}

bool shouldAutoCheck(const Settings &settings, const QDateTime &nowUtc) {
  if (!settings.autoCheckUpdates)
    return false;
  if (qEnvironmentVariable("CLASSICMAC_DISABLE_UPDATE_CHECK") == "1" ||
      !qEnvironmentVariableIsEmpty("CLASSICMAC_QUALIFY"))
    return false;
  const QStringList args = QCoreApplication::arguments();
  for (const QString &arg : args) {
    if (arg == "--self-test" || arg.startsWith("--qualify") ||
        arg == "--display-test" || arg == "--catalog-check" ||
        arg == "--qualification-engine")
      return false;
  }
  if (!settings.lastUpdateCheckUtc.isEmpty()) {
    const QDateTime last =
        QDateTime::fromString(settings.lastUpdateCheckUtc, Qt::ISODate);
    if (last.isValid()) {
      const qint64 elapsed = last.secsTo(nowUtc);
      if (elapsed >= 0 && elapsed < 24 * 3600)
        return false;
    }
  }
  return true;
}

std::optional<ReleaseInfo> checkLatest(std::atomic_bool &cancel,
                                       const QString &platform) {
  QByteArray json;
  fetchHttps(QString::fromLatin1(latestReleaseApiUrl), kMaxMetadataBytes, true,
             cancel, nullptr,
             [&](const QByteArray &chunk) { json.append(chunk); });
  return parseLatestRelease(json, QString::fromLatin1(CLASSICMAC_VERSION),
                            platform);
}

QString downloadAndVerify(const ReleaseInfo &release, const QString &targetPath,
                          std::atomic_bool &cancel, const Progress &progress) {
  if (targetPath.isEmpty())
    Model::fail("In-place updates are only supported from packaged builds.");
  if (!isAllowedUrl(release.sumsUrl) || !isAllowedUrl(release.assetUrl))
    Model::fail("Update URLs are not allowed.");

  QByteArray sumsData;
  fetchHttps(release.sumsUrl, kMaxSumsBytes, false, cancel, nullptr,
             [&](const QByteArray &chunk) { sumsData.append(chunk); });
  const QString expectedSha =
      parseSha256Sums(QString::fromUtf8(sumsData), release.assetName);
  if (expectedSha.isEmpty())
    Model::fail("SHA256SUMS did not contain a checksum for " +
                release.assetName + ".");

  const QFileInfo targetInfo(targetPath);
  const QString parentDir = targetInfo.absolutePath();
  if (parentDir.isEmpty() || !QDir(parentDir).exists())
    Model::fail("The installation folder is unavailable.");

  const QString tempPath =
      parentDir + "/." + targetInfo.fileName() + ".update." +
      QString::number(QCoreApplication::applicationPid()) + ".tmp";
  QFile::remove(tempPath);

  bool keepTemp = false;
  auto cleanup = qScopeGuard([&] {
    if (!keepTemp)
      QFile::remove(tempPath);
  });

  QFile out(tempPath);
  if (!out.open(QIODevice::WriteOnly | QIODevice::Truncate))
    Model::fail("Cannot write the update next to ClassicMac. Check folder "
                "permissions.");

  qint64 totalBytes = 0;
  qint64 downloadedBytes = 0;
  QElapsedTimer throttle;
  throttle.start();

  auto report = [&](const QString &phase, qint64 done, qint64 total,
                    bool force) {
    if (!progress)
      return;
    if (!force && done != total && throttle.elapsed() < 100)
      return;
    throttle.restart();
    progress(phase, done, total);
  };

  fetchHttps(
      release.assetUrl, kMaxAssetBytes, false, cancel,
      [&](qint64 contentLength) {
        totalBytes = contentLength > 0 ? contentLength : 0;
        report(QStringLiteral("Downloading"), 0, totalBytes, true);
      },
      [&](const QByteArray &chunk) {
        if (out.write(chunk) != chunk.size())
          Model::fail("Disk full or write error while saving update.");
        downloadedBytes += chunk.size();
        report(QStringLiteral("Downloading"), downloadedBytes, totalBytes,
               false);
      });

  if (!out.flush())
    Model::fail("Could not flush downloaded update to disk.");
  out.close();

  if (totalBytes > 0 && downloadedBytes != totalBytes)
    Model::fail("The update download ended early.");
  const qint64 actualSize = QFileInfo(tempPath).size();
  if (actualSize <= 0)
    Model::fail("The downloaded update is empty.");

  QFile verifyFile(tempPath);
  if (!verifyFile.open(QIODevice::ReadOnly))
    Model::fail("Cannot read the downloaded update for verification.");

  QCryptographicHash sha(QCryptographicHash::Sha256);
  qint64 verifiedBytes = 0;
  report(QStringLiteral("Checking download"), 0, actualSize, true);
  while (!verifyFile.atEnd()) {
    if (cancel)
      Model::fail("Update canceled.");
    const QByteArray buf = verifyFile.read(1048576);
    if (buf.isEmpty() && verifyFile.error() != QFile::NoError)
      Model::fail("Error reading the downloaded update.");
    sha.addData(buf);
    verifiedBytes += buf.size();
    report(QStringLiteral("Checking download"), verifiedBytes, actualSize,
           false);
  }
  verifyFile.close();
  report(QStringLiteral("Checking download"), actualSize, actualSize, true);

  const QString actualSha = QString::fromLatin1(sha.result().toHex()).toLower();
  if (actualSha != expectedSha)
    Model::fail("The downloaded update failed its SHA-256 verification.");

#ifdef Q_OS_LINUX
  if (::chmod(QFile::encodeName(tempPath).constData(), 0755) != 0)
    Model::fail(QString("Could not mark the new AppImage executable: %1")
                    .arg(QString::fromLocal8Bit(std::strerror(errno))));
#endif

  keepTemp = true;
  return tempPath;
}

void applyVerifiedUpdateAndRestart(const QString &verifiedTempPath,
                                   const QString &targetPath) {
  if (verifiedTempPath.isEmpty() || targetPath.isEmpty() ||
      !QFileInfo::exists(verifiedTempPath))
    Model::fail("The verified update file is missing.");

#if defined(Q_OS_LINUX)
  if (::chmod(QFile::encodeName(verifiedTempPath).constData(), 0755) != 0) {
    QFile::remove(verifiedTempPath);
    Model::fail(QString("Could not set permissions on update: %1")
                    .arg(QString::fromLocal8Bit(std::strerror(errno))));
  }
  if (::rename(QFile::encodeName(verifiedTempPath).constData(),
               QFile::encodeName(targetPath).constData()) != 0) {
    const QString err = QString::fromLocal8Bit(std::strerror(errno));
    QFile::remove(verifiedTempPath);
    Model::fail("Could not replace the AppImage: " + err);
  }
  QProcessEnvironment env = Model::hostEnvironment();
  env.remove("APPIMAGE");
  env.remove("APPDIR");
  env.remove("ARGV0");
  env.remove("OWD");
  QProcess proc;
  proc.setProgram(targetPath);
  proc.setProcessEnvironment(env);
  if (!proc.startDetached())
    Model::fail("The update was installed, but ClassicMac could not relaunch.");
  QCoreApplication::quit();
#elif defined(Q_OS_WIN)
  const QString nativeTarget = QDir::toNativeSeparators(targetPath);
  const QString nativeTemp = QDir::toNativeSeparators(verifiedTempPath);
  const QString nativeOld = nativeTarget + ".old";
  for (const QString &p : {nativeTarget, nativeTemp}) {
    if (p.contains('"') || p.contains('\r') || p.contains('\n') ||
        p.contains('%')) {
      QFile::remove(verifiedTempPath);
      Model::fail("The installation path contains unsupported characters.");
    }
  }
  const qint64 pid = QCoreApplication::applicationPid();
  const QString scriptPath =
      QDir::tempPath() + "/classicmac-update-" + QString::number(pid) + ".cmd";
  const QString script = QStringLiteral(
                             "@echo off\r\n"
                             "setlocal DisableDelayedExpansion\r\n"
                             "set \"PID=%1\"\r\n"
                             "set \"TARGET=%2\"\r\n"
                             "set \"NEWEXE=%3\"\r\n"
                             "set \"OLDEXE=%4\"\r\n"
                             "set /a TRIES=0\r\n"
                             ":waitloop\r\n"
                             "tasklist /FI \"PID eq %PID%\" /NH 2>nul | findstr /R /C:\"\\<%PID%\\>\" >nul\r\n"
                             "if not errorlevel 1 (\r\n"
                             "  set /a TRIES+=1\r\n"
                             "  if %TRIES% GEQ 30 goto fail\r\n"
                             "  ping -n 2 127.0.0.1 >nul\r\n"
                             "  goto waitloop\r\n"
                             ")\r\n"
                             "if exist \"%OLDEXE%\" del /f /q \"%OLDEXE%\" >nul 2>&1\r\n"
                             "set /a MOVETRIES=0\r\n"
                             ":moveloop\r\n"
                             "move /y \"%TARGET%\" \"%OLDEXE%\" >nul 2>&1\r\n"
                             "if errorlevel 1 (\r\n"
                             "  set /a MOVETRIES+=1\r\n"
                             "  if %MOVETRIES% GEQ 5 goto fail\r\n"
                             "  ping -n 2 127.0.0.1 >nul\r\n"
                             "  goto moveloop\r\n"
                             ")\r\n"
                             "move /y \"%NEWEXE%\" \"%TARGET%\" >nul 2>&1\r\n"
                             "if errorlevel 1 (\r\n"
                             "  move /y \"%OLDEXE%\" \"%TARGET%\" >nul 2>&1\r\n"
                             "  goto fail\r\n"
                             ")\r\n"
                             "del /f /q \"%OLDEXE%\" >nul 2>&1\r\n"
                             "start \"\" \"%TARGET%\"\r\n"
                             "(goto) 2>nul & del /f /q \"%~f0\"\r\n"
                             "exit /b 0\r\n"
                             ":fail\r\n"
                             "if exist \"%NEWEXE%\" del /f /q \"%NEWEXE%\" >nul 2>&1\r\n"
                             "(goto) 2>nul & del /f /q \"%~f0\"\r\n"
                             "exit /b 1\r\n")
                             .arg(QString::number(pid), nativeTarget,
                                  nativeTemp, nativeOld);
  {
    QFile sf(scriptPath);
    if (!sf.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
      QFile::remove(verifiedTempPath);
      Model::fail("Could not create the Windows update helper script.");
    }
    const QByteArray bytes = script.toUtf8();
    if (sf.write(bytes) != bytes.size() || !sf.flush()) {
      QFile::remove(scriptPath);
      QFile::remove(verifiedTempPath);
      Model::fail("Could not write the Windows update helper script.");
    }
  }
  QProcess proc;
  proc.setProgram("cmd.exe");
  proc.setArguments({"/c", QDir::toNativeSeparators(scriptPath)});
  proc.setWorkingDirectory(QFileInfo(targetPath).absolutePath());
  proc.setCreateProcessArgumentsModifier([](QProcess::CreateProcessArguments *a) {
    a->flags |= 0x08000000;
  });
  if (!proc.startDetached()) {
    QFile::remove(scriptPath);
    QFile::remove(verifiedTempPath);
    Model::fail("Could not launch the Windows update helper.");
  }
  QCoreApplication::quit();
#else
  Q_UNUSED(verifiedTempPath);
  Q_UNUSED(targetPath);
  Model::fail("In-place updates are only supported on Linux and Windows.");
#endif
}

} // namespace Updater
