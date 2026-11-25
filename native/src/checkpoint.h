#pragma once
#include "machine.h"
#include <QList>
#include <QString>
namespace Checkpoints {
struct Entry {
  QString id;
  QString label;
  QString kind; // "manual", "fresh-install", "pre-restore", "pre-recovery"
  QString created;
  QString method; // "reflink", "clonefile", "sparse", "copy"
  qint64 diskBytes = 0;
  qint64 allocatedBytes = 0;
};
QList<Entry> list(const QString &machineFolder);
Entry create(const Model::Machine &machine, const QString &label,
             const QString &kind = QStringLiteral("manual"));
void createFreshInstallIfPossible(const QString &machineFolder);
void restore(const Model::Machine &machine, const QString &checkpointId);
void remove(const QString &machineFolder, const QString &checkpointId);
bool lastSessionClean(const QString &machineFolder);
void markSessionState(const QString &machineFolder, bool clean,
                      const QString &reason = {});
} // namespace Checkpoints
