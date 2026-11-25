#include "vnc.h"
#include "display.h"
#include "platinum.h"
#include <QtEndian>
#include <QtWidgets>
#include <algorithm>
#include <cmath>
#include <cstring>
#include <openssl/des.h>
#ifdef Q_OS_WIN
#include <windows.h>
#endif

namespace {
quint16 u16(const char *p) { return (quint8(p[0]) << 8) | quint8(p[1]); }
quint32 u32(const char *p) {
  return (quint32(u16(p)) << 16) | u16(p + 2);
}
void put16(QByteArray &b, quint16 n) {
  b += char(n >> 8);
  b += char(n);
}
void put32(QByteArray &b, quint32 n) {
  put16(b, n >> 16);
  put16(b, n);
}

quint64 physicalKeyId(const QKeyEvent *e) {
  if (e->nativeScanCode() != 0)
    return (quint64(1) << 48) | e->nativeScanCode();
  if (e->nativeVirtualKey() != 0)
    return (quint64(2) << 48) | e->nativeVirtualKey();
  return (quint64(3) << 48) | quint32(e->key());
}

quint32 swapAltMetaCode(quint32 rfb, bool swap) {
  if (!swap)
    return rfb;
  if (rfb == 0x38) return 0xdb;
  if (rfb == 0xb8) return 0xdc;
  if (rfb == 0xdb) return 0x38;
  if (rfb == 0xdc) return 0xb8;
  return rfb;
}

quint32 fallbackRfbKeycode(const QKeyEvent *e) {
  const int k = e->key();
  const bool kp = bool(e->modifiers() & Qt::KeypadModifier);
  if (k >= Qt::Key_A && k <= Qt::Key_Z) {
    static const quint8 map[26] = {
        0x1e, 0x30, 0x2e, 0x20, 0x12, 0x21, 0x22, 0x23, 0x17,
        0x24, 0x25, 0x26, 0x32, 0x31, 0x18, 0x19, 0x10, 0x13,
        0x1f, 0x14, 0x16, 0x2f, 0x11, 0x2d, 0x15, 0x2c};
    return map[k - Qt::Key_A];
  }
  if (k >= Qt::Key_0 && k <= Qt::Key_9) {
    if (kp) {
      static const quint8 kpmap[10] = {0x52, 0x4f, 0x50, 0x51, 0x4b,
                                       0x4c, 0x4d, 0x47, 0x48, 0x49};
      return kpmap[k - Qt::Key_0];
    }
    return k == Qt::Key_0 ? 0x0b : quint32(0x02 + (k - Qt::Key_1));
  }
  if (k >= Qt::Key_F1 && k <= Qt::Key_F12) {
    if (k <= Qt::Key_F10)
      return quint32(0x3b + (k - Qt::Key_F1));
    return k == Qt::Key_F11 ? 0x57 : 0x58;
  }
  switch (k) {
  case Qt::Key_Escape: return 0x01;
  case Qt::Key_Minus:
  case Qt::Key_Underscore: return kp ? 0x4a : 0x0c;
  case Qt::Key_Equal:
  case Qt::Key_Plus: return kp ? 0x4e : 0x0d;
  case Qt::Key_Backspace: return 0x0e;
  case Qt::Key_Tab:
  case Qt::Key_Backtab: return 0x0f;
  case Qt::Key_BracketLeft:
  case Qt::Key_BraceLeft: return 0x1a;
  case Qt::Key_BracketRight:
  case Qt::Key_BraceRight: return 0x1b;
  case Qt::Key_Return: return 0x1c;
  case Qt::Key_Enter: return kp ? 0x9c : 0x1c;
  case Qt::Key_Control: return 0x1d;
  case Qt::Key_Semicolon:
  case Qt::Key_Colon: return 0x27;
  case Qt::Key_Apostrophe:
  case Qt::Key_QuoteDbl: return 0x28;
  case Qt::Key_QuoteLeft:
  case Qt::Key_AsciiTilde: return 0x29;
  case Qt::Key_Shift: return 0x2a;
  case Qt::Key_Backslash:
  case Qt::Key_Bar: return 0x2b;
  case Qt::Key_Comma:
  case Qt::Key_Less: return 0x33;
  case Qt::Key_Period:
  case Qt::Key_Greater: return kp ? 0x53 : 0x34;
  case Qt::Key_Slash:
  case Qt::Key_Question: return kp ? 0xb5 : 0x35;
  case Qt::Key_Asterisk: return kp ? 0x37 : 0x09;
  case Qt::Key_Alt: return 0x38;
  case Qt::Key_AltGr: return 0xb8;
  case Qt::Key_Space: return 0x39;
  case Qt::Key_CapsLock: return 0x3a;
  case Qt::Key_NumLock: return 0x45;
  case Qt::Key_ScrollLock: return 0x46;
  case Qt::Key_Home: return 0xc7;
  case Qt::Key_Up: return 0xc8;
  case Qt::Key_PageUp: return 0xc9;
  case Qt::Key_Left: return 0xcb;
  case Qt::Key_Right: return 0xcd;
  case Qt::Key_End: return 0xcf;
  case Qt::Key_Down: return 0xd0;
  case Qt::Key_PageDown: return 0xd1;
  case Qt::Key_Insert: return 0xd2;
  case Qt::Key_Delete: return 0xd3;
  case Qt::Key_Meta:
  case Qt::Key_Super_L: return 0xdb;
  case Qt::Key_Super_R: return 0xdc;
  case Qt::Key_Menu: return 0xdd;
  case Qt::Key_Exclam: return 0x02;
  case Qt::Key_At: return 0x03;
  case Qt::Key_NumberSign: return 0x04;
  case Qt::Key_Dollar: return 0x05;
  case Qt::Key_Percent: return 0x06;
  case Qt::Key_AsciiCircum: return 0x07;
  case Qt::Key_Ampersand: return 0x08;
  case Qt::Key_ParenLeft: return 0x0a;
  case Qt::Key_ParenRight: return 0x0b;
  case Qt::Key_F13: return 0x5d;
  case Qt::Key_F14: return 0x5e;
  case Qt::Key_F15: return 0x5f;
  case Qt::Key_F16: return 0x55;
  case Qt::Key_F17: return 0x83;
  case Qt::Key_F18: return 0xf7;
  case Qt::Key_F19: return 0x84;
  case Qt::Key_F20: return 0x5a;
  default: return 0;
  }
}

quint32 rfbKeycodeFor(const QKeyEvent *e, bool swapAltCommand) {
  quint32 rfb = 0;
#if defined(Q_OS_WIN)
  const quint32 sc = e->nativeScanCode();
  if (sc != 0) {
    const quint32 low = sc & 0x7fu;
    const bool ext = (sc & 0xff00u) != 0;
    rfb = ext ? (0x80u | low) : low;
  }
#elif defined(Q_OS_MACOS)
  if (e->nativeVirtualKey() != 0 || e->nativeScanCode() != 0 ||
      e->key() == Qt::Key_A) {
    static const QHash<quint32, quint32> macToRfb = {
        {0x00, 0x1e}, {0x01, 0x1f}, {0x02, 0x20}, {0x03, 0x21}, {0x04, 0x23},
        {0x05, 0x22}, {0x06, 0x2c}, {0x07, 0x2d}, {0x08, 0x2e}, {0x09, 0x2f},
        {0x0a, 0x56}, {0x0b, 0x30}, {0x0c, 0x10}, {0x0d, 0x11}, {0x0e, 0x12},
        {0x0f, 0x13}, {0x10, 0x15}, {0x11, 0x14}, {0x12, 0x02}, {0x13, 0x03},
        {0x14, 0x04}, {0x15, 0x05}, {0x16, 0x07}, {0x17, 0x06}, {0x18, 0x0d},
        {0x19, 0x0a}, {0x1a, 0x08}, {0x1b, 0x0c}, {0x1c, 0x09}, {0x1d, 0x0b},
        {0x1e, 0x1b}, {0x1f, 0x18}, {0x20, 0x16}, {0x21, 0x1a}, {0x22, 0x17},
        {0x23, 0x19}, {0x24, 0x1c}, {0x25, 0x26}, {0x26, 0x24}, {0x27, 0x28},
        {0x28, 0x25}, {0x29, 0x27}, {0x2a, 0x2b}, {0x2b, 0x33}, {0x2c, 0x35},
        {0x2d, 0x31}, {0x2e, 0x32}, {0x2f, 0x34}, {0x30, 0x0f}, {0x31, 0x39},
        {0x32, 0x29}, {0x33, 0x0e}, {0x35, 0x01}, {0x36, 0xdc}, {0x37, 0xdb},
        {0x38, 0x2a}, {0x39, 0x3a}, {0x3a, 0x38}, {0x3b, 0x1d}, {0x3c, 0x36},
        {0x3d, 0xb8}, {0x3e, 0x9d}, {0x41, 0x53}, {0x43, 0x37}, {0x45, 0x4e},
        {0x47, 0x45}, {0x4b, 0xb5}, {0x4c, 0x9c}, {0x4e, 0x4a}, {0x51, 0x59},
        {0x52, 0x52}, {0x53, 0x4f}, {0x54, 0x50}, {0x55, 0x51}, {0x56, 0x4b},
        {0x57, 0x4c}, {0x58, 0x4d}, {0x59, 0x47}, {0x5b, 0x48}, {0x5c, 0x49},
        {0x60, 0x3f}, {0x61, 0x40}, {0x62, 0x41}, {0x63, 0x3d}, {0x64, 0x42},
        {0x65, 0x43}, {0x67, 0x57}, {0x6d, 0x44}, {0x6f, 0x58}, {0x73, 0xc7},
        {0x74, 0xc9}, {0x75, 0xd3}, {0x76, 0x3e}, {0x77, 0xcf}, {0x78, 0x3c},
        {0x79, 0xd1}, {0x7a, 0x3b}, {0x7b, 0xcb}, {0x7c, 0xcd}, {0x7d, 0xd0},
        {0x7e, 0xc8}};
    rfb = macToRfb.value(e->nativeVirtualKey(), 0);
  }
#else
  if (e->nativeScanCode() >= 8) {
    const quint32 evdev = e->nativeScanCode() - 8;
    static const QHash<quint32, quint32> evdevToRfb = {
        {1, 0x01},   {2, 0x02},   {3, 0x03},   {4, 0x04},   {5, 0x05},
        {6, 0x06},   {7, 0x07},   {8, 0x08},   {9, 0x09},   {10, 0x0a},
        {11, 0x0b},  {12, 0x0c},  {13, 0x0d},  {14, 0x0e},  {15, 0x0f},
        {16, 0x10},  {17, 0x11},  {18, 0x12},  {19, 0x13},  {20, 0x14},
        {21, 0x15},  {22, 0x16},  {23, 0x17},  {24, 0x18},  {25, 0x19},
        {26, 0x1a},  {27, 0x1b},  {28, 0x1c},  {29, 0x1d},  {30, 0x1e},
        {31, 0x1f},  {32, 0x20},  {33, 0x21},  {34, 0x22},  {35, 0x23},
        {36, 0x24},  {37, 0x25},  {38, 0x26},  {39, 0x27},  {40, 0x28},
        {41, 0x29},  {42, 0x2a},  {43, 0x2b},  {44, 0x2c},  {45, 0x2d},
        {46, 0x2e},  {47, 0x2f},  {48, 0x30},  {49, 0x31},  {50, 0x32},
        {51, 0x33},  {52, 0x34},  {53, 0x35},  {54, 0x36},  {55, 0x37},
        {56, 0x38},  {57, 0x39},  {58, 0x3a},  {59, 0x3b},  {60, 0x3c},
        {61, 0x3d},  {62, 0x3e},  {63, 0x3f},  {64, 0x40},  {65, 0x41},
        {66, 0x42},  {67, 0x43},  {68, 0x44},  {69, 0x45},  {70, 0x46},
        {71, 0x47},  {72, 0x48},  {73, 0x49},  {74, 0x4a},  {75, 0x4b},
        {76, 0x4c},  {77, 0x4d},  {78, 0x4e},  {79, 0x4f},  {80, 0x50},
        {81, 0x51},  {82, 0x52},  {83, 0x53},  {85, 0x76},  {86, 0x56},
        {87, 0x57},  {88, 0x58},  {89, 0x73},  {90, 0x78},  {91, 0x77},
        {92, 0x79},  {93, 0x70},  {94, 0x7b},  {96, 0x9c},  {97, 0x9d},
        {98, 0xb5},  {99, 0x54},  {100, 0xb8}, {102, 0xc7}, {103, 0xc8},
        {104, 0xc9}, {105, 0xcb}, {106, 0xcd}, {107, 0xcf}, {108, 0xd0},
        {109, 0xd1}, {110, 0xd2}, {111, 0xd3}, {113, 0xa0}, {114, 0xae},
        {115, 0xb0}, {116, 0xde}, {117, 0x59}, {119, 0xc6}, {121, 0x7e},
        {122, 0x72}, {123, 0x71}, {124, 0x7d}, {125, 0xdb}, {126, 0xdc},
        {127, 0xdd}, {183, 0x5d}, {184, 0x5e}, {185, 0x5f}, {186, 0x55},
        {187, 0x83}, {188, 0xf7}, {189, 0x84}, {190, 0x5a}};
    rfb = evdevToRfb.value(evdev, 0);
  }
#endif
  if (!rfb)
    rfb = fallbackRfbKeycode(e);
  return swapAltMetaCode(rfb, swapAltCommand);
}

#ifdef Q_OS_WIN
class WindowsRawInputFilter : public QAbstractNativeEventFilter {
public:
  HWND targetHwnd = nullptr;
  VNC *vnc = nullptr;
  int *mask = nullptr;
  bool *legacyMouse = nullptr;
  int *mouseDpi = nullptr;
  double *remX = nullptr;
  double *remY = nullptr;
  WindowsRawInputFilter(HWND hwnd, VNC *client, int *buttons, bool *legacy,
                        int *dpi, double *rx, double *ry)
      : targetHwnd(hwnd), vnc(client), mask(buttons), legacyMouse(legacy),
        mouseDpi(dpi), remX(rx), remY(ry) {}
  bool nativeEventFilter(const QByteArray &eventType, void *message,
                         qintptr *) override {
    if (eventType != "windows_generic_MSG" || !vnc || !mask)
      return false;
    auto *msg = static_cast<MSG *>(message);
    if (msg->message != WM_INPUT || (targetHwnd && msg->hwnd != targetHwnd))
      return false;
    UINT size = 0;
    auto hRaw = reinterpret_cast<HRAWINPUT>(msg->lParam);
    if (GetRawInputData(hRaw, RID_INPUT, nullptr, &size,
                        sizeof(RAWINPUTHEADER)) != 0 ||
        size == 0 || size > 1024)
      return false;
    alignas(RAWINPUT) BYTE buf[1024];
    if (GetRawInputData(hRaw, RID_INPUT, buf, &size,
                        sizeof(RAWINPUTHEADER)) != size)
      return false;
    const auto *raw = reinterpret_cast<const RAWINPUT *>(buf);
    if (raw->header.dwType != RIM_TYPEMOUSE)
      return false;
    const RAWMOUSE &m = raw->data.mouse;
    if (!(m.usFlags & MOUSE_MOVE_ABSOLUTE)) {
      const int dx = int(m.lLastX), dy = int(m.lLastY);
      if (dx || dy) {
        int outX = 0, outY = 0;
        const bool legacy = legacyMouse ? *legacyMouse : true;
        const int dpi = mouseDpi ? *mouseDpi : 0;
        double dummyX = 0.0, dummyY = 0.0;
        if (GuestView::scaleNativeRelative(remX ? *remX : dummyX,
                                           remY ? *remY : dummyY, double(dx),
                                           double(dy), dpi, 1.0, legacy, outX,
                                           outY)) {
          vnc->pointer(0x7fff + std::clamp(outX, -16384, 16384),
                       0x7fff + std::clamp(outY, -16384, 16384), *mask);
        }
      }
    }
    return false;
  }
};
#endif
} // namespace

