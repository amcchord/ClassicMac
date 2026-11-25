#include "printer.h"
#include <QCoreApplication>
#include <QDateTime>
#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QFileInfo>
#include <QHostAddress>
#include <QProcess>
#include <QRegularExpression>
#include <QSaveFile>
#include <QStandardPaths>
#include <QTcpServer>
#include <QTcpSocket>
#include <QTemporaryFile>
#include <QtEndian>
#include <algorithm>
#include <cstring>
#include <vector>

#ifdef Q_OS_WIN
#include <windows.h>
#include <winspool.h>
#else
#include <arpa/inet.h>
#include <cerrno>
#include <csignal>
#include <fcntl.h>
#include <netdb.h>
#include <poll.h>
#include <sys/socket.h>
#include <unistd.h>
#endif

namespace Printer {

namespace {

constexpr int kMaxJobBytes = 64 * 1024 * 1024;
const QString kPrinterLabel = QStringLiteral("ClassicMac Virtual Printer");

QString decodeMacRoman(const QByteArray &bytes) {
  static const ushort kHigh[128] = {
      0x00C4, 0x00C5, 0x00C7, 0x00C9, 0x00D1, 0x00D6, 0x00DC, 0x00E1,
      0x00E0, 0x00E2, 0x00E4, 0x00E3, 0x00E5, 0x00E7, 0x00E9, 0x00E8,
      0x00EA, 0x00EB, 0x00ED, 0x00EC, 0x00EE, 0x00EF, 0x00F1, 0x00F3,
      0x00F2, 0x00F4, 0x00F6, 0x00F5, 0x00FA, 0x00F9, 0x00FB, 0x00FC,
      0x2020, 0x00B0, 0x00A2, 0x00A3, 0x00A7, 0x2022, 0x00B6, 0x00DF,
      0x00AE, 0x00A9, 0x2122, 0x00B4, 0x00A8, 0x2260, 0x00C6, 0x00D8,
      0x221E, 0x00B1, 0x2264, 0x2265, 0x00A5, 0x00B5, 0x2202, 0x2211,
      0x220F, 0x03C0, 0x222B, 0x00AA, 0x00BA, 0x03A9, 0x00E6, 0x00F8,
      0x00BF, 0x00A1, 0x00AC, 0x221A, 0x0192, 0x2248, 0x2206, 0x00AB,
      0x00BB, 0x2026, 0x00A0, 0x00C0, 0x00C3, 0x00D5, 0x0152, 0x0153,
      0x2013, 0x2014, 0x201C, 0x201D, 0x2018, 0x2019, 0x00F7, 0x25CA,
      0x00FF, 0x0178, 0x2044, 0x20AC, 0x2039, 0x203A, 0xFB01, 0xFB02,
      0x2021, 0x00B7, 0x201A, 0x201E, 0x2030, 0x00C2, 0x00CA, 0x00C1,
      0x00CB, 0x00C8, 0x00CD, 0x00CE, 0x00CF, 0x00CC, 0x00D3, 0x00D4,
      0xF8FF, 0x00D2, 0x00DA, 0x00DB, 0x00D9, 0x0131, 0x02C6, 0x02DC,
      0x00AF, 0x02D8, 0x02D9, 0x02DA, 0x00B8, 0x02DD, 0x02DB, 0x02C7};
  QString out;
  out.reserve(bytes.size());
  for (unsigned char ch : bytes) {
    out.append(ch < 0x80 ? QChar(ch) : QChar(kHigh[ch - 0x80]));
  }
  return out;
}

QByteArray stripEot(const QByteArray &data) {
  int start = 0;
  int end = data.size();
  while (start < end && data[start] == '\x04')
    ++start;
  while (end > start && data[end - 1] == '\x04')
    --end;
  return data.mid(start, end - start);
}

bool isPdfData(const QByteArray &data) {
  int pos = 0;
  while (pos < data.size()) {
    const unsigned char c = static_cast<unsigned char>(data[pos]);
    if (c == ' ' || c == '\t' || c == '\r' || c == '\n' || c == '\f' ||
        c == '\v')
      ++pos;
    else
      break;
  }
  return data.mid(pos, 5) == "%PDF-";
}

QString sanitizeJobName(const QString &raw) {
  QString name = raw.trimmed();
  name.replace(QRegularExpression("[^A-Za-z0-9 ._()-]+"), " ");
  name = name.split(QRegularExpression("\\s+"), Qt::SkipEmptyParts).join(' ');
  for (const auto &suffix : {".ps", ".eps", ".pdf"}) {
    if (name.endsWith(suffix, Qt::CaseInsensitive)) {
      name.chop(int(std::strlen(suffix)));
      break;
    }
  }
  while (!name.isEmpty() && QStringLiteral(" ._-").contains(name.front()))
    name.remove(0, 1);
  while (!name.isEmpty() && QStringLiteral(" ._-").contains(name.back()))
    name.chop(1);
  if (name.isEmpty())
    name = QStringLiteral("Mac OS Print");
  return name.left(64);
}

QString extractPsTitle(const QByteArray &data) {
  const QByteArray head = data.left(4096);
  const QList<QByteArray> lines = head.split('\n');
  for (QByteArray line : lines) {
    if (line.endsWith('\r'))
      line.chop(1);
    if (line.startsWith("%%Title:")) {
      QByteArray val = line.mid(8).trimmed();
      while (!val.isEmpty() &&
             (val.front() == '(' || val.front() == ')' || val.front() == ' '))
        val.remove(0, 1);
      while (!val.isEmpty() &&
             (val.back() == '(' || val.back() == ')' || val.back() == ' '))
        val.chop(1);
      if (!val.isEmpty())
        return decodeMacRoman(val);
    }
  }
  return QStringLiteral("Mac OS Print");
}

QString parseLpdControl(const QByteArray &data) {
  const QString text = decodeMacRoman(data);
  const QStringList lines = text.split(QRegularExpression("[\r\n]+"),
                                       Qt::SkipEmptyParts);
  for (const QString &line : lines) {
    if ((line.startsWith('J') || line.startsWith('N')) &&
        !line.mid(1).trimmed().isEmpty())
      return line.mid(1).trimmed();
  }
  return QStringLiteral("Mac OS Print");
}

QString extractIppQueue(const QString &path) {
  QString p = path;
  const int qIdx = p.indexOf('?');
  if (qIdx >= 0)
    p = p.left(qIdx);
  while (p.endsWith('/'))
    p.chop(1);
  if (p.startsWith("/printers/")) {
    QString q = QUrl::fromPercentEncoding(p.mid(10).section('/', 0, 0).toUtf8())
                    .trimmed();
    if (!q.isEmpty())
      return q;
  }
  return QStringLiteral("classicmac");
}

QString psEscape(const QString &raw) {
  QString out;
  out.reserve(raw.size());
  for (QChar ch : raw) {
    const ushort code = ch.unicode();
    if (code < 0x20 || code == 0x7f) {
      out.append(' ');
    } else if (code > 0x7e) {
      out.append('?');
    } else if (ch == '\\' || ch == '(' || ch == ')') {
      out.append('\\');
      out.append(ch);
    } else {
      out.append(ch);
    }
  }
  return out;
}

quint16 readBe16(const QByteArray &b, int off) {
  return (quint16(quint8(b[off])) << 8) | quint16(quint8(b[off + 1]));
}

quint32 readBe32(const QByteArray &b, int off) {
  return (quint32(quint8(b[off])) << 24) | (quint32(quint8(b[off + 1])) << 16) |
         (quint32(quint8(b[off + 2])) << 8) | quint32(quint8(b[off + 3]));
}

void writeBe16(QByteArray &out, quint16 v) {
  out.append(char((v >> 8) & 0xff));
  out.append(char(v & 0xff));
}

void writeBe32(QByteArray &out, quint32 v) {
  out.append(char((v >> 24) & 0xff));
  out.append(char((v >> 16) & 0xff));
  out.append(char((v >> 8) & 0xff));
  out.append(char(v & 0xff));
}

QByteArray ippAttr(quint8 tag, const QByteArray &name, const QByteArray &value) {
  QByteArray out;
  out.reserve(5 + name.size() + value.size());
  out.append(char(tag));
  writeBe16(out, quint16(name.size()));
  out.append(name);
  writeBe16(out, quint16(value.size()));
  out.append(value);
  return out;
}

QByteArray ippIntAttr(quint8 tag, const QByteArray &name, qint32 val) {
  QByteArray v(4, '\0');
  qToBigEndian(val, v.data());
  return ippAttr(tag, name, v);
}

QByteArray buildIppResponse(quint16 status, quint32 reqId,
                            const QByteArray &extra = {}) {
  QByteArray out;
  out.append('\x01');
  out.append('\x01');
  writeBe16(out, status);
  writeBe32(out, reqId);
  out.append('\x01'); // operation-attributes-tag
  out.append(ippAttr(0x47, "attributes-charset", "utf-8"));
  out.append(ippAttr(0x48, "attributes-natural-language", "en"));
  out.append(extra);
  out.append('\x03'); // end-of-attributes-tag
  return out;
}

struct IppParsed {
  bool valid = false;
  quint16 opId = 0;
  quint32 reqId = 1;
  QString jobName = QStringLiteral("Mac OS Print");
  bool hasJobId = false;
  int jobId = 0;
  bool lastDocument = true;
  QByteArray payload;
};

IppParsed parseIppRequest(const QByteArray &body) {
  IppParsed res;
  if (body.size() < 8)
    return res;
  res.valid = true;
  res.opId = readBe16(body, 2);
  res.reqId = readBe32(body, 4);
  int pos = 8;
  const int n = body.size();
  while (pos < n) {
    const quint8 tag = quint8(body[pos++]);
    if (tag == 0x03)
      break;
    if (tag <= 0x0F)
      continue;
    if (pos + 2 > n)
      break;
    const quint16 klen = readBe16(body, pos);
    pos += 2;
    if (pos + klen + 2 > n)
      break;
    const QByteArray key = body.mid(pos, klen);
    pos += klen;
    const quint16 vlen = readBe16(body, pos);
    pos += 2;
    if (pos + vlen > n)
      break;
    const QByteArray val = body.mid(pos, vlen);
    pos += vlen;
    if (key == "job-name" && !val.isEmpty()) {
      res.jobName = QString::fromUtf8(val);
    } else if (key == "job-id" && vlen == 4 && (tag == 0x21 || tag == 0x23)) {
      res.hasJobId = true;
      res.jobId = qFromBigEndian<qint32>(val.constData());
    } else if (key == "job-uri" && !val.isEmpty() && !res.hasJobId) {
      const QString uri = QString::fromUtf8(val);
      static const QRegularExpression re("/jobs/(\\d+)/*$");
      const auto m = re.match(uri);
      if (m.hasMatch()) {
        res.hasJobId = true;
        res.jobId = m.captured(1).toInt();
      }
    } else if (key == "last-document" && vlen == 1 && tag == 0x22) {
      res.lastDocument = (val[0] != '\0');
    }
  }
  res.payload = body.mid(pos);
  return res;
}

bool runHostProcess(const QString &program, const QStringList &args,
                    const QByteArray &stdinData, QByteArray &stdoutOut,
                    QByteArray &stderrOut, int timeoutMs = 15000) {
  QProcess proc;
  auto env = Model::hostEnvironment();
  env.insert(QStringLiteral("LC_ALL"), QStringLiteral("C"));
  proc.setProcessEnvironment(env);
  proc.start(program, args);
  if (!proc.waitForStarted(5000)) {
    stderrOut = proc.errorString().toUtf8();
    return false;
  }
  if (!stdinData.isEmpty()) {
    qint64 written = 0;
    while (written < stdinData.size()) {
      const qint64 n =
          proc.write(stdinData.constData() + written, stdinData.size() - written);
      if (n <= 0)
        break;
      written += n;
    }
  }
  proc.closeWriteChannel();
  if (!proc.waitForFinished(timeoutMs)) {
    proc.kill();
    proc.waitForFinished(2000);
    stderrOut = QByteArrayLiteral("Process timed out.");
    return false;
  }
  stdoutOut = proc.readAllStandardOutput();
  stderrOut = proc.readAllStandardError();
  return proc.exitStatus() == QProcess::NormalExit && proc.exitCode() == 0;
}

bool convertPsToPdf(const QString &gsPath, const QByteArray &psData,
                    const QString &outPdfPath, QString *errorOut) {
  if (gsPath.isEmpty()) {
    if (errorOut)
      *errorOut = QStringLiteral(
          "Ghostscript is not installed. Install Ghostscript to convert "
          "PostScript print jobs to PDF.");
    return false;
  }
  QByteArray out, err;
  const QStringList args = {QStringLiteral("-q"),
                            QStringLiteral("-dBATCH"),
                            QStringLiteral("-dNOPAUSE"),
                            QStringLiteral("-dSAFER"),
                            QStringLiteral("-sDEVICE=pdfwrite"),
                            QStringLiteral("-sOutputFile=") + outPdfPath,
                            QStringLiteral("-")};
  if (!runHostProcess(gsPath, args, psData, out, err, 120000) ||
      !QFileInfo(outPdfPath).isFile() || QFileInfo(outPdfPath).size() == 0) {
    const QString msg = QString::fromUtf8(err).trimmed().right(200);
    if (errorOut)
      *errorOut = msg.isEmpty()
                      ? QStringLiteral("Ghostscript could not convert the print job.")
                      : QStringLiteral("PDF conversion failed: ") + msg;
    return false;
  }
  return true;
}

#ifdef Q_OS_WIN
bool submitWinSpoolRaw(const QString &queueName, const QString &docName,
                       const QByteArray &data, QString *errorOut) {
  std::wstring wQueue = queueName.toStdWString();
  std::wstring wDoc = docName.toStdWString();
  std::wstring wType = L"RAW";
  HANDLE hPrinter = nullptr;
  if (!OpenPrinterW(wQueue.data(), &hPrinter, nullptr)) {
    if (errorOut)
      *errorOut = QStringLiteral(
                      "Could not open Windows printer \"%1\" (error %2). "
                      "Install Ghostscript (gswin64c.exe) for PostScript/PDF printing.")
                      .arg(queueName)
                      .arg(GetLastError());
    return false;
  }
  DOC_INFO_1W docInfo{};
  docInfo.pDocName = wDoc.data();
  docInfo.pOutputFile = nullptr;
  docInfo.pDatatype = wType.data();
  DWORD jobId = StartDocPrinterW(hPrinter, 1, reinterpret_cast<LPBYTE>(&docInfo));
  if (jobId == 0) {
    const DWORD err = GetLastError();
    ClosePrinter(hPrinter);
    if (errorOut)
      *errorOut = QStringLiteral(
                      "Could not start print job on \"%1\" (WinSpool error %2). "
                      "Install Ghostscript (gswin64c.exe) to print PostScript/PDF jobs on Windows.")
                      .arg(queueName)
                      .arg(err);
    return false;
  }
  bool ok = StartPagePrinter(hPrinter) != 0;
  if (ok) {
    DWORD writtenTotal = 0;
    while (writtenTotal < static_cast<DWORD>(data.size())) {
      DWORD chunk = 0;
      if (!WritePrinter(hPrinter,
                        const_cast<char *>(data.constData() + writtenTotal),
                        static_cast<DWORD>(data.size()) - writtenTotal, &chunk) ||
          chunk == 0) {
        ok = false;
        break;
      }
      writtenTotal += chunk;
    }
    EndPagePrinter(hPrinter);
  }
  const DWORD writeErr = ok ? 0 : GetLastError();
  EndDocPrinter(hPrinter);
  ClosePrinter(hPrinter);
  if (!ok && errorOut) {
    *errorOut = QStringLiteral(
                    "Could not write print data to \"%1\" (WinSpool error %2). "
                    "Install Ghostscript (gswin64c.exe) to print PostScript/PDF jobs on Windows.")
                    .arg(queueName)
                    .arg(writeErr);
  }
  return ok;
}
#endif

} // namespace

QList<HostPrinter> availablePrinters() {
  QList<HostPrinter> printers;
#ifdef Q_OS_WIN
  const QString defName = defaultPrinterName();
  DWORD needed = 0, returned = 0;
  EnumPrintersW(PRINTER_ENUM_LOCAL | PRINTER_ENUM_CONNECTIONS, nullptr, 2,
                nullptr, 0, &needed, &returned);
  if (needed > 0) {
    std::vector<BYTE> buf(needed);
    if (EnumPrintersW(PRINTER_ENUM_LOCAL | PRINTER_ENUM_CONNECTIONS, nullptr, 2,
                      buf.data(), needed, &needed, &returned)) {
      const auto *info = reinterpret_cast<const PRINTER_INFO_2W *>(buf.data());
      for (DWORD i = 0; i < returned; ++i) {
        if (!info[i].pPrinterName)
          continue;
        const QString name = QString::fromWCharArray(info[i].pPrinterName);
        if (name.isEmpty())
          continue;
        HostPrinter hp;
        hp.queueName = name;
        hp.displayName = name;
        hp.isDefault = !defName.isEmpty() &&
                       name.compare(defName, Qt::CaseInsensitive) == 0;
        printers.append(hp);
      }
    }
  }
#else
  const QString defQueue = defaultPrinterName();
  QByteArray out, err;
  if (!runHostProcess(QStringLiteral("lpstat"), {QStringLiteral("-p"), QStringLiteral("-l")},
                      {}, out, err, 5000)) {
    return printers;
  }
  const QStringList lines = QString::fromUtf8(out).split('\n');
  int currentIdx = -1;
  for (const QString &rawLine : lines) {
    if (rawLine.trimmed().isEmpty())
      continue;
    if (!rawLine[0].isSpace()) {
      currentIdx = -1;
      if (rawLine.startsWith(QStringLiteral("printer "))) {
        const QString rest = rawLine.mid(8);
        const QString qname = rest.section(QRegularExpression("\\s+"), 0, 0).trimmed();
        if (!qname.isEmpty()) {
          HostPrinter hp;
          hp.queueName = qname;
          hp.displayName = qname;
          hp.isDefault = (!defQueue.isEmpty() && qname == defQueue);
          printers.append(hp);
          currentIdx = int(printers.size()) - 1;
        }
      }
    } else if (currentIdx >= 0) {
      const QString trimmed = rawLine.trimmed();
      if (trimmed.startsWith(QStringLiteral("Description:"))) {
        const QString desc = trimmed.mid(12).trimmed();
        if (!desc.isEmpty())
          printers[currentIdx].displayName = desc;
      }
    }
  }
#endif
  return printers;
}

QString defaultPrinterName() {
#ifdef Q_OS_WIN
  DWORD size = 0;
  GetDefaultPrinterW(nullptr, &size);
  if (size == 0)
    return {};
  std::vector<wchar_t> buf(size);
  if (!GetDefaultPrinterW(buf.data(), &size))
    return {};
  return QString::fromWCharArray(buf.data()).trimmed();
#else
  QByteArray out, err;
  if (!runHostProcess(QStringLiteral("lpstat"), {QStringLiteral("-d")}, {}, out, err,
                      5000))
    return {};
  const QString text = QString::fromUtf8(out);
  for (const QString &line : text.split('\n')) {
    const int idx = line.indexOf(QStringLiteral("system default destination:"));
    if (idx >= 0)
      return line.mid(idx + 27).trimmed();
  }
  return {};
#endif
}

QString ghostscriptPath() {
  const auto env = Model::hostEnvironment();
#ifdef Q_OS_WIN
  const QStringList pathDirs =
      env.value(QStringLiteral("PATH")).split(';', Qt::SkipEmptyParts);
  for (const QString &exe : {"gswin64c.exe", "gswin32c.exe", "gs.exe"}) {
    const QString found = QStandardPaths::findExecutable(exe, pathDirs);
    if (!found.isEmpty())
      return found;
  }
  for (const QString &root :
       {env.value(QStringLiteral("ProgramFiles"), QStringLiteral("C:/Program Files")),
        env.value(QStringLiteral("ProgramFiles(x86)"),
                  QStringLiteral("C:/Program Files (x86)"))}) {
    const QDir gsDir(root + QStringLiteral("/gs"));
    if (!gsDir.exists())
      continue;
    QStringList subdirs = gsDir.entryList(QDir::Dirs | QDir::NoDotAndDotDot,
                                          QDir::Name | QDir::Reversed);
    for (const QString &sub : subdirs) {
      for (const QString &exe : {"gswin64c.exe", "gswin32c.exe"}) {
        const QString cand = gsDir.filePath(sub + QStringLiteral("/bin/") + exe);
        if (QFileInfo(cand).isExecutable())
          return QDir::toNativeSeparators(cand);
      }
    }
  }
  return {};
#else
  const QStringList pathDirs =
      env.value(QStringLiteral("PATH")).split(':', Qt::SkipEmptyParts);
  const QString inPath =
      QStandardPaths::findExecutable(QStringLiteral("gs"), pathDirs);
  if (!inPath.isEmpty())
    return inPath;
  for (const char *cand : {"/usr/bin/gs", "/opt/homebrew/bin/gs", "/usr/local/bin/gs"}) {
    const QFileInfo fi(QString::fromLatin1(cand));
    if (fi.exists() && fi.isExecutable())
      return fi.filePath();
  }
  return {};
#endif
}

QByteArray generateTestPostScript(const QString &targetLabel,
                                  const QString &version) {
  const QString verLine =
      version.trimmed().isEmpty()
          ? QStringLiteral("ClassicMac")
          : (QStringLiteral("ClassicMac ") + version.trimmed());
  const QString escVer = psEscape(verLine);
  const QString escTarget =
      psEscape(targetLabel.trimmed().isEmpty()
                   ? QStringLiteral("ClassicMac Printer")
                   : targetLabel.trimmed());
  const QString ps = QStringLiteral(
      "%!PS-Adobe-3.0\n"
      "%%Title: ClassicMac Printer Test Page\n"
      "%%Creator: ClassicMac\n"
      "%%Pages: 1\n"
      "%%BoundingBox: 0 0 612 792\n"
      "%%EndComments\n"
      "%%Page: 1 1\n"
      "/Helvetica-Bold findfont 22 scalefont setfont\n"
      "72 710 moveto (%1 - Printer Test Page) show\n\n"
      "/Helvetica findfont 12 scalefont setfont\n"
      "72 682 moveto (Target: %2) show\n"
      "72 664 moveto (Page size: US Letter 612 x 792 pt \\(PostScript Level 2/3\\)) show\n\n"
      "% Outer border\n"
      "0.8 setlinewidth\n"
      "0 setgray\n"
      "54 54 504 684 rectstroke\n\n"
      "% Checkerboard test pattern\n"
      "72 520 translate\n"
      "0 1 7 {\n"
      "  /row exch def\n"
      "  0 1 15 {\n"
      "    /col exch def\n"
      "    row col add 2 mod 0 eq {\n"
      "      0.15 setgray\n"
      "    } {\n"
      "      0.92 setgray\n"
      "    } ifelse\n"
      "    col 18 mul row 14 mul 18 14 rectfill\n"
      "  } for\n"
      "} for\n"
      "0 setgray\n"
      "0 0 288 112 rectstroke\n\n"
      "% Grayscale ramp\n"
      "0 -60 translate\n"
      "0 1 9 {\n"
      "  /i exch def\n"
      "  i 10 div setgray\n"
      "  i 28 mul 0 28 32 rectfill\n"
      "} for\n"
      "0 setgray\n"
      "0 0 280 32 rectstroke\n\n"
      "grestore\n"
      "gsave\n"
      "/Helvetica findfont 10 scalefont setfont\n"
      "72 90 moveto (If you can read this page clearly, ClassicMac printing is working.) show\n"
      "grestore\n"
      "showpage\n"
      "%%EOF\n")
                         .arg(escVer, escTarget);
  return ps.toLatin1();
}

QString defaultPdfDir(const Model::Machine &m) {
  const QString shared = m.config.value(QStringLiteral("sharedFolderPath")).toString();
  if (!shared.isEmpty() && QFileInfo(shared).isDir())
    return QDir(shared).filePath(QStringLiteral("Printed PDFs"));
  return Model::documents() + QStringLiteral("/Printed PDFs");
}

bool submitJob(const QByteArray &rawData, const QString &jobName,
               const QString &targetPrinter, const QString &pdfOutputDir,
               QString *errorOut, QString *savedPathOut) {
  if (savedPathOut)
    savedPathOut->clear();
  const QByteArray cleanData = stripEot(rawData);
  if (cleanData.isEmpty() || cleanData.size() > kMaxJobBytes) {
    if (errorOut)
      *errorOut = QStringLiteral("The print job is empty or too large.");
    return false;
  }
  const QString cleanName = sanitizeJobName(jobName);
  const bool isPdf = isPdfData(cleanData);
  const QString target = targetPrinter.trimmed();

  if (target == QStringLiteral("pdf")) {
    const QString outDir = pdfOutputDir.isEmpty()
                               ? (Model::documents() + QStringLiteral("/Printed PDFs"))
                               : pdfOutputDir;
    if (!QDir().mkpath(outDir)) {
      if (errorOut)
        *errorOut = QStringLiteral("Could not create the Printed PDFs folder.");
      return false;
    }
    const QString stamp =
        QDateTime::currentDateTime().toString(QStringLiteral("yyyy-MM-dd HH.mm.ss"));
    const QString base = stamp + QStringLiteral(" - ") + cleanName;
    QString dst = QDir(outDir).filePath(base + QStringLiteral(".pdf"));
    int n = 1;
    while (QFileInfo::exists(dst)) {
      ++n;
      dst = QDir(outDir).filePath(
          QStringLiteral("%1 (%2).pdf").arg(base).arg(n));
    }
    QByteArray pdfBytes;
    if (isPdf) {
      pdfBytes = cleanData;
    } else {
      QTemporaryFile tmpPdf(QDir::tempPath() + QStringLiteral("/cm-print-XXXXXX.pdf"));
      if (!tmpPdf.open()) {
        if (errorOut)
          *errorOut = QStringLiteral("Could not create a temporary PDF file.");
        return false;
      }
      const QString tmpPdfPath = tmpPdf.fileName();
      tmpPdf.close();
      if (!convertPsToPdf(ghostscriptPath(), cleanData, tmpPdfPath, errorOut))
        return false;
      QFile rf(tmpPdfPath);
      if (!rf.open(QFile::ReadOnly)) {
        if (errorOut)
          *errorOut = QStringLiteral("Could not read converted PDF.");
        return false;
      }
      pdfBytes = rf.readAll();
    }
    QSaveFile out(dst);
    if (!out.open(QFile::WriteOnly) || out.write(pdfBytes) != pdfBytes.size() ||
        !out.commit()) {
      if (errorOut)
        *errorOut = QStringLiteral("Could not save the PDF file to ") + dst;
      return false;
    }
    if (savedPathOut)
      *savedPathOut = dst;
    return true;
  }

  const QString queue = target.isEmpty() ? defaultPrinterName() : target;
  if (queue.isEmpty()) {
    if (errorOut)
      *errorOut = QStringLiteral(
          "No default printer is configured. Choose a printer in Get Info or "
          "select Save as PDF.");
    return false;
  }

  const QString gs = ghostscriptPath();

#ifdef Q_OS_WIN
  if (!gs.isEmpty()) {
    QTemporaryFile tmpIn(QDir::tempPath() +
                         (isPdf ? QStringLiteral("/cm-print-XXXXXX.pdf")
                                : QStringLiteral("/cm-print-XXXXXX.ps")));
    if (!tmpIn.open() || tmpIn.write(cleanData) != cleanData.size()) {
      if (errorOut)
        *errorOut = QStringLiteral("Could not write temporary print file.");
      return false;
    }
    const QString inPath = QDir::toNativeSeparators(tmpIn.fileName());
    tmpIn.close();
    QByteArray out, err;
    const QStringList args = {QStringLiteral("-dBATCH"),
                              QStringLiteral("-dNOPAUSE"),
                              QStringLiteral("-dSAFER"),
                              QStringLiteral("-q"),
                              QStringLiteral("-sDEVICE=mswinpr2"),
                              QStringLiteral("-sOutputFile=%printer%") + queue,
                              inPath};
    if (!runHostProcess(gs, args, {}, out, err, 120000)) {
      const QString msg = QString::fromUtf8(err).trimmed().right(200);
      if (errorOut)
        *errorOut = msg.isEmpty()
                        ? QStringLiteral("Printing to \"%1\" via Ghostscript failed.")
                              .arg(queue)
                        : QStringLiteral("Printing failed: ") + msg;
      return false;
    }
    return true;
  }
  return submitWinSpoolRaw(queue, cleanName, cleanData, errorOut);
#else
  if (isPdf) {
    QTemporaryFile tmpPdf(QDir::tempPath() + QStringLiteral("/cm-print-XXXXXX.pdf"));
    if (!tmpPdf.open() || tmpPdf.write(cleanData) != cleanData.size()) {
      if (errorOut)
        *errorOut = QStringLiteral("Could not write temporary PDF file.");
      return false;
    }
    const QString pdfPath = tmpPdf.fileName();
    tmpPdf.close();
    QByteArray out, err;
    if (!runHostProcess(QStringLiteral("lp"),
                        {QStringLiteral("-d"), queue, QStringLiteral("-t"),
                         cleanName, QStringLiteral("--"), pdfPath},
                        {}, out, err, 30000)) {
      const QString msg = QString::fromUtf8(err).trimmed();
      if (errorOut)
        *errorOut = msg.isEmpty()
                        ? QStringLiteral("lp failed to print to \"%1\".").arg(queue)
                        : msg;
      return false;
    }
    return true;
  }

  if (!gs.isEmpty()) {
    QTemporaryFile tmpPdf(QDir::tempPath() + QStringLiteral("/cm-print-XXXXXX.pdf"));
    if (!tmpPdf.open()) {
      if (errorOut)
        *errorOut = QStringLiteral("Could not create a temporary PDF file.");
      return false;
    }
    const QString pdfPath = tmpPdf.fileName();
    tmpPdf.close();
    if (!convertPsToPdf(gs, cleanData, pdfPath, errorOut))
      return false;
    QByteArray out, err;
    if (!runHostProcess(QStringLiteral("lp"),
                        {QStringLiteral("-d"), queue, QStringLiteral("-t"),
                         cleanName, QStringLiteral("--"), pdfPath},
                        {}, out, err, 30000)) {
      const QString msg = QString::fromUtf8(err).trimmed();
      if (errorOut)
        *errorOut = msg.isEmpty()
                        ? QStringLiteral("lp failed to print to \"%1\".").arg(queue)
                        : msg;
      return false;
    }
    return true;
  }

  QTemporaryFile tmpPs(QDir::tempPath() + QStringLiteral("/cm-print-XXXXXX.ps"));
  if (!tmpPs.open() || tmpPs.write(cleanData) != cleanData.size()) {
    if (errorOut)
      *errorOut = QStringLiteral("Could not write temporary PostScript file.");
    return false;
  }
  const QString psPath = tmpPs.fileName();
  tmpPs.close();
  QByteArray out, err;
  if (!runHostProcess(
          QStringLiteral("lp"),
          {QStringLiteral("-d"), queue, QStringLiteral("-t"), cleanName,
           QStringLiteral("-o"), QStringLiteral("document-format=application/postscript"),
           QStringLiteral("--"), psPath},
          {}, out, err, 30000)) {
    const QString msg = QString::fromUtf8(err).trimmed();
    if (errorOut)
      *errorOut =
          QStringLiteral("Printing failed (%1). Install Ghostscript (gs) so "
                         "PostScript jobs can be converted to PDF before printing.")
              .arg(msg.isEmpty() ? QStringLiteral("lp failed") : msg);
    return false;
  }
  return true;
#endif
}

int runTcpBridge(const QString &host, quint16 port) {
#ifdef Q_OS_WIN
  Q_UNUSED(host);
  Q_UNUSED(port);
  return 1;
#else
  if (host.isEmpty() || port == 0)
    return 1;
  std::signal(SIGPIPE, SIG_IGN);

  struct addrinfo hints{};
  hints.ai_family = AF_UNSPEC;
  hints.ai_socktype = SOCK_STREAM;
  struct addrinfo *res = nullptr;
  const QByteArray hostUtf8 = host.toUtf8();
  const QByteArray portUtf8 = QByteArray::number(port);
  if (getaddrinfo(hostUtf8.constData(), portUtf8.constData(), &hints, &res) != 0 ||
      !res)
    return 1;

  int sock = -1;
  for (struct addrinfo *ai = res; ai; ai = ai->ai_next) {
    sock = ::socket(ai->ai_family, ai->ai_socktype, ai->ai_protocol);
    if (sock < 0)
      continue;
    if (::connect(sock, ai->ai_addr, ai->ai_addrlen) == 0)
      break;
    ::close(sock);
    sock = -1;
  }
  freeaddrinfo(res);
  if (sock < 0)
    return 1;

  auto setNonBlock = [](int fd) {
    const int flags = ::fcntl(fd, F_GETFL, 0);
    if (flags >= 0)
      ::fcntl(fd, F_SETFL, flags | O_NONBLOCK);
  };
  setNonBlock(sock);
  setNonBlock(STDIN_FILENO);
  setNonBlock(STDOUT_FILENO);

  QByteArray toSock;
  QByteArray toStdout;
  bool stdinEof = false;
  bool sockEof = false;
  bool sockShutdownWr = false;

  auto flushBuffer = [](int fd, QByteArray &buf, bool &eofFlag) -> bool {
    while (!buf.isEmpty()) {
      const ssize_t n = ::write(fd, buf.constData(), size_t(buf.size()));
      if (n > 0) {
        buf.remove(0, int(n));
      } else if (n < 0 && (errno == EINTR)) {
        continue;
      } else if (n < 0 && (errno == EAGAIN || errno == EWOULDBLOCK)) {
        return true;
      } else {
        eofFlag = true;
        return false;
      }
    }
    return true;
  };

  while (!(sockEof && toStdout.isEmpty())) {
    struct pollfd pfds[3]{};
    int nfds = 0;
    int idxStdin = -1, idxStdout = -1, idxSock = -1;

    if (!stdinEof && toSock.size() < 262144) {
      idxStdin = nfds;
      pfds[nfds++] = {STDIN_FILENO, POLLIN, 0};
    }
    if (!toStdout.isEmpty()) {
      idxStdout = nfds;
      pfds[nfds++] = {STDOUT_FILENO, POLLOUT, 0};
    }
    {
      short ev = 0;
      if (!sockEof && toStdout.size() < 262144)
        ev |= POLLIN;
      if (!toSock.isEmpty())
        ev |= POLLOUT;
      if (ev != 0) {
        idxSock = nfds;
        pfds[nfds++] = {sock, ev, 0};
      }
    }
    if (nfds == 0)
      break;

    const int rc = ::poll(pfds, nfds, 30000);
    if (rc < 0) {
      if (errno == EINTR)
        continue;
      break;
    }

    if (idxStdin >= 0 && (pfds[idxStdin].revents & (POLLIN | POLLHUP | POLLERR))) {
      char buf[65536];
      for (;;) {
        const ssize_t n = ::read(STDIN_FILENO, buf, sizeof(buf));
        if (n > 0) {
          toSock.append(buf, int(n));
        } else if (n == 0) {
          stdinEof = true;
          break;
        } else if (errno == EINTR) {
          continue;
        } else if (errno == EAGAIN || errno == EWOULDBLOCK) {
          break;
        } else {
          stdinEof = true;
          break;
        }
      }
    }

    if (idxSock >= 0 && (pfds[idxSock].revents & (POLLIN | POLLHUP | POLLERR))) {
      char buf[65536];
      for (;;) {
        const ssize_t n = ::read(sock, buf, sizeof(buf));
        if (n > 0) {
          toStdout.append(buf, int(n));
        } else if (n == 0) {
          sockEof = true;
          break;
        } else if (errno == EINTR) {
          continue;
        } else if (errno == EAGAIN || errno == EWOULDBLOCK) {
          break;
        } else {
          sockEof = true;
          break;
        }
      }
    }

    if (!toSock.isEmpty()) {
      bool writeErr = false;
      flushBuffer(sock, toSock, writeErr);
      if (writeErr)
        break;
    }
    if (stdinEof && toSock.isEmpty() && !sockShutdownWr) {
      ::shutdown(sock, SHUT_WR);
      sockShutdownWr = true;
    }

    if (!toStdout.isEmpty()) {
      bool stdoutErr = false;
      if (idxStdout >= 0 && (pfds[idxStdout].revents & (POLLERR | POLLHUP)))
        break;
      flushBuffer(STDOUT_FILENO, toStdout, stdoutErr);
      if (stdoutErr)
        break;
    }
  }

  ::close(sock);
  return 0;
#endif
}

QString ensureBridgeSymlink() {
#ifdef Q_OS_WIN
  return {};
#else
  const QString exe = QCoreApplication::applicationFilePath();
  if (exe.isEmpty())
    return {};
  const QString linkPath =
      QStringLiteral("/tmp/cm-bridge-%1").arg(static_cast<qulonglong>(::geteuid()));
  const QFileInfo info(linkPath);
  if (info.isSymLink() && info.symLinkTarget() == exe)
    return linkPath;
  QFile::remove(linkPath);
  if (QFile::link(exe, linkPath))
    return linkPath;
  return exe;
#endif
}

class SessionHelper {
public:
  static bool submit(Server *srv, const QByteArray &data, const QString &jobName,
                     QString *err) {
    return srv->dispatchSubmission(data, jobName, err);
  }
  static QByteArray ipp(Server *srv, const QByteArray &body,
                        const QString &queue) {
    return srv->dispatchIpp(body, queue);
  }
};

namespace {

class LpdSession : public QObject {
public:
  LpdSession(QTcpSocket *sock, Server *srv)
      : QObject(srv), socket(sock), server(srv) {
    socket->setParent(this);
    connect(socket, &QTcpSocket::readyRead, this, &LpdSession::onReadyRead);
    connect(socket, &QTcpSocket::readChannelFinished, this,
            &LpdSession::onEof);
    connect(socket, &QTcpSocket::disconnected, this, &LpdSession::onDisconnected);
  }

private:
  enum State { WaitCmd, WaitSubcmd, ReadDataUntilEof, ReadDataExact, WaitTerm, Done };
  QTcpSocket *socket;
  Server *server;
  State state = WaitCmd;
  QByteArray buf;
  QByteArray currentSub;
  QByteArray dataBuf;
  quint8 expectedType = 0;
  int expectedCount = 0;
  QString queue = QStringLiteral("classicmac");
  QString jobName = QStringLiteral("Mac OS Print");
  QList<QByteArray> payloads;
  bool submitted = false;

