#include "calibration-dialog.hpp"

#include <algorithm>
#include <functional>

#include <QDialogButtonBox>
#include <QImage>
#include <QLabel>
#include <QMouseEvent>
#include <QPainter>
#include <QPushButton>
#include <QVBoxLayout>

namespace quickscore {

class CalibrationCanvas final : public QWidget {
public:
  explicit CalibrationCanvas(QImage image, QWidget *parent = nullptr)
      : QWidget(parent), image_(std::move(image))
  {
    setMinimumSize(720, 405);
    setCursor(Qt::CrossCursor);
  }

  std::function<void(QPointF)> selected;
  std::vector<QPointF> points;

protected:
  void paintEvent(QPaintEvent *) override
  {
    QPainter painter(this);
    painter.fillRect(rect(), QColor(20, 20, 20));
    const QRectF target = imageRect();
    painter.drawImage(target, image_);
    const QColor colors[]{QColor("#ff5252"), QColor("#40c4ff"), QColor("#69f0ae")};
    for (size_t i = 0; i < points.size(); ++i) {
      const QPointF p(target.left() + points[i].x() * target.width(),
                      target.top() + points[i].y() * target.height());
      painter.setPen(QPen(colors[std::min<size_t>(i, 2)], 3));
      painter.drawEllipse(p, 10, 10);
      painter.drawLine(p + QPointF(-18, 0), p + QPointF(18, 0));
      painter.drawLine(p + QPointF(0, -18), p + QPointF(0, 18));
    }
  }

  void mousePressEvent(QMouseEvent *event) override
  {
    const QRectF target = imageRect();
    if (!target.contains(event->position())) return;
    const QPointF normalized((event->position().x() - target.left()) / target.width(),
                             (event->position().y() - target.top()) / target.height());
    if (selected) selected(normalized);
  }

private:
  QRectF imageRect() const
  {
    QSizeF size = image_.size();
    size.scale(this->size(), Qt::KeepAspectRatio);
    return QRectF((width() - size.width()) / 2.0, (height() - size.height()) / 2.0,
                  size.width(), size.height());
  }

  QImage image_;
};

CalibrationDialog::CalibrationDialog(const Frame &frame, QWidget *parent) : QDialog(parent)
{
  setWindowTitle(tr("MK8DX QuickScore 初回校正"));
  resize(900, 650);
  QImage image(frame.rgba.data(), frame.width, frame.height, frame.stride, QImage::Format_RGBA8888);
  canvas_ = new CalibrationCanvas(image.copy());
  instruction_ = new QLabel;
  instruction_->setWordWrap(true);
  instruction_->setStyleSheet("font-size: 16px; font-weight: 700; padding: 8px;");
  auto *buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
  accept_ = buttons->button(QDialogButtonBox::Ok);
  accept_->setText(tr("この位置で校正"));
  accept_->setEnabled(false);
  auto *layout = new QVBoxLayout(this);
  layout->addWidget(instruction_);
  layout->addWidget(canvas_, 1);
  layout->addWidget(buttons);
  canvas_->selected = [this](QPointF point) { pointSelected(point); };
  connect(buttons, &QDialogButtonBox::accepted, this, &QDialog::accept);
  connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
  updateInstruction();
}

void CalibrationDialog::pointSelected(QPointF point)
{
  if (step_ >= 3) {
    step_ = 0;
    canvas_->points.clear();
    accept_->setEnabled(false);
  }
  points_[static_cast<size_t>(step_)] = point;
  canvas_->points.push_back(point);
  ++step_;
  if (step_ == 3) accept_->setEnabled(true);
  canvas_->update();
  updateInstruction();
}

void CalibrationDialog::updateInstruction()
{
  static const char *messages[]{
      "① リザルトの「12位」という順位数字の中央をクリックしてください（赤）",
      "② 1位プレイヤー名の先頭にあるチームタグの中央をクリックしてください（水色）",
      "③ 12位プレイヤー名の先頭にあるチームタグの中央をクリックしてください（緑）",
      "3点を指定しました。ずれていたら映像をもう一度クリックしてやり直せます。"};
  instruction_->setText(tr(messages[std::min(step_, 3)]));
}

CalibrationPoints CalibrationDialog::points() const
{
  return {points_[0], points_[1], points_[2]};
}

} // namespace quickscore
