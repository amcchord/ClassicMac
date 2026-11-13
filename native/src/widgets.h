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
  void mouseDoubleClickEvent(QMouseEvent *) override;
  void focusInEvent(QFocusEvent *) override;
  void focusOutEvent(QFocusEvent *) override;
  QVariant inputMethodQuery(Qt::InputMethodQuery) const override;

private:
  int scroll = 0, anchor = 0;
  QTimer caretTimer;
  bool caretVisible = true;
  void resetCaret();
  int position(int x) const;
};
class PCombo : public QComboBox {
  Q_OBJECT
public:
  explicit PCombo(QWidget *parent = nullptr);
  QSize sizeHint() const override;
  void showPopup() override;
  void hidePopup() override;

protected:
  void paintEvent(QPaintEvent *) override;

private:
  QPointer<QMenu> menu;
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
  explicit PProgress(QWidget *parent = nullptr);
  void setValue(double v);

protected:
  void paintEvent(QPaintEvent *) override;
  void showEvent(QShowEvent *) override;
  void hideEvent(QHideEvent *) override;

private:
  QTimer animation;
  int phase = 0;
};
class PWindow : public QWidget {
  Q_OBJECT
public:
  QWidget *body;
  QString title;
  bool shaded = false, allowShade = true, growable = true, dialogFrame = false;
  bool contentOnlyFullScreen = false;
  QSize expanded;
  QSize expandedMinimum;
  std::function<bool()> canClose;
  explicit PWindow(QString title, QSize size, QWidget *parent = nullptr);
  void shade();
  void fitToScreen();
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
  void showEvent(QShowEvent *) override;
  bool event(QEvent *) override;
#ifdef Q_OS_WIN
  bool nativeEvent(const QByteArray &, void *, qintptr *) override;
#endif
  void changeEvent(QEvent *) override;
  void mousePressEvent(QMouseEvent *) override;
  void mouseMoveEvent(QMouseEvent *) override;
  void mouseReleaseEvent(QMouseEvent *) override;
  void mouseDoubleClickEvent(QMouseEvent *) override;
  void closeEvent(QCloseEvent *) override;
  void keyPressEvent(QKeyEvent *) override;

private:
  QString held;
  bool heldInside = false;
  QWidget *growOverlay;
  QPoint drag, origin;
  QSize startSize;
  bool resizing = false;
  bool screenConnected = false;
  QMetaObject::Connection workAreaConnection;
  void followWorkArea();
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
// A toolbar keeps every action accessible when the screen window narrows.
class PFlowLayout : public QLayout {
public:
  explicit PFlowLayout(QWidget *parent);
  ~PFlowLayout() override;
  void addItem(QLayoutItem *) override;
  int count() const override { return items.size(); }
  QLayoutItem *itemAt(int i) const override { return items.value(i); }
  QLayoutItem *takeAt(int i) override;
  bool hasHeightForWidth() const override { return true; }
  int heightForWidth(int width) const override;
  QSize sizeHint() const override { return minimumSize(); }
  QSize minimumSize() const override;
  void setGeometry(const QRect &) override;
private:
  QList<QLayoutItem *> items;
  int arrange(const QRect &, bool apply) const;
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
