#include "display.h"
#include "qualification.h"
#include "machine.h"
#include "vnc.h"
#include "widgets.h"
#include <QtWidgets>

namespace {
void check(bool passed, const QString &reason) {
  if (!passed)
    Model::fail(reason);
}
void waitFor(const std::function<bool()> &ready) {
  QEventLoop loop;
  QTimer poll, deadline;
  poll.setInterval(5);
  deadline.setSingleShot(true);
  QObject::connect(&poll, &QTimer::timeout, &loop, [&] { if (ready()) loop.quit(); });
  QObject::connect(&deadline, &QTimer::timeout, &loop, &QEventLoop::quit);
  poll.start();
  deadline.start(2000);
  if (!ready()) loop.exec();
  check(ready(), "Timed out in native display protocol regression");
}
void p16(QByteArray &b, int n) { b += char(n >> 8); b += char(n); }
void p32(QByteArray &b, quint32 n) { p16(b, n >> 16); p16(b, n); }
int n16(const QByteArray &b, int i) { return quint8(b[i]) * 256 + quint8(b[i+1]); }

// A deliberately fragmented authenticated RFB peer exercises the real parser,
// wire messages and native input, independently of the rendering functions.
class Peer : public QObject {
public:
  QTcpServer server;
  QTcpSocket *socket = nullptr;
  QByteArray input;
  QList<QByteArray> messages;
  int state = 0;
  bool passwordMatched = false;
  Peer() {
    check(server.listen(QHostAddress::LocalHost, 0), "RFB test listen failed");
    connect(&server, &QTcpServer::newConnection, this, [&] {
      socket = server.nextPendingConnection();
      connect(socket, &QTcpSocket::readyRead, this, [&] { input += socket->readAll(); parse(); });
      socket->write("RFB 003.008\n");
    });
  }
  void parse() {
    for (;;) {
      int bytes = 0;
      if (state == 0) {
        if (input.size() < 12) return;
        bytes = 12;
        socket->write(QByteArray::fromHex("0102"));
        ++state;
      } else if (state == 1) {
        if (input.isEmpty()) return;
        bytes = 1;
        socket->write(QByteArray(16, 'x'));
        ++state;
      } else if (state == 2) {
        if (input.size() < 16) return;
        passwordMatched = input.left(16) == VNC::authenticate(QByteArray(16, 'x'), "testpass");
        bytes = 16;
        socket->write(QByteArray(4, 0));
        ++state;
      } else if (state == 3) {
        if (input.isEmpty()) return;
        bytes = 1;
        QByteArray init;
        p16(init, 640); p16(init, 480);
        init += QByteArray(16, 0);
        p32(init, 4); init += "Test";
        socket->write(init);
        ++state;
      } else {
        if (input.isEmpty()) return;
        int type = quint8(input[0]);
        if (type == 0) bytes = 20;
        else if (type == 2) {
          if (input.size() < 4) return;
          bytes = 4 + n16(input, 2) * 4;
        } else if (type == 3) bytes = 10;
        else if (type == 4) bytes = 8;
        else if (type == 5) bytes = 6;
        else if (type == 251) bytes = 24;
        else { check(false, "Unexpected native RFB client message"); }
        if (input.size() < bytes) return;
        messages << input.left(bytes);
      }
      input.remove(0, bytes);
    }
  }
  QByteArray layout(QSize size, int reason = 0) {
    QByteArray b = QByteArray::fromHex("00000001");
    p16(b, reason ? 1 : 0); p16(b, reason);
    p16(b, size.width()); p16(b, size.height()); p32(b, quint32(-308));
    b += char(1); b += QByteArray(3, 0);
    p32(b, 0); p16(b, 0); p16(b, 0);
    p16(b, size.width()); p16(b, size.height()); p32(b, 0);
    return b;
  }
  void sendFragmented(QByteArray b) {
    socket->write(b.left(15));
    QTimer::singleShot(15, this, [this, b] { socket->write(b.mid(15, 5)); });
    QTimer::singleShot(30, this, [this, b] { socket->write(b.mid(20)); });
  }
};
}

