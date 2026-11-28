#pragma once
#include "machine.h"
#include <atomic>
#include <functional>
namespace Library {
constexpr const char *url = "https://mcchord.net/classicmac/catalog-v2.json";
constexpr const char *retroBootURL = "https://mcchord.net/retroboot-downloads/catalog.json";
using Progress = std::function<void(QString, qint64, qint64)>;
QJsonArray parse(const QByteArray &);
QJsonArray load();
QJsonArray load(std::atomic_bool &cancel);
QString partialWarning();
QString cacheFile(const QJsonObject &entry);
bool discardDownload(const QJsonObject &entry);
qint64 cachedBytes(const QJsonObject &entry);
qint64 requiredDiskBytes(const QJsonObject &entry, const QString &targetDir);
bool supportsSparse(const QString &dir);
Model::Machine install(QJsonObject entry, const QString &parent,
                       const QString &name, std::atomic_bool &cancel,
                       const Progress &progress);
QString install(const QJsonObject &entry, const QString &name,
                std::atomic_bool &cancel, Progress progress,
                const QString &destinationDir = {});
QString downloadPippinROM(std::atomic_bool &cancel, const Progress &progress);
QString displayTitle(const QJsonObject &entry);
QString cleanSummary(const QJsonObject &entry);
void extract(const QString &archive, const QString &folder,
             const QJsonObject &entry, std::atomic_bool &cancel,
             const Progress &progress);
} // namespace Library
