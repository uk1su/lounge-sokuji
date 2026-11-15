#pragma once
#include "image.hpp"
#include <array>
#include <optional>
namespace mk {
constexpr int width=1920,height=1080;
struct Result {
 std::vector<int> ranks;
 std::array<double,12> tag{},anchor{},plus{};
 int anchors=0,pluses=0,ambiguous=0;
 bool visible=false,valid=false;
 std::string text() const;
 std::optional<std::string> message() const;
};
class CourseRecognizer;
struct Pattern { int w=0,h=0; std::vector<float> data; };
class Recognizer {
 std::vector<Pattern> tags_;
 Pattern tag_,plus_;
 std::array<Pattern,12> ranks_;
public:
 static Recognizer calibrate(const Image& source);
 void save(const std::wstring& path) const;
 void save(const std::string& path) const;
 static Recognizer load(const std::wstring& path);
 static Recognizer load(const std::string& path);
 Result read(const Image& image) const;
 bool visibleLastRank(const Image& image, double* score=nullptr, double* margin=nullptr) const;
};
// Startup must first observe a non-result scene, avoiding re-posts after OBS restarts.
class Gate {
 bool armed_=false,locked_=false;
 int count_=0;
 uint64_t absent_=0,first_=0,last_=0;
 std::string candidate_;
public:
 std::optional<std::string> observe(const Result& r,uint64_t now,bool allow=true);
 std::optional<std::string> manual(const Result& r) { auto msg=r.message();if(!msg||locked_)return {};locked_=true;return msg; }
 void pause() {armed_=false;count_=0;candidate_.clear();absent_=0;last_=0;}
 void reset() { *this=Gate{}; }
 bool armed() const {return armed_&&!locked_;}
 bool locked() const {return locked_;}
};
}