void displayTests(const QString &fixtures, const QString &output) {
  {
    QWidget host;
    host.resize(933, 725);
    VNC framebuffer;
    framebuffer.ready = true;
    GuestView view(&framebuffer, &host);
    view.setGeometry(6, 22, 920, 638);
    const qreal dpi = view.devicePixelRatioF();
    const auto size = NativeDisplay::requestedSize(view.size(), dpi, 8);
    framebuffer.frame = QImage(size, QImage::Format_RGB32);
    for (int y = 0; y < size.height(); ++y)
      for (int x = 0; x < size.width(); ++x)
        framebuffer.frame.setPixel(x, y, qRgb(x % 251, y % 251, (x + y) % 251));
    host.show();
    QApplication::processEvents();
    auto captured = host.grab().toImage();
    const auto r = view.screenRect();
    const int zoom = qRound(r.width() * dpi / size.width());
    const QPoint at(qRound(view.x() * dpi) + qRound(r.x() * dpi),
                     qRound(view.y() * dpi) + qRound(r.y() * dpi));
    check(zoom >= 1, "Synthetic guest cannot fit at integral zoom");
    for (int y = 0; y < size.height() * zoom; ++y)
      for (int x = 0; x < size.width() * zoom; ++x)
        check(captured.valid(at.x() + x, at.y() + y) &&
                captured.pixel(at.x() + x, at.y() + y) == framebuffer.frame.pixel(x / zoom, y / zoom),
                "Guest framebuffer pixels were clipped/resampled at DPR " + QString::number(dpi));
    if (!output.isEmpty()) host.grab().save(output + "/guest-grid.png");
    host.close();
  }
  {
    PDialog dialog("Small display", {420, 250}, nullptr);
    auto v = dialogLayout(&dialog);
    auto tall = new QWidget;
    tall->setMinimumHeight(1000);
    v->addWidget(tall);
    auto save = button("Save", [] {}, &dialog, true);
    buttonRow(v, {save});
    dialog.show();
    QApplication::processEvents();
    check(dialog.rect().contains(QRect(save->mapTo(&dialog, QPoint()), save->size())),
          "Dialog footer is clipped on a short scaled desktop");
    auto scroll = dialog.body->findChild<QScrollArea *>();
    check(scroll && scroll->verticalScrollBar()->maximum() > 0,
          "Oversized dialog content is unreachable");
    if (!output.isEmpty()) dialog.grab().save(output + "/small-dialog.png");
    dialog.close();
  }
  const QList<qreal> scales = {1, 1.25, 1.5, 1.75, 2, 2.5, 3};
  for (qreal scale : scales) {
    PButton native("", nullptr);
    native.resize(80, 20);
    native.setFocusPolicy(Qt::NoFocus);
    QImage actual(qCeil(80 * scale), qCeil(20 * scale), QImage::Format_ARGB32);
    actual.setDevicePixelRatio(scale);
    actual.fill(Platinum::face);
    native.render(&actual);
    QImage expected(actual.size(), actual.format());
    expected.setDevicePixelRatio(scale);
    expected.fill(Platinum::face);
    {
      QPainter p(&expected);
      p.setRenderHint(QPainter::SmoothPixmapTransform, false);
      p.drawImage(0, 0, QImage(fixtures + "/button-normal.png"));
    }
    check(actual == expected, "Native button changed reference pixels at DPI " + QString::number(scale));
    for (QSize viewport : {QSize(643, 487), QSize(1001, 713), QSize(1900, 1300)}) {
      auto target = NativeDisplay::requestedSize(viewport, scale, 8);
      check(target.width() % 8 == 0 && target.width() >= 512 && target.height() >= 384,
             "Invalid guest resolution alignment/minimum");
      auto r = NativeDisplay::imageRect(viewport, target, scale);
      qreal zoom = r.width() * scale / target.width();
      if (target.width() * NativeDisplay::preferredZoom(scale) <= qRound(viewport.width() * scale) &&
          target.height() * NativeDisplay::preferredZoom(scale) <= qRound(viewport.height() * scale))
        check(qAbs(zoom - qRound(zoom)) < 0.000001, "Nonintegral guest pixel zoom");
      check(qAbs(r.x() * scale - qRound(r.x() * scale)) < 0.000001 &&
             qAbs(r.y() * scale - qRound(r.y() * scale)) < 0.000001,
             "Guest image is not aligned to device pixels");
    }
    if (!output.isEmpty())
      actual.save(output + "/button-dpi-" + QString::number(scale) + ".png");
  }
  check(NativeDisplay::requestedSize({99999, 99999}, 3, 8) == QSize(3840, 2160),
        "Guest resize exceeds display/VRAM limits");
  check(NativeDisplay::imageRect({1000, 800}, {640, 480}, 1, false, 2).width() == 640,
        "A fixed screen that cannot fit 2x used a fractional sharp-pixel zoom");
  Peer peer;
  VNC client;
  bool failed = false;
  QObject::connect(&client, &VNC::problem, &client, [&](QString) { failed = true; });
  client.open(peer.server.serverPort(), "testpass");
  waitFor([&] { return client.ready && peer.messages.size() >= 3; });
  check(peer.passwordMatched, "RFB password exchange changed");
  peer.sendFragmented(peer.layout({640, 480}));
  waitFor([&] { return client.desktopResizeSupported; });
  check(client.resizeDesktop({800, 600}), "Native desktop resize not sent");
  waitFor([&] { return !peer.messages.isEmpty() && quint8(peer.messages.last()[0]) == 251; });
  QByteArray expected = QByteArray::fromHex("fb0003200258010000000000000000000320025800000000");
  check(peer.messages.last() == expected, "SetDesktopSize wire format differs");
  peer.sendFragmented(peer.layout({640, 480}, 4));
  waitFor([&] { return client.desktopResizeResult == 4; });
  check(client.ready && client.frame.size() == QSize(640, 480), "Deferred resize corrupted the current framebuffer");
  peer.sendFragmented(peer.layout({800, 600}));
  waitFor([&] { return client.frame.size() == QSize(800, 600); });
  check(!failed, "Fragmented ExtendedDesktopSize rejected");
  {
    GuestView view(&client);
    view.resize(1000, 700);
    view.show();
    QApplication::processEvents();
    QPointF point = view.screenRect().center();
    QMouseEvent press(QEvent::MouseButtonPress, point, point,
                       Qt::LeftButton, Qt::LeftButton, Qt::NoModifier);
    QApplication::sendEvent(&view, &press);
    QFocusEvent blur(QEvent::FocusOut);
    QApplication::sendEvent(&view, &blur);
    waitFor([&] { return !peer.messages.isEmpty() && quint8(peer.messages.last()[0]) == 5; });
    check(quint8(peer.messages.last()[1]) == 0, "Focus loss left a guest mouse button held");
    QMouseEvent outsideRelease(QEvent::MouseButtonRelease, QPointF(-40, -40), QPointF(-40, -40),
                                Qt::LeftButton, Qt::NoButton, Qt::NoModifier);
    auto pointerCount = [&] {
      return std::count_if(peer.messages.begin(), peer.messages.end(),
                           [](const QByteArray &b) { return quint8(b[0]) == 5; });
    };
    auto before = pointerCount();
    QApplication::sendEvent(&view, &press);
    QApplication::sendEvent(&view, &outsideRelease);
    waitFor([&] { return pointerCount() >= before + 2; });
    QByteArray released;
    for (auto b : peer.messages)
      if (quint8(b[0]) == 5) released = b;
    check(quint8(released[1]) == 0 && n16(released, 2) == 0 && n16(released, 4) == 0,
          "Pointer release outside the image was not clamped");
    view.close();
  }
  peer.sendFragmented(peer.layout({800, 600}, 3));
  waitFor([&] { return client.desktopResizeResult == 3; });
  check(!client.resizeDesktop({1024, 768}) && client.ready && client.frame.size() == QSize(800, 600),
        "A fixed-size/rejected display did not retain a usable frame");
  client.close();
  check(!client.resizeDesktop({800, 600}), "Disconnected resize was sent");
}
