#pragma once
#include <QDateTime>
#include <QString>
#include <atomic>
#include <functional>
#include <optional>

namespace Updater {

constexpr const char *latestReleaseApiUrl =
    "https://api.github.com/repos/amcchord/ClassicMac/releases/latest";
constexpr const char *releasesHtmlUrl =
    "https://github.com/amcchord/ClassicMac/releases";

struct ReleaseInfo {
  QString version;
  QString tag;
  QString notes;
  QString htmlUrl;
  QString assetName;
  QString assetUrl;
  QString sumsUrl;
};

struct Settings {
  bool autoCheckUpdates = true;
  QString lastUpdateCheckUtc;
  QString skippedUpdateVersion;
};

using Progress =
    std::function<void(QString phase, qint64 complete, qint64 total)>;

QString currentPlatform();
int compareVersions(const QString &a, const QString &b);
bool isAllowedUrl(const QString &urlString);
std::optional<ReleaseInfo> parseLatestRelease(const QByteArray &json,
                                              const QString &currentVersion,
                                              const QString &platform);
QString parseSha256Sums(const QString &text, const QString &filename);
QString inPlaceTargetPath();
bool canInstallInPlace();

QString settingsPath();
Settings loadSettings();
void saveSettings(const Settings &settings);
bool shouldAutoCheck(const Settings &settings,
                     const QDateTime &nowUtc = QDateTime::currentDateTimeUtc());

std::optional<ReleaseInfo> checkLatest(std::atomic_bool &cancel,
                                       const QString &platform = currentPlatform());
QString downloadAndVerify(const ReleaseInfo &release, const QString &targetPath,
                          std::atomic_bool &cancel, const Progress &progress);
void applyVerifiedUpdateAndRestart(const QString &verifiedTempPath,
                                   const QString &targetPath);

} // namespace Updater
