#pragma once
#include <QImage>
#include <QWidget>
#include <QtNetwork>
class VNC : public QObject {
  Q_OBJECT
public:
  QImage frame;
  bool relative = false, ready = false;
  bool desktopResizeSupported = false;
  int desktopResizeResult = -1;
  QSize lastRequestedSize;
  explicit VNC(QObject *parent = nullptr);
  void open(int port, const QByteArray &password);
  void close();
  void key(quint32 keysym, bool down);
  void pointer(int x, int y, int mask);
  bool resizeDesktop(QSize);
  static QByteArray authenticate(QByteArray challenge, QByteArray password);
signals:
  void changed();
  void problem(QString);

private:
  QTcpSocket socket;
  QByteArray buffer, password;
  int state = 0, rects = -1;
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
  bool followsWindow = false, fitWindow = false;
  bool paused = false;
  int widthAlignment = 1;
  int pixelZoom = 0;
  QString resizeNotice;
  QString notice = "Starting up…";
  explicit GuestView(VNC *, QWidget *parent = nullptr);
  ~GuestView() override;
  void release();
  QRectF screenRect() const;
  QSize desiredSize() const;
  void scheduleResize();
signals:
  void captureChanged(bool);
  void displayChanged();
  void toggleFullScreen();

protected:
  void paintEvent(QPaintEvent *) override;
  void keyPressEvent(QKeyEvent *) override;
  void keyReleaseEvent(QKeyEvent *) override;
  void mousePressEvent(QMouseEvent *) override;
  void mouseReleaseEvent(QMouseEvent *) override;
  void mouseMoveEvent(QMouseEvent *) override;
  void wheelEvent(QWheelEvent *) override;
  void focusOutEvent(QFocusEvent *) override;
  void resizeEvent(QResizeEvent *) override;
  void showEvent(QShowEvent *) override;
  bool event(QEvent *) override;

private:
  void mouse(QMouseEvent *);
  QSet<quint32> keys;
  int mask = 0;
  bool controlClick = false;
  QTimer resizeDebounce, resizeDeadline;
  QSize observedSize, pendingSize;
  QPoint lastPointer;
  int resizeAttempts = 0, wheelRemainder = 0;
  void requestResize();
  quint32 keySym(QKeyEvent *) const;
};
