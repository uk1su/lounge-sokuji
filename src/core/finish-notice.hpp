#pragma once
#include <QImage>
#include <string>

namespace quickscore {

constexpr char twelfthFinishNoticeText[] = "順位が確定したので レースを終了しました";

bool isTwelfthFinishNoticeText(const std::string &text);
bool hasTwelfthFinishNotice(const QImage &source);

} // namespace quickscore
