#pragma once
#include "catalog.h"
#include "engine.h"
#include "widgets.h"
class App : public PWindow {
  Q_OBJECT
public:
  QList<Model::Machine> machines;
  QMap<QString, Engine *> engines;
  QMap<QString, PWindow *> guests;
  int selected = 0;
  bool settingUp = false;
  explicit App();
  void addMachine(const QString &);
  void startSelected();
  void showLibrary();
  void showEditor(bool create = false);
  void showMedia();
  void showSetup();
  QJsonObject evidence() const;
  void saveLibrary();
  void render();
  Model::Machine *current();
  Engine *currentEngine() const;
  PWindow *guest(Engine *);

private:
  QWidget *home = nullptr;
  QListWidget *list = nullptr;
  PLabel *nameLabel = nullptr, *statusLabel = nullptr, *details = nullptr,
         *notice = nullptr;
  QWidget *preview = nullptr;
  PButton *startButton = nullptr, *infoButton = nullptr, *mediaButton = nullptr;
  QJsonArray catalogRows;
  QStringList unavailablePaths;
  void buildHome();
  void importMachine();
  void registerMachine(const Model::Machine &);
  void settingsSaved(QJsonObject);
  QJsonArray loadCatalog();
  Model::Machine install(QJsonObject, const QString &name);
  void showPaste(Engine *);
  void select(int);
};