  void finishJobAndClose() {
    if (!submitted && !payloads.isEmpty()) {
      submitted = true;
      QByteArray joined;
      for (const auto &p : payloads)
        joined.append(p);
      payloads.clear();
      QString err;
      SessionHelper::submit(server, joined, jobName, &err);
    }
    state = Done;
    socket->disconnectFromHost();
  }

  void process() {
    while (state != Done) {
      if (state == WaitCmd) {
        const int nl = buf.indexOf('\n');
        if (nl < 0)
          return;
        const QByteArray line = buf.left(nl + 1);
        buf.remove(0, nl + 1);
        if (line.isEmpty())
          continue;
        const quint8 cmd = quint8(line[0]);
        if (cmd == 1 || cmd == 3 || cmd == 4) {
          socket->write(cmd == 1 ? QByteArray("\x00", 1)
                                 : QByteArray("no entries\n"));
          socket->flush();
          state = Done;
          socket->disconnectFromHost();
          return;
        }
        if (cmd != 2) {
          socket->write("\x01", 1);
          socket->flush();
          state = Done;
          socket->disconnectFromHost();
          return;
        }
        const QString q = QString::fromLatin1(line.mid(1)).trimmed();
        if (!q.isEmpty())
          queue = q;
        socket->write("\x00", 1);
        state = WaitSubcmd;
      } else if (state == WaitSubcmd) {
        const int nl = buf.indexOf('\n');
        if (nl < 0)
          return;
        QByteArray sub = buf.left(nl + 1);
        buf.remove(0, nl + 1);
        // Classic Mac OS 9 Desktop PrintMonitor can emit leading NUL padding.
        while (!sub.isEmpty() && sub[0] == '\0')
          sub.remove(0, 1);
        if (sub.isEmpty() || sub == "\n")
          continue;
        const quint8 stype = quint8(sub[0]);
        if (stype == 1) {
          socket->write("\x00", 1);
          socket->flush();
          payloads.clear();
          state = Done;
          socket->disconnectFromHost();
          return;
        }
        if (stype != 2 && stype != 3) {
          socket->write("\x01", 1);
          socket->flush();
          state = Done;
          socket->disconnectFromHost();
          return;
        }
        const QString rest = QString::fromLatin1(sub.mid(1)).trimmed();
        const QStringList parts =
            rest.split(QRegularExpression("\\s+"), Qt::SkipEmptyParts);
        if (parts.isEmpty()) {
          socket->write("\x01", 1);
          socket->flush();
          state = Done;
          socket->disconnectFromHost();
          return;
        }
        bool ok = false;
        const qint64 count = parts[0].toLongLong(&ok);
        if (!ok || count < 0 || count > kMaxJobBytes) {
          socket->write("\x01", 1);
          socket->flush();
          state = Done;
          socket->disconnectFromHost();
          return;
        }
        currentSub = sub;
        expectedType = stype;
        expectedCount = int(count);
        dataBuf.clear();
        socket->write("\x00", 1);
        state = (expectedCount == 0) ? ReadDataUntilEof : ReadDataExact;
      } else if (state == ReadDataUntilEof) {
        if (!buf.isEmpty()) {
          if (dataBuf.size() + buf.size() > kMaxJobBytes) {
            state = Done;
            socket->abort();
            return;
          }
          dataBuf.append(buf);
          buf.clear();
        }
        return;
      } else if (state == ReadDataExact) {
        if (buf.isEmpty())
          return;
        if (dataBuf.isEmpty()) {
          // Classic Mac OS 9 Desktop PrintMonitor can re-send the subcommand
          // header line right after receiving 0x00 and wait for a second 0x00.
          if (buf.startsWith(currentSub)) {
            socket->write("\x00", 1);
            buf.remove(0, currentSub.size());
            continue;
          }
          if (buf.size() < currentSub.size() && currentSub.startsWith(buf)) {
            return;
          }
        }
        const int need = expectedCount - dataBuf.size();
        const int take = std::min(need, int(buf.size()));
        dataBuf.append(buf.constData(), take);
        buf.remove(0, take);
        if (dataBuf.size() == expectedCount)
          state = WaitTerm;
      } else if (state == WaitTerm) {
        if (buf.isEmpty())
          return;
        const char term = buf[0];
        buf.remove(0, 1);
        if (term != '\0') {
          socket->write("\x01", 1);
          socket->flush();
          state = Done;
          socket->disconnectFromHost();
          return;
        }
        socket->write("\x00", 1);
        if (expectedType == 2)
          jobName = parseLpdControl(dataBuf);
        else
          payloads.append(dataBuf);
        dataBuf.clear();
        state = WaitSubcmd;
      }
    }
  }

