#include "widgets.h"
#include <QStyledItemDelegate>
#ifdef Q_OS_WIN
#include <windows.h>
#endif
using namespace Platinum;
static void draw(QWidget *w, const QImage &image) {
  QPainter p(w);
  p.setRenderHint(QPainter::SmoothPixmapTransform, false);
  p.drawImage(0, 0, image);
}
static QStringList lines(const QString &s, int width, const QString &font) {
  QStringList result;
  for (auto paragraph : s.split('\n')) {
    QString row;
    for (auto word : paragraph.split(' ')) {
      if (!row.isEmpty() && textWidth(row + " " + word, font) > width) {
        result << row;
        row.clear();
      }
      row += (row.isEmpty() ? "" : " ") + word;
    }
    result << row;
  }
  return result;
}
PLabel::PLabel(QString s, QWidget *p) : QWidget(p), value(s) {
  setAccessibleName(s);
  setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Fixed);
}
QSize PLabel::sizeHint() const {
  return {textWidth(value, font), heightForWidth(width() > 0 ? width() : 400)};
}
int PLabel::heightForWidth(int w) const {
  return (wrap ? lines(value, w, font).size() : value.count('\n') + 1) *
         (font == "small" ? 13 : 16);
}
void PLabel::paintEvent(QPaintEvent *) {
  Pixels p(width(), height());
  auto rows = wrap ? lines(value, width(), font) : value.split('\n');
  int y = font == "small" ? 10 : 12;
  for (auto &row : rows) {
    text(p, elide(row, width(), font), 0, y, isEnabled() ? ink : 0xff888888,
         font);
    y += font == "small" ? 13 : 16;
  }
  draw(this, p.image);
}
PButton::PButton(const QString &s, QWidget *p, bool def)
    : QAbstractButton(p), defaultButton(def) {
  setText(s);
  setAccessibleName(s);
  setFocusPolicy(Qt::StrongFocus);
  setSizePolicy(QSizePolicy::Minimum, QSizePolicy::Fixed);
  connect(this, &QAbstractButton::pressed, this, qOverload<>(&QWidget::update));
  connect(this, &QAbstractButton::released, this,
          qOverload<>(&QWidget::update));
  connect(this, &QAbstractButton::toggled, this, qOverload<>(&QWidget::update));
}
QSize PButton::sizeHint() const {
  if (!widgetKind.isEmpty())
    return {13, 13};
  if (toggleButton)
    return {22 + textWidth(text()), 22};
  return {std::max(59, textWidth(text()) + 24) + (defaultButton ? 6 : 4),
          defaultButton ? 26 : 24};
}
void PButton::paintEvent(QPaintEvent *) {
  Pixels p(width(), height());
  QString state = !isEnabled() ? "disabled" : isDown() ? "pressed" : "normal";
  if (!widgetKind.isEmpty()) {
    titleBox(p, widgetKind, isDown());
  } else if (toggleButton) {
    if (hasFocus()) {
      if (radio) p.sprite(measured()["RADIO_FOCUSED"].toArray());
      else p.fill(0, 0, 16, 16, 0xff6666cc);
    }
    Pixels indicator(14, 12, 0);
    Platinum::toggle(indicator, radio, isChecked(), state);
    QPainter(&p.image).drawImage(2, 2, indicator.image);
    Platinum::text(p, text(), 20, 14, isEnabled() ? 0xff000000 : 0xff888888);
  } else {
    const int inset = defaultButton ? 0 : 2;
    if (hasFocus() && !defaultButton)
      p.stretch(measured()["BUTTON_FOCUS"].toArray(), 6);
    Pixels body(width() - inset * 2, height() - inset * 2, 0);
    Platinum::button(body, state, defaultButton);
    QPainter(&p.image).drawImage(inset, inset, body.image);
    Platinum::text(p, text(), (width() - textWidth(text())) / 2,
                   (height() - 16) / 2 + 12,
                   !isEnabled() ? 0xff888888
                   : isDown()   ? 0xffffffff
                                : 0xff000000);
  }
  draw(this, p.image);
}
PEdit::PEdit(const QString &s, QWidget *p) : QLineEdit(s, p) {
  setFixedHeight(22);
  setMaxLength(100);
  setFrame(false);
  setContextMenuPolicy(Qt::NoContextMenu);
  connect(this, &QLineEdit::textChanged, this, qOverload<>(&QWidget::update));
  connect(this, &QLineEdit::cursorPositionChanged, this,
          qOverload<>(&QWidget::update));
  connect(this, &QLineEdit::selectionChanged, this,
          qOverload<>(&QWidget::update));
  connect(this, &QLineEdit::cursorPositionChanged, this, &PEdit::resetCaret);
  connect(this, &QLineEdit::textChanged, this, &PEdit::resetCaret);
  connect(&caretTimer, &QTimer::timeout, this, [this] {
    caretVisible = !caretVisible;
    update();
  });
}
void PEdit::resetCaret() {
  caretVisible = true;
  const int interval = QApplication::cursorFlashTime();
  if (hasFocus() && interval > 0)
    caretTimer.start(std::max(1, interval / 2));
  update();
}
void PEdit::focusInEvent(QFocusEvent *e) {
  QLineEdit::focusInEvent(e);
  resetCaret();
}
void PEdit::focusOutEvent(QFocusEvent *e) {
  caretTimer.stop();
  QLineEdit::focusOutEvent(e);
  update();
}
QVariant PEdit::inputMethodQuery(Qt::InputMethodQuery query) const {
  if (query == Qt::ImCursorRectangle)
    return QRect(5 + textWidth(displayText().left(cursorPosition())) - scroll,
                 4, 1, 14);
  return QLineEdit::inputMethodQuery(query);
}
void PEdit::paintEvent(QPaintEvent *) {
  Pixels p(width(), height());
  sunken(p);
  p.fill(2, 2, width() - 4, height() - 4,
         isEnabled() ? 0xffffffff : 0xffeeeeee);
  if (hasFocus())
    p.stretch(measured()["FIELD_FOCUS"].toArray(), 3);
  int cur = textWidth(displayText().left(cursorPosition()));
  scroll =
      std::clamp(scroll, std::max(0, cur - width() + 12), std::max(0, cur));
  // A field's text and selection must never overwrite its frame.
  Pixels content(std::max(1, width() - 10), 16,
                 isEnabled() ? 0xffffffff : 0xffeeeeee);
  int x = -scroll;
  const int first = selectionStart(), last = first + int(selectedText().size());
  for (int i = 0; i < displayText().size(); ++i) {
    QString ch = displayText().mid(i, 1);
    int w = textWidth(ch);
    if (first >= 0 && i >= first && i < last)
      content.fill(x, 0, w, 16, 0xffccccff);
    Platinum::text(content, ch, x, 12,
                   isEnabled() ? 0xff000000 : 0xff888888);
    x += w;
  }
  if (hasFocus() && caretVisible && !hasSelectedText() && !isReadOnly())
    content.vertical(cur - scroll, 1, 14, 0xff000000);
  QPainter(&p.image).drawImage(5, 3, content.image);
  draw(this, p.image);
}
int PEdit::position(int x) const {
  int pos = 0, advance = 5 - scroll;
  while (pos < text().size()) {
    int w = textWidth(displayText().mid(pos, 1));
    if (x < advance + w / 2)
      break;
    advance += w;
    ++pos;
  }
  return pos;
}
void PEdit::mousePressEvent(QMouseEvent *e) {
  if (e->button() != Qt::LeftButton) {
    QLineEdit::mousePressEvent(e);
    return;
  }
  setFocus();
  const int pos = position(e->position().x());
  if (e->modifiers() & Qt::ShiftModifier) {
    if (!hasSelectedText()) anchor = cursorPosition();
    setSelection(anchor, pos - anchor);
  } else {
    anchor = pos;
    setCursorPosition(anchor);
  }
  resetCaret();
}
void PEdit::mouseMoveEvent(QMouseEvent *e) {
  if (e->buttons() & Qt::LeftButton)
    setSelection(anchor, position(e->position().x()) - anchor);
}
void PEdit::mouseDoubleClickEvent(QMouseEvent *e) {
  if (e->button() != Qt::LeftButton) return;
  int first = position(e->position().x()), last = first;
  while (first > 0 && !text()[first - 1].isSpace()) --first;
  while (last < text().size() && !text()[last].isSpace()) ++last;
  anchor = first;
  setSelection(first, last - first);
}
class BitmapDelegate : public QStyledItemDelegate {
public:
  using QStyledItemDelegate::QStyledItemDelegate;
  void paint(QPainter *q, const QStyleOptionViewItem &o,
             const QModelIndex &i) const override {
    Pixels p(o.rect.width(), o.rect.height(),
             o.state & QStyle::State_Selected ? 0xff6666cc : face);
    text(p, elide(i.data().toString(), p.w() - 16), 8, 14,
         o.state & QStyle::State_Selected ? 0xffffffff : 0xff000000);
    q->save();
    q->setRenderHint(QPainter::SmoothPixmapTransform, false);
    q->drawImage(o.rect.topLeft(), p.image);
    q->restore();
  }
  QSize sizeHint(const QStyleOptionViewItem &,
                 const QModelIndex &i) const override {
    return {textWidth(i.data().toString()) + 25, 20};
  }
};
PCombo::PCombo(QWidget *p) : QComboBox(p) {
  setFixedHeight(20);
  setItemDelegate(new BitmapDelegate(this));
  setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
}
QSize PCombo::sizeHint() const {
  int w = 70;
  for (int i = 0; i < count(); ++i)
    w = std::max(w, textWidth(itemText(i)) + 36);
  return {w, 20};
}
void PCombo::showPopup() {
  if (!isEnabled() || count() == 0 || menu) return;
  auto popupMenu = new QMenu(this);
  popupMenu->setWindowFlag(Qt::NoDropShadowWindowHint);
  menu = popupMenu;
  QAction *selected = nullptr;
  for (int i = 0; i < count(); ++i) {
    const auto index = model()->index(i, modelColumn(), rootModelIndex());
    if (index.data(Qt::AccessibleDescriptionRole).toString() == "separator") {
      popupMenu->addSeparator();
      continue;
    }
    auto action = popupMenu->addAction(itemText(i));
    action->setEnabled(index.flags() & Qt::ItemIsEnabled);
    action->setCheckable(true);
    action->setChecked(i == currentIndex());
    connect(action, &QAction::triggered, this, [this, i] {
      setCurrentIndex(i);
      emit activated(i);
    });
    if (i == currentIndex()) selected = action;
  }
  connect(popupMenu, &QMenu::aboutToHide, this, [this, popupMenu] {
    menu.clear();
    QComboBox::hidePopup();
    update();
    popupMenu->deleteLater();
  });
  popupMenu->ensurePolished();
  popupMenu->setMinimumWidth(width() + 11);
  // Menu text stays over the selected button text, as in the Menu Manager.
  const int row = selected ? popupMenu->actionGeometry(selected).top() : 3;
  popupMenu->popup(mapToGlobal(QPoint(-11, 2 - row)));
  popupMenu->setActiveAction(selected);
  update();
}
void PCombo::hidePopup() {
  if (menu) menu->hide();
  QComboBox::hidePopup();
}
void PCombo::paintEvent(QPaintEvent *) {
  Pixels p(width(), height());
  popup(p, !isEnabled() ? "disabled" : menu ? "pressed" : "normal");
  text(p, elide(currentText(), width() - 36), 9, 14,
       !isEnabled() ? 0xff888888 : menu ? 0xffffffff : 0xff000000);
  draw(this, p.image);
}
PGroup::PGroup(QString s, QWidget *p) : QWidget(p), title(s) {}
void PGroup::paintEvent(QPaintEvent *) {
  Pixels p(width(), height());
  group(p, textWidth(title));
  text(p, title, 11, 12);
  draw(this, p.image);
}
PProgress::PProgress(QWidget *parent) : QWidget(parent) {
  setFixedHeight(14);
  setAccessibleName("Progress");
  animation.setInterval(80);
  connect(&animation, &QTimer::timeout, this, [this] {
    phase = (phase + 1) % 16;
    update();
  });
}
void PProgress::setValue(double v) {
  value = v;
  if (value < 0 && isVisible()) animation.start();
  else animation.stop();
  update();
}
void PProgress::showEvent(QShowEvent *e) {
  QWidget::showEvent(e);
  if (value < 0) animation.start();
}
void PProgress::hideEvent(QHideEvent *e) {
  animation.stop();
  QWidget::hideEvent(e);
}
void PProgress::paintEvent(QPaintEvent *) {
  Pixels p(width(), height());
  progress(p, value, phase);
  draw(this, p.image);
}
class GrowOverlay : public QWidget {
  PWindow *owner;

public:
  explicit GrowOverlay(PWindow *window) : QWidget(window->body), owner(window) {
    setAttribute(Qt::WA_TransparentForMouseEvents);
    setAttribute(Qt::WA_NoSystemBackground);
  }
  void paintEvent(QPaintEvent *) override {
    if (!owner->growable || owner->shaded)
      return;
    Pixels pixels(17, 17);
    pixels.sprite(measured()[owner->isActiveWindow() ? "WINDOW_GROW"
                                                     : "WINDOW_GROW_INACTIVE"]
                      .toArray());
    QPainter(this).drawImage(0, 0, pixels.image);
  }
};
PWindow::PWindow(QString s, QSize size, QWidget *parent)
    : QWidget(parent, Qt::Window | Qt::FramelessWindowHint), title(s) {
  setWindowTitle(s);
  setAttribute(Qt::WA_OpaquePaintEvent);
  body = new QWidget(this);
  growOverlay = new GrowOverlay(this);
  body->setAutoFillBackground(true);
  auto palette = body->palette();
  palette.setColor(QPalette::Window, QColor(0xdd, 0xdd, 0xdd));
  body->setPalette(palette);
  resize(size);
  setMinimumSize(300, 150);
  setFocusPolicy(Qt::StrongFocus);
  setSizeIncrement(1, 1);
}
void PWindow::paintEvent(QPaintEvent *) {
  if (contentOnlyFullScreen && isFullScreen()) return;
  Pixels p(width(), height());
  bool active = isActiveWindow();
  const auto caption = elide(title, std::max(0, width() - (dialogFrame ? 24 : 88)));
  if (dialogFrame) movableDialog(p, textWidth(caption), active);
  else windowFrame(p, active, shaded, textWidth(caption), true, growable);
  text(p, caption, (width() - 1 - textWidth(caption)) / 2, 16,
       active ? 0xff000000 : 0xff888888);
  if (!held.isEmpty() && heldInside)
    titleBox(p, held, true,
             held == "close" ? 4 : width() - 1 - (held == "zoom" ? 33 : 17), 4);
  draw(this, p.image);
}
void PWindow::resizeEvent(QResizeEvent *) {
  if (contentOnlyFullScreen && isFullScreen()) {
    body->setGeometry(rect());
    growOverlay->hide();
    return;
  }
  body->setGeometry(dialogFrame ? 3 : 6, dialogFrame ? 24 : 22,
                    width() - (dialogFrame ? 7 : 13),
                    std::max(0, height() - (dialogFrame ? 28 : 29)));
  growOverlay->setGeometry(body->width() - 15, body->height() - 15, 15, 15);
  growOverlay->setVisible(growable && !shaded && !(contentOnlyFullScreen && isFullScreen()));
  growOverlay->raise();
  QTimer::singleShot(0, this, &PWindow::fitToScreen);
}
void PWindow::fitToScreen() {
  if (isFullScreen() || isMaximized() || shaded || !screen())
    return;
  const auto workArea = screen()->availableGeometry();
  if (workArea.contains(frameGeometry()))
    return;
  auto available = workArea.adjusted(4, 4, -4, -4);
  if (available.isEmpty())
    return;
  resize(size().boundedTo(available.size()).expandedTo(minimumSize()));
  move(std::clamp(pos().x(), available.left(),
                  std::max(available.left(), available.right() - width() + 1)),
       std::clamp(pos().y(), available.top(),
                  std::max(available.top(), available.bottom() - height() + 1)));
}
void PWindow::showEvent(QShowEvent *e) {
  QWidget::showEvent(e);
  growOverlay->setVisible(growable && !shaded && !(contentOnlyFullScreen && isFullScreen()));
  growOverlay->raise();
  if (!screenConnected && windowHandle()) {
    screenConnected = true;
    connect(windowHandle(), &QWindow::screenChanged, this, [this] {
      followWorkArea();
      QTimer::singleShot(0, this, &PWindow::fitToScreen);
    });
    followWorkArea();
  }
  QTimer::singleShot(0, this, &PWindow::fitToScreen);
}
void PWindow::followWorkArea() {
  disconnect(workAreaConnection);
  if (screen())
    workAreaConnection = connect(screen(), &QScreen::availableGeometryChanged, this, [this] {
      QTimer::singleShot(0, this, &PWindow::fitToScreen);
    });
}
bool PWindow::event(QEvent *e) {
#if QT_VERSION >= QT_VERSION_CHECK(6, 6, 0)
  if (e->type() == QEvent::DevicePixelRatioChange)
    QTimer::singleShot(0, this, &PWindow::fitToScreen);
#endif
  return QWidget::event(e);
}
#ifdef Q_OS_WIN
bool PWindow::nativeEvent(const QByteArray &type, void *message, qintptr *result) {
  auto msg = static_cast<MSG *>(message);
  const qreal dpr = devicePixelRatioF();
  const int zoom = qRound(dpr);
  if (msg->message == WM_SIZING && zoom > 1 && qAbs(dpr - zoom) < 0.001) {
    auto r = reinterpret_cast<RECT *>(msg->lParam);
    RECT outer{}, client{};
    GetWindowRect(msg->hwnd, &outer);
    GetClientRect(msg->hwnd, &client);
    const int borderW = outer.right - outer.left - client.right;
    const int borderH = outer.bottom - outer.top - client.bottom;
    const int width = qRound(qreal(r->right - r->left - borderW) / zoom) * zoom + borderW;
    const int height = qRound(qreal(r->bottom - r->top - borderH) / zoom) * zoom + borderH;
    const bool left = msg->wParam == WMSZ_LEFT || msg->wParam == WMSZ_TOPLEFT || msg->wParam == WMSZ_BOTTOMLEFT;
    const bool top = msg->wParam == WMSZ_TOP || msg->wParam == WMSZ_TOPLEFT || msg->wParam == WMSZ_TOPRIGHT;
    if (left) r->left = r->right - width;
    else r->right = r->left + width;
    if (top) r->top = r->bottom - height;
    else r->bottom = r->top + height;
    *result = TRUE;
    return true;
  }
  return QWidget::nativeEvent(type, message, result);
}
#endif
void PWindow::changeEvent(QEvent *e) {
  if (e->type() == QEvent::WindowStateChange) resizeEvent(nullptr);
  if (e->type() == QEvent::ActivationChange) {
    update();
    body->update();
    growOverlay->update();
  }
  QWidget::changeEvent(e);
}
QString PWindow::hit(QPoint p) const {
  if (dialogFrame) return {};
  if (p.y() >= 4 && p.y() < 17) {
    if (p.x() >= 4 && p.x() < 17)
      return "close";
    if (growable && p.x() >= width() - 34 && p.x() < width() - 21)
      return "zoom";
    if (allowShade && p.x() >= width() - 18 && p.x() < width() - 5)
      return "collapse";
  }
  return {};
}
void PWindow::shade() {
  if (!allowShade)
    return;
  if (shaded) {
    shaded = false;
    body->show();
    setMinimumSize(expandedMinimum);
    resize(expanded);
    QTimer::singleShot(0, this, &PWindow::fitToScreen);
  } else {
    if (isMaximized() || isFullScreen())
      showNormal();
    expanded = size();
    expandedMinimum = minimumSize();
    shaded = true;
    body->hide();
    setMinimumHeight(23);
    resize(width(), 23);
  }
  update();
}
void PWindow::mousePressEvent(QMouseEvent *e) {
  if (e->button() != Qt::LeftButton)
    return;
  held = hit(e->position().toPoint());
  heldInside = !held.isEmpty();
  drag = e->globalPosition().toPoint();
  origin = pos();
  startSize = size();
  resizing = growable && !shaded && e->position().x() > width() - 23 &&
             e->position().y() > height() - 23;
  if (held.isEmpty() && windowHandle()) {
    bool moved =
        resizing
            ? windowHandle()->startSystemResize(Qt::BottomEdge | Qt::RightEdge)
            : e->position().y() < 21 && windowHandle()->startSystemMove();
    if (moved) {
      drag = {};
      return;
    }
  }
  if (!held.isEmpty())
    update();
  else if (e->position().y() < 21 || resizing)
    grabMouse();
  else
    drag = {};
}
void PWindow::mouseMoveEvent(QMouseEvent *e) {
  if (!held.isEmpty()) {
    heldInside = hit(e->position().toPoint()) == held;
    update();
    return;
  }
  if (drag.isNull() || !held.isEmpty() || !(e->buttons() & Qt::LeftButton))
    return;
  auto delta = e->globalPosition().toPoint() - drag;
  if (resizing)
    resize(startSize + QSize(delta.x(), delta.y()));
  else
    move(origin + delta);
}
void PWindow::mouseReleaseEvent(QMouseEvent *e) {
  if (e->button() != Qt::LeftButton) return;
  releaseMouse();
  auto a = held;
  held.clear();
  heldInside = false;
  drag = {};
  if (a == hit(e->position().toPoint())) {
    if (a == "close")
      close();
    else if (a == "collapse")
      shade();
    else if (a == "zoom") {
      if (isMaximized())
        showNormal();
      else
        showMaximized();
    }
  }
  update();
}
void PWindow::mouseDoubleClickEvent(QMouseEvent *e) {
  if (e->position().y() < 21 && hit(e->position().toPoint()).isEmpty())
    shade();
}
void PWindow::closeEvent(QCloseEvent *e) {
  if (canClose && !canClose()) {
    e->ignore();
    return;
  }
  e->accept();
  emit closed();
}
void PWindow::keyPressEvent(QKeyEvent *e) {
  if (dialogFrame && (e->key() == Qt::Key_Escape ||
      (e->key() == Qt::Key_Period && e->modifiers() & Qt::ControlModifier))) {
    close();
    return;
  }
  if (e->key() == Qt::Key_Return || e->key() == Qt::Key_Enter) {
    for (auto b : body->findChildren<PButton *>())
      if (b->defaultButton && b->isVisible() && b->isEnabled()) {
        b->animateClick();
        return;
      }
  }
  QWidget::keyPressEvent(e);
}
PDialog::PDialog(QString s, QSize size, QWidget *p) : PWindow(s, size, p) {
  allowShade = false;
  growable = false;
  dialogFrame = true;
  setWindowModality(Qt::WindowModal);
  connect(this, &PWindow::closed, &loop, &QEventLoop::quit);
}
int PDialog::exec() {
  if (auto parent = qobject_cast<PWindow *>(parentWidget());
      parent && parent->shaded)
    parent->shade();
  if (parentWidget())
    move(parentWidget()->geometry().center() -
         QPoint(width() / 2, height() / 2));
  show();
  raise();
  activateWindow();
  loop.exec();
  hide();
  return result;
}
void PDialog::done(int code) {
  result = code;
  close();
}
PFlowLayout::PFlowLayout(QWidget *parent) : QLayout(parent) {
  setContentsMargins(8, 0, 24, 6);
  setSpacing(6);
}
PFlowLayout::~PFlowLayout() { while (auto item = takeAt(0)) delete item; }
void PFlowLayout::addItem(QLayoutItem *item) { items.append(item); }
QLayoutItem *PFlowLayout::takeAt(int i) {
  return i >= 0 && i < items.size() ? items.takeAt(i) : nullptr;
}
QSize PFlowLayout::minimumSize() const {
  QSize s;
  for (auto item : items) s = s.expandedTo(item->minimumSize());
  const auto m = contentsMargins();
  return s + QSize(m.left() + m.right(), m.top() + m.bottom());
}
int PFlowLayout::arrange(const QRect &rect, bool apply) const {
  const auto m = contentsMargins();
  const auto area = rect.adjusted(m.left(), m.top(), -m.right(), -m.bottom());
  int x = area.left(), y = area.top(), rowHeight = 0;
  for (auto item : items) {
    if (item->isEmpty()) continue;
    const auto size = item->sizeHint();
    if (x > area.left() && x + size.width() > area.right() + 1) {
      x = area.left();
      y += rowHeight + spacing();
      rowHeight = 0;
    }
    if (apply) item->setGeometry(QRect(QPoint(x, y), size));
    x += size.width() + spacing();
    rowHeight = std::max(rowHeight, size.height());
  }
  return y - rect.top() + rowHeight + m.bottom();
}
int PFlowLayout::heightForWidth(int width) const {
  return arrange(QRect(0, 0, width, 0), false);
}
void PFlowLayout::setGeometry(const QRect &r) {
  QLayout::setGeometry(r);
  arrange(r, true);
}
PLabel *label(QString s, QWidget *p, QString font, bool wrap) {
  auto w = new PLabel(s, p);
  w->font = font;
  w->wrap = wrap;
  w->setSizePolicy(QSizePolicy::Preferred,
                   wrap ? QSizePolicy::Minimum : QSizePolicy::Fixed);
  return w;
}
PButton *button(QString s, std::function<void()> action, QWidget *p, bool def) {
  auto b = new PButton(s, p, def);
  QObject::connect(b, &PButton::clicked, b, [action] {
    try {
      action();
    } catch (const std::exception &e) {
      problem(QApplication::activeWindow(), QString::fromUtf8(e.what()));
    }
  });
  return b;
}
QHBoxLayout *buttonRow(QVBoxLayout *v, const QList<QWidget *> &widgets) {
  auto h = new QHBoxLayout;
  h->setSpacing(12);
  h->addStretch();
  for (auto w : widgets)
    h->addWidget(w);
  auto footer = qobject_cast<QVBoxLayout *>(v->property("dialogFooter").value<QObject *>());
  (footer ? footer : v)->addLayout(h);
  return h;
}
QVBoxLayout *dialogLayout(PWindow *d) {
  auto outer = new QVBoxLayout(d->body);
  outer->setContentsMargins(12, 12, 12, 12);
  outer->setSpacing(12);
  auto area = new QScrollArea(d->body);
  area->setFrameShape(QFrame::NoFrame);
  area->setWidgetResizable(true);
  area->setAutoFillBackground(false);
  area->viewport()->setAutoFillBackground(false);
  auto content = new QWidget;
  auto v = new QVBoxLayout(content);
  v->setContentsMargins(0, 0, 0, 0);
  v->setSpacing(12);
  v->setProperty("dialogFooter", QVariant::fromValue<QObject *>(outer));
  area->setWidget(content);
  outer->addWidget(area, 1);
  return v;
}
bool question(QWidget *p, const QString &title, const QString &message,
              QString accept) {
  PDialog d(title, {450, 210}, p);
  auto v = dialogLayout(&d);
  v->addWidget(label(message, &d, "regular", true), 1);
  buttonRow(v, {button(
                    "Cancel", [&] { d.done(0); }, &d),
                button(accept, [&] { d.done(1); }, &d, true)});
  return d.exec() == 1;
}
void problem(QWidget *p, const QString &message) {
  PDialog d("ClassicMac", {480, 240}, p);
  auto v = dialogLayout(&d);
  auto area = new QScrollArea(&d);
  area->setWidgetResizable(true);
  area->setFrameShape(QFrame::NoFrame);
  area->setWidget(label(message, &d, "regular", true));
  v->addWidget(area, 1);
  buttonRow(v, {button("OK", [&] { d.done(); }, &d, true)});
  d.exec();
}
class PStyle : public QProxyStyle {
public:
  PStyle() : QProxyStyle("Fusion") {}
  void drawControl(ControlElement ce, const QStyleOption *o, QPainter *q,
                   const QWidget *w = nullptr) const override {
    if (ce == CE_MenuItem) {
      auto item = qstyleoption_cast<const QStyleOptionMenuItem *>(o);
      if (!item) return;
      const bool selected = item->state & State_Selected &&
          item->menuItemType != QStyleOptionMenuItem::Separator;
      const bool enabled = item->state & State_Enabled;
      const auto bounds = item->rect.adjusted(-1, 0, 0, 0);
      Pixels p(bounds.width(), bounds.height(),
               selected && enabled ? 0xff333399 : face);
      if (item->menuItemType == QStyleOptionMenuItem::Separator) {
        p.vertical(0, 0, p.h(), 0xffffffff);
        p.vertical(p.w() - 1, 0, p.h(), 0xff999999);
        p.line(0, 2, p.w(), 0xff888888);
        p.line(0, 3, p.w(), 0xffffffff);
      } else {
        p.vertical(0, 0, p.h(), selected && enabled ? 0xff6666cc : 0xffffffff);
        p.vertical(p.w() - 1, 0, p.h(), selected && enabled ? 0xff000088 : 0xff999999);
        const auto ink = !enabled ? 0xff888888 : selected ? 0xffffffff : 0xff000000;
        if (item->checked) {
          const QStringList mark = {".......##", "......##.", "......##.",
              ".....##..", "##..##...", ".####....", "..##.....", "..#......"};
          for (int y = 0; y < mark.size(); ++y)
            for (int x = 0; x < mark[y].size(); ++x)
              if (mark[y][x] == '#') p.pixel(5 + x, 4 + y, ink);
        }
        text(p, elide(item->text.section('\t', 0, 0), p.w() - 37), 19, 12, ink);
      }
      q->save();
      q->setRenderHint(QPainter::SmoothPixmapTransform, false);
      q->drawImage(bounds.topLeft(), p.image);
      q->restore();
      return;
    }
    if (ce == CE_MenuEmptyArea) return;
    if (ce == CE_SizeGrip) {
      Pixels p(o->rect.width(), o->rect.height());
      p.sprite(measured()[o->state & State_Active ? "WINDOW_GROW"
                                                : "WINDOW_GROW_INACTIVE"].toArray());
      q->save();
      q->setRenderHint(QPainter::SmoothPixmapTransform, false);
      q->drawImage(o->rect.topLeft(), p.image);
      q->restore();
      return;
    }
    if (ce == CE_ShapedFrame && (qobject_cast<const QListWidget *>(w) ||
                                qobject_cast<const QPlainTextEdit *>(w))) {
      Pixels p(o->rect.width(), o->rect.height(), 0);
      sunken(p);
      q->save();
      q->setRenderHint(QPainter::SmoothPixmapTransform, false);
      q->drawImage(o->rect.topLeft(), p.image);
      q->restore();
      return;
    }
    QProxyStyle::drawControl(ce, o, q, w);
  }
  int pixelMetric(PixelMetric m, const QStyleOption *o = nullptr,
                  const QWidget *w = nullptr) const override {
    if (m == PM_MenuPanelWidth) return 1;
    if (m == PM_MenuHMargin) return 1;
    if (m == PM_MenuVMargin) return 2;
    if (m == PM_MenuDesktopFrameWidth) return 0;
    if (m == PM_ScrollBarExtent)
      return 16;
    if (m == PM_ScrollBarSliderMin)
      return 17;
    if (m == PM_DefaultFrameWidth)
      return 1;
    return QProxyStyle::pixelMetric(m, o, w);
  }
  QSize sizeFromContents(ContentsType ct, const QStyleOption *o, const QSize &s,
                         const QWidget *w = nullptr) const override {
    if (ct == CT_MenuItem) {
      auto item = qstyleoption_cast<const QStyleOptionMenuItem *>(o);
      if (item)
        return {textWidth(item->text.section('\t', 0, 0)) + 38,
                 item->menuItemType == QStyleOptionMenuItem::Separator ? 6 : 16};
    }
    return QProxyStyle::sizeFromContents(ct, o, s, w);
  }
  int styleHint(StyleHint hint, const QStyleOption *o = nullptr,
                 const QWidget *w = nullptr, QStyleHintReturn *data = nullptr) const override {
    if (hint == SH_Menu_Scrollable) return true;
    if (hint == SH_Menu_AllowActiveAndDisabled) return false;
    if (hint == SH_Menu_SubMenuPopupDelay) return 200;
    return QProxyStyle::styleHint(hint, o, w, data);
  }
  QRect subControlRect(ComplexControl cc, const QStyleOptionComplex *o,
                       SubControl sc,
                       const QWidget *widget = nullptr) const override {
    if (cc != CC_ScrollBar)
      return QProxyStyle::subControlRect(cc, o, sc, widget);
    auto s = qstyleoption_cast<const QStyleOptionSlider *>(o);
    if (!s)
      return {};
    bool vertical = s->orientation == Qt::Vertical;
    int length = vertical ? o->rect.height() : o->rect.width();
    int pos = (s->maximum == s->minimum
                        ? 0
                        : QStyle::sliderPositionFromValue(
                              s->minimum, s->maximum, s->sliderPosition,
                              std::max(0, length - 48), s->upsideDown));
    auto rect = [&](int at, int n) {
      return vertical ? QRect(o->rect.x(), o->rect.y() + at, 16, n)
                      : QRect(o->rect.x() + at, o->rect.y(), n, 16);
    };
    switch (sc) {
    case SC_ScrollBarSubLine:
      return rect(std::max(0, length - 31), 15);
    case SC_ScrollBarAddLine:
      return rect(std::max(0, length - 16), 15);
    case SC_ScrollBarGroove:
      return rect(0, std::max(0, length - 31));
    case SC_ScrollBarSlider:
      return rect(pos, 17);
    case SC_ScrollBarSubPage:
      return rect(0, pos);
    case SC_ScrollBarAddPage:
      return rect(pos + 17, std::max(0, length - pos - 48));
    default:
      return {};
    }
  }
  SubControl hitTestComplexControl(ComplexControl cc,
                                   const QStyleOptionComplex *o,
                                   const QPoint &point,
                                   const QWidget *w = nullptr) const override {
    if (cc != CC_ScrollBar)
      return QProxyStyle::hitTestComplexControl(cc, o, point, w);
    for (auto sc :
         {SC_ScrollBarSubLine, SC_ScrollBarAddLine, SC_ScrollBarSlider,
          SC_ScrollBarSubPage, SC_ScrollBarAddPage})
      if (subControlRect(cc, o, sc, w).contains(point))
        return sc;
    return SC_None;
  }
  void drawComplexControl(ComplexControl cc, const QStyleOptionComplex *o,
                          QPainter *q,
                          const QWidget *w = nullptr) const override {
    if (cc != CC_ScrollBar) {
      QProxyStyle::drawComplexControl(cc, o, q, w);
      return;
    }
    auto s = qstyleoption_cast<const QStyleOptionSlider *>(o);
    if (!s)
      return;
    bool vertical = s->orientation == Qt::Vertical;
    Pixels p(16, vertical ? o->rect.height() : o->rect.width());
    auto slider = subControlRect(cc, o, SC_ScrollBarSlider, w);
    int pos = vertical ? slider.y() - o->rect.y() : slider.x() - o->rect.x();
    int pressed = (o->state & State_Sunken)
                      ? (o->activeSubControls & SC_ScrollBarSubLine   ? -1
                         : o->activeSubControls & SC_ScrollBarAddLine ? 1
                                                                      : 0)
                      : 0;
    scrollBar(p, s->maximum > s->minimum && o->state & State_Enabled ? pos : -1, pressed);
    q->save();
    q->setRenderHint(QPainter::SmoothPixmapTransform, false);
    q->drawImage(o->rect.topLeft(),
                 vertical ? p.image
                          : p.image.transformed(QTransform(0, 1, 1, 0, 0, 0)));
    q->restore();
  }
  void drawPrimitive(PrimitiveElement pe, const QStyleOption *o, QPainter *p,
                     const QWidget *w = nullptr) const override {
    if (pe == PE_FrameMenu) return; // The measured panel includes its frame.
    if (pe == PE_PanelMenu) {
      Pixels panel(o->rect.width(), o->rect.height());
      menuFrame(panel);
      p->save();
      p->setRenderHint(QPainter::SmoothPixmapTransform, false);
      p->drawImage(o->rect.topLeft(), panel.image);
      p->restore();
      return;
    }
    if (pe == PE_PanelScrollAreaCorner) {
      p->fillRect(o->rect, QColor(face));
      return;
    }
    if (pe == PE_PanelItemViewItem ||
        pe == PE_FrameFocusRect) {
      p->fillRect(o->rect,
                  QColor(pe == PE_FrameFocusRect ? 0xccccff : 0xdddddd));
      return;
    }
    QProxyStyle::drawPrimitive(pe, o, p, w);
  }
};
void installPlatinumStyle() {
  QFontDatabase::addApplicationFont(":/RetroBoot-Regular.ttf");
  QFontDatabase::addApplicationFont(":/RetroBoot-System.ttf");
  qApp->setStyle(new PStyle);
  QPalette p;
  p.setColor(QPalette::Window, QColor(0xdddddd));
  p.setColor(QPalette::Base, Qt::white);
  p.setColor(QPalette::Text, Qt::black);
  p.setColor(QPalette::Highlight, QColor(0xccccff));
  p.setColor(QPalette::HighlightedText, Qt::black);
  qApp->setPalette(p);
}
