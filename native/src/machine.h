#pragma once
#include <QtCore>
#include <stdexcept>
namespace Model {
[[noreturn]] inline void fail(const QString &s){throw std::runtime_error(s.toUtf8().constData());}
struct Machine {QString folder;QJsonObject config;QString id()const{return config["id"].toString();}QString name()const{return config["name"].toString();}QString family()const{return config["machineFamily"].toString();}};
QString runtime();QString dataDirectory();QString libraryFile();QString documents();
QJsonObject readJSON(const QString &,qint64 max=65536);
void writeJSON(const QString &,const QJsonValue &);
QString localName(QJsonValue,QString fallback={});
QString machineFile(const QString &,const QString &);
QJsonObject validate(QJsonObject);
QJsonObject freshConfig(QString name,QString family="powerMacG4");
Machine readMachine(const QString &);
QString uniqueFolder(const QString &parent,const QString &name);
Machine createMachine(QString name,QString family,int size,const QString &parent,const QString &cd={});
QString binary(const QString &name);
QStringList argumentsFor(const Machine &,int port,bool silent=false);
bool supportedVersion(const QString &);
bool makeSparse(QFile &);
}