  void onReadyRead() {
    buf.append(socket->readAll());
    process();
  }

  void onEof() {
    buf.append(socket->readAll());
    process();
    if (state == ReadDataUntilEof) {
      if (expectedType == 2)
        jobName = parseLpdControl(dataBuf);
      else if (!dataBuf.isEmpty())
        payloads.append(dataBuf);
      dataBuf.clear();
      finishJobAndClose();
    } else if (state == WaitTerm && buf.isEmpty()) {
      socket->write("\x00", 1);
      if (expectedType == 2)
        jobName = parseLpdControl(dataBuf);
      else
        payloads.append(dataBuf);
      dataBuf.clear();
      finishJobAndClose();
    } else if (state == WaitSubcmd) {
      finishJobAndClose();
    } else if (state == ReadDataExact && !buf.isEmpty()) {
      const int need = expectedCount - dataBuf.size();
      const int take = std::min(need, int(buf.size()));
      dataBuf.append(buf.constData(), take);
      buf.remove(0, take);
      if (dataBuf.size() == expectedCount) {
        socket->write("\x00", 1);
        if (expectedType == 2)
          jobName = parseLpdControl(dataBuf);
        else
          payloads.append(dataBuf);
        dataBuf.clear();
      }
      finishJobAndClose();
    } else if (state != Done) {
      state = Done;
      socket->disconnectFromHost();
    }
  }

