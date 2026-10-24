#pragma once
#include "machine.h"
#include <atomic>
#include <functional>
namespace Library {
constexpr const char *url = "https://mcchord.net/classicmac/catalog-v2.json";
using Progress = std::function<void(QString, qint64, qint64)>;
QJsonArray parse(const QByteArray &);
QJsonArray load();
Model::Machine install(QJsonObject entry, const QString &parent,
                       const QString &name, std::atomic_bool &cancel,
                       const Progress &progress);
void extract(const QString &archive, const QString &folder,
             const QJsonObject &entry, std::atomic_bool &cancel,
             const Progress &progress);
} // namespace Library