VNC::VNC(QObject *p) : QObject(p) {
  connect(&socket, &QTcpSocket::readyRead, this, [&] {
    if (buffer.isEmpty())
      buffer = socket.readAll();
    else
      buffer += socket.readAll();
    if (buffer.size() > 96 * 1048576) {
      failure("The guest display exceeded its buffer limit.");
      return;
    }
    parse();
  });
  connect(&socket, &QTcpSocket::errorOccurred, this, [&] {
    // QEMU closes RFB before its process-exit notification on normal shutdown.
    // The engine owns reporting an unexpected process exit. A transient modal
    // must never be opened against the guest window while it is being retired.
    if (socket.error() == QAbstractSocket::RemoteHostClosedError) {
      close();
      return;
    }
    if (state)
      failure("The guest display disconnected: " + socket.errorString());
  });
  refresh.setSingleShot(true);
  refresh.setInterval(8);
  connect(&refresh, &QTimer::timeout, this, [&] { updateRequest(true); });
}
QByteArray VNC::authenticate(QByteArray challenge, QByteArray password) {
  DES_cblock key = {};
  for (int i = 0; i < std::min(8, int(password.size())); ++i) {
    unsigned char b = password[i], r = 0;
    for (int j = 0; j < 8; ++j) {
      r = (r << 1) | (b & 1);
      b >>= 1;
    }
    key[i] = r;
  }
  DES_key_schedule schedule;
  DES_set_key_unchecked(&key, &schedule);
  QByteArray out(16, 0);
  for (int i = 0; i < 2; ++i)
    DES_ecb_encrypt(
        reinterpret_cast<const_DES_cblock *>(challenge.data() + i * 8),
        reinterpret_cast<DES_cblock *>(out.data() + i * 8), &schedule,
        DES_ENCRYPT);
  return out;
}
void VNC::open(int port, const QByteArray &pass) {
  close();
  password = pass;
  state = 1;
  socket.connectToHost(QHostAddress::LocalHost, quint16(port));
}
void VNC::close() {
  state = 0;
  ready = false;
  refresh.stop();
  socket.abort();
  buffer.clear();
  rects = -1;
  hadFrameRect = false;
  frame = {};
  cursorImage = {};
  cursorHotSpot = {};
  hasCursor = false;
  extendedKeySupported = false;
  relative = false;
  desktopResizeSupported = false;
  desktopResizeResult = -1;
  lastRequestedSize = {};
}
void VNC::failure(QString s) {
  close();
  emit problem(s);
}
void VNC::parse() {
  int pos = 0;
  bool needUpdate = false;
  bool needSlowUpdate = false;
  auto avail = [&] { return buffer.size() - pos; };
  auto data = [&] { return buffer.constData() + pos; };
  auto finish = qScopeGuard([&] {
    if (state == 0)
      return;
    if (pos > 0)
      buffer.remove(0, pos);
    if (needUpdate) {
      refresh.stop();
      updateRequest(true);
    } else if (needSlowUpdate && !refresh.isActive()) {
      refresh.start(8);
    }
  });
  for (;;) {
    if (state == 1) {
      if (avail() < 12)
        return;
      if (QByteArrayView(data(), 8) != "RFB 003.") {
        failure("Unsupported guest display protocol.");
        return;
      }
      pos += 12;
      socket.write("RFB 003.008\n");
      state = 2;
    } else if (state == 2) {
      if (avail() < 1)
        return;
      int n = quint8(data()[0]);
      if (!n) {
        failure("The guest display refused the connection.");
        return;
      }
      if (avail() < n + 1)
        return;
      bool auth = false;
      for (int i = 1; i <= n; ++i)
        if (data()[i] == char(2)) {
          auth = true;
          break;
        }
      if (!auth) {
        failure("The guest display did not offer password authentication.");
        return;
      }
      pos += n + 1;
      socket.write(QByteArray(1, char(2)));
      state = 3;
    } else if (state == 3) {
      if (avail() < 16)
        return;
      socket.write(authenticate(QByteArray(data(), 16), password));
      pos += 16;
      state = 4;
    } else if (state == 4) {
      if (avail() < 4)
        return;
      if (u32(data()) != 0) {
        failure("The guest display password was rejected.");
        return;
      }
      pos += 4;
      socket.write(QByteArray(1, char(1)));
      state = 5;
    } else if (state == 5) {
      if (avail() < 24)
        return;
      auto n = u32(data() + 20);
      if (n > 4096) {
        failure("Invalid guest display name.");
        return;
      }
      if (avail() < 24 + int(n))
        return;
      int w = u16(data()), h = u16(data() + 2);
      if (w < 1 || h < 1 || w > 4096 || h > 2160) {
        failure("Unsupported guest screen size.");
        return;
      }
      frame = QImage(w, h, QImage::Format_RGB32);
      frame.fill(Qt::black);
      pos += 24 + int(n);
      QByteArray format(4, 0);
      format += char(32);
      format += char(24);
      format += char(0);
      format += char(1);
      for (int i = 0; i < 3; ++i)
        put16(format, 255);
      format += char(16);
      format += char(8);
      format += QByteArray(4, 0);
      socket.write(format);
      QByteArray enc;
      enc += char(2);
      enc += char(0);
      put16(enc, 8);
      for (int i : {0, 1, -314, -239, -223, -257, -258, -308})
        put32(enc, quint32(i));
      socket.write(enc);
      state = 6;
      ready = true;
      updateRequest(false);
      emit changed();
    } else if (state == 6) {
      if (rects < 0) {
        if (avail() < 1)
          return;
        int type = quint8(data()[0]);
        if (type == 0) {
          if (avail() < 4)
            return;
          rects = u16(data() + 2);
          hadFrameRect = false;
          pos += 4;
        } else if (type == 2) {
          pos += 1;
          continue;
        } else if (type == 3) {
          if (avail() < 8)
            return;
          auto n = u32(data() + 4);
          if (n > 1048576) {
            failure("Guest clipboard is too large.");
            return;
          }
          if (avail() < 8 + int(n))
            return;
          pos += 8 + int(n);
          continue;
        } else {
          failure("Unsupported guest display message.");
          return;
        }
      }
      if (rects == 0) {
        rects = -1;
        emit changed();
        if (hadFrameRect) {
          needUpdate = true;
          needSlowUpdate = false;
        } else if (!needUpdate) {
          needSlowUpdate = true;
        }
        continue;
      }
      if (avail() < 12)
        return;
      const char *b = data();
      int x = u16(b), y = u16(b + 2), w = u16(b + 4), h = u16(b + 6);
      qint32 encoding = qint32(u32(b + 8));
      if (encoding == -308) {
        // ExtendedDesktopSize has a variable screen layout. QEMU returns
        // status 4 when the guest driver will apply the request asynchronously.
        if (avail() < 16)
          return;
        int screens = quint8(b[12]);
        if (screens < 1 || screens > 16) {
          failure("Invalid guest display layout.");
          return;
        }
        int bytes = 16 + screens * 16;
        if (avail() < bytes)
          return;
        if (w < 1 || h < 1 || w > 4096 || h > 2160) {
          failure("Unsupported guest screen size.");
          return;
        }
        desktopResizeResult = x == 1 ? y : 0;
        desktopResizeSupported =
            desktopResizeResult == 0 || desktopResizeResult == 4;
        if (desktopResizeResult == 0 && frame.size() != QSize(w, h)) {
          frame = QImage(w, h, QImage::Format_RGB32);
          frame.fill(Qt::black);
        }
        hadFrameRect = true;
        pos += bytes;
      } else if (encoding == -223) {
        if (w < 1 || h < 1 || w > 4096 || h > 2160) {
          failure("Unsupported guest screen size.");
          return;
        }
        frame = QImage(w, h, QImage::Format_RGB32);
        frame.fill(Qt::black);
        hadFrameRect = true;
        pos += 12;
      } else if (encoding == -257) {
        relative = x == 0;
        hadFrameRect = true;
        pos += 12;
      } else if (encoding == -258) {
        extendedKeySupported = true;
        hadFrameRect = true;
        pos += 12;
      } else if (encoding == -314) {
        if (w > 512 || h > 512) {
          failure("Unsupported guest cursor size.");
          return;
        }
        if (avail() < 16)
          return;
        qint32 subEnc = qint32(u32(b + 12));
        if (subEnc != 0) {
          failure("Unsupported guest cursor encoding.");
          return;
        }
        const qint64 bytes = 16 + qint64(w) * h * 4;
        if (avail() < bytes)
          return;
        if (w == 0 || h == 0) {
          cursorImage = {};
          cursorHotSpot = {};
        } else {
          QImage img(w, h, QImage::Format_ARGB32);
          const uchar *src = reinterpret_cast<const uchar *>(b + 16);
          for (int j = 0; j < h; ++j) {
            auto *dst = reinterpret_cast<quint32 *>(img.scanLine(j));
            const uchar *srow = src + qint64(j) * w * 4;
            for (int i = 0; i < w; ++i)
              dst[i] = qFromLittleEndian<quint32>(srow + i * 4);
          }
          cursorImage = img;
          cursorHotSpot = QPoint(std::clamp(x, 0, w - 1),
                                 std::clamp(y, 0, h - 1));
        }
        hasCursor = true;
        hadFrameRect = true;
        pos += int(bytes);
        emit cursorChanged();
      } else if (encoding == -239) {
        if (w > 512 || h > 512) {
          failure("Unsupported guest cursor size.");
          return;
        }
        const qint64 pixelBytes = qint64(w) * h * 4;
        const qint64 maskStride = (w + 7) / 8;
        const qint64 bytes = 12 + pixelBytes + maskStride * h;
        if (avail() < bytes)
          return;
        if (w == 0 || h == 0) {
          cursorImage = {};
          cursorHotSpot = {};
        } else {
          QImage img(w, h, QImage::Format_ARGB32);
          const uchar *px = reinterpret_cast<const uchar *>(b + 12);
          const uchar *maskPtr = px + pixelBytes;
          for (int j = 0; j < h; ++j) {
            auto *dst = reinterpret_cast<quint32 *>(img.scanLine(j));
            const uchar *srow = px + qint64(j) * w * 4;
            const uchar *mrow = maskPtr + j * maskStride;
            for (int i = 0; i < w; ++i) {
              const bool opaque = (mrow[i >> 3] & (0x80 >> (i & 7))) != 0;
              const quint32 rgb =
                  qFromLittleEndian<quint32>(srow + i * 4) & 0x00ffffffu;
              dst[i] = opaque ? (0xff000000u | rgb) : 0x00000000u;
            }
          }
          cursorImage = img;
          cursorHotSpot = QPoint(std::clamp(x, 0, w - 1),
                                 std::clamp(y, 0, h - 1));
        }
        hasCursor = true;
        hadFrameRect = true;
        pos += int(bytes);
        emit cursorChanged();
      } else if ((encoding == 0 || encoding == 1) && x + w <= frame.width() &&
                 y + h <= frame.height()) {
        const qint64 bytes = encoding == 0 ? qint64(w) * h * 4 : 4;
        if (avail() < 12 + bytes)
          return;
        if (encoding == 0) {
          const uchar *source = reinterpret_cast<const uchar *>(b + 12);
          for (int j = 0; j < h; ++j) {
            auto *dst = reinterpret_cast<quint32 *>(frame.scanLine(y + j)) + x;
            const uchar *srow = source + qint64(j) * w * 4;
            for (int i = 0; i < w; ++i)
              dst[i] = qFromLittleEndian<quint32>(srow + i * 4) | 0xff000000u;
          }
        } else {
          int sx = u16(b + 12), sy = u16(b + 14);
          if (sx + w > frame.width() || sy + h > frame.height()) {
            failure("Invalid guest display copy.");
            return;
          }
          if (w > 0 && h > 0) {
            if (y <= sy) {
              for (int j = 0; j < h; ++j)
                std::memmove(frame.scanLine(y + j) + x * 4,
                             frame.constScanLine(sy + j) + sx * 4,
                             size_t(w) * 4);
            } else {
              for (int j = h - 1; j >= 0; --j)
                std::memmove(frame.scanLine(y + j) + x * 4,
                             frame.constScanLine(sy + j) + sx * 4,
                             size_t(w) * 4);
            }
          }
        }
        hadFrameRect = true;
        pos += 12 + int(bytes);
      } else {
        failure("Invalid guest display rectangle.");
        return;
      }
      --rects;
    } else
      return;
  }
}
void VNC::updateRequest(bool incremental) {
  if (!ready)
    return;
  QByteArray b;
  b += char(3);
  b += char(incremental);
  put16(b, 0);
  put16(b, 0);
  put16(b, frame.width());
  put16(b, frame.height());
  socket.write(b);
}
void VNC::key(quint32 sym, bool down, quint32 rfbKeycode) {
  if (!ready)
    return;
  QByteArray b;
  if (extendedKeySupported && rfbKeycode != 0) {
    b += char(255);
    b += char(0);
    put16(b, down ? 1 : 0);
    put32(b, sym);
    put32(b, rfbKeycode);
  } else {
    b += char(4);
    b += char(down);
    put16(b, 0);
    put32(b, sym);
  }
  socket.write(b);
}
void VNC::pointer(int x, int y, int mask) {
  if (!ready)
    return;
  QByteArray b;
  b += char(5);
  b += char(mask);
  put16(b, quint16(std::clamp(x, 0, 65535)));
  put16(b, quint16(std::clamp(y, 0, 65535)));
  socket.write(b);
}
bool VNC::resizeDesktop(QSize size) {
  if (!ready || !desktopResizeSupported || size.width() < 512 ||
      size.height() < 384 || size.width() > 3840 || size.height() > 2160)
    return false;
  QByteArray b;
  b += char(251);
  b += char(0);
  put16(b, size.width());
  put16(b, size.height());
  b += char(1); // one display, matching QEMU's console
  b += char(0);
  put32(b, 0);
  put16(b, 0);
  put16(b, 0);
  put16(b, size.width());
  put16(b, size.height());
  put32(b, 0);
  socket.write(b);
  lastRequestedSize = size;
  return true;
}