  void onDisconnected() {
    onEof();
    deleteLater();
  }
};

class RawSession : public QObject {
public:
  RawSession(QTcpSocket *sock, Server *srv)
      : QObject(srv), socket(sock), server(srv) {
    socket->setParent(this);
    connect(socket, &QTcpSocket::readyRead, this, &RawSession::onReadyRead);
    connect(socket, &QTcpSocket::readChannelFinished, this,
            &RawSession::onFinish);
    connect(socket, &QTcpSocket::disconnected, this, &RawSession::onDisconnected);
  }

private:
  QTcpSocket *socket;
  Server *server;
  QByteArray buf;
  bool submitted = false;
  bool aborted = false;

  void onReadyRead() {
    if (aborted)
      return;
    buf.append(socket->readAll());
    if (buf.size() > kMaxJobBytes) {
      aborted = true;
      buf.clear();
      socket->abort();
    }
  }

  void onFinish() {
    if (aborted || submitted)
      return;
    buf.append(socket->readAll());
    if (buf.size() > kMaxJobBytes) {
      aborted = true;
      buf.clear();
      socket->abort();
      return;
    }
    submitted = true;
    if (!buf.isEmpty()) {
      const QString title = extractPsTitle(stripEot(buf));
      QString err;
      SessionHelper::submit(server, buf, title, &err);
    }
    socket->disconnectFromHost();
  }

