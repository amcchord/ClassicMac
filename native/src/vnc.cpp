#include "vnc.h"
#include "platinum.h"
#include "display.h"
#include <QtWidgets>
#include <openssl/des.h>
static quint16 u16(const char *p) { return (quint8(p[0]) << 8) | quint8(p[1]); }
static quint32 u32(const char *p) {
  return (quint32(u16(p)) << 16) | u16(p + 2);
}
static void put16(QByteArray &b, quint16 n) {
  b += char(n >> 8);
  b += char(n);
}
static void put32(QByteArray &b, quint32 n) {
  put16(b, n >> 16);
  put16(b, n);
}
VNC::VNC(QObject *p) : QObject(p) {
  connect(&socket, &QTcpSocket::readyRead, this, [&] {
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
  refresh.setInterval(25);
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
  frame = {};
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
  for (;;) {
    if (state == 1) {
      if (buffer.size() < 12)
        return;
      if (!buffer.startsWith("RFB 003.")) {
        failure("Unsupported guest display protocol.");
        return;
      }
      buffer.remove(0, 12);
      socket.write("RFB 003.008\n");
      state = 2;
    } else if (state == 2) {
      if (buffer.isEmpty())
        return;
      int n = quint8(buffer[0]);
      if (!n) {
        failure("The guest display refused the connection.");
        return;
      }
      if (buffer.size() < n + 1)
        return;
      bool auth = buffer.mid(1, n).contains(char(2));
      if (!auth) {
        failure("The guest display did not offer password authentication.");
        return;
      }
      buffer.remove(0, n + 1);
      socket.write(QByteArray(1, char(2)));
      state = 3;
    } else if (state == 3) {
      if (buffer.size() < 16)
        return;
      socket.write(authenticate(buffer.left(16), password));
      buffer.remove(0, 16);
      state = 4;
    } else if (state == 4) {
      if (buffer.size() < 4)
        return;
      if (u32(buffer.constData()) != 0) {
        failure("The guest display password was rejected.");
        return;
      }
      buffer.remove(0, 4);
      socket.write(QByteArray(1, char(1)));
      state = 5;
    } else if (state == 5) {
      if (buffer.size() < 24)
        return;
      auto n = u32(buffer.constData() + 20);
      if (n > 4096) {
        failure("Invalid guest display name.");
        return;
      }
      if (buffer.size() < 24 + int(n))
        return;
      int w = u16(buffer.constData()), h = u16(buffer.constData() + 2);
      if (w < 1 || h < 1 || w > 4096 || h > 2160) {
        failure("Unsupported guest screen size.");
        return;
      }
      frame = QImage(w, h, QImage::Format_RGB32);
      frame.fill(Qt::black);
      buffer.remove(0, 24 + n);
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
      put16(enc, 5);
      for (int i : {0, 1, -223, -257, -308})
        put32(enc, quint32(i));
      socket.write(enc);
      state = 6;
      ready = true;
      updateRequest(false);
      emit changed();
    } else if (state == 6) {
      if (rects < 0) {
        if (buffer.isEmpty())
          return;
        int type = quint8(buffer[0]);
        if (type == 0) {
          if (buffer.size() < 4)
            return;
          rects = u16(buffer.constData() + 2);
          buffer.remove(0, 4);
        } else if (type == 2) {
          buffer.remove(0, 1);
          continue;
        } else if (type == 3) {
          if (buffer.size() < 8)
            return;
          auto n = u32(buffer.constData() + 4);
          if (n > 1048576) {
            failure("Guest clipboard is too large.");
            return;
          }
          if (buffer.size() < 8 + int(n))
            return;
          buffer.remove(0, 8 + n);
          continue;
        } else {
          failure("Unsupported guest display message.");
          return;
        }
      }
      if (rects == 0) {
        rects = -1;
        emit changed();
        refresh.start();
        continue;
      }
      if (buffer.size() < 12)
        return;
      const char *b = buffer.constData();
      int x = u16(b), y = u16(b + 2), w = u16(b + 4), h = u16(b + 6);
      qint32 encoding = qint32(u32(b + 8));
      if (encoding == -308) {
        // ExtendedDesktopSize has a variable screen layout. QEMU returns
        // status 4 when the guest driver will apply the request asynchronously.
        if (buffer.size() < 16)
          return;
        int screens = quint8(buffer[12]);
        if (screens < 1 || screens > 16) {
          failure("Invalid guest display layout.");
          return;
        }
        int bytes = 16 + screens * 16;
        if (buffer.size() < bytes)
          return;
        if (w < 1 || h < 1 || w > 4096 || h > 2160) {
          failure("Unsupported guest screen size.");
          return;
        }
        desktopResizeResult = x == 1 ? y : 0;
        desktopResizeSupported = desktopResizeResult == 0 || desktopResizeResult == 4;
        if (desktopResizeResult == 0 && frame.size() != QSize(w, h)) {
          frame = QImage(w, h, QImage::Format_RGB32);
          frame.fill(Qt::black);
        }
        buffer.remove(0, bytes);
      } else if (encoding == -223) {
        if (w < 1 || h < 1 || w > 4096 || h > 2160) {
          failure("Unsupported guest screen size.");
          return;
        }
        frame = QImage(w, h, QImage::Format_RGB32);
        frame.fill(Qt::black);
        buffer.remove(0, 12);
      } else if (encoding == -257) {
        relative = x == 0;
        buffer.remove(0, 12);
      } else if ((encoding == 0 || encoding == 1) && x + w <= frame.width() &&
                 y + h <= frame.height()) {
        qint64 bytes = encoding == 0 ? qint64(w) * h * 4 : 4;
        if (buffer.size() < 12 + bytes)
          return;
        if (encoding == 0) {
          const uchar *source = reinterpret_cast<const uchar *>(b + 12);
          for (int j = 0; j < h; ++j) {
            auto row = reinterpret_cast<QRgb *>(frame.scanLine(y + j)) + x;
            for (int i = 0; i < w; ++i) {
              row[i] = qRgb(source[2], source[1], source[0]);
              source += 4;
            }
          }
        } else {
          int sx = u16(b + 12), sy = u16(b + 14);
          if (sx + w > frame.width() || sy + h > frame.height()) {
            failure("Invalid guest display copy.");
            return;
          }
          auto copy = frame.copy(sx, sy, w, h);
          QPainter p(&frame);
          p.drawImage(x, y, copy);
        }
        buffer.remove(0, 12 + bytes);
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
void VNC::key(quint32 sym, bool down) {
  if (!ready)
    return;
  QByteArray b;
  b += char(4);
  b += char(down);
  put16(b, 0);
  put32(b, sym);
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
  setFocusPolicy(Qt::StrongFocus);
  setMouseTracking(true);
  setAccessibleName("Mac screen");
  resizeDebounce.setSingleShot(true);
  resizeDebounce.setInterval(250);
  resizeDeadline.setSingleShot(true);
  resizeDeadline.setInterval(5000);
  connect(&resizeDebounce, &QTimer::timeout, this, &GuestView::requestResize);
  connect(&resizeDeadline, &QTimer::timeout, this, [this] {
    if (vnc->frame.size() != pendingSize) {
      resizeNotice = "This Mac has kept its current screen size.";
      emit displayChanged();
    }
  });
  connect(vnc, &VNC::changed, this, [this] {
    if (captured && !vnc->relative)
      release();
    if (vnc->ready && observedSize != vnc->frame.size()) {
      observedSize = vnc->frame.size();
      if (observedSize == pendingSize) {
        resizeDeadline.stop();
        resizeNotice.clear();
      } else if (wantsResize && resizeAttempts < 3)
        resizeDebounce.start();
      emit displayChanged();
    }
    update();
  });
}
GuestView::~GuestView() { release(); }
QRectF GuestView::screenRect() const {
  if (vnc->frame.isNull())
    return rect();
  return NativeDisplay::imageRect(size(), vnc->frame.size(), devicePixelRatioF(), fitWindow, pixelZoom);
}
QSize GuestView::desiredSize() const {
  return NativeDisplay::requestedSize(size(), fitWindow ? 1 : devicePixelRatioF(),
                                widthAlignment, fitWindow ? 1 : pixelZoom);
}
void GuestView::scheduleResize() {
  wantsResize = true;
  resizeAttempts = 0;
  resizeDebounce.start();
}
void GuestView::requestResize() {
  if (!wantsResize)
    return;
  if (desiredSize() == vnc->frame.size()) {
    resizeDeadline.stop();
    if (!resizeNotice.isEmpty()) {
      resizeNotice.clear();
      emit displayChanged();
    }
    return;
  }
  if (!followsWindow || paused || !isVisible() || !vnc->ready ||
      window()->isMinimized() || desiredSize() == vnc->frame.size() ||
      resizeAttempts >= 3)
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
  if (resizeArmed)
    scheduleResize();
}
void GuestView::showEvent(QShowEvent *e) {
  QWidget::showEvent(e);
  if (!shownOnce) {
    shownOnce = true;
    // Initial layout/work-area fitting must not ask a classic video driver
    // to hot-plug its screen while Display Manager is still starting up.
    QTimer::singleShot(500, this, [this] { resizeArmed = true; });
  } else if (wantsResize)
    resizeDebounce.start();
}
bool GuestView::event(QEvent *e) {
#if QT_VERSION >= QT_VERSION_CHECK(6, 6, 0)
  if (e->type() == QEvent::DevicePixelRatioChange) {
    if (resizeArmed)
      scheduleResize();
    update();
  }
#endif
  return QWidget::event(e);
}
void GuestView::paintEvent(QPaintEvent *) {
  QPainter q(this);
  q.fillRect(rect(), vnc->ready ? Qt::black : QColor(0x63639c));
  q.setRenderHint(QPainter::SmoothPixmapTransform, false);
  if (!vnc->frame.isNull()) {
    // Rasterize in integral device coordinates. A logical QRectF whose height
    // is 768 / 1.75 can round just below 768 and drop a source row even with
    // nearest-neighbor sampling, especially when the image is letterboxed.
    const auto device = q.deviceTransform();
    const auto target = device.mapRect(screenRect());
    q.setWorldTransform(device.inverted());
    q.drawImage(QRectF(qRound(target.x()), qRound(target.y()),
                       qRound(target.width()), qRound(target.height())), vnc->frame);
  }
  else {
    Platinum::Pixels p(width(), height(), 0xff63639c);
    Platinum::text(p, notice,
                   (width() - Platinum::textWidth(notice, "small")) / 2,
                   height() / 2, 0xffffffff, "small");
    q.drawImage(0, 0, p.image);
  }
}
quint32 GuestView::keySym(QKeyEvent *e) const {
  int k = e->key();
  if (k >= Qt::Key_A && k <= Qt::Key_Z)
    return (e->modifiers() & Qt::ShiftModifier) ? 'A' + k - Qt::Key_A
                                                : 'a' + k - Qt::Key_A;
  if (k >= Qt::Key_0 && k <= Qt::Key_9 && !e->text().isEmpty())
    return e->text()[0].unicode();
  if (k >= Qt::Key_F1 && k <= Qt::Key_F12)
    return 0xffbe + k - Qt::Key_F1;
  switch (k) {
  case Qt::Key_Return:
  case Qt::Key_Enter:
    return 0xff0d;
  case Qt::Key_Backspace:
    return 0xff08;
  case Qt::Key_Tab:
    return 0xff09;
  case Qt::Key_Escape:
    return 0xff1b;
  case Qt::Key_Delete:
    return 0xffff;
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
    return 0xffe9;
  case Qt::Key_Meta:
    return 0xffeb;
  case Qt::Key_CapsLock:
    return 0xffe5;
  default:
    return e->text().isEmpty() ? 0 : e->text()[0].unicode();
  }
}
void GuestView::keyPressEvent(QKeyEvent *e) {
  if (e->key() == Qt::Key_Escape && captured) {
    release();
    e->accept();
    return;
  }
  if (captured && ((e->key() == Qt::Key_Alt && e->modifiers() & Qt::ControlModifier) ||
                   (e->key() == Qt::Key_Control && e->modifiers() & Qt::AltModifier))) {
    release();
    e->accept();
    return;
  }
  if (e->key() == Qt::Key_Escape && window()->isFullScreen()) {
    emit toggleFullScreen();
    e->accept();
    return;
  }
  auto key = keySym(e);
  if (key) {
    keys.insert(key);
    vnc->key(key, true);
  }
  e->accept();
}
void GuestView::keyReleaseEvent(QKeyEvent *e) {
  auto key = keySym(e);
  if (key) {
    keys.remove(key);
    vnc->key(key, false);
  }
  e->accept();
}
void GuestView::release() {
  for (auto k : keys)
    vnc->key(k, false);
  keys.clear();
  if (controlClick)
    vnc->key(0xffe3, false);
  controlClick = false;
  if (mask)
    vnc->pointer(vnc->relative ? 0x7fff : lastPointer.x(),
                 vnc->relative ? 0x7fff : lastPointer.y(), 0);
  mask = 0;
  wheelRemainder = 0;
  if (captured) {
    captured = false;
    releaseMouse();
    unsetCursor();
    emit captureChanged(false);
  }
  update();
}
void GuestView::focusOutEvent(QFocusEvent *e) {
  release();
  QWidget::focusOutEvent(e);
}
void GuestView::mouse(QMouseEvent *e) {
  auto r = screenRect();
  if (vnc->relative) {
    if (!captured)
      return;
    QPoint center = rect().center(), delta = e->position().toPoint() - center;
    if (e->type() == QEvent::MouseMove && delta.isNull())
      return;
    vnc->pointer(0x7fff + delta.x(), 0x7fff + delta.y(), mask);
    if (e->type() == QEvent::MouseMove)
      QCursor::setPos(mapToGlobal(center));
  } else if (!vnc->frame.isNull() &&
             (r.contains(e->position()) || mask || e->type() == QEvent::MouseButtonRelease)) {
    lastPointer = {
        std::clamp(int((e->position().x() - r.x()) * vnc->frame.width() /
                        std::max(qreal(1), r.width())), 0, vnc->frame.width() - 1),
        std::clamp(int((e->position().y() - r.y()) * vnc->frame.height() /
                        std::max(qreal(1), r.height())), 0, vnc->frame.height() - 1)};
    vnc->pointer(lastPointer.x(), lastPointer.y(), mask);
  }
}
void GuestView::mousePressEvent(QMouseEvent *e) {
  setFocus();
  if (vnc->relative && !captured) {
    captured = true;
    grabMouse();
    setCursor(Qt::BlankCursor);
    QCursor::setPos(mapToGlobal(rect().center()));
    emit captureChanged(true);
    update();
    return;
  }
  int bit = e->button() == Qt::LeftButton     ? 1
            : e->button() == Qt::MiddleButton ? 2
                                              : 4;
  if (bit == 4 && helpers && !vnc->relative) {
    vnc->key(0xffe3, true);
    controlClick = true;
    bit = 1;
  }
  mask |= bit;
  mouse(e);
}
void GuestView::mouseReleaseEvent(QMouseEvent *e) {
  int bit = e->button() == Qt::LeftButton     ? 1
            : e->button() == Qt::MiddleButton ? 2
                                              : 4;
  if (bit == 4 && controlClick)
    bit = 1;
  mask &= ~bit;
  mouse(e);
  if (controlClick) {
    vnc->key(0xffe3, false);
    controlClick = false;
  }
}
void GuestView::mouseMoveEvent(QMouseEvent *e) { mouse(e); }
void GuestView::wheelEvent(QWheelEvent *e) {
  if (e->angleDelta().y() == 0 || (vnc->relative && !captured)) {
    e->ignore();
    return;
  }
  wheelRemainder += e->angleDelta().y();
  int steps = wheelRemainder / 120;
  wheelRemainder %= 120;
  if (helpers && !vnc->relative) {
    quint32 k = steps > 0 ? 0xff52 : 0xff54;
    for (int n = 0; n < std::abs(steps); ++n) {
      vnc->key(k, true);
      vnc->key(k, false);
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
