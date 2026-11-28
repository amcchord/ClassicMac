#pragma once
#include <QtCore>
#include <stdexcept>
namespace Model {
[[noreturn]] inline void fail(const QString &s) {
  throw std::runtime_error(s.toUtf8().constData());
}
struct Machine {
  QString folder;
  QJsonObject config;
  QString id() const { return config["id"].toString(); }
  QString name() const { return config["name"].toString(); }
  QString family() const { return config["machineFamily"].toString(); }
};
QString runtime();
QString dataDirectory();
QString libraryFile();
QString documents();
QJsonObject readJSON(const QString &, qint64 max = 65536);
void writeJSON(const QString &, const QJsonValue &);
QString localName(QJsonValue, QString fallback = {});
QString machineFile(const QString &, const QString &);
QJsonObject validate(QJsonObject);
QJsonObject freshConfig(QString name, QString family = "powerMacG4");
Machine readMachine(const QString &);
QString uniqueFolder(const QString &parent, const QString &name);
Machine createMachine(QString name, QString family, int size,
                      const QString &parent, const QString &cd = {});
QString binary(const QString &name);
QString pippinROMPath();
QString resolvePippinROM(const Machine &m);
bool supportsHostSharing();
QString sanitizedName(const QString &raw);
QProcessEnvironment hostEnvironment();
struct PrintPorts {
  quint16 lpd = 0;
  quint16 ipp = 0;
  quint16 raw = 0;
  QString bridgePath;
  bool active() const {
    return lpd > 0 && ipp > 0 && raw > 0 && !bridgePath.isEmpty();
  }
};
QStringList qemuArguments(const Machine &m, int vncPort,
                          const QString &audioOverride = {},
                          const PrintPorts &printPorts = {});
QStringList argumentsFor(const Machine &, int port, bool silent = false);
bool supportedVersion(const QString &);
bool makeSparse(QFile &);
bool isOSXFamily(const QString &family);
bool supportsG5OS(const QString &osVersion);
bool validateOsxNvram(const QByteArray &data, QString *errorOut = nullptr);
QString osxRtcBase(const QString &nvramPath, qint64 nowUtcSeconds = -1);
} // namespace Model