  void onDisconnected() {
    onFinish();
    deleteLater();
  }
};

class IppSession : public QObject {
public:
  IppSession(QTcpSocket *sock, Server *srv)
      : QObject(srv), socket(sock), server(srv) {
    socket->setParent(this);
    connect(socket, &QTcpSocket::readyRead, this, &IppSession::onReadyRead);
    connect(socket, &QTcpSocket::readChannelFinished, this,
            &IppSession::onFinish);
    connect(socket, &QTcpSocket::disconnected, this, &IppSession::onDisconnected);
  }

private:
  enum State { ReadHeaders, ReadFixed, ReadChunkSize, ReadChunkData, Done };
  QTcpSocket *socket;
  Server *server;
  State state = ReadHeaders;
  QByteArray buf;
  QByteArray body;
  QString method;
  QString queue;
  bool closeAfter = false;
  qint64 contentLength = 0;
  qint64 currentChunkSize = 0;

  void writeHttpResponse(int status, const QByteArray &ctype,
                         const QByteArray &payload) {
    const QByteArray hdr =
        QStringLiteral(
            "HTTP/1.1 %1 OK\r\nContent-Type: %2\r\nContent-Length: %3\r\n"
            "Connection: %4\r\n\r\n")
            .arg(status)
            .arg(QString::fromLatin1(ctype))
            .arg(payload.size())
            .arg(closeAfter ? QStringLiteral("close")
                            : QStringLiteral("keep-alive"))
            .toLatin1();
    socket->write(hdr);
    socket->write(payload);
    socket->flush();
  }

