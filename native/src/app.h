#pragma once
#include "catalog.h"
#include "engine.h"
#include "updater.h"
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
  void showMedia(QWidget *parent = nullptr);
  void showSetup();
  void showMachineActions();
  void showCheckpoints(const Model::Machine &);
  void removeSelected();
  void showPowerActions(Engine *);
  void checkForUpdates(bool manual = true);
  bool requestQuit();
  QJsonObject evidence() const;
  void saveLibrary();
  void render();
  Model::Machine *current();
  Engine *currentEngine() const;
  PWindow *guest(Engine *);

private:
  friend class Qualification;
  friend class Preview;
  QWidget *home = nullptr;
  QWidget *machineSidebar = nullptr;
  QBoxLayout *homeColumns = nullptr;
  PLabel *machineCount = nullptr;
  QListWidget *list = nullptr;
  PLabel *nameLabel = nullptr, *statusLabel = nullptr, *details = nullptr,
         *notice = nullptr;
  QWidget *preview = nullptr;
  PButton *startButton = nullptr, *infoButton = nullptr, *mediaButton = nullptr;
  PButton *manageButton = nullptr, *deleteButton = nullptr,
          *powerButton = nullptr;
  QJsonArray catalogRows;
  QStringList unavailablePaths;
  QMap<QString, QImage> livePreviews;
  void buildHome();
  void importMachine();
  void registerMachine(const Model::Machine &);
  void settingsSaved(QJsonObject);
  QJsonArray loadCatalog();
  Model::Machine install(QJsonObject, const QString &name,
                         const QString &destination = {},
                         const QString &targetFamily = {});
  void showAbout();
  void showUpdateDialog(const Updater::ReleaseInfo &info, bool manual);
  void installUpdate(const Updater::ReleaseInfo &info);
  void showPaste(Engine *, QWidget *parent = nullptr);
  void select(int);
  void reflowHome();
protected:
  void resizeEvent(QResizeEvent *) override;
};
