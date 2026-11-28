#include "machine.h"
#include <QTimeZone>
#include <algorithm>
#include <zlib.h>
#ifdef Q_OS_WIN
#include <io.h>
#include <windows.h>
#include <winioctl.h>
#endif
namespace Model {
QString runtime() {
  auto r = qEnvironmentVariable("CLASSICMAC_RUNTIME");
  return r.isEmpty() ? QCoreApplication::applicationDirPath() + "/runtime"
                     : QDir(r).absolutePath();
}
QString dataDirectory() {
  auto r = qEnvironmentVariable("CLASSICMAC_TEST_HOME");
  if (r.isEmpty()) {
#ifdef Q_OS_WIN
    r = qEnvironmentVariable("APPDATA") + "/classicmac-desktop";
#else
    r = QStandardPaths::writableLocation(
            QStandardPaths::GenericConfigLocation) +
        "/classicmac-desktop";
#endif
  }
  QDir().mkpath(r);
  return r;
}
QString libraryFile() { return dataDirectory() + "/library.json"; }
QString documents() {
  auto p = qEnvironmentVariable("CLASSICMAC_TEST_DOCUMENTS");
  if (p.isEmpty())
    p = QStandardPaths::writableLocation(QStandardPaths::DocumentsLocation) +
        "/ClassicMac";
  if (!QDir().mkpath(p))
    fail("Cannot create the ClassicMac documents folder.");
  return p;
}
QJsonObject readJSON(const QString &path, qint64 max) {
  QFile f(path);
  if (!f.open(QFile::ReadOnly) || f.size() > max)
    fail("Cannot read " + QFileInfo(path).fileName() + ".");
  QJsonParseError e;
  auto d = QJsonDocument::fromJson(f.readAll(), &e);
  if (e.error != QJsonParseError::NoError || !d.isObject())
    fail("Invalid machine JSON.");
  return d.object();
}
void writeJSON(const QString &path, const QJsonValue &value) {
  QSaveFile f(path);
  if (!f.open(QFile::WriteOnly))
    fail("Cannot save " + QFileInfo(path).fileName() + ".");
  auto data = value.isArray() ? QJsonDocument(value.toArray()).toJson()
                              : QJsonDocument(value.toObject()).toJson();
  if (f.write(data) != data.size() || !f.commit())
    fail("The disk could not save the configuration.");
}
QString localName(QJsonValue v, QString fallback) {
  auto s = (v.isUndefined() || v.isNull()) ? fallback : v.toString();
  if (s.isEmpty() || s.size() > 100 || s == "." || s == ".." ||
      s.contains(QRegularExpression("[\\\\/:\\x00-\\x1f]")))
    fail("A machine file name must stay inside its .classic folder.");
  return s;
}
QString machineFile(const QString &folder, const QString &name) {
  QString root = QFileInfo(folder).canonicalFilePath();
  QFileInfo f(root + "/" + localName(name));
  if (root.isEmpty() || !f.isFile() || f.isSymLink() ||
      QFileInfo(f.canonicalFilePath()).absolutePath() != root)
    fail("Machine files must be regular files inside the .classic folder.");
  return f.canonicalFilePath();
}
static int integer(QJsonObject &c, const QString &key, int fallback, int low,
                   int high) {
  if (!c.contains(key))
    c[key] = fallback;
  auto v = c[key];
  double d = v.toDouble(-1);
  if (!v.isDouble() || d != int(d) || d < low || d > high)
    fail("Unsupported " + key + " setting.");
  return int(d);
}
QJsonObject validate(QJsonObject c) {
  if (!QRegularExpression("^[a-f0-9]{8}(-[a-f0-9]{4}){3}-[a-f0-9]{12}$",
                          QRegularExpression::CaseInsensitiveOption)
           .match(c["id"].toString())
           .hasMatch())
    fail("Invalid machine identity.");
  if (!c.contains("machineFamily") || c["machineFamily"].isNull() ||
      (c["machineFamily"].isString() &&
       c["machineFamily"].toString().isEmpty()))
    c["machineFamily"] = c.contains("pramImageName")
                             ? QStringLiteral("quadra800")
                             : QStringLiteral("powerMacG4");
  auto family = c["machineFamily"].toString();
  if (!QStringList{"powerMacG4", "quadra800", "powerMac7500", "powerMacOSX",
                   "pippin"}
           .contains(family))
    fail("Unknown Mac model.");
  auto name = c["name"].toString().trimmed();
  if (name.isEmpty() || name.size() > 100 ||
      name.contains(QRegularExpression("[\\x00-\\x1f\\x7f]")))
    fail("Give your Mac a name of up to 100 characters.");
  c["name"] = name;
  bool quad = family == "quadra800", copland = family == "powerMac7500";
  bool osx = family == "powerMacOSX", pippin = family == "pippin";
  const int defaultRam = quad      ? 128
                         : copland ? 32
                         : osx     ? 1024
                         : pippin  ? 13
                                   : 512;
  const int minRam = quad ? 16 : osx ? 256 : pippin ? 5 : 32;
  const int maxRam = quad      ? 1000
                     : copland ? 32
                     : osx     ? 2048
                     : pippin  ? 16
                               : 896;
  int ram = integer(c, "ramMB", defaultRam, 5, 65536);
  c["ramMB"] = std::clamp(ram, minRam, maxRam);
  int width = integer(c, "width", 1024, 480, 3840);
  int height = integer(c, "height", 768, 384, 2160);
  int depth = integer(c, "depth", 16, 1, 32);
  if (!QList<int>{1, 2, 4, 8, 16, 24, 32}.contains(depth))
    fail("Unsupported color depth.");
  c["diskImageName"] = localName(c["diskImageName"], "disk.img");
  c["pramImageName"] = localName(c["pramImageName"], "pram.img");
  if (pippin) {
    QString videoOut =
        c.contains("pippinVideoOut") && c["pippinVideoOut"].isString()
            ? c["pippinVideoOut"].toString()
            : QStringLiteral("ntsc");
    if (!QStringList{"ntsc", "pal", "vga"}.contains(videoOut))
      videoOut = QStringLiteral("ntsc");
    c["pippinVideoOut"] = videoOut;
    c["flashImageName"] = localName(c["flashImageName"], "flash.img");
    if (c.contains("pippinROMPath") && !c["pippinROMPath"].isNull()) {
      if (!c["pippinROMPath"].isString() ||
          c["pippinROMPath"].toString().contains(QChar(0)))
        fail("Invalid pippinROMPath option.");
      const QString trimmedRom = c["pippinROMPath"].toString().trimmed();
      if (trimmedRom.isEmpty())
        c.remove("pippinROMPath");
      else
        c["pippinROMPath"] = trimmedRom;
    }
    if (c.contains("pippinControls") && !c["pippinControls"].isNull() &&
        !c["pippinControls"].isObject())
      fail("Invalid pippinControls option.");
  }
  if (!c.contains("graphicsCard") || c["graphicsCard"].isNull())
    c["graphicsCard"] = "standard";
  if (!QStringList{"standard", "atiRadeon"}.contains(
          c["graphicsCard"].toString()))
    fail("Unknown graphics card.");
  if (family != "powerMacG4")
    c["graphicsCard"] = "standard";
  for (auto key : {"sound", "networking", "tabletInput", "classicInputHelpers",
                   "useG4CPU", "useEnhancedFramebuffer", "bootFromCD",
                   "bootFromFloppy", "toolsCDInserted", "useVirtioDisk",
                   "legacyMouseTracking", "printingEnabled"}) {
    if (!c.contains(key))
      c[key] = !QStringList{"bootFromCD", "bootFromFloppy", "toolsCDInserted",
                            "useVirtioDisk", "printingEnabled"}
                    .contains(key);
    if (!c[key].isBool())
      fail("Invalid " + QString(key) + " option.");
  }
  if (c.contains("printerName")) {
    if (c["printerName"].isNull()) {
      c.remove("printerName");
    } else {
      if (!c["printerName"].isString())
        fail("Invalid printerName option.");
      const QString rawPrinter = c["printerName"].toString();
      if (rawPrinter.size() > 256 ||
          rawPrinter.contains(QRegularExpression("[\\x00-\\x1f\\x7f]")))
        fail("Invalid printerName option.");
      const QString trimmed = rawPrinter.trimmed();
      if (trimmed.isEmpty())
        c.remove("printerName");
      else
        c["printerName"] = trimmed;
    }
  }
  if (copland) {
    c["printingEnabled"] = false;
    c.remove("printerName");
  }
  int mouseDpi = integer(c, "mouseDPI", 0, 0, 100000);
  if (mouseDpi != 0 && mouseDpi < 100)
    fail("Unsupported mouseDPI setting.");
  if (c.contains("swapAltCommand") && !c["swapAltCommand"].isBool())
    fail("Invalid swapAltCommand option.");
  if (family != "powerMacG4")
    c["useVirtioDisk"] = false;
  if (family != "quadra800")
    c["useEnhancedFramebuffer"] = false;
  if (quad && !c["useEnhancedFramebuffer"].toBool()) {
    struct Mode {
      int w, h;
    };
    const Mode modes[] = {{640, 480}, {800, 600}, {1152, 870}};
    Mode best = modes[0];
    int bestDist = std::abs(width - best.w) + std::abs(height - best.h);
    for (const auto &mode : modes) {
      const int dist = std::abs(width - mode.w) + std::abs(height - mode.h);
      if (dist < bestDist) {
        best = mode;
        bestDist = dist;
      }
    }
    width = best.w;
    height = best.h;
    c["width"] = width;
    c["height"] = height;
    if (width == 1152) {
      if (depth > 8)
        depth = 8;
    } else if (depth == 16 || depth >= 24) {
      depth = 24;
    }
    c["depth"] = depth;
  }
  if (osx) {
    c["graphicsCard"] = "standard";
    c["depth"] = 32;
    c["classicInputHelpers"] = false;
    c["toolsCDInserted"] = false;
  }
  if (pippin) {
    const QString videoOut = c["pippinVideoOut"].toString("ntsc");
    c["width"] = 640;
    c["height"] = videoOut == "pal" ? 512 : 480;
    c["depth"] = 16;
    c["diskSizeGB"] = 0;
    c["graphicsCard"] = "standard";
    c["networking"] = false;
    c["printingEnabled"] = false;
    c.remove("printerName");
    c["tabletInput"] = false;
    c["classicInputHelpers"] = false;
    c["toolsCDInserted"] = false;
  }
  for (auto key : {"cdImagePath", "floppyImagePath", "sharedFolderPath"})
    if (c.contains(key) && !c[key].isNull() &&
        (!c[key].isString() || c[key].toString().contains(QChar(0))))
      fail("Invalid media path.");
  if (osx || pippin) {
    c.remove("sharedFolderPath");
    c.remove("floppyImagePath");
  }
  if (pippin) {
    c["bootFromCD"] = !c["cdImagePath"].toString().trimmed().isEmpty();
  }
  if (!quad || c["floppyImagePath"].toString().isEmpty() ||
      c["bootFromCD"].toBool()) {
    c["bootFromFloppy"] = false;
  }
  return c;
}
QJsonObject freshConfig(QString name, QString family) {
  return validate({{"id", QUuid::createUuid().toString(QUuid::WithoutBraces)},
                   {"name", name},
                   {"machineFamily", family},
                   {"diskSizeGB",
                    family == "quadra800" ? 2
                    : family == "pippin"  ? 0
                                          : 32}});
}
Machine readMachine(const QString &folder) {
  auto raw = readJSON(machineFile(folder, "config.json"));
  if (!raw.contains("machineFamily") || raw["machineFamily"].isNull() ||
      (raw["machineFamily"].isString() &&
       raw["machineFamily"].toString().isEmpty())) {
    const QString root = QFileInfo(folder).canonicalFilePath();
    const QString pramName =
        raw.contains("pramImageName") && raw["pramImageName"].isString()
            ? raw["pramImageName"].toString()
            : QStringLiteral("pram.img");
    const bool validPramName =
        !pramName.isEmpty() && pramName != "." && pramName != ".." &&
        !pramName.contains(QRegularExpression("[\\\\/:\\x00-\\x1f]"));
    const bool hasPram = !root.isEmpty() && validPramName &&
                         QFileInfo::exists(root + "/" + pramName);
    raw["machineFamily"] =
        hasPram ? QStringLiteral("quadra800") : QStringLiteral("powerMacG4");
  }
  auto c = validate(raw);
  if (c["machineFamily"].toString() == "pippin")
    machineFile(folder, c["flashImageName"].toString("flash.img"));
  else
    machineFile(folder, c["diskImageName"].toString());
  return {QFileInfo(folder).canonicalFilePath(), c};
}
QString uniqueFolder(const QString &parent, const QString &name) {
  QString base = name.trimmed();
  base.replace(QRegularExpression("[<>:\"/\\\\|?*\\x00-\\x1f]"), "-");
  base.remove(QRegularExpression("^[. ]+"));
  base.remove(QRegularExpression("[. ]+$"));
  base = base.left(100);
  if (base.isEmpty())
    base = "Mac OS";
  if (QRegularExpression("^(CON|PRN|AUX|NUL|COM[1-9]|LPT[1-9])(\\..*)?$",
                         QRegularExpression::CaseInsensitiveOption)
          .match(base)
          .hasMatch())
    base = "Mac " + base.left(96);
  QDir d(parent);
  if (!d.exists() && !d.mkpath("."))
    fail("Cannot create the machine destination.");
  for (int n = 1; n < 10000; ++n) {
    QString path = d.filePath(
        base + (n == 1 ? QString() : " " + QString::number(n)) + ".classic");
    if (QDir().mkdir(path))
      return path;
    if (!QFileInfo::exists(path))
      fail("Cannot create the machine folder.");
  }
  fail("Choose another machine name.");
}
bool makeSparse(QFile &f) {
#ifdef Q_OS_WIN
  DWORD returned = 0;
  return DeviceIoControl(reinterpret_cast<HANDLE>(_get_osfhandle(f.handle())),
                         FSCTL_SET_SPARSE, nullptr, 0, nullptr, 0, &returned,
                         nullptr);
#else
  Q_UNUSED(f);
  return true;
#endif
}
QString binary(const QString &name) {
  return runtime() + "/bin/" + name +
#ifdef Q_OS_WIN
         ".exe";
#else
         "";
#endif
}
Machine createMachine(QString name, QString family, int size,
                      const QString &parent, const QString &cd) {
  const bool pippin = family == "pippin";
  if (!QStringList{"powerMacG4", "quadra800", "powerMacOSX", "pippin"}.contains(
          family) ||
      (!pippin && !QList<int>{1, 2, 4, 8, 16, 32, 64, 120}.contains(size)))
    fail("Choose a supported Mac model and disk capacity.");
  auto c = freshConfig(name, family);
  c["diskSizeGB"] = pippin ? 0 : size;
  QString folder = uniqueFolder(parent, name);
  bool complete = false;
  auto cleanup = qScopeGuard([&] {
    if (!complete)
      QDir(folder).removeRecursively();
  });
  if (pippin) {
    QFile flash(folder + "/" + c["flashImageName"].toString("flash.img"));
    if (!flash.open(QFile::WriteOnly | QFile::NewOnly))
      fail("Could not create Pippin Save Flash storage.");
    flash.write(QByteArray(131072, char(0xff)));
    flash.close();
    QFile nvram(folder + "/nvram.img");
    if (nvram.open(QFile::WriteOnly | QFile::NewOnly)) {
      nvram.write(QByteArray(8192, char(0x00)));
      nvram.close();
    }
    QFile pram(folder + "/pram.img");
    if (pram.open(QFile::WriteOnly | QFile::NewOnly)) {
      pram.write(QByteArray(256, char(0x00)));
      pram.close();
    }
  } else {
    QFile disk(folder + "/disk.img");
    if (!disk.open(QFile::WriteOnly | QFile::NewOnly) || !makeSparse(disk) ||
        !disk.resize(qint64(size) * 1073741824))
      fail("Could not create a sparse hard disk.");
    disk.close();
  }
  if (family == "quadra800") {
    const QString pram = folder + "/pram.img";
    if (!QFile::copy(runtime() + "/pram-seed.img", pram))
      fail("Could not prepare the Mac's parameter memory.");
    QFile::setPermissions(pram, QFile::permissions(pram) |
                                    QFileDevice::ReadOwner |
                                    QFileDevice::WriteOwner |
                                    QFileDevice::ReadUser |
                                    QFileDevice::WriteUser);
  }
  if (!cd.isEmpty()) {
    QString suffix = QFileInfo(cd).suffix().toLower();
    if (!QStringList{"iso", "img", "toast", "cdr"}.contains(suffix) ||
        !QFileInfo(cd).isFile())
      fail("Choose an ISO, IMG, Toast or CDR image.");
    QString target = folder + "/Install CD." + suffix;
    if (!QFile::copy(cd, target))
      fail("Could not copy the installation CD.");
    QFile::setPermissions(target, QFile::permissions(target) |
                                      QFileDevice::ReadOwner |
                                      QFileDevice::WriteOwner |
                                      QFileDevice::ReadUser |
                                      QFileDevice::WriteUser);
    c["cdImagePath"] = target;
    c["bootFromCD"] = true;
  }
  writeJSON(folder + "/config.json", c);
  complete = true;
  return readMachine(folder);
}
bool supportedVersion(const QString &minimum) {
  auto a = QString(CLASSICMAC_VERSION).split('.'), b = minimum.split('.');
  for (int i = 0; i < 3; ++i) {
    int x = a.value(i).toInt(), y = b.value(i).toInt();
    if (x != y)
      return x > y;
  }
  return true;
}
bool supportsHostSharing() {
#ifdef Q_OS_WIN
  return false;
#else
  return true;
#endif
}
QString sanitizedName(const QString &raw) {
  QString s = raw;
  s.remove(QRegularExpression("[\\x00-\\x1f\\x7f]"));
  return s.trimmed().left(100).trimmed();
}
QProcessEnvironment hostEnvironment() {
  QProcessEnvironment env = QProcessEnvironment::systemEnvironment();
#ifdef Q_OS_WIN
  const QString appDir =
      QDir::toNativeSeparators(QCoreApplication::applicationDirPath());
  const QString currentPath = env.value("PATH");
  if (!appDir.isEmpty() &&
      !currentPath.split(';', Qt::SkipEmptyParts)
           .contains(appDir, Qt::CaseInsensitive))
    env.insert("PATH",
               currentPath.isEmpty() ? appDir : (appDir + ";" + currentPath));
#elif defined(Q_OS_LINUX)
  if (env.contains("CLASSICMAC_ORIG_LD_LIBRARY_PATH")) {
    const QString orig = env.value("CLASSICMAC_ORIG_LD_LIBRARY_PATH");
    if (orig.isEmpty())
      env.remove("LD_LIBRARY_PATH");
    else
      env.insert("LD_LIBRARY_PATH", orig);
  } else if (env.contains("APPDIR")) {
    const QString appDir = env.value("APPDIR");
    QStringList kept;
    for (const auto &entry :
         env.value("LD_LIBRARY_PATH").split(':', Qt::SkipEmptyParts))
      if (!entry.startsWith(appDir))
        kept << entry;
    if (kept.isEmpty())
      env.remove("LD_LIBRARY_PATH");
    else
      env.insert("LD_LIBRARY_PATH", kept.join(':'));
  }
  if (env.contains("CLASSICMAC_ORIG_QT_PLUGIN_PATH")) {
    const QString orig = env.value("CLASSICMAC_ORIG_QT_PLUGIN_PATH");
    if (orig.isEmpty())
      env.remove("QT_PLUGIN_PATH");
    else
      env.insert("QT_PLUGIN_PATH", orig);
  } else if (env.contains("APPDIR") ||
             env.value("QT_PLUGIN_PATH")
                 .startsWith(QCoreApplication::applicationDirPath())) {
    env.remove("QT_PLUGIN_PATH");
  }
  env.remove("CLASSICMAC_ORIG_LD_LIBRARY_PATH");
  env.remove("CLASSICMAC_ORIG_QT_PLUGIN_PATH");
#endif
  return env;
}
namespace {
constexpr int kOsxNvramBytes = 16384;
constexpr int kOsxBankBytes = 8192;

quint16 readBe16(const char *p) {
  return (quint16(quint8(p[0])) << 8) | quint16(quint8(p[1]));
}

quint32 readBe32(const char *p) {
  return (quint32(quint8(p[0])) << 24) | (quint32(quint8(p[1])) << 16) |
         (quint32(quint8(p[2])) << 8) | quint32(quint8(p[3]));
}

quint8 osxHeaderSum(const char *header16) {
  int total = 0;
  for (int i = 0; i < 16; ++i)
    total += (i == 1) ? 0 : int(quint8(header16[i]));
  while (total > 255)
    total = (total & 255) + (total >> 8);
  return quint8(total);
}

QList<QByteArray> validOsxBanks(const QByteArray &data) {
  QList<QByteArray> banks;
  if (data.size() != kOsxNvramBytes)
    return banks;
  for (int offset : {0, kOsxBankBytes}) {
    const QByteArray bank = data.mid(offset, kOsxBankBytes);
    const char *b = bank.constData();
    const quint32 computedAdler = static_cast<quint32>(::adler32(
        1L, reinterpret_cast<const Bytef *>(b + 20), kOsxBankBytes - 20));
    if (quint8(b[0]) == 0x5au && readBe16(b + 2) == 2 &&
        quint8(b[1]) == osxHeaderSum(b) && readBe32(b + 16) == computedAdler) {
      banks.append(bank);
    }
  }
  return banks;
}

int osxRtcOffset(const QByteArray &data) {
  QList<QByteArray> banks = validOsxBanks(data);
  if (banks.isEmpty())
    fail("This Mac's OS X startup memory is damaged. Restore a checkpoint or "
         "import the Mac again.");
  std::sort(banks.begin(), banks.end(),
            [](const QByteArray &a, const QByteArray &b) {
              return readBe32(a.constData() + 20) <
                     readBe32(b.constData() + 20);
            });
  if (banks.size() == 2) {
    const quint32 g0 = readBe32(banks[0].constData() + 20);
    const quint32 g1 = readBe32(banks[1].constData() + 20);
    if ((g0 == g1 && banks[0] != banks[1]) || (g1 - g0) >= 0x80000000u)
      fail("This Mac's startup-memory clock record is ambiguous.");
  }
  const QByteArray &bank = banks.last();
  const char *b = bank.constData();
  int offset = 0;
  QList<int> found;
  while (offset < kOsxBankBytes) {
    if (offset + 16 > kOsxBankBytes ||
        quint8(b[offset + 1]) != osxHeaderSum(b + offset))
      fail("This Mac's startup-memory partition record is damaged.");
    const int length = int(readBe16(b + offset + 2)) * 16;
    if (length < 16 || offset + length > kOsxBankBytes)
      fail("This Mac's startup-memory partition record is damaged.");
    QByteArray partName = bank.mid(offset + 4, 12);
    while (!partName.isEmpty() && partName.endsWith('\0'))
      partName.chop(1);
    if (partName == "APL,MacOS75") {
      if (length < 16 + 256)
        fail("This Mac's startup-memory clock record is incomplete.");
      const quint32 value = readBe32(b + offset + 16 + 0xec) & 0x00ffffffu;
      const int signedVal =
          (value & 0x800000u) ? int(value) - 0x1000000 : int(value);
      found.append(signedVal);
    }
    offset += length;
  }
  if (found.size() != 1 || found[0] < -86400 || found[0] > 86400)
    fail("This Mac's startup-memory clock record is not valid.");
  return found[0];
}
} // namespace

bool validateOsxNvram(const QByteArray &data, QString *errorOut) {
  if (!validOsxBanks(data).isEmpty())
    return true;
  if (errorOut)
    *errorOut = QStringLiteral(
        "This Mac's OS X startup memory is damaged. Restore a checkpoint or "
        "import the Mac again.");
  return false;
}

QString osxRtcBase(const QString &nvramPath, qint64 nowUtcSeconds) {
  const QFileInfo info(nvramPath);
  if (!info.exists() && !info.isSymLink())
    return QStringLiteral("base=utc");
  if (info.isSymLink() || !info.isFile())
    fail("This Mac's OS X startup memory could not be read.");
  QFile f(nvramPath);
  if (!f.open(QFile::ReadOnly) || f.size() != kOsxNvramBytes)
    fail("This Mac's OS X startup memory could not be read.");
  const QByteArray data = f.readAll();
  if (data.size() != kOsxNvramBytes)
    fail("This Mac's OS X startup memory could not be read.");
  const int offsetSec = osxRtcOffset(data);
  if (offsetSec == 0)
    return QStringLiteral("base=utc");
  const qint64 nowSec =
      nowUtcSeconds >= 0 ? nowUtcSeconds : QDateTime::currentSecsSinceEpoch();
#if QT_VERSION >= QT_VERSION_CHECK(6, 5, 0)
  const QDateTime instant =
      QDateTime::fromSecsSinceEpoch(nowSec + offsetSec, QTimeZone::utc());
#else
  const QDateTime instant =
      QDateTime::fromSecsSinceEpoch(nowSec + offsetSec, Qt::UTC);
#endif
  return QStringLiteral("base=") +
         instant.toString(QStringLiteral("yyyy-MM-dd'T'HH:mm:ss"));
}

QStringList qemuArguments(const Machine &m, int port,
                          const QString &audioOverride,
                          const PrintPorts &printPorts) {
  if (port < 5900 || port > 65535)
    fail("Invalid VNC display port.");
  auto c = validate(m.config);
  auto s = [&](const char *k) { return c[k].toString(); };
  auto b = [&](const char *k) { return c[k].toBool(); };
  auto opt = [](QString v) { return v.replace(",", ",,"); };
  auto r = runtime();
  if (m.family() == "powerMac7500")
    fail("Copland requires the macOS edition.");
  auto disk = m.family() == "pippin"
                  ? opt(m.folder + "/" + s("flashImageName"))
                  : opt(m.folder + "/" + s("diskImageName"));
#ifdef Q_OS_WIN
  if (!s("sharedFolderPath").isEmpty())
    fail("Host folder sharing requires Linux or macOS. Turn it off in Media "
         "for Windows.");
  QString audio = "dsound";
#elif defined(Q_OS_MACOS)
  QString audio = "coreaudio";
#else
  QString audio = "pa";
#endif
  if (!b("sound"))
    audio = "none";
  else if (!audioOverride.isEmpty()) {
    if (!QStringList{"none", "pa", "alsa", "dsound", "coreaudio", "pipewire",
                     "jack", "sdl"}
             .contains(audioOverride))
      fail("Unsupported audio backend.");
    audio = audioOverride;
  }
  bool ppc = m.family() == "powerMacG4",
       cdBoot = b("bootFromCD") && !s("cdImagePath").isEmpty(),
       legacy = ppc && b("useVirtioDisk"),
       compatCD = cdBoot && (!b("useG4CPU") || legacy),
       ati = ppc && !cdBoot && s("graphicsCard") == "atiRadeon",
       tools = ppc && b("toolsCDInserted") && !cdBoot,
       sharing = !cdBoot && !s("sharedFolderPath").isEmpty(),
       needsNdrvLoader =
           ppc && (tools || b("tabletInput") || ati || legacy || compatCD || sharing);
  const QString nicModel =
      (m.family() == "powerMacOSX" || ppc)
          ? (legacy ? QStringLiteral("rtl8139") : QStringLiteral("sungem"))
          : QStringLiteral("dp83932");
  auto buildNicArg = [&]() -> QString {
    if (!b("printingEnabled"))
      return b("networking") ? QStringLiteral("user,model=") + nicModel
                             : QStringLiteral("none");
    QString nic = QStringLiteral("user");
    if (!b("networking"))
      nic += QStringLiteral(",restrict=on");
    nic += QStringLiteral(",model=") + nicModel;
    if (printPorts.active()) {
      const QString bridge = opt(printPorts.bridgePath);
      nic += QString(",guestfwd=tcp:10.0.2.4:515-cmd:%1 --tcp-bridge 127.0.0.1 %2")
                 .arg(bridge)
                 .arg(printPorts.lpd);
      nic += QString(",guestfwd=tcp:10.0.2.4:631-cmd:%1 --tcp-bridge 127.0.0.1 %2")
                 .arg(bridge)
                 .arg(printPorts.ipp);
      nic += QString(",guestfwd=tcp:10.0.2.4:9100-cmd:%1 --tcp-bridge 127.0.0.1 %2")
                 .arg(bridge)
                 .arg(printPorts.raw);
    }
    return nic;
  };
  QStringList a = {"-L",
                   r + (m.family() == "powerMacOSX" ? "/qemu-osx" : "/qemu/pc-bios"),
                   "-name",
                   opt(m.name()),
                   "-m",
                   QString::number(c["ramMB"].toInt()),
                   "-audiodev",
                   audio + ",id=snd0",
                   "-display",
                   "vnc=127.0.0.1:" + QString::number(port - 5900) +
                       ",password=on,share=force-shared",
                   "-qmp",
                   "stdio",
                   "-monitor",
                   "none",
                   "-serial",
                   "none",
                   "-action",
                   "reboot=shutdown"};
  if (m.family() == "pippin") {
    const QString videoOut =
        c["pippinVideoOut"].toString(QStringLiteral("ntsc"));
    QString romPath = resolvePippinROM(m);
    if (romPath.isEmpty())
      romPath = m.folder + "/pippin.rom";
    const QString nvramPath = opt(m.folder + "/nvram.img");
    const QString pramPath = opt(m.folder + "/pram.img");
    a << "-accel" << "tcg,tb-size=512"
      << "-M" << QString("pippin,video-out=%1,audiodev=snd0").arg(videoOut)
      << "-bios" << romPath << "-drive"
      << "file=" + disk + ",if=pflash,format=raw" << "-drive"
      << "file=" + nvramPath + ",if=mtd,index=0,format=raw" << "-drive"
      << "file=" + pramPath + ",if=mtd,index=1,format=raw";
    if (!s("cdImagePath").isEmpty())
      a << "-drive"
        << "file=" + opt(s("cdImagePath")) +
               ",if=none,id=cd0,media=cdrom,readonly=on,format=raw";
    else
      a << "-drive" << "if=none,id=cd0,media=cdrom,readonly=on";
    a << "-device"
      << "scsi-cd,bus=mesh-scsi.0,scsi-id=3,drive=cd0,"
         "vendor=SONY    ,product=CD-ROM CDU-8003A,ver=1.9a,"
         "quirk_mode_page_apple_vendor=on,"
         "quirk_mode_sense_rom_use_dbd=on,"
         "quirk_mode_page_vendor_specific_apple=on,"
         "quirk_mode_page_truncated=on";
    return a;
  }
  if (m.family() == "powerMacOSX") {
    const auto width = c["width"].toInt() / 8 * 8;
    const auto height = c["height"].toInt();
    const auto flash = opt(m.folder + "/nvram.img");
    QString cd = "if=ide,index=2,media=cdrom,id=cd0,readonly=on";
    if (!s("cdImagePath").isEmpty())
      cd += ",file=" + opt(s("cdImagePath")) + ",format=raw";
    QString osVersion;
    const QString tplPath = m.folder + "/template-info.json";
    if (QFileInfo::exists(tplPath)) {
      try {
        osVersion = readJSON(tplPath).value("osVersion").toString();
      } catch (...) {
      }
    }
    a << "-accel" << "tcg,tb-size=512"
      << "-M" << "mac99,via=pmu,usb=off,audiodev=snd0" << "-cpu" << "7400"
      << "-bios" << r + "/qemu-osx/openbios-ppc-osx"
      << "-global" << "macio-nvram.osx-flash=on"
      << "-drive" << "if=none,id=osxnv,file=" + flash + ",format=raw"
      << "-global" << "macio-nvram.drive=osxnv"
      << "-rtc" << osxRtcBase(m.folder + "/nvram.img")
      << "-drive" << "file=" + disk + ",format=raw,media=disk,index=0"
      << "-drive" << cd << "-boot" << (cdBoot ? "d" : "c")
      << "-vga" << "std" << "-global" << "VGA.romfile="
      << "-global" << "VGA.vgamem_mb=64"
      << "-global" << QString("VGA.xres=%1").arg(width)
      << "-global" << QString("VGA.yres=%1").arg(height)
      << "-g" << QString("%1x%2x32").arg(width).arg(height)
      << "-prom-env" << "output-device=ttya";
    if (!cdBoot) {
      a << "-global" << "screamer.osx-codec=on"
        << "-global" << "screamer.dma-lead-us=0";
      if (!osVersion.trimmed().startsWith(QStringLiteral("10.2")))
        a << "-global" << "via-pmu.osx-sleep=on";
    }
    a << "-device" << "pci-ohci,id=osx-usb"
      << "-device" << "usb-kbd,bus=osx-usb.0"
      << "-device" << (b("tabletInput") ? "usb-tablet,bus=osx-usb.0"
                                       : "usb-mouse,bus=osx-usb.0")
      << "-nic" << buildNicArg();
    return a;
  }
  if (ppc) {
    if (legacy)
      a << "-nodefaults" << "-global" << "cuda.idle-irq-ms=50";
    else {
      if (!ati)
        a << "-global" << "cuda.idle-irq-ms=50";
      a << "-device"
        << (b("classicInputHelpers")
                ? "usb-mouse,id=classicmac-game-mouse"
                : "usb-mouse,id=classicmac-game-mouse,wheel-per-report=1");
    }
    a << "-accel" << "tcg,tb-size=512" << "-M"
      << (legacy ? "mac99,via=cuda,usb=off,audiodev=snd0"
                 : "mac99,via=cuda,audiodev=snd0")
      << "-cpu" << (b("useG4CPU") ? "7400" : "g3") << "-global"
      << QString("macio-ide.dma-completion-delay-ns=%1")
             .arg(cdBoot ? 1000000 : 0)
      << "-prom-env" << "output-device=ttya";
    if (!cdBoot && !legacy)
      a << "-global" << "macio-ide.pio-status-drain=on";
    if (legacy) {
      a << "-drive" << "file=" + disk + ",format=raw,if=none,id=startup0";
      if (!compatCD)
        a << "-device" << "virtio-blk-pci,drive=startup0,addr=0xf";
      if (!cdBoot)
        a << "-prom-env" << "boot-device=/pci@f2000000/scsi@f/disk:\\\\:tbxi";
    } else
      a << "-drive" << "file=" + disk + ",format=raw,media=disk";
    if (ati) {
      a << "-device" << "classicmac-display-control";
      int w = std::clamp(c["width"].toInt() / 8 * 8, 640, 1920),
          h = std::clamp(c["height"].toInt(), 480, 1440);
      a << "-vga" << "none" << "-device"
        << QString("ati-vga,id=ati0,model=r100,vgamem_mb=32,romfile=,guest_"
                   "hwcursor=on,xres=%1,yres=%2,refresh_rate=60000")
               .arg(w)
               .arg(h)
        << "-g" << "640x480x32" << "-prom-env"
        << QString("classicmac-display=%1x%2").arg(w).arg(h) << "-prom-env"
        << QString("retroboot-display=%1x%2").arg(w).arg(h);
    } else {
      a << "-vga" << "std" << "-global" << "VGA.host-resize=on" << "-global"
        << "VGA.vgamem_mb=64" << "-global" << "VGA.packed-lowbpp=on"
        << "-global" << "VGA.untracked-vram=on";
      if (b("tabletInput"))
        a << "-global" << "VGA.hardware-cursor=on";
      a << "-g"
        << QString("%1x%2x%3")
               .arg(c["width"].toInt())
               .arg(c["height"].toInt())
               .arg(c["depth"].toInt() >= 24 ? 32 : 15);
      if (!cdBoot && !legacy)
        a << "-icount" << "shift=4,sleep=off" << "-global"
          << "VGA.classicmac-boot-handoff=on";
    }
    if (!legacy || !s("cdImagePath").isEmpty())
      a << "-drive"
        << QString("if=ide,index=%1,media=cdrom,id=cd0,readonly=on")
                   .arg(cdBoot ? 2 : 3) +
               (s("cdImagePath").isEmpty()
                    ? QString()
                    : ",file=" + opt(s("cdImagePath")) + ",format=raw");
    if (!legacy)
      a << "-drive"
        << QString("if=ide,index=%1,media=cdrom,id=tools0,readonly=on")
               .arg(cdBoot ? 3 : 2);
    if (tools)
      a << "-drive"
        << "if=none,id=classicmac-tools,file=" +
               opt(r + "/ClassicMacTools.iso") + ",format=raw,readonly=on"
        << "-device"
        << (legacy ? "virtio-blk-pci,drive=classicmac-tools,addr=0x10"
                   : "virtio-blk-pci,drive=classicmac-tools");
    if (needsNdrvLoader)
      a << "-device" << "loader,addr=0x4000000,file=" + opt(r + "/ndrvloader")
        << "-prom-env" << "boot-command=init-program go";
    const bool hasNic = b("networking") || b("printingEnabled");
    if (b("tabletInput")) {
      QString tablet = "virtio-tablet-pci";
      // With ATI, Sungem and Tools present, automatic allocation puts the
      // tablet in slot 18, sharing Sungem's interrupt. Mac OS 9 then stalls
      // during startup. Slot 19 uses the quiet line on every desktop host,
      // shared with the display-control device, which has no PCI interrupt.
      // On legacy 8.5/8.6, skip slot 18 (rtl8139 IRQ 29) and pin to 0x14.
      if (legacy)
        tablet += ",addr=0x14";
      else if (ati && tools && hasNic)
        tablet += ",addr=0x13";
      a << "-device" << tablet;
    }
    if (compatCD) {
      a << "-drive"
        << "if=none,id=bootcd,file=" + opt(s("cdImagePath")) +
               ",format=raw,readonly=on"
        << "-device"
        << (legacy ? "virtio-blk-pci,drive=bootcd,addr=0xf"
                   : "virtio-blk-pci,drive=bootcd")
        << "-prom-env"
        << (legacy ? "boot-device=/pci@f2000000/scsi@f/disk:\\\\:tbxi"
                   : "boot-device=virtio0:\\\\:tbxi");
      if (legacy)
        a << "-device" << "virtio-blk-pci,drive=startup0,addr=0x10";
    } else if (cdBoot) {
      a << "-boot" << "d";
    } else if (legacy && !s("cdImagePath").isEmpty()) {
      a << "-drive"
        << "if=none,id=classicmac-cd,file=" + opt(s("cdImagePath")) +
               ",format=raw,readonly=on"
        << "-device" << "virtio-blk-pci,drive=classicmac-cd,addr=0x11";
    }
  } else {
    a << "-M"
      << QString("q800%1,audiodev=snd0")
             .arg(b("useEnhancedFramebuffer") ? ",fb=none" : "")
      << "-bios" << r + "/Quadra800.rom" << "-drive"
      << "file=" + opt(m.folder + "/" + s("pramImageName")) +
             ",format=raw,if=mtd"
      << "-device" << "scsi-hd,scsi-id=0,drive=hd0" << "-drive"
      << "file=" + disk + ",format=raw,if=none,id=hd0" << "-device"
      << "scsi-cd,scsi-id=3,drive=cd0" << "-drive"
      << "media=cdrom,if=none,id=cd0,readonly=on" +
             (s("cdImagePath").isEmpty()
                  ? QString()
                  : ",file=" + opt(s("cdImagePath")) + ",format=raw")
      << "-device" << "scsi-cd,scsi-id=4,drive=tools0" << "-drive"
      << "media=cdrom,if=none,id=tools0,readonly=on" +
             (b("toolsCDInserted")
                  ? ",file=" + opt(r + "/ClassicMacTools.iso") + ",format=raw"
                  : QString())
      << "-device" << "nubus-virtio-mmio,romfile=" + opt(r + "/declrom")
      << "-drive"
      << "if=none,id=fd0" +
             (s("floppyImagePath").isEmpty()
                  ? ",media=cdrom,readonly=off"
                  : ",file=" + opt(s("floppyImagePath")) + ",format=raw")
      << "-device" << "virtio-blk-device,drive=fd0,removable=on";
    if (b("useEnhancedFramebuffer"))
      a << "-device"
        << QString("nubus-qfb,width=%1,height=%2,depth=%3")
               .arg(c["width"].toInt())
               .arg(c["height"].toInt())
               .arg(c["depth"].toInt());
    else
      a << "-g"
        << QString("%1x%2x%3")
               .arg(c["width"].toInt())
               .arg(c["height"].toInt())
               .arg(c["depth"].toInt() >= 24 ? 24 : c["depth"].toInt());
    if (b("tabletInput"))
      a << "-device" << "virtio-tablet-device";
    if (cdBoot)
      a << "-boot" << "d";
  }
  if (sharing) {
    QString tag = c.value("sharedVolumeName").toString("Shared Files");
    if (tag.contains(QRegularExpression("[\\x00-\\x1f]")))
      fail("Invalid shared volume name.");
    tag.remove(':');
    tag = tag.trimmed().left(27).trimmed();
    if (tag.isEmpty())
      tag = "Shared Files";
    QString dev = ppc ? QStringLiteral("virtio-9p-pci")
                      : QStringLiteral("virtio-9p-device");
    if (ppc && legacy)
      dev += ",addr=0x13";
    else if (ppc && ati && (b("networking") || b("printingEnabled")))
      dev += ",addr=0x14";
    a << "-fsdev"
      << "local,id=share0,security_model=none,path=" +
             opt(s("sharedFolderPath"))
      << "-device"
      << dev + ",fsdev=share0,mount_tag=" + opt(tag);
  }
  a << "-nic" << buildNicArg();
  if (legacy && (b("networking") || b("printingEnabled")))
    a << "-global" << "rtl8139.romfile=" << "-global"
      << "rtl8139.tx-delay-ns=1000000";
  return a;
}
QStringList argumentsFor(const Machine &m, int port, bool silent) {
  return qemuArguments(m, port, silent ? QStringLiteral("none") : QString());
}
QString pippinROMPath() { return dataDirectory() + "/ROMs/pippin.rom"; }
QString resolvePippinROM(const Machine &m) {
  const QString explicitPath =
      m.config["pippinROMPath"].toString().trimmed();
  if (!explicitPath.isEmpty() && QFileInfo(explicitPath).isFile())
    return explicitPath;
  const QString r = runtime();
  for (const QString &candidate :
       {m.folder + "/pippin.rom", m.folder + "/kinka13.rom",
        m.folder + "/bootrom.bin", pippinROMPath(), r + "/pippin.rom"}) {
    if (QFileInfo(candidate).isFile())
      return candidate;
  }
  return {};
}
} // namespace Model
