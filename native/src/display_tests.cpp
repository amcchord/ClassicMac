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
        else if (type == 255) bytes = 12;
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
  check(NativeDisplay::requestedSize({400, 300}, 1, 8, 0, {640, 480}, {1920, 1440}) == QSize(640, 480), "ATI minimum mode");
  check(NativeDisplay::requestedSize({5000, 4000}, 2, 8, 1, {640, 480}, {1920, 1440}) == QSize(1920, 1440), "ATI hardware envelope");
  check(NativeDisplay::requestedSize({900, 1400}, 1, 8, 1, {640, 480}, {1920, 1440}) == QSize(896, 1400), "ATI portrait/alignment");

  {
    QWidget host;
    host.resize(933, 725);
    VNC framebuffer;
    framebuffer.ready = true;
    GuestView view(&framebuffer, &host);
    view.setGeometry(6, 22, 920, 638);
    const qreal dpi = view.devicePixelRatioF();
    const QList<QSize> sources = {NativeDisplay::requestedSize(view.size(), dpi, 8),
                                  QSize(1024, 768), QSize(640, 480)};
    for (const auto size : sources) {
      framebuffer.frame = QImage(size, QImage::Format_RGB32);
      for (int y = 0; y < size.height(); ++y)
        for (int x = 0; x < size.width(); ++x)
          framebuffer.frame.setPixel(x, y, qRgb(x % 251, y % 251, (x + y) % 251));
      emit framebuffer.changed();
      host.show();
      QApplication::processEvents();
      auto captured = host.grab().toImage();
      const auto r = view.screenRect();
      const auto factor = r.width() * dpi / size.width();
      if (factor < 1) continue; // A reduced-to-fit screen intentionally resamples.
      const int zoom = qRound(factor);
      const QPoint at(qRound((view.x() + r.x()) * dpi),
                       qRound((view.y() + r.y()) * dpi));
      check(zoom >= 1, "Synthetic guest cannot fit at integral zoom");
      for (int y = 0; y < size.height() * zoom; ++y)
        for (int x = 0; x < size.width() * zoom; ++x)
          if (x + qRound(r.x() * dpi) < qFloor(view.displayViewport().width() * dpi) &&
              y + qRound(r.y() * dpi) < qFloor(view.displayViewport().height() * dpi))
          check(captured.valid(at.x() + x, at.y() + y) &&
                  captured.pixel(at.x() + x, at.y() + y) == framebuffer.frame.pixel(x / zoom, y / zoom),
                  "Guest framebuffer pixels were clipped/resampled at DPR " + QString::number(dpi) +
                  " for " + QString::number(size.width()) + "x" + QString::number(size.height()));
      if (!output.isEmpty()) host.grab().save(output + "/guest-grid-" + QString::number(size.width()) + ".png");
    }
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
    native.resize(84, 24);
    native.setFocusPolicy(Qt::NoFocus);
    QImage actual(qCeil(84 * scale), qCeil(24 * scale), QImage::Format_ARGB32);
    actual.setDevicePixelRatio(scale);
    actual.fill(Platinum::face);
    native.render(&actual);
    QImage expected(actual.size(), actual.format());
    expected.setDevicePixelRatio(scale);
    expected.fill(Platinum::face);
    {
      QPainter p(&expected);
      p.setRenderHint(QPainter::SmoothPixmapTransform, false);
      p.drawImage(2, 2, QImage(fixtures + "/button-normal.png"));
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
  check(NativeDisplay::imageRect({400, 300}, {640, 480}, 1).width() == 640,
        "Sharp Pixels silently shrank an oversized guest");
  check(NativeDisplay::imageRect({400, 300}, {640, 480}, 1, true).width() == 400,
        "Fit Window stopped fitting the complete guest");
  {
    VNC frame;
    frame.ready = true;
    frame.frame = QImage(640, 480, QImage::Format_RGB32);
    for (int y = 0; y < 480; ++y)
      for (int x = 0; x < 640; ++x)
        frame.frame.setPixel(x, y, qRgb(x % 251, y % 251, (x + y) % 251));
    GuestView view(&frame);
    view.resize(400, 300);
    view.show();
    emit frame.changed();
    QApplication::processEvents();
    const auto dpr = view.devicePixelRatioF();
    if (640 > view.width() * dpr || 480 > view.height() * dpr) {
      auto bars = view.findChildren<QScrollBar *>();
      check(bars.size() == 2, "Sharp Pixels has no screen scrollbars");
      for (auto bar : bars) {
        check(bar->isVisible() && bar->maximum() > 0, "Oversized guest edge is unreachable");
        bar->setValue(bar->maximum());
      }
      QApplication::processEvents();
      auto image = view.grab().toImage();
      const auto r = view.screenRect();
      const int x = qRound(r.x() * dpr) + 639, y = qRound(r.y() * dpr) + 479;
      check(image.valid(x, y) && image.pixel(x, y) == frame.frame.pixel(639, 479),
            "Scrolling did not expose the final guest pixel intact");
      if (!output.isEmpty()) view.grab().save(output + "/guest-sharp-scrolled.png");
    }
    view.fitWindow = true;
    view.scheduleResize();
    QApplication::processEvents();
    for (auto bar : view.findChildren<QScrollBar *>())
      check(!bar->isVisible(), "Fit Window left unnecessary scrollbars");
  }
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
  {
    // Verify AlphaCursor (-314), RichCursor (-239), and ExtendedKeyEvent (-258).
    QByteArray extKey = QByteArray::fromHex("00000001");
    p16(extKey, 0); p16(extKey, 0); p16(extKey, 0); p16(extKey, 0);
    p32(extKey, quint32(-258));
    peer.socket->write(extKey);
    waitFor([&] { return client.extendedKeySupported; });

    QByteArray alpha = QByteArray::fromHex("00000001");
    p16(alpha, 1); p16(alpha, 1); p16(alpha, 2); p16(alpha, 2);
    p32(alpha, quint32(-314));
    p32(alpha, 0); // RAW sub-encoding
    alpha += QByteArray::fromHex("ff00008000ff00ff0000ff40ffffff00"); // 4 BGRA pixels
    peer.socket->write(alpha);
    waitFor([&] { return client.hasCursor && client.cursorImage.size() == QSize(2, 2); });
    check(client.cursorHotSpot == QPoint(1, 1) &&
          client.cursorImage.pixel(0, 0) == 0x800000ffu &&
          client.cursorImage.pixel(1, 0) == 0xff00ff00u,
          "AlphaCursor decoding failed");

    QByteArray rich = QByteArray::fromHex("00000001");
    p16(rich, 0); p16(rich, 1); p16(rich, 2); p16(rich, 2);
    p32(rich, quint32(-239));
    // 4 BGRX pixels: red, green, blue, white
    rich += QByteArray::fromHex("0000ff0000ff0000ff000000ffffff00");
    // 2 rows x 1 byte mask: row 0 = 10000000 (0x80), row 1 = 01000000 (0x40)
    rich += QByteArray::fromHex("8040");
    peer.socket->write(rich);
    waitFor([&] { return client.cursorHotSpot == QPoint(0, 1); });
    check(client.cursorImage.pixel(0, 0) == 0xffff0000u &&
          qAlpha(client.cursorImage.pixel(1, 0)) == 0 &&
          qAlpha(client.cursorImage.pixel(0, 1)) == 0 &&
          client.cursorImage.pixel(1, 1) == 0xffffffffu,
          "RichCursor decoding failed");

    QWidget host;
    auto *layout = new QHBoxLayout(&host);
    GuestView view(&client, &host);
    view.swapAltCommand = true;
    auto *other = new QPushButton("Other", &host);
    layout->addWidget(&view);
    layout->addWidget(other);
    host.resize(800, 600);
    host.show();
    host.activateWindow();
    view.setFocus();
    QApplication::processEvents();
    const auto msgCount = [&] { return peer.messages.size(); };
    // Tab and Shift+Tab must not focus-cycle away from GuestView.
    auto before = msgCount();
    QKeyEvent tabDown(QEvent::KeyPress, Qt::Key_Tab, Qt::NoModifier);
    QKeyEvent backtabDown(QEvent::KeyPress, Qt::Key_Backtab, Qt::ShiftModifier);
    QKeyEvent spaceDown(QEvent::KeyPress, Qt::Key_Space, Qt::ControlModifier);
    QKeyEvent numDown(QEvent::KeyPress, Qt::Key_1, Qt::AltModifier);
    QKeyEvent dotDown(QEvent::KeyPress, Qt::Key_Period, Qt::AltModifier);
    QKeyEvent altDown(QEvent::KeyPress, Qt::Key_Alt, Qt::AltModifier);
    QKeyEvent repeatDown(QEvent::KeyPress, Qt::Key_A, Qt::NoModifier, "a", true);
    QApplication::sendEvent(&view, &tabDown);
    check(view.hasFocus() && !other->hasFocus(), "Tab moved focus away from GuestView");
    QApplication::sendEvent(&view, &backtabDown);
    check(view.hasFocus() && !other->hasFocus(), "Shift+Tab moved focus away from GuestView");
    QApplication::sendEvent(&view, &spaceDown);
    QApplication::sendEvent(&view, &numDown);
    QApplication::sendEvent(&view, &dotDown);
    QApplication::sendEvent(&view, &altDown);
    QApplication::sendEvent(&view, &repeatDown);
    waitFor([&] { return msgCount() >= before + 6; });
    check(msgCount() == before + 6, "Auto-repeat key was not filtered");
    // Last message must be ExtendedKeyEvent (255) for swapped Alt -> Meta_L (0xffeb, 0xdb).
    const QByteArray lastKey = peer.messages.last();
    check(quint8(lastKey[0]) == 255 && n16(lastKey, 2) == 1 &&
          quint32(quint8(lastKey[4]) << 24 | quint8(lastKey[5]) << 16 |
                  quint8(lastKey[6]) << 8 | quint8(lastKey[7])) == 0xffeb &&
          quint32(quint8(lastKey[8]) << 24 | quint8(lastKey[9]) << 16 |
                  quint8(lastKey[10]) << 8 | quint8(lastKey[11])) == 0xdb,
          "Swap Alt and Command or ExtendedKeyEvent failed");

    // Shift pressed + 'A', then Shift released before 'A' release must still release 'A'.
    before = msgCount();
    QKeyEvent shiftPress(QEvent::KeyPress, Qt::Key_Shift, Qt::ShiftModifier);
    QKeyEvent aUpperPress(QEvent::KeyPress, Qt::Key_A, Qt::ShiftModifier, "A");
    QKeyEvent shiftRelease(QEvent::KeyRelease, Qt::Key_Shift, Qt::NoModifier);
    QKeyEvent aLowerRelease(QEvent::KeyRelease, Qt::Key_A, Qt::NoModifier, "a");
    QApplication::sendEvent(&view, &shiftPress);
    QApplication::sendEvent(&view, &aUpperPress);
    QApplication::sendEvent(&view, &shiftRelease);
    QApplication::sendEvent(&view, &aLowerRelease);
    waitFor([&] { return msgCount() >= before + 4; });
    const QByteArray aRel = peer.messages.last();
    check(quint8(aRel[0]) == 255 && n16(aRel, 2) == 0 && quint8(aRel[7]) == 'A',
          "Early Shift release mismatched key release keysym");

    // XButton1 must be ignored; Left release while Right controlClick held must not clear Ctrl.
    const auto ptrCount = [&] {
      return std::count_if(peer.messages.begin(), peer.messages.end(),
                           [](const QByteArray &b) { return quint8(b[0]) == 5; });
    };
    const auto pBefore = ptrCount();
    const auto mBefore = msgCount();
    QPointF pt = view.screenRect().center();
    QMouseEvent xbPress(QEvent::MouseButtonPress, pt, pt, Qt::XButton1, Qt::XButton1, Qt::NoModifier);
    QApplication::sendEvent(&view, &xbPress);
    QCoreApplication::processEvents();
    check(ptrCount() == pBefore && msgCount() == mBefore, "XButton1 was not ignored");

    QMouseEvent rPress(QEvent::MouseButtonPress, pt, pt, Qt::RightButton, Qt::RightButton, Qt::NoModifier);
    QMouseEvent lRelease(QEvent::MouseButtonRelease, pt, pt, Qt::LeftButton, Qt::NoButton, Qt::NoModifier);
    QMouseEvent rRelease(QEvent::MouseButtonRelease, pt, pt, Qt::RightButton, Qt::NoButton, Qt::NoModifier);
    QApplication::sendEvent(&view, &rPress);
    QApplication::sendEvent(&view, &lRelease);
    // Ctrl key-up must NOT have been sent yet on LeftButton release.
    QCoreApplication::processEvents();
    auto keyReleasesSince = std::count_if(peer.messages.begin() + mBefore, peer.messages.end(),
                                          [](const QByteArray &b) {
                                            return quint8(b[0]) == 255 && quint8(b[3]) == 0;
                                          });
    check(keyReleasesSince == 0, "controlClick released early on LeftButton release");
    QApplication::sendEvent(&view, &rRelease);
    waitFor([&] {
      return std::count_if(peer.messages.begin() + mBefore, peer.messages.end(),
                           [](const QByteArray &b) {
                             return quint8(b[0]) == 255 && quint8(b[3]) == 0;
                           }) == 1;
    });
    view.close();
    host.close();
  }
  {
    // Verify Enter activates focused push button instead of default button,
    // and short dialogs do not force visible scrollbars.
    PDialog dialog("Focus & Scroll", {420, 220}, nullptr);
    auto v = dialogLayout(&dialog);
    v->addWidget(label("Short message.", &dialog));
    bool defaultClicked = false, focusedClicked = false;
    auto *defBtn = button("Default", [&] { defaultClicked = true; }, &dialog, true);
    auto *altBtn = button("Alternate", [&] { focusedClicked = true; }, &dialog, false);
    buttonRow(v, {altBtn, defBtn});
    dialog.show();
    dialog.activateWindow();
    altBtn->setFocus();
    QApplication::processEvents();
    auto *scroll = dialog.body->findChild<QScrollArea *>();
    check(scroll && scroll->verticalScrollBar()->maximum() == 0 &&
          scroll->horizontalScrollBar()->maximum() == 0,
          "Short dialog unexpectedly overflowed into scrollbars");
    QKeyEvent enter(QEvent::KeyPress, Qt::Key_Return, Qt::NoModifier);
    QApplication::sendEvent(&dialog, &enter);
    waitFor([&] { return focusedClicked || defaultClicked; });
    check(focusedClicked && !defaultClicked, "Enter triggered defaultButton instead of focused button");
    dialog.close();
  }
  {
    // Verify a guest window sized from measured footer height opens without scrollbars.
    client.frame = QImage(512, 384, QImage::Format_RGB32);
    client.frame.fill(0xff63639c);
    PWindow guestWin("Guest Layout Check", {512 + 13, 384 + 92});
    auto *v = new QVBoxLayout(guestWin.body);
    v->setContentsMargins(0, 0, 0, 0);
    v->setSpacing(0);
    auto *view = new GuestView(&client, guestWin.body);
    view->followsWindow = true;
    v->addWidget(view, 1);
    auto *footer = new QWidget(guestWin.body);
    auto *footerLayout = new QVBoxLayout(footer);
    footerLayout->setContentsMargins(0, 6, 0, 0);
    footerLayout->setSpacing(6);
    auto *displayRow = new QHBoxLayout;
    displayRow->setContentsMargins(8, 0, 24, 0);
    auto *status = label("512 × 384", &guestWin, "small");
    displayRow->addWidget(status, 1);
    auto *scale = new PCombo(&guestWin);
    scale->addItems({"Sharp Pixels", "1× Pixels", "2× Pixels", "Fit Window"});
    scale->setFixedWidth(126);
    displayRow->addWidget(scale);
    footerLayout->addLayout(displayRow);
    auto *toolbar = new QWidget(footer);
    auto *controls = new PFlowLayout(toolbar);
    for (const auto &title : {"Pause", "Power…", "Paste…", "Media…", "Full Screen"})
      controls->addWidget(button(title, [] {}, &guestWin));
    footerLayout->addWidget(toolbar);
    v->addWidget(footer);
    guestWin.ensurePolished();
    footerLayout->activate();
    const int fh = std::max(footerLayout->heightForWidth(512), 63);
    const QSize targetSize(512 + 13, 384 + 29 + fh);
    const QRect avail = guestWin.screen() ? guestWin.screen()->availableGeometry() : QRect(0, 0, 2000, 2000);
    if (avail.width() >= targetSize.width() && avail.height() >= targetSize.height()) {
      guestWin.resize(targetSize);
      guestWin.expanded = targetSize;
      guestWin.show();
      QApplication::processEvents();
      check(view->size().width() == 512 && view->size().height() == 384,
            "GuestView viewport did not equal 512x384");
      const auto bars = view->findChildren<QScrollBar *>();
      check(bars.size() == 2 && bars[0]->maximum() == 0 && bars[1]->maximum() == 0,
            "512x384 guest window showed unexpected scrollbars");
      guestWin.close();
    }
    const int fh1024 = std::max(footerLayout->heightForWidth(1024), 63);
    check(fh1024 > 58, "Measured footer height would under-size 1024x768");
    client.frame = QImage(1024, 768, QImage::Format_RGB32);
    guestWin.body->setGeometry(6, 22, 1024, 768 + fh1024);
    v->activate();
    QApplication::processEvents();
    check(view->size().width() == 1024 && view->size().height() == 768,
          "GuestView 1024x768 viewport did not match measured footer layout");
  }
  client.close();
  check(!client.resizeDesktop({800, 600}), "Disconnected resize was sent");
}