GuestView::GuestView(VNC *client, QWidget *p) : QWidget(p), vnc(client) {
  horizontalScroll = new QScrollBar(Qt::Horizontal, this);
  verticalScroll = new QScrollBar(Qt::Vertical, this);
  horizontalScroll->setAccessibleName("Mac screen horizontal scroll");
  verticalScroll->setAccessibleName("Mac screen vertical scroll");
  horizontalScroll->hide();
  verticalScroll->hide();
  connect(horizontalScroll, &QScrollBar::valueChanged, this,
          qOverload<>(&QWidget::update));
  connect(verticalScroll, &QScrollBar::valueChanged, this,
          qOverload<>(&QWidget::update));
  setFocusPolicy(Qt::StrongFocus);
  setMouseTracking(true);
  setAccessibleName("Mac screen");
  resizeDebounce.setSingleShot(true);
  resizeDebounce.setInterval(250);
  resizeDeadline.setSingleShot(true);
  resizeDeadline.setInterval(5000);
  resizeSettled.setSingleShot(true);
  resizeSettled.setInterval(5000);
  fullScreenHintTimer.setSingleShot(true);
  fullScreenHintTimer.setInterval(3500);
  connect(&fullScreenHintTimer, &QTimer::timeout, this,
          &GuestView::hideFullScreenHint);
  connect(&resizeDebounce, &QTimer::timeout, this, &GuestView::requestResize);
  connect(&resizeSettled, &QTimer::timeout, this, [this] {
    if (vnc->frame.size() == desiredSize())
      wantsResize = false;
  });
  connect(&resizeDeadline, &QTimer::timeout, this, [this] {
    if (vnc->frame.size() != pendingSize) {
      if (wantsResize && resizeAttempts < maxResizeAttempts) {
        resizeNotice = "Waiting for the Mac to change its screen size…";
        resizeDebounce.start();
      } else
        resizeNotice = "This Mac has kept its current screen size.";
      emit displayChanged();
    }
  });
  connect(vnc, &VNC::cursorChanged, this, &GuestView::updateGuestCursor);
  connect(vnc, &VNC::changed, this, [this] {
    updateScrollbars();
    if (captured && !vnc->relative)
      release();
    if (vnc->ready && observedSize != vnc->frame.size()) {
      observedSize = vnc->frame.size();
      updateGuestCursor();
      if (observedSize == pendingSize) {
        resizeDeadline.stop();
        resizeNotice.clear();
        if (wantsResize)
          resizeSettled.start();
      } else {
        resizeSettled.stop();
        if (wantsResize && resizeAttempts < maxResizeAttempts)
          resizeDebounce.start();
      }
      emit displayChanged();
    } else if (underMouse() && !captured) {
      applyCursorAt(mapFromGlobal(QCursor::pos()));
    }
    update();
  });
}
GuestView::~GuestView() { release(); }

