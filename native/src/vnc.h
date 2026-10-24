#pragma once
#include <QImage>
#include <QWidget>
#include <QtNetwork>
class VNC : public QObject {
  Q_OBJECT
public:
  QImage frame;
  bool relative = false, ready = false;
  explicit VNC(QObject *parent = nullptr);
  void open(int port, const QByteArray &password);
  void close();
  void key(quint32 keysym, bool down);
  void pointer(int x, int y, int mask);
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
  QString notice = "Starting up…";
  explicit GuestView(VNC *, QWidget *parent = nullptr);
  ~GuestView() override;
  void release();
signals:
  void captureChanged(bool);

protected:
  void paintEvent(QPaintEvent *) override;
  void keyPressEvent(QKeyEvent *) override;
  void keyReleaseEvent(QKeyEvent *) override;
  void mousePressEvent(QMouseEvent *) override;
  void mouseReleaseEvent(QMouseEvent *) override;
  void mouseMoveEvent(QMouseEvent *) override;
  void wheelEvent(QWheelEvent *) override;
  void focusOutEvent(QFocusEvent *) override;

private:
  QRect screenRect() const;
  void mouse(QMouseEvent *);
  QSet<quint32> keys;
  int mask = 0;
  bool controlClick = false;
  quint32 keySym(QKeyEvent *) const;
};
