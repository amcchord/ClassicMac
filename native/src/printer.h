#pragma once
#include "machine.h"
#include <QByteArray>
#include <QList>
#include <QObject>
#include <QString>
#include <functional>

class QTcpServer;

namespace Printer {

struct HostPrinter {
  QString queueName;
  QString displayName;
  bool isDefault = false;
};

QList<HostPrinter> availablePrinters();
QString defaultPrinterName();
QString ghostscriptPath();
QByteArray generateTestPostScript(const QString &targetLabel,
                                  const QString &version = {});
QString defaultPdfDir(const Model::Machine &m);
bool submitJob(const QByteArray &rawData, const QString &jobName,
               const QString &targetPrinter, const QString &pdfOutputDir,
               QString *errorOut = nullptr, QString *savedPathOut = nullptr);

int runTcpBridge(const QString &host, quint16 port);
QString ensureBridgeSymlink();

class Server : public QObject {
  Q_OBJECT
public:
  explicit Server(QString targetPrinter, QString pdfOutputDir,
                  bool ephemeralForTest = false, QObject *parent = nullptr);
  ~Server() override;

  bool start();
  void stop();

  quint16 lpdPort() const { return boundLpd; }
  quint16 ippPort() const { return boundIpp; }
  quint16 rawPort() const { return boundRaw; }

  std::function<bool(const QByteArray &data, const QString &jobName,
                     QString *errorOut)>
      testHandler;

private:
  friend class SessionHelper;
  QString targetPrinter;
  QString pdfOutputDir;
  bool ephemeralForTest = false;
  QTcpServer *lpdServer = nullptr;
  QTcpServer *ippServer = nullptr;
  QTcpServer *rawServer = nullptr;
  quint16 boundLpd = 0;
  quint16 boundIpp = 0;
  quint16 boundRaw = 0;

  struct IppJob {
    int id = 0;
    QString jobName;
    QString queue;
    int state = 4;
    QList<QByteArray> chunks;
  };
  QList<IppJob> ippJobs;
  int nextIppJobId = 0;
  int lastIppJobId = 0;

  bool dispatchSubmission(const QByteArray &rawData, const QString &jobName,
                          QString *errorOut);
  QByteArray dispatchIpp(const QByteArray &body, const QString &queue);
  IppJob *findIppJob(int id);
  IppJob &createIppJob(const QString &jobName, const QString &queue, int state);
  QByteArray ippJobAttrs(const IppJob &job) const;
};

} // namespace Printer
