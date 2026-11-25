#pragma once
#include "machine.h"
#include "vnc.h"
class Engine : public QObject {
  Q_OBJECT
public:
  Model::Machine machine;
  VNC vnc;
  bool running = false, paused = false, audioUnavailable = false,
       silentAudioFallback = false;
  QString log;
  int port = 0;
  explicit Engine(Model::Machine, QObject *parent = nullptr);
  ~Engine() override;
  qint64 processId() const { return process.processId(); }
  bool stoppingRequested() const { return stopping; }
  bool forcedStopRequested() const { return forced; }
  bool osxShutdownInProgress() const { return osxShuttingDown; }
  QImage currentFrame() const;
  static bool detectOsxMenuOpened(int width, int height,
                                  const QByteArray &beforeRgb,
                                  const QByteArray &currentRgb);
  static bool detectOsxMenuSelectionMoved(int width, int height,
                                          const QByteArray &prevMenuRgb,
                                          const QByteArray &currentRgb);
  static bool detectOsxConfirmationDialog(int width, int height,
                                          const QByteArray &beforeRgb,
                                          const QByteArray &currentRgb);
  static bool hasBlessedSystemFolder(const QString &diskPath);
  static void applyQuadraBootPRAM(const QString &pramPath,
                                  const QJsonObject &config);
  void start();
  QJsonValue command(QString name, QJsonObject args = {});
  QJsonValue hmp(const QString &);
  void pause();
  void resume();
  void shutdown(bool restart = false);
  void forceOff();
  static QList<QStringList> pastePlan(QString text);
  void paste(QString text, bool slow, std::atomic_bool &cancel,
             const std::function<void(int, int)> &progress);
signals:
  void changed();
  void exited(bool restart, QString error);
  void response(int id, QJsonObject message);
  void greeting();

private:
  friend class Qualification;
  QProcess process;
  QByteArray buffer;
  bool initializing = false, stopping = false, restarting = false,
       forced = false, osxShuttingDown = false;
  int sequence = 0;
  QString reason;
  QByteArray password;
  QTimer bootHandoffTimer;
  void read();
};
