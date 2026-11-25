#pragma once
#include <QAbstractNativeEventFilter>
#include <QCursor>
#include <QHash>
#include <QImage>
#include <QPair>
#include <QScrollBar>
#include <QWidget>
#include <QtNetwork>
class VNC : public QObject {
  Q_OBJECT
public:
  QImage frame;
  QImage cursorImage;
  QPoint cursorHotSpot;
  bool hasCursor = false;
  bool extendedKeySupported = false;
  bool relative = false, ready = false;
  bool desktopResizeSupported = false;
  int desktopResizeResult = -1;
  QSize lastRequestedSize;
  explicit VNC(QObject *parent = nullptr);
  void open(int port, const QByteArray &password);
  void close();
  void key(quint32 keysym, bool down, quint32 rfbKeycode = 0);
  void pointer(int x, int y, int mask);
  bool resizeDesktop(QSize);
  static QByteArray authenticate(QByteArray challenge, QByteArray password);
signals:
  void changed();
  void cursorChanged();
  void problem(QString);

private:
  QTcpSocket socket;
  QByteArray buffer, password;
  int state = 0, rects = -1;
  bool hadFrameRect = false;
  QTimer refresh;
  void parse();
  void updateRequest(bool incremental);
  void failure(QString);
};
class GuestView : public QWidget {
  Q_OBJECT
public:
  VNC *vnc;
  bool helpers = true, captured = false;
  bool swapAltCommand = false;
  bool legacyMouse = true;
  int mouseDpi = 0;
  bool followsWindow = false, fitWindow = false;
  bool paused = false;
  int widthAlignment = 1;
  int pixelZoom = 0;
  QSize minimumResolution = {512, 384}, maximumResolution = {3840, 2160};
  QString resizeNotice;
  QString notice = "Starting up…";
  explicit GuestView(VNC *, QWidget *parent = nullptr);
  ~GuestView() override;
  void release();
  void showFullScreenHint();
  void hideFullScreenHint();
  QRectF screenRect() const;
  QRect displayViewport() const;
  QSize desiredSize() const;
  void scheduleResize();
  static bool scaleNativeRelative(double &remX, double &remY, double dx,
                                  double dy, int dpi, double scale,
                                  bool legacyNative, int &outX, int &outY);
signals:
  void captureChanged(bool);
  void displayChanged();
  void toggleFullScreen();

protected:
  bool focusNextPrevChild(bool) override { return false; }
  void paintEvent(QPaintEvent *) override;
  void keyPressEvent(QKeyEvent *) override;
  void keyReleaseEvent(QKeyEvent *) override;
  void mousePressEvent(QMouseEvent *) override;
  void mouseReleaseEvent(QMouseEvent *) override;
  void mouseMoveEvent(QMouseEvent *) override;
  void enterEvent(QEnterEvent *) override;
  void leaveEvent(QEvent *) override;
  void wheelEvent(QWheelEvent *) override;
  void focusOutEvent(QFocusEvent *) override;
  void resizeEvent(QResizeEvent *) override;
  void showEvent(QShowEvent *) override;
  bool event(QEvent *) override;

private:
  QScrollBar *horizontalScroll, *verticalScroll;
  void updateScrollbars(bool force = false);
  void updateGuestCursor();
  void applyCursorAt(QPointF pos);
  void checkDprChange();
  void connectScreenSignal();
  void mouse(QMouseEvent *);
  QHash<quint64, QPair<quint32, quint32>> activeKeys;
  int mask = 0;
  bool controlClick = false;
  QTimer resizeDebounce, resizeDeadline, resizeSettled, fullScreenHintTimer;
  static constexpr int maxResizeAttempts = 60;
  QSize observedSize, pendingSize;
  QSize lastScrollFrameSize, lastScrollViewSize;
  qreal lastScrollDpr = 0, lastDpr = 0;
  bool lastScrollFit = false;
  int lastScrollZoom = -1;
  QPoint lastPointer, lastRelativePos;
  QCursor guestCursor;
  QMetaObject::Connection screenChangedConn;
  bool fullScreenHintVisible = false;
  bool rawInputActive = false;
  QAbstractNativeEventFilter *rawFilter = nullptr;
  int resizeAttempts = 0, wheelRemainder = 0;
  double relRemX = 0.0, relRemY = 0.0;
  bool resizeArmed = false, wantsResize = false, shownOnce = false;
  void requestResize();
  quint32 keySym(QKeyEvent *) const;
};