void GuestView::showFullScreenHint() {
  fullScreenHintVisible = true;
  fullScreenHintTimer.start();
  update();
}
void GuestView::hideFullScreenHint() {
  fullScreenHintTimer.stop();
  if (fullScreenHintVisible) {
    fullScreenHintVisible = false;
    update();
  }
}

QRectF GuestView::screenRect() const {
  if (vnc->frame.isNull())
    return rect();
  const auto dpi = devicePixelRatioF();
  return NativeDisplay::imageRect(displayViewport().size(), vnc->frame.size(),
                                  dpi, fitWindow, pixelZoom)
      .translated(-qRound(horizontalScroll->value() * dpi) / dpi,
                  -qRound(verticalScroll->value() * dpi) / dpi);
}
QRect GuestView::displayViewport() const {
  return rect().adjusted(0, 0, verticalScroll->isHidden() ? 0 : -16,
                         horizontalScroll->isHidden() ? 0 : -16);
}
void GuestView::updateScrollbars(bool force) {
  const auto frameSize = vnc->frame.size();
  const auto viewSize = size();
  const qreal dpr = devicePixelRatioF();
  if (!force && frameSize == lastScrollFrameSize &&
      viewSize == lastScrollViewSize && qFuzzyCompare(dpr, lastScrollDpr) &&
      fitWindow == lastScrollFit && pixelZoom == lastScrollZoom)
    return;
  lastScrollFrameSize = frameSize;
  lastScrollViewSize = viewSize;
  lastScrollDpr = dpr;
  lastScrollFit = fitWindow;
  lastScrollZoom = pixelZoom;

  const auto target =
      NativeDisplay::imageRect(viewSize, frameSize, dpr, fitWindow, pixelZoom);
  bool h = false, v = false;
  for (int i = 0; i < 3 && !fitWindow && !vnc->frame.isNull(); ++i) {
    h = target.width() > width() - (v ? 16 : 0) + 0.001;
    v = target.height() > height() - (h ? 16 : 0) + 0.001;
  }
  horizontalScroll->setVisible(h);
  verticalScroll->setVisible(v);
  const auto area = displayViewport();
  horizontalScroll->setGeometry(0, area.height(), area.width(), 16);
  verticalScroll->setGeometry(area.width(), 0, 16, area.height());
  horizontalScroll->setRange(
      0, h ? std::max(0, qCeil(target.width() - area.width())) : 0);
  verticalScroll->setRange(
      0, v ? std::max(0, qCeil(target.height() - area.height())) : 0);
  horizontalScroll->setPageStep(area.width());
  verticalScroll->setPageStep(area.height());
}
void GuestView::updateGuestCursor() {
  if (vnc->hasCursor && !vnc->cursorImage.isNull()) {
    const QRectF r = screenRect();
    const qreal scale =
        (!vnc->frame.isNull() && r.width() > 0)
            ? (r.width() / vnc->frame.width())
            : 1.0;
    const qreal dpr = devicePixelRatioF();
    const int pw = std::max(1, qRound(vnc->cursorImage.width() * scale * dpr));
    const int ph = std::max(1, qRound(vnc->cursorImage.height() * scale * dpr));
    QPixmap pm = QPixmap::fromImage(vnc->cursorImage.scaled(
        pw, ph, Qt::IgnoreAspectRatio, Qt::FastTransformation));
    pm.setDevicePixelRatio(dpr);
    const QSizeF logicalSize = pm.deviceIndependentSize();
    const int hx = std::clamp(
        qRound(vnc->cursorHotSpot.x() * scale), 0,
        std::max(0, qRound(logicalSize.width()) - 1));
    const int hy = std::clamp(
        qRound(vnc->cursorHotSpot.y() * scale), 0,
        std::max(0, qRound(logicalSize.height()) - 1));
    guestCursor = QCursor(pm, hx, hy);
  } else {
    guestCursor = QCursor();
  }
  if (underMouse() || captured)
    applyCursorAt(mapFromGlobal(QCursor::pos()));
}
void GuestView::applyCursorAt(QPointF pos) {
  if (captured) {
    setCursor(Qt::BlankCursor);
    return;
  }
  if (paused || !vnc->ready || vnc->frame.isNull()) {
    unsetCursor();
    return;
  }
  if (displayViewport().contains(pos.toPoint()) && screenRect().contains(pos)) {
    if (vnc->hasCursor && !vnc->cursorImage.isNull())
      setCursor(guestCursor);
    else
      setCursor(Qt::BlankCursor);
  } else {
    unsetCursor();
  }
}
void GuestView::checkDprChange() {
  const qreal dpr = devicePixelRatioF();
  if (!qFuzzyCompare(dpr, lastDpr)) {
    const bool changed = lastDpr > 0;
    lastDpr = dpr;
    if (changed) {
      updateScrollbars(true);
      updateGuestCursor();
      if (resizeArmed)
        scheduleResize();
      update();
    }
  }
}
void GuestView::connectScreenSignal() {
  disconnect(screenChangedConn);
  if (auto *win = window(); win && win->windowHandle()) {
    screenChangedConn =
        connect(win->windowHandle(), &QWindow::screenChanged, this,
                [this] { checkDprChange(); });
  }
}
QSize GuestView::desiredSize() const {
  return NativeDisplay::requestedSize(size(), fitWindow ? 1 : devicePixelRatioF(),
                                      widthAlignment, fitWindow ? 1 : pixelZoom,
                                      minimumResolution, maximumResolution);
}
void GuestView::scheduleResize() {
  updateScrollbars(true);
  updateGuestCursor();
  resizeSettled.stop();
  wantsResize = true;
  resizeAttempts = 0;
  resizeDebounce.start();
}
void GuestView::requestResize() {
  if (!wantsResize)
    return;
  if (desiredSize() == vnc->frame.size()) {
    resizeDeadline.stop();
    if (!resizeSettled.isActive())
      resizeSettled.start();
    if (!resizeNotice.isEmpty()) {
      resizeNotice.clear();
      emit displayChanged();
    }
    return;
  }
  if (!followsWindow || paused || !isVisible() || !vnc->ready ||
      window()->isMinimized() || desiredSize() == vnc->frame.size() ||
      resizeAttempts >= maxResizeAttempts)
    return;
  if (vnc->resizeDesktop(desiredSize())) {
    pendingSize = desiredSize();
    ++resizeAttempts;
    resizeNotice.clear();
    resizeDeadline.start();
  } else if (vnc->desktopResizeResult > 0) {
    resizeNotice = "This display uses a fixed screen size.";
    emit displayChanged();
  }
}
void GuestView::resizeEvent(QResizeEvent *e) {
  QWidget::resizeEvent(e);
  checkDprChange();
  updateScrollbars();
  updateGuestCursor();
#ifdef Q_OS_WIN
  if (captured && rawInputActive && window()) {
    if (auto hwnd = reinterpret_cast<HWND>(window()->winId())) {
      const qreal dpr = devicePixelRatioF();
      const QPoint tl = mapTo(window(), QPoint(0, 0));
      const QPoint br = mapTo(window(), QPoint(width(), height()));
      POINT p1{qRound(tl.x() * dpr), qRound(tl.y() * dpr)};
      POINT p2{qRound(br.x() * dpr), qRound(br.y() * dpr)};
      ClientToScreen(hwnd, &p1);
      ClientToScreen(hwnd, &p2);
      RECT clip{p1.x, p1.y, std::max(p1.x + 1, p2.x), std::max(p1.y + 1, p2.y)};
      ClipCursor(&clip);
    }
  }
#endif
  if (resizeArmed)
    scheduleResize();
}
void GuestView::showEvent(QShowEvent *e) {
  QWidget::showEvent(e);
  lastDpr = devicePixelRatioF();
  connectScreenSignal();
  if (!shownOnce) {
    shownOnce = true;
    // Initial layout/work-area fitting must not ask a classic video driver
    // to hot-plug its screen while Display Manager is still starting up.
    QTimer::singleShot(500, this, [this] { resizeArmed = true; });
  } else if (wantsResize)
    resizeDebounce.start();
}
bool GuestView::event(QEvent *e) {
  if (e->type() == QEvent::WinIdChange || e->type() == QEvent::ParentChange)
    connectScreenSignal();
#if QT_VERSION >= QT_VERSION_CHECK(6, 6, 0)
  if (e->type() == QEvent::DevicePixelRatioChange)
    checkDprChange();
#endif
  return QWidget::event(e);
}
void GuestView::paintEvent(QPaintEvent *) {
  checkDprChange();
  QPainter q(this);
  q.fillRect(rect(), vnc->ready ? Qt::black : QColor(0x63639c));
  q.setRenderHint(QPainter::SmoothPixmapTransform, false);
  if (!horizontalScroll->isHidden() && !verticalScroll->isHidden())
    q.fillRect(QRect(width() - 16, height() - 16, 16, 16),
               QColor(Platinum::face));
  const QRect viewport = displayViewport();
  q.setClipRect(viewport);
  if (!vnc->frame.isNull()) {
    // Rasterize in integral device coordinates. A logical QRectF whose height
    // is 768 / 1.75 can round just below 768 and drop a source row even with
    // nearest-neighbor sampling, especially when the image is letterboxed.
    const auto device = q.deviceTransform();
    const auto target = device.mapRect(screenRect());
    q.setWorldTransform(device.inverted());
    q.drawImage(QRectF(qRound(target.x()), qRound(target.y()),
                       qRound(target.width()), qRound(target.height())),
                vnc->frame);
    q.setWorldTransform(QTransform());
  } else {
    Platinum::Pixels p(width(), height(), 0xff63639c);
    Platinum::text(p, notice,
                   (width() - Platinum::textWidth(notice, "small")) / 2,
                   height() / 2, 0xffffffff, "small");
    q.drawImage(0, 0, p.image);
  }
  if (paused) {
    const QString text = "Paused";
    const int bw = Platinum::textWidth(text) + 24;
    const int bh = 22;
    Platinum::Pixels box(bw, bh, Platinum::face);
    box.frame(0, 0, bw, bh, 0xff000000);
    Platinum::text(box, text, 12, 15, 0xff000000);
    q.drawImage((viewport.width() - bw) / 2, (viewport.height() - bh) / 2,
                box.image);
  }
  if (fullScreenHintVisible) {
    const QString hint = "Press Alt+Enter or Ctrl+Alt+F to exit Full Screen";
    const int bw = Platinum::textWidth(hint, "small") + 20;
    const int bh = 20;
    if (bw <= viewport.width() - 16) {
      Platinum::Pixels box(bw, bh, Platinum::face);
      box.frame(0, 0, bw, bh, 0xff000000);
      Platinum::text(box, hint, 10, 13, 0xff000000, "small");
      q.drawImage((viewport.width() - bw) / 2, 12, box.image);
    }
  }
}
quint32 GuestView::keySym(QKeyEvent *e) const {
  const int k = e->key();
  const bool kp = bool(e->modifiers() & Qt::KeypadModifier);
  if (k >= Qt::Key_A && k <= Qt::Key_Z)
    return (e->modifiers() & Qt::ShiftModifier) ? 'A' + k - Qt::Key_A
                                                : 'a' + k - Qt::Key_A;
  if (k >= Qt::Key_0 && k <= Qt::Key_9)
    return kp ? quint32(0xffb0 + k - Qt::Key_0) : quint32(k);
  if (k >= Qt::Key_F1 && k <= Qt::Key_F12)
    return 0xffbe + k - Qt::Key_F1;
  if (k >= Qt::Key_F13 && k <= Qt::Key_F20)
    return 0xffca + k - Qt::Key_F13;
  if (kp) {
    switch (k) {
    case Qt::Key_Plus: return 0xffab;
    case Qt::Key_Minus: return 0xffad;
    case Qt::Key_Asterisk: return 0xffaa;
    case Qt::Key_Slash: return 0xffaf;
    case Qt::Key_Period: return 0xffae;
    case Qt::Key_Equal: return 0xffbd;
    case Qt::Key_Enter: return 0xff8d;
    default: break;
    }
  }
  if (k >= Qt::Key_Space && k <= Qt::Key_AsciiTilde)
    return quint32(k);
  switch (k) {
  case Qt::Key_Return:
  case Qt::Key_Enter:
    return 0xff0d;
  case Qt::Key_Backspace:
    return 0xff08;
  case Qt::Key_Tab:
  case Qt::Key_Backtab:
    return 0xff09;
  case Qt::Key_Escape:
    return 0xff1b;
  case Qt::Key_Delete:
    return 0xffff;
  case Qt::Key_Insert:
    return 0xff63;
  case Qt::Key_Left:
    return 0xff51;
  case Qt::Key_Up:
    return 0xff52;
  case Qt::Key_Right:
    return 0xff53;
  case Qt::Key_Down:
    return 0xff54;
  case Qt::Key_Home:
    return 0xff50;
  case Qt::Key_End:
    return 0xff57;
  case Qt::Key_PageUp:
    return 0xff55;
  case Qt::Key_PageDown:
    return 0xff56;
  case Qt::Key_Shift:
    return 0xffe1;
  case Qt::Key_Control:
    return 0xffe3;
  case Qt::Key_Alt:
    return swapAltCommand ? 0xffeb : 0xffe9;
  case Qt::Key_AltGr:
    return swapAltCommand ? 0xffec : 0xffea;
  case Qt::Key_Meta:
  case Qt::Key_Super_L:
    return swapAltCommand ? 0xffe9 : 0xffeb;
  case Qt::Key_Super_R:
    return swapAltCommand ? 0xffea : 0xffec;
  case Qt::Key_CapsLock:
    return 0xffe5;
  case Qt::Key_NumLock:
    return 0xff7f;
  case Qt::Key_ScrollLock:
    return 0xff14;
  case Qt::Key_Pause:
    return 0xff13;
  case Qt::Key_Print:
    return 0xff61;
  case Qt::Key_Menu:
    return 0xff67;
  default:
    return e->text().isEmpty() ? 0 : e->text()[0].unicode();
  }
}
void GuestView::keyPressEvent(QKeyEvent *e) {
  if (e->isAutoRepeat()) {
    e->accept();
    return;
  }
  if (captured && e->key() == Qt::Key_Escape) {
    release();
    e->accept();
    return;
  }
  if (captured &&
      ((e->key() == Qt::Key_Alt && (e->modifiers() & Qt::ControlModifier)) ||
       (e->key() == Qt::Key_Control && (e->modifiers() & Qt::AltModifier)))) {
    release();
    e->accept();
    return;
  }
  if ((e->key() == Qt::Key_F &&
       (e->modifiers() & (Qt::ControlModifier | Qt::AltModifier)) ==
           (Qt::ControlModifier | Qt::AltModifier)) ||
      ((e->key() == Qt::Key_Return || e->key() == Qt::Key_Enter) &&
       (e->modifiers() & Qt::AltModifier) &&
       !(e->modifiers() & Qt::ControlModifier))) {
    release();
    emit toggleFullScreen();
    e->accept();
    return;
  }
  if (paused) {
    e->accept();
    return;
  }
  const auto sym = keySym(e);
  const auto code = rfbKeycodeFor(e, swapAltCommand);
  if (sym || code) {
    const quint64 phys = physicalKeyId(e);
    if (activeKeys.contains(phys)) {
      const auto prev = activeKeys.take(phys);
      vnc->key(prev.first, false, prev.second);
    }
    activeKeys.insert(phys, {sym, code});
    vnc->key(sym, true, code);
  }
  e->accept();
}
void GuestView::keyReleaseEvent(QKeyEvent *e) {
  if (e->isAutoRepeat()) {
    e->accept();
    return;
  }
  const quint64 phys = physicalKeyId(e);
  if (activeKeys.contains(phys)) {
    const auto prev = activeKeys.take(phys);
    vnc->key(prev.first, false, prev.second);
  } else {
    const auto sym = keySym(e);
    const auto code = rfbKeycodeFor(e, swapAltCommand);
    if (sym || code)
      vnc->key(sym, false, code);
  }
  e->accept();
}
void GuestView::release() {
  for (auto it = activeKeys.constBegin(); it != activeKeys.constEnd(); ++it)
    vnc->key(it.value().first, false, it.value().second);
  activeKeys.clear();
  if (controlClick)
    vnc->key(0xffe3, false, 0x1d);
  controlClick = false;
  if (mask)
    vnc->pointer(vnc->relative ? 0x7fff : lastPointer.x(),
                 vnc->relative ? 0x7fff : lastPointer.y(), 0);
  mask = 0;
  wheelRemainder = 0;
  relRemX = 0.0;
  relRemY = 0.0;
  if (captured) {
    captured = false;
#ifdef Q_OS_WIN
    if (rawInputActive) {
      RAWINPUTDEVICE rid{};
      rid.usUsagePage = 0x01;
      rid.usUsage = 0x02;
      rid.dwFlags = RIDEV_REMOVE;
      rid.hwndTarget = nullptr;
      RegisterRawInputDevices(&rid, 1, sizeof(rid));
      rawInputActive = false;
    }
    ClipCursor(nullptr);
    if (rawFilter) {
      qApp->removeNativeEventFilter(rawFilter);
      delete rawFilter;
      rawFilter = nullptr;
    }
#endif
    releaseMouse();
    unsetCursor();
    emit captureChanged(false);
  }
  if (underMouse())
    applyCursorAt(mapFromGlobal(QCursor::pos()));
  update();
}
void GuestView::focusOutEvent(QFocusEvent *e) {
  release();
  QWidget::focusOutEvent(e);
}
bool GuestView::scaleNativeRelative(double &remX, double &remY, double dx,
                                    double dy, int dpi, double scale,
                                    bool legacyNative, int &outX, int &outY) {
  outX = 0;
  outY = 0;
  if (!std::isfinite(dx) || !std::isfinite(dy) || !std::isfinite(scale) ||
      scale <= 0.0) {
    remX = 0.0;
    remY = 0.0;
    return false;
  }
  auto axis = [](double &remainder, double motion) -> int {
    const double total = remainder + motion;
    if (total >= 32767.0 || total <= -32767.0) {
      remainder = 0.0;
      return total > 0.0 ? 32767 : -32767;
    }
    const int pixels = int(total);
    remainder = total - double(pixels);
    return pixels;
  };
  if (legacyNative) {
    outX = axis(remX, dx);
    outY = axis(remY, dy);
    return outX != 0 || outY != 0;
  }
  const double ratio = (dpi > 0 ? double(dpi) : 1000.0) / 200.0;
  outX = axis(remX, dx * scale / ratio);
  outY = axis(remY, dy * scale / ratio);
  return outX != 0 || outY != 0;
}

