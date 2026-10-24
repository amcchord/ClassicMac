#include "machine.h"
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
  auto family = c["machineFamily"].toString();
  if (!QStringList{"powerMacG4", "quadra800", "powerMac7500"}.contains(family))
    fail("Unknown Mac model.");
  auto name = c["name"].toString().trimmed();
  if (name.isEmpty() || name.size() > 100 ||
      name.contains(QRegularExpression("[\\x00-\\x1f]")))
    fail("Give your Mac a name of up to 100 characters.");
  c["name"] = name;
  bool quad = family == "quadra800", copland = family == "powerMac7500";
  integer(c, "ramMB",
          quad      ? 128
          : copland ? 32
                    : 512,
          quad ? 16 : 32,
          quad      ? 1000
          : copland ? 32
                    : 896);
  integer(c, "width", 1024, 480, 3840);
  integer(c, "height", 768, 480, 2160);
  int depth = integer(c, "depth", 16, 1, 32);
  if (!QList<int>{1, 2, 4, 8, 16, 32}.contains(depth))
    fail("Unsupported color depth.");
  c["diskImageName"] = localName(c["diskImageName"], "disk.img");
  c["pramImageName"] = localName(c["pramImageName"], "pram.img");
  if (!c.contains("graphicsCard"))
    c["graphicsCard"] = "standard";
  if (!QStringList{"standard", "atiRadeon"}.contains(
          c["graphicsCard"].toString()))
    fail("Unknown graphics card.");
  for (auto key : {"sound", "networking", "tabletInput", "classicInputHelpers",
                   "useG4CPU", "useEnhancedFramebuffer", "bootFromCD",
                   "toolsCDInserted", "useVirtioDisk"}) {
    if (!c.contains(key))
      c[key] = !QStringList{"bootFromCD", "toolsCDInserted", "useVirtioDisk"}
                    .contains(key);
    if (!c[key].isBool())
      fail("Invalid " + QString(key) + " option.");
  }
  if (family != "powerMacG4")
    c["useVirtioDisk"] = false;
  for (auto key : {"cdImagePath", "floppyImagePath", "sharedFolderPath"})
    if (c.contains(key) && !c[key].isNull() &&
        (!c[key].isString() || c[key].toString().contains(QChar(0))))
      fail("Invalid media path.");
  return c;
}
QJsonObject freshConfig(QString name, QString family) {
  return validate({{"id", QUuid::createUuid().toString(QUuid::WithoutBraces)},
                   {"name", name},
                   {"machineFamily", family},
                   {"diskSizeGB", family == "quadra800" ? 2 : 32}});
}
Machine readMachine(const QString &folder) {
  auto c = validate(readJSON(machineFile(folder, "config.json")));
  machineFile(folder, c["diskImageName"].toString());
  return {QFileInfo(folder).canonicalFilePath(), c};
}
QString uniqueFolder(const QString &parent, const QString &name) {
  QString base = name.trimmed();
  base.replace(QRegularExpression("[<>:\"/\\\\|?*\\x00-\\x1f]"), "-");
  base.remove(QRegularExpression("[. ]+$"));
  base = base.left(100);
  if (base.isEmpty())
    base = "Mac OS";
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
  if (!QStringList{"powerMacG4", "quadra800"}.contains(family) ||
      !QList<int>{1, 2, 4, 8, 16, 32, 64, 120}.contains(size))
    fail("Choose a supported Mac model and disk capacity.");
  auto c = freshConfig(name, family);
  c["diskSizeGB"] = size;
  QString folder = uniqueFolder(parent, name);
  bool complete = false;
  auto cleanup = qScopeGuard([&] {
    if (!complete)
      QDir(folder).removeRecursively();
  });
  QFile disk(folder + "/disk.img");
  if (!disk.open(QFile::WriteOnly | QFile::NewOnly) || !makeSparse(disk) ||
      !disk.resize(qint64(size) * 1073741824))
    fail("Could not create a sparse hard disk.");
  disk.close();
  if (family == "quadra800" &&
      !QFile::copy(runtime() + "/pram-seed.img", folder + "/pram.img"))
    fail("Could not prepare the Mac's parameter memory.");
  if (!cd.isEmpty()) {
    QString suffix = QFileInfo(cd).suffix().toLower();
    if (!QStringList{"iso", "img", "toast", "cdr"}.contains(suffix) ||
        !QFileInfo(cd).isFile())
      fail("Choose an ISO, IMG, Toast or CDR image.");
    QString target = folder + "/Install CD." + suffix;
    if (!QFile::copy(cd, target))
      fail("Could not copy the installation CD.");
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
QStringList argumentsFor(const Machine &m, int port, bool silent) {
  auto c = validate(m.config);
  auto s = [&](const char *k) { return c[k].toString(); };
  auto b = [&](const char *k) { return c[k].toBool(); };
  auto opt = [](QString v) { return v.replace(",", ",,"); };
  auto disk = opt(m.folder + "/" + s("diskImageName"));
  auto r = runtime();
  if (m.family() == "powerMac7500")
    fail("Copland requires the macOS edition.");
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
  if (silent || !b("sound"))
    audio = "none";
  bool ppc = m.family() == "powerMacG4",
       cdBoot = b("bootFromCD") && !s("cdImagePath").isEmpty(),
       legacy = ppc && b("useVirtioDisk"),
       compatCD = cdBoot && (!b("useG4CPU") || legacy),
       ati = ppc && !cdBoot && s("graphicsCard") == "atiRadeon";
  QStringList a = {"-L",
                   r + "/qemu/pc-bios",
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
  if (ppc) {
    if (legacy)
      a << "-nodefaults" << "-global" << "cuda.idle-irq-ms=50";
    else
      a << "-device" << "usb-mouse,id=classicmac-game-mouse";
    a << "-accel" << "tcg,tb-size=512" << "-M"
      << (legacy ? "mac99,via=cuda,usb=off,audiodev=snd0"
                 : "mac99,via=cuda,audiodev=snd0")
      << "-cpu" << (b("useG4CPU") ? "7400" : "g3") << "-global"
      << QString("macio-ide.dma-completion-delay-ns=%1")
             .arg(cdBoot ? 1000000 : 0)
      << "-prom-env" << "output-device=ttya";
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
        << "-global" << "VGA.untracked-vram=on" << "-global"
        << "VGA.hardware-cursor=on" << "-g"
        << QString("%1x%2x%3")
               .arg(c["width"].toInt())
               .arg(c["height"].toInt())
               .arg(c["depth"].toInt() == 32 ? 32 : 15);
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
    bool tools = b("toolsCDInserted") && !cdBoot;
    if (tools)
      a << "-drive"
        << "if=none,id=classicmac-tools,file=" +
               opt(r + "/ClassicMacTools.iso") + ",format=raw,readonly=on"
        << "-device" << "virtio-blk-pci,drive=classicmac-tools";
    if (b("tabletInput") || tools || legacy || ati) {
      a << "-device" << "loader,addr=0x4000000,file=" + opt(r + "/ndrvloader")
        << "-prom-env" << "boot-command=init-program go";
      if (b("tabletInput"))
        a << "-device" << "virtio-tablet-pci";
    }
    if (compatCD) {
      if (!b("tabletInput") && !tools && !legacy && !ati)
        a << "-device" << "loader,addr=0x4000000,file=" + opt(r + "/ndrvloader")
          << "-prom-env" << "boot-command=init-program go";
      a << "-drive"
        << "if=none,id=bootcd,file=" + opt(s("cdImagePath")) +
               ",format=raw,readonly=on"
        << "-device"
        << (legacy ? "virtio-blk-pci,drive=bootcd,addr=0xf"
                   : "virtio-blk-pci,drive=bootcd")
        << "-prom-env"
        << (legacy ? "boot-device=/pci@f2000000/scsi@f/disk:\\\\:tbxi"
                   : "boot-device=virtio0:\\:tbxi");
      if (legacy)
        a << "-device" << "virtio-blk-pci,drive=startup0,addr=0x10";
    } else if (cdBoot)
      a << "-boot" << "d";
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
               .arg(c["depth"].toInt());
    if (b("tabletInput"))
      a << "-device" << "virtio-tablet-device";
    if (cdBoot)
      a << "-boot" << "d";
  }
  if (!s("sharedFolderPath").isEmpty()) {
    QString tag = c.value("sharedVolumeName").toString("Shared Files");
    if (tag.size() > 31 || tag.contains(QRegularExpression("[\\x00-\\x1f]")))
      fail("Invalid shared volume name.");
    a << "-fsdev"
      << "local,id=share0,security_model=none,path=" +
             opt(s("sharedFolderPath"))
      << "-device"
      << QString(ppc ? "virtio-9p-pci" : "virtio-9p-device") +
             ",fsdev=share0,mount_tag=" + opt(tag);
    if (ppc && !legacy && !ati && !b("tabletInput") && !b("toolsCDInserted"))
      a << "-device" << "loader,addr=0x4000000,file=" + opt(r + "/ndrvloader")
        << "-prom-env" << "boot-command=init-program go";
  }
  a << "-nic"
    << (b("networking")
            ? QString("user,model=") +
                  (ppc ? (legacy ? "rtl8139" : "sungem") : "dp83932")
            : "none");
  if (legacy && b("networking"))
    a << "-global" << "rtl8139.romfile=" << "-global"
      << "rtl8139.tx-delay-ns=1000000";
  return a;
}
} // namespace Model