  void finishRequest() {
    if (method == QStringLiteral("POST")) {
      const QByteArray resp = SessionHelper::ipp(server, body, queue);
      writeHttpResponse(200, "application/ipp", resp);
    } else {
      writeHttpResponse(200, "text/plain; charset=utf-8",
                        kPrinterLabel.toUtf8() + '\n');
    }
    body.clear();
    if (closeAfter) {
      state = Done;
      socket->disconnectFromHost();
    } else {
      state = ReadHeaders;
    }
  }

  void process() {
    while (state != Done) {
      if (state == ReadHeaders) {
        // Consume any trailing CRLF after a previous chunked 0-size terminator.
        while (buf.startsWith("\r\n"))
          buf.remove(0, 2);
        const int sep = buf.indexOf("\r\n\r\n");
        if (sep < 0) {
          if (buf.size() > 65536) {
            state = Done;
            socket->abort();
          }
          return;
        }
        const QByteArray hdrBytes = buf.left(sep);
        buf.remove(0, sep + 4);
        const QStringList lines = QString::fromLatin1(hdrBytes).split("\r\n");
        if (lines.isEmpty()) {
          state = Done;
          socket->abort();
          return;
        }
        const QStringList reqParts =
            lines[0].split(QRegularExpression("\\s+"), Qt::SkipEmptyParts);
        method = reqParts.value(0).toUpper();
        const QString reqPath = reqParts.value(1, QStringLiteral("/"));
        queue = extractIppQueue(reqPath);
        QMap<QString, QString> headers;
        for (int i = 1; i < lines.size(); ++i) {
          const int colon = lines[i].indexOf(':');
          if (colon > 0)
            headers[lines[i].left(colon).trimmed().toLower()] =
                lines[i].mid(colon + 1).trimmed();
        }
        closeAfter =
            headers.value(QStringLiteral("connection")).toLower() == "close" ||
            lines[0].contains(QStringLiteral("HTTP/1.0"));
        if (headers.value(QStringLiteral("expect")).toLower() == "100-continue") {
          socket->write("HTTP/1.1 100 Continue\r\n\r\n");
          socket->flush();
        }
        body.clear();
        if (headers.value(QStringLiteral("transfer-encoding"))
                .toLower()
                .contains(QStringLiteral("chunked"))) {
          state = ReadChunkSize;
        } else {
          bool ok = true;
          const QString clStr = headers.value(QStringLiteral("content-length"));
          contentLength = clStr.isEmpty() ? 0 : clStr.toLongLong(&ok);
          if (!ok || contentLength < 0 || contentLength > kMaxJobBytes) {
            state = Done;
            socket->abort();
            return;
          }
          if (contentLength == 0) {
            finishRequest();
          } else {
            state = ReadFixed;
          }
        }
      } else if (state == ReadFixed) {
        const qint64 need = contentLength - body.size();
        const int take = int(std::min<qint64>(need, buf.size()));
        if (take > 0) {
          body.append(buf.constData(), take);
          buf.remove(0, take);
        }
        if (body.size() < contentLength)
          return;
        finishRequest();
      } else if (state == ReadChunkSize) {
        const int nl = buf.indexOf("\r\n");
        if (nl < 0)
          return;
        const QByteArray sizeLine = buf.left(nl);
        buf.remove(0, nl + 2);
        const QByteArray hexPart = sizeLine.split(';').first().trimmed();
        bool ok = false;
        currentChunkSize = hexPart.toLongLong(&ok, 16);
        if (!ok || currentChunkSize < 0 ||
            body.size() + currentChunkSize > kMaxJobBytes) {
          state = Done;
          socket->abort();
          return;
        }
        if (currentChunkSize == 0) {
          if (buf.startsWith("\r\n"))
            buf.remove(0, 2);
          finishRequest();
        } else {
          state = ReadChunkData;
        }
      } else if (state == ReadChunkData) {
        if (buf.size() < currentChunkSize + 2)
          return;
        body.append(buf.constData(), int(currentChunkSize));
        buf.remove(0, int(currentChunkSize) + 2);
        state = ReadChunkSize;
      }
    }
  }