void GuestView::mouse(QMouseEvent *e) {
  auto r = screenRect();
  if (vnc->relative) {
    if (!captured)
      return;
    const bool wayland =
        QGuiApplication::platformName().contains("wayland", Qt::CaseInsensitive);
    if (rawInputActive || wayland) {
      if (e->type() == QEvent::MouseMove) {
        if (rawInputActive)
          return;
        const QPoint cur = e->position().toPoint();
        const QPoint delta = cur - lastRelativePos;
        lastRelativePos = cur;
        if (delta.isNull())
          return;
        int outX = 0, outY = 0;
        if (scaleNativeRelative(relRemX, relRemY, delta.x(), delta.y(),
                                mouseDpi, devicePixelRatioF(), legacyMouse,
                                outX, outY))
          vnc->pointer(0x7fff + outX, 0x7fff + outY, mask);
      } else {
        vnc->pointer(0x7fff, 0x7fff, mask);
      }
      return;
    }
    QPoint center = rect().center(), delta = e->position().toPoint() - center;
    if (e->type() == QEvent::MouseMove) {
      if (delta.isNull())
        return;
      int outX = 0, outY = 0;
      if (scaleNativeRelative(relRemX, relRemY, delta.x(), delta.y(),
                              mouseDpi, devicePixelRatioF(), legacyMouse,
                              outX, outY))
        vnc->pointer(0x7fff + outX, 0x7fff + outY, mask);
      QCursor::setPos(mapToGlobal(center));
    } else {
      vnc->pointer(0x7fff, 0x7fff, mask);
    }
  } else if (!vnc->frame.isNull() &&
             (r.contains(e->position()) || mask ||
              e->type() == QEvent::MouseButtonRelease)) {
    lastPointer = {
        std::clamp(int((e->position().x() - r.x()) * vnc->frame.width() /
                       std::max(qreal(1), r.width())),
                   0, vnc->frame.width() - 1),
        std::clamp(int((e->position().y() - r.y()) * vnc->frame.height() /
                       std::max(qreal(1), r.height())),
                   0, vnc->frame.height() - 1)};
    vnc->pointer(lastPointer.x(), lastPointer.y(), mask);
  }
}
void GuestView::mousePressEvent(QMouseEvent *e) {
  setFocus();
  if (paused) {
    e->ignore();
    return;
  }
  int bit = e->button() == Qt::LeftButton     ? 1
            : e->button() == Qt::MiddleButton ? 2
            : e->button() == Qt::RightButton  ? 4
                                              : 0;
  if (!bit) {
    e->ignore();
    return;
  }
  if (vnc->relative && !captured) {
    if (bit != 1) {
      e->ignore();
      return;
    }
    captured = true;
    relRemX = 0.0;
    relRemY = 0.0;
    grabMouse();
    setCursor(Qt::BlankCursor);
    lastRelativePos = e->position().toPoint();
    const bool wayland =
        QGuiApplication::platformName().contains("wayland", Qt::CaseInsensitive);
#ifdef Q_OS_WIN
    if (window()) {
      if (auto hwnd = reinterpret_cast<HWND>(window()->winId())) {
        RAWINPUTDEVICE rid{};
        rid.usUsagePage = 0x01;
        rid.usUsage = 0x02;
        rid.dwFlags = 0;
        rid.hwndTarget = hwnd;
        if (RegisterRawInputDevices(&rid, 1, sizeof(rid))) {
          rawInputActive = true;
          rawFilter = new WindowsRawInputFilter(hwnd, vnc, &mask, &legacyMouse,
                                                &mouseDpi, &relRemX, &relRemY);
          qApp->installNativeEventFilter(rawFilter);
        }
        const qreal dpr = devicePixelRatioF();
        const QPoint tl = mapTo(window(), QPoint(0, 0));
        const QPoint br = mapTo(window(), QPoint(width(), height()));
        POINT p1{qRound(tl.x() * dpr), qRound(tl.y() * dpr)};
        POINT p2{qRound(br.x() * dpr), qRound(br.y() * dpr)};
        ClientToScreen(hwnd, &p1);
        ClientToScreen(hwnd, &p2);
        RECT clip{p1.x, p1.y, std::max(p1.x + 1, p2.x), std::max(p1.y + 1, p2.y)};
        ClipCursor(&clip);
      }
    }
    if (!rawInputActive)
      QCursor::setPos(mapToGlobal(rect().center()));
#else
    if (!wayland)
      QCursor::setPos(mapToGlobal(rect().center()));
#endif
    emit captureChanged(true);
    update();
    return;
  }
  if (bit == 4 && helpers && !vnc->relative) {
    vnc->key(0xffe3, true, 0x1d);
    controlClick = true;
    bit = 1;
  }
  mask |= bit;
  mouse(e);
}
void GuestView::mouseReleaseEvent(QMouseEvent *e) {
  int bit = e->button() == Qt::LeftButton     ? 1
            : e->button() == Qt::MiddleButton ? 2
            : e->button() == Qt::RightButton  ? 4
                                              : 0;
  if (!bit) {
    e->ignore();
    return;
  }
  const bool wasControlRight = (e->button() == Qt::RightButton && controlClick);
  if (wasControlRight)
    bit = 1;
  mask &= ~bit;
  mouse(e);
  if (wasControlRight) {
    vnc->key(0xffe3, false, 0x1d);
    controlClick = false;
  }
}
void GuestView::mouseMoveEvent(QMouseEvent *e) {
  if (!captured)
    applyCursorAt(e->position());
  if (!paused)
    mouse(e);
}
void GuestView::enterEvent(QEnterEvent *e) {
  QWidget::enterEvent(e);
  applyCursorAt(e->position());
}
void GuestView::leaveEvent(QEvent *e) {
  QWidget::leaveEvent(e);
  if (!captured)
    unsetCursor();
}
void GuestView::wheelEvent(QWheelEvent *e) {
  if (paused || e->angleDelta().y() == 0 || (vnc->relative && !captured)) {
    e->ignore();
    return;
  }
  wheelRemainder += e->angleDelta().y();
  int steps = wheelRemainder / 120;
  wheelRemainder %= 120;
  if (helpers && !vnc->relative) {
    quint32 k = steps > 0 ? 0xff52 : 0xff54;
    quint32 rfb = steps > 0 ? 0xc8 : 0xd0;
    for (int n = 0; n < std::abs(steps); ++n) {
      vnc->key(k, true, rfb);
      vnc->key(k, false, rfb);
    }
  } else {
    int bit = steps > 0 ? 8 : 16;
    for (int n = 0; n < std::abs(steps); ++n) {
      vnc->pointer(vnc->relative ? 0x7fff : lastPointer.x(),
                   vnc->relative ? 0x7fff : lastPointer.y(), mask | bit);
      vnc->pointer(vnc->relative ? 0x7fff : lastPointer.x(),
                   vnc->relative ? 0x7fff : lastPointer.y(), mask);
    }
  }
  e->accept();
}
