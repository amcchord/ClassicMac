#pragma once
#include "platinum.h"
#include <QtWidgets>
#include <functional>

// Native widgets retain Qt's focus, accessibility, editing and input-method
// services. Their visible surfaces are drawn only by the shared bitmap engine.
class PLabel : public QWidget {
  Q_OBJECT
public:
  QString value, font = "system";
  QRgb ink = 0xff000000;
  bool wrap = false;
  explicit PLabel(QString text = {}, QWidget *parent = nullptr);
  void setText(const QString &s) {
    value = s;
    setAccessibleName(s);
    updateGeometry();
    update();
  }
  QSize sizeHint() const override;
  bool hasHeightForWidth() const override { return wrap; }
  int heightForWidth(int w) const override;

protected:
  void paintEvent(QPaintEvent *) override;
};
class PButton : public QAbstractButton {
  Q_OBJECT
public:
  bool defaultButton = false, radio = false, toggleButton = false;
  QString widgetKind;
  PButton(const QString &, QWidget *parent = nullptr, bool def = false);
  QSize sizeHint() const override;

protected:
  void paintEvent(QPaintEvent *) override;
};
class PEdit : public QLineEdit {
  Q_OBJECT
public:
  explicit PEdit(const QString &s = {}, QWidget *parent = nullptr);

protected:
  void paintEvent(QPaintEvent *) override;
  void mousePressEvent(QMouseEvent *) override;
  void mouseMoveEvent(QMouseEvent *) override;

private:
  int scroll = 0, anchor = 0;
  int position(int x) const;
};
class PCombo : public QComboBox {
  Q_OBJECT
public:
  explicit PCombo(QWidget *parent = nullptr);
  QSize sizeHint() const override;

protected:
  void paintEvent(QPaintEvent *) override;
};
class PGroup : public QWidget {
  Q_OBJECT
public:
  QString title;
  explicit PGroup(QString title = {}, QWidget *parent = nullptr);

protected:
  void paintEvent(QPaintEvent *) override;
};
class PProgress : public QWidget {
  Q_OBJECT
public:
  double value = 0;
  explicit PProgress(QWidget *parent = nullptr) : QWidget(parent) {
    setFixedHeight(14);
    setAccessibleName("Progress");
  }
  void setValue(double v) {
    value = v;
    update();
  }

protected:
  void paintEvent(QPaintEvent *) override;
};
class PWindow : public QWidget {
  Q_OBJECT
public:
  QWidget *body;
  QString title;
  bool shaded = false, allowShade = true, growable = true;
  QSize expanded;
  QSize expandedMinimum;
  std::function<bool()> canClose;
  explicit PWindow(QString title, QSize size, QWidget *parent = nullptr);
  void shade();
  void setTitle(const QString &s) {
    title = s;
    setWindowTitle(s);
    update();
  }
signals:
  void closed();

protected:
  void paintEvent(QPaintEvent *) override;
  void resizeEvent(QResizeEvent *) override;
  void changeEvent(QEvent *) override;
  void mousePressEvent(QMouseEvent *) override;
  void mouseMoveEvent(QMouseEvent *) override;
  void mouseReleaseEvent(QMouseEvent *) override;
  void mouseDoubleClickEvent(QMouseEvent *) override;
  void closeEvent(QCloseEvent *) override;
  void keyPressEvent(QKeyEvent *) override;

private:
  QString held;
  QPoint drag, origin;
  QSize startSize;
  bool resizing = false;
  QString hit(QPoint) const;
};
class PDialog : public PWindow {
  Q_OBJECT
public:
  explicit PDialog(QString title, QSize size, QWidget *parent);
  int exec();
  void done(int code = 1);

private:
  QEventLoop loop;
  int result = 0;
};
PLabel *label(QString text, QWidget *parent = nullptr, QString font = "system",
              bool wrap = false);
PButton *button(QString text, std::function<void()> action,
                QWidget *parent = nullptr, bool def = false);
QHBoxLayout *buttonRow(QVBoxLayout *, const QList<QWidget *> &);
QVBoxLayout *dialogLayout(PWindow *);
bool question(QWidget *, const QString &, const QString &,
              QString accept = "OK");
void problem(QWidget *, const QString &);
void installPlatinumStyle();