  void onReadyRead() {
    buf.append(socket->readAll());
    process();
  }

  void onFinish() {
    buf.append(socket->readAll());
    process();
    if (state != Done) {
      state = Done;
      socket->disconnectFromHost();
    }
  }

  void onDisconnected() { deleteLater(); }
};

} // namespace

Server::Server(QString target, QString pdfDir, bool ephemeral, QObject *parent)
    : QObject(parent),
      targetPrinter(std::move(target)),
      pdfOutputDir(std::move(pdfDir)),
      ephemeralForTest(ephemeral) {}

Server::~Server() { stop(); }

bool Server::start() {
  stop();
#ifdef Q_OS_WIN
  const quint16 wantLpd = ephemeralForTest ? 0 : 515;
  const quint16 wantIpp = ephemeralForTest ? 0 : 631;
  const quint16 wantRaw = ephemeralForTest ? 0 : 9100;
#else
  const quint16 wantLpd = 0;
  const quint16 wantIpp = 0;
  const quint16 wantRaw = 0;
#endif

  lpdServer = new QTcpServer(this);
  if (lpdServer->listen(QHostAddress::LocalHost, wantLpd)) {
    boundLpd = lpdServer->serverPort();
    connect(lpdServer, &QTcpServer::newConnection, this, [this] {
      while (auto *sock = lpdServer->nextPendingConnection())
        new LpdSession(sock, this);
    });
  } else {
    delete lpdServer;
    lpdServer = nullptr;
  }

  ippServer = new QTcpServer(this);
  if (ippServer->listen(QHostAddress::LocalHost, wantIpp)) {
    boundIpp = ippServer->serverPort();
    connect(ippServer, &QTcpServer::newConnection, this, [this] {
      while (auto *sock = ippServer->nextPendingConnection())
        new IppSession(sock, this);
    });
  } else {
    delete ippServer;
    ippServer = nullptr;
  }

  rawServer = new QTcpServer(this);
  if (rawServer->listen(QHostAddress::LocalHost, wantRaw)) {
    boundRaw = rawServer->serverPort();
    connect(rawServer, &QTcpServer::newConnection, this, [this] {
      while (auto *sock = rawServer->nextPendingConnection())
        new RawSession(sock, this);
    });
  } else {
    delete rawServer;
    rawServer = nullptr;
  }

  return lpdServer != nullptr || ippServer != nullptr || rawServer != nullptr;
}

void Server::stop() {
  for (auto *srv : {lpdServer, ippServer, rawServer}) {
    if (srv) {
      srv->close();
      delete srv;
    }
  }
  lpdServer = nullptr;
  ippServer = nullptr;
  rawServer = nullptr;
  boundLpd = 0;
  boundIpp = 0;
  boundRaw = 0;
}

bool Server::dispatchSubmission(const QByteArray &rawData,
                                const QString &jobName, QString *errorOut) {
  const QByteArray cleanData = stripEot(rawData);
  if (cleanData.isEmpty() || cleanData.size() > kMaxJobBytes) {
    if (errorOut)
      *errorOut = QStringLiteral("The print job is empty or too large.");
    return false;
  }
  const QString clean = sanitizeJobName(jobName);
  if (testHandler)
    return testHandler(cleanData, clean, errorOut);
  return submitJob(cleanData, clean, targetPrinter, pdfOutputDir, errorOut);
}

Server::IppJob *Server::findIppJob(int id) {
  for (auto &job : ippJobs)
    if (job.id == id)
      return &job;
  return nullptr;
}

Server::IppJob &Server::createIppJob(const QString &jobName,
                                     const QString &queue, int state) {
  ++nextIppJobId;
  lastIppJobId = nextIppJobId;
  if (ippJobs.size() >= 50)
    ippJobs.removeFirst();
  ippJobs.append({nextIppJobId, jobName, queue, state, {}});
  return ippJobs.last();
}

QByteArray Server::ippJobAttrs(const IppJob &job) const {
  const quint16 port = boundIpp ? boundIpp : 631;
  const QByteArray juri =
      QStringLiteral("ipp://10.0.2.2:%1/jobs/%2").arg(port).arg(job.id).toUtf8();
  QByteArray out;
  out.append('\x02'); // job-attributes-tag
  out.append(ippIntAttr(0x21, "job-id", job.id));
  out.append(ippAttr(0x45, "job-uri", juri));
  out.append(ippIntAttr(0x23, "job-state", job.state));
  out.append(ippAttr(0x42, "job-name", job.jobName.toUtf8()));
  return out;
}

QByteArray Server::dispatchIpp(const QByteArray &body, const QString &queue) {
  const IppParsed req = parseIppRequest(body);
  if (!req.valid)
    return buildIppResponse(0x0400, 1);

  const QString jobName = req.jobName;
  const quint16 port = boundIpp ? boundIpp : 631;
  const QString qLabel =
      (!queue.isEmpty() && queue != QStringLiteral("classicmac") &&
       queue != QStringLiteral("retroboot"))
          ? queue
          : kPrinterLabel;
  const QString qPath = queue.isEmpty() ? QStringLiteral("classicmac") : queue;
  const QByteArray printerUri =
      QStringLiteral("ipp://10.0.2.2:%1/printers/%2")
          .arg(port)
          .arg(qPath)
          .toUtf8();

  if (req.opId == 0x000B) { // Get-Printer-Attributes
    const int ops[] = {2, 4, 5, 6, 8, 9, 10, 11};
    QByteArray opsBytes = ippIntAttr(0x23, "operations-supported", ops[0]);
    for (int i = 1; i < int(sizeof(ops) / sizeof(ops[0])); ++i)
      opsBytes.append(ippIntAttr(0x23, "", ops[i]));
    QByteArray extra;
    extra.append('\x04'); // printer-attributes-tag
    extra.append(ippAttr(0x45, "printer-uri-supported", printerUri));
    extra.append(ippAttr(0x44, "uri-security-supported", "none"));
    extra.append(ippAttr(0x44, "uri-authentication-supported", "none"));
    extra.append(ippAttr(0x42, "printer-name", qLabel.toUtf8()));
    extra.append(ippIntAttr(0x23, "printer-state", 3));
    extra.append(ippAttr(0x44, "printer-state-reasons", "none"));
    extra.append(ippAttr(0x22, "printer-is-accepting-jobs", QByteArray("\x01", 1)));
    extra.append(opsBytes);
    extra.append(ippAttr(0x44, "pdl-override-supported", "not-attempted"));
    extra.append(ippAttr(0x22, "color-supported", QByteArray("\x01", 1)));
    extra.append(ippAttr(0x49, "document-format-default", "application/postscript"));
    extra.append(ippAttr(0x49, "document-format-supported", "application/postscript"));
    extra.append(ippAttr(0x49, "", "application/pdf"));
    extra.append(ippAttr(0x47, "charset-configured", "utf-8"));
    extra.append(ippAttr(0x47, "charset-supported", "utf-8"));
    extra.append(ippAttr(0x48, "natural-language-configured", "en"));
    extra.append(ippAttr(0x48, "generated-natural-language-supported", "en"));
    return buildIppResponse(0x0000, req.reqId, extra);
  }

  if (req.opId == 0x0004) // Validate-Job
    return buildIppResponse(0x0000, req.reqId);

  if (req.opId == 0x0005) { // Create-Job
    const IppJob &job = createIppJob(jobName, queue, 4);
    return buildIppResponse(0x0000, req.reqId, ippJobAttrs(job));
  }

  if (req.opId == 0x0006) { // Send-Document
    const int jid = req.hasJobId ? req.jobId : lastIppJobId;
    IppJob *job = (jid > 0) ? findIppJob(jid) : nullptr;
    if (!job)
      return buildIppResponse(0x0406, req.reqId);
    if (job->state == 7)
      return buildIppResponse(0x0407, req.reqId);
    if (!req.payload.isEmpty())
      job->chunks.append(req.payload);
    if (!jobName.isEmpty() && jobName != QStringLiteral("Mac OS Print") &&
        job->jobName == QStringLiteral("Mac OS Print")) {
      job->jobName = jobName;
    }
    if (!req.lastDocument) {
      job->state = 3;
      return buildIppResponse(0x0000, req.reqId, ippJobAttrs(*job));
    }
    QByteArray fullData;
    for (const auto &c : job->chunks)
      fullData.append(c);
    job->chunks.clear();
    QString err;
    if (!dispatchSubmission(fullData, job->jobName, &err)) {
      job->state = 8; // aborted
      return buildIppResponse(0x0500, req.reqId);
    }
    job->state = 9; // completed
    return buildIppResponse(0x0000, req.reqId, ippJobAttrs(*job));
  }

  if (req.opId == 0x0008) { // Cancel-Job
    IppJob *job = req.hasJobId ? findIppJob(req.jobId) : nullptr;
    if (!job)
      return buildIppResponse(0x0406, req.reqId);
    job->state = 7;
    job->chunks.clear();
    return buildIppResponse(0x0000, req.reqId);
  }

  if (req.opId == 0x0009) { // Get-Job-Attributes
    IppJob *job = req.hasJobId ? findIppJob(req.jobId) : nullptr;
    if (!job)
      return buildIppResponse(0x0406, req.reqId);
    return buildIppResponse(0x0000, req.reqId, ippJobAttrs(*job));
  }

  if (req.opId == 0x000A) { // Get-Jobs
    QByteArray extra;
    const int start = std::max(0, int(ippJobs.size()) - 10);
    for (int i = start; i < ippJobs.size(); ++i)
      extra.append(ippJobAttrs(ippJobs[i]));
    return buildIppResponse(0x0000, req.reqId, extra);
  }

  if (req.opId == 0x0002) { // Print-Job
    IppJob &job = createIppJob(jobName, queue, 5);
    QString err;
    if (!dispatchSubmission(req.payload, jobName, &err)) {
      job.state = 8;
      return buildIppResponse(0x0500, req.reqId);
    }
    job.state = 9;
    return buildIppResponse(0x0000, req.reqId, ippJobAttrs(job));
  }

  return buildIppResponse(0x0501, req.reqId);
}

} // namespace Printer
