#pragma once

#include "vision.hpp"

#include <QDialog>
#include <QPointF>

#include <array>

class QLabel;
class QPushButton;

namespace quickscore {

struct CalibrationPoints {
  QPointF detectorCenter;
  QPointF firstTagCenter;
  QPointF lastTagCenter;
};

class CalibrationCanvas;

class CalibrationDialog final : public QDialog {
public:
  explicit CalibrationDialog(const Frame &frame, QWidget *parent = nullptr);
  CalibrationPoints points() const;

private:
  void pointSelected(QPointF point);
  void updateInstruction();

  CalibrationCanvas *canvas_{};
  QLabel *instruction_{};
  QPushButton *accept_{};
  std::array<QPointF, 3> points_{};
  int step_{};
};

} // namespace quickscore
