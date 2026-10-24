#pragma once
#include "machine.h"
#include "vnc.h"
class Engine:public QObject {
    Q_OBJECT
public:
    Model::Machine machine;VNC vnc;bool running=false,paused=false,audioUnavailable=false;QString log;int port=0;
    explicit Engine(Model::Machine,QObject *parent=nullptr);~Engine()override;
    void start();QJsonValue command(QString name,QJsonObject args={});QJsonValue hmp(const QString &);
    void pause();void resume();void shutdown(bool restart=false);void forceOff();
    static QList<QStringList> pastePlan(QString text);void paste(QString text,bool slow,std::atomic_bool &cancel,const std::function<void(int,int)> &progress);
signals:void changed();void exited(bool restart,QString error);void response(int id,QJsonObject message);void greeting();
private:QProcess process;QByteArray buffer;bool initializing=false,stopping=false,restarting=false;int sequence=0;QString reason;QByteArray password;
    void read();
};
