#include "recognizer.hpp"
#include <algorithm>
#include <cmath>
#include <fstream>
#include <filesystem>
#include <stdexcept>
namespace mk {
static int row_y(int r) { return int(std::lround(112.0+78.0*r)); }
static float luminance(const uint8_t* p) {return .114f*p[0]+.587f*p[1]+.299f*p[2];}
static bool yellow(const Image& im,int row) {
 int n=0; for(int y=row_y(row)-25;y<row_y(row)+25;y+=5) {auto p=im.at(1130,y); if(p[2]>160&&p[1]>150&&p[0]<100) n++;} return n>=6;
}
static bool yellowPixels(const uint8_t* pixels, int stride, bool isRgba, int row) {
 int n=0;
 const int rIdx = isRgba ? 0 : 2;
 const int bIdx = isRgba ? 2 : 0;
 for(int y=row_y(row)-25;y<row_y(row)+25;y+=5) {
  const uint8_t* p = pixels + size_t(y)*size_t(stride) + 1130*4;
  if(p[rIdx]>160&&p[1]>150&&p[bIdx]<100) n++;
 }
 return n>=6;
}
static double matchRoi(const uint8_t* pixels, int stride, bool isRgba, const Pattern& p, int x, int y,
                       bool inv, bool both=false, int radius_y=3, int min_dx=-3, int max_dx=3) {
 const int roiX = x + min_dx;
 const int roiY = y - radius_y;
 const int roiW = p.w + (max_dx - min_dx);
 const int roiH = p.h + 2 * radius_y;
 if (roiX < 0 || roiY < 0 || roiX + roiW > width || roiY + roiH > height || p.w <= 0 || p.h <= 0)
  return -1.0;

 float stackBuf[160 * 160];
 std::vector<float> heapBuf;
 float* roi = stackBuf;
 if (roiW * roiH > int(std::size(stackBuf))) {
  heapBuf.resize(size_t(roiW) * size_t(roiH));
  roi = heapBuf.data();
 }
 for (int j = 0; j < roiH; ++j) {
  const uint8_t* rowPtr = pixels + size_t(roiY + j) * size_t(stride) + size_t(roiX) * 4;
  float* dst = roi + size_t(j) * size_t(roiW);
  if (isRgba) {
   for (int i = 0; i < roiW; ++i) {
    const uint8_t* px = rowPtr + size_t(i) * 4;
    dst[i] = .299f * px[0] + .587f * px[1] + .114f * px[2];
   }
  } else {
   for (int i = 0; i < roiW; ++i) {
    const uint8_t* px = rowPtr + size_t(i) * 4;
    dst[i] = .114f * px[0] + .587f * px[1] + .299f * px[2];
   }
  }
 }

 double psum = 0, psq = 0;
 int n = 0;
 const int step = p.w > 30 ? 2 : 1;
 for (int j = 0; j < p.h; j += step)
  for (int i = 0; i < p.w; i += step) {
   const double v = p.data[size_t(j) * p.w + i];
   psum += v;
   psq += v * v;
   ++n;
  }
 const double pvariance = psq - psum * psum / n;
 if (pvariance <= .001)
  return -1.0;

 double best = -1.0;
 const int dyCount = 2 * radius_y;
 const int dxCount = max_dx - min_dx;
 for (int oy = 0; oy <= dyCount; ++oy) {
  for (int ox = 0; ox <= dxCount; ++ox) {
   double sum = 0, sq = 0, dot = 0;
   for (int j = 0; j < p.h; j += step) {
    const float* rRow = roi + size_t(oy + j) * size_t(roiW) + ox;
    const float* pRow = p.data.data() + size_t(j) * size_t(p.w);
    for (int i = 0; i < p.w; i += step) {
     const float v = rRow[i];
     sum += v;
     sq += v * v;
     dot += v * pRow[i];
    }
   }
   const double variance = sq - sum * sum / n;
   if (variance > 100.0) {
    const double corr = (dot - sum * psum / n) / std::sqrt(variance * pvariance);
    best = std::max(best, both ? std::abs(corr) : (inv ? -corr : corr));
   }
  }
 }
 return best;
}
static Pattern pattern(const Image& im,int x,int y,int w,int h,bool invert=false) {
 Pattern p{w,h,{}}; p.data.reserve(size_t(w)*h); double mean=0;
 for(int j=0;j<h;j++)for(int i=0;i<w;i++){float v=luminance(im.at(x+i,y+j)); if(invert)v=255-v;p.data.push_back(v);mean+=v;}
 mean/=p.data.size(); double norm=0;for(auto& v:p.data){v-=float(mean);norm+=v*v;}
 norm=std::sqrt(norm); if(norm<1)norm=1;for(auto& v:p.data)v/=float(norm);return p;
}
static double match(const std::vector<float>& gray,const Pattern& p,int x,int y,bool inv,bool both=false,int radius_y=3,int min_dx=-3,int max_dx=3) {
 double psum=0,psq=0;int n=0;
 const int step=p.w>30?2:1;
 for(int j=0;j<p.h;j+=step)for(int i=0;i<p.w;i+=step){double v=p.data[size_t(j)*p.w+i];psum+=v;psq+=v*v;n++;}
 double pvariance=psq-psum*psum/n;
 double best=-1;
 for(int dy=-radius_y;dy<=radius_y;dy++)for(int dx=min_dx;dx<=max_dx;dx++) {
  double sum=0,sq=0,dot=0;
  for(int j=0;j<p.h;j+=step)for(int i=0;i<p.w;i+=step) {
   float v=gray[size_t(y+dy+j)*width+x+dx+i];
   sum+=v;sq+=v*v;dot+=v*p.data[size_t(j)*p.w+i];
  }
  double variance=sq-sum*sum/n;
  if(variance>100&&pvariance>.001){double corr=(dot-sum*psum/n)/std::sqrt(variance*pvariance);best=std::max(best,both?std::abs(corr):(inv?-corr:corr));}
 } return best;
}
Recognizer Recognizer::calibrate(const Image& source) {
 auto im=resize(source,width,height); Recognizer r;
 // Only the Latin prefix ITF. is stored, never the player's full name.
 r.tag_=pattern(im,1020,row_y(1)-22,72,40);
 r.tags_.push_back(r.tag_);
 r.plus_=pattern(im,1585,row_y(0)-10,23,28);
 for(int i=0;i<12;i++) r.ranks_[i]=pattern(im,837,row_y(i)-30,64,64); // rank outline retained for selected row
 return r;
}
void Recognizer::save(const std::wstring& path) const {
 std::ofstream f(std::filesystem::path(path),std::ios::binary);f.write("MKR2",4);
 auto write=[&](const Pattern& p){f.write(reinterpret_cast<const char*>(&p.w),4);f.write(reinterpret_cast<const char*>(&p.h),4);f.write(reinterpret_cast<const char*>(p.data.data()),std::streamsize(p.data.size()*4));};
 uint32_t count = static_cast<uint32_t>(tags_.empty() ? 1 : tags_.size());
 f.write(reinterpret_cast<const char*>(&count), 4);
 if(tags_.empty()) write(tag_);
 else for(const auto& t : tags_) write(t);
 write(plus_);for(auto& p:ranks_)write(p);if(!f)throw std::runtime_error("Cannot save templates");
}
void Recognizer::save(const std::string& path) const {
 save(std::filesystem::u8path(path).wstring());
}
Recognizer Recognizer::load(const std::wstring& path) {
 std::ifstream f(std::filesystem::path(path),std::ios::binary);char magic[4]{};f.read(magic,4);
 std::string m(magic,4);
 auto read=[&](Pattern& p){f.read(reinterpret_cast<char*>(&p.w),4);f.read(reinterpret_cast<char*>(&p.h),4);if(p.w<1||p.h<1||p.w>128||p.h>128)throw std::runtime_error("Invalid template dimensions");p.data.resize(size_t(p.w)*p.h);f.read(reinterpret_cast<char*>(p.data.data()),std::streamsize(p.data.size()*4));};
 Recognizer r;
 if (m == "MKR2") {
  uint32_t count = 0;
  f.read(reinterpret_cast<char*>(&count), 4);
  if (count < 1 || count > 10) throw std::runtime_error("Invalid template tag count");
  r.tags_.resize(count);
  for (uint32_t i = 0; i < count; i++) read(r.tags_[i]);
  if (!r.tags_.empty()) r.tag_ = r.tags_[0];
 } else if (m == "MKR1") {
  read(r.tag_);
  r.tags_.push_back(r.tag_);
 } else {
  throw std::runtime_error("Invalid template file format");
 }
 read(r.plus_);for(auto& p:r.ranks_)read(p);if(!f)throw std::runtime_error("Truncated templates");return r;
}
Recognizer Recognizer::load(const std::string& path) {
 return load(std::filesystem::u8path(path).wstring());
}
static double rowContrastMatchRoi(const float* roi, int roiW, const Pattern& p) {
 double best = -1;
 const int glyphHeight = std::min(46, p.h);
 for (int oy = 0; oy <= 6; ++oy) {
  for (int ox = 0; ox <= 6; ++ox) {
   double weighted = 0, weight = 0;
   for (int j = 0; j < glyphHeight; ++j) {
    const float* pRow = p.data.data() + size_t(j) * size_t(p.w);
    const float* rRow = roi + size_t(oy + j) * size_t(roiW) + ox;
    double tm = 0, sm = 0;
    for (int i = 0; i < p.w; ++i) {
     tm += pRow[i];
     sm += rRow[i];
    }
    tm /= p.w;
    sm /= p.w;
    double tv = 0, sv = 0, dot = 0;
    for (int i = 0; i < p.w; ++i) {
     const double t = pRow[i] - tm, s = rRow[i] - sm;
     tv += t * t;
     sv += s * s;
     dot += t * s;
    }
    if (tv < 1e-8)
     continue;
    const double w = std::sqrt(tv);
    weight += w;
    if (sv > 100)
     weighted += w * dot / std::sqrt(tv * sv);
   }
   if (weight > 0)
    best = std::max(best, std::abs(weighted / weight));
  }
 }
 return best;
}
bool Recognizer::visibleLastRankPixels(const uint8_t* pixels, int stride, bool isRgba, double* score, double* margin) const {
 if (!pixels || stride < width * 4)
  return false;
 int maxW = 0, maxH = 0;
 for (int rank = 0; rank < 12; ++rank) {
  maxW = std::max(maxW, ranks_[size_t(rank)].w);
  maxH = std::max(maxH, ranks_[size_t(rank)].h);
 }
 const int roiX = 837 - 3;
 const int roiY = row_y(11) - 30 - 3;
 const int roiW = maxW + 6;
 const int roiH = maxH + 6;
 if (roiX < 0 || roiY < 0 || roiX + roiW > width || roiY + roiH > height)
  return false;
 float stackBuf[160 * 160];
 std::vector<float> heapBuf;
 float* roi = stackBuf;
 if (roiW * roiH > int(std::size(stackBuf))) {
  heapBuf.resize(size_t(roiW) * size_t(roiH));
  roi = heapBuf.data();
 }
 for (int j = 0; j < roiH; ++j) {
  const uint8_t* rowPtr = pixels + size_t(roiY + j) * size_t(stride) + size_t(roiX) * 4;
  float* dst = roi + size_t(j) * size_t(roiW);
  if (isRgba) {
   for (int i = 0; i < roiW; ++i) {
    const uint8_t* px = rowPtr + size_t(i) * 4;
    dst[i] = .299f * px[0] + .587f * px[1] + .114f * px[2];
   }
  } else {
   for (int i = 0; i < roiW; ++i) {
    const uint8_t* px = rowPtr + size_t(i) * 4;
    dst[i] = .114f * px[0] + .587f * px[1] + .299f * px[2];
   }
  }
 }
 double expected = -1, competitor = -1;
 for (int rank = 0; rank < 12; ++rank) {
  const double value = rowContrastMatchRoi(roi, roiW, ranks_[size_t(rank)]);
  if (rank == 11)
   expected = value;
  else
   competitor = std::max(competitor, value);
 }
 if (score) *score = expected;
 if (margin) *margin = expected - competitor;
 return expected >= .85 && expected - competitor >= .06;
}
bool Recognizer::visibleLastRank(const Image& source, double* score, double* margin) const {
 if (source.w != width || source.h != height) return false;
 return visibleLastRankPixels(source.pixels.data(), width * 4, false, score, margin);
}
bool Recognizer::visibleTiedRankPixels(const uint8_t* pixels, int stride, bool isRgba, int row, double* score) const {
 if (!pixels || stride < width * 4 || row <= 0 || row >= 12) return false;
 double best = -1;
 for (int k = 0; k < row; ++k)
  best = std::max(best, matchRoi(pixels, stride, isRgba, ranks_[size_t(k)], 837, row_y(row) - 30, false, true, 3, -3, 3));
 if (score) *score = best;
 return best >= .72;
}
bool Recognizer::visibleTiedRank(const Image& source, int row, double* score) const {
 if (source.w != width || source.h != height || row <= 0 || row >= 12) return false;
 return visibleTiedRankPixels(source.pixels.data(), width * 4, false, row, score);
}
Result Recognizer::readLayoutOnlyPixels(const uint8_t* pixels, int stride, bool isRgba) const {
 Result r;
 if (!pixels || stride < width * 4)
  return r;
 for (int i = 0; i < 12; ++i) {
  r.anchor[size_t(i)] = matchRoi(pixels, stride, isRgba, ranks_[size_t(i)], 837, row_y(i) - 30, false, true, 3, -3, 3);
  if (r.anchor[size_t(i)] >= .65)
   ++r.anchors;
  // Early exit: even if all remaining rows matched, anchors could never reach 8
  // (minimum needed for visible >= 9 or cumulative tied-rank check >= 8).
  if (r.anchors + (11 - i) < 8)
   break;
 }
 if (r.anchors >= 8) {
  for (int i = 0; i < 12; ++i) {
   const bool inv = yellowPixels(pixels, stride, isRgba, i);
   r.plus[size_t(i)] = matchRoi(pixels, stride, isRgba, plus_, 1585, row_y(i) - 10, inv, false, 3, -3, 3);
   if (r.plus[size_t(i)] >= .72)
    ++r.pluses;
  }
 }
 r.visible = r.anchors >= 9;
 return r;
}
Result Recognizer::readLayoutOnly(const Image& source) const {
 if (source.w != width || source.h != height) return readLayoutOnly(resize(source, width, height));
 return readLayoutOnlyPixels(source.pixels.data(), width * 4, false);
}
Result Recognizer::read(const Image& source) const {
 if(source.w!=width||source.h!=height) return read(resize(source,width,height));
 Result r;
 std::vector<float> gray(size_t(width)*height);
 for(size_t i=0;i<gray.size();i++)gray[i]=luminance(source.pixels.data()+i*4);
 for(int i=0;i<12;i++) {
  bool inv=yellow(source,i);
  double best_tag = -1.0;
  for (const auto& t : tags_) {
   int base_y = row_y(i) - t.h / 2;
   int base_x = (t.h == 27) ? 1024 : 1020;
   double s = match(gray, t, base_x, base_y, inv, false, 4, -15, 30);
   if (s > best_tag) best_tag = s;
  }
  r.tag[i] = best_tag;
  // Both polarities are tested: the local player's rank reverses colour.
  r.anchor[i]=match(gray,ranks_[i],837,row_y(i)-30,false,true,3,-3,3);
  r.plus[i]=match(gray,plus_,1585,row_y(i)-10,inv,false,3,-3,3);
  if(r.tag[i]>=.78)r.ranks.push_back(i+1);else if(r.tag[i]>=.55)r.ambiguous++;
  if(r.anchor[i]>=.65)r.anchors++;
  if(r.plus[i]>=.72)r.pluses++;
 }
 r.visible=r.anchors>=9;
 r.valid=r.anchors==12&&r.pluses>=10&&r.ranks.size()==6&&r.ambiguous==0;
 return r;
}
std::string Result::text() const {std::string s;for(int v:ranks)s+=std::to_string(v);return s;}
std::optional<std::string> Result::message() const {
 if(valid)return text();
 if(anchors==12&&pluses>=10&&ranks.size()!=6)return "6人見つかりません";
 return {};
}
std::optional<std::string> Gate::observe(const Result& r,uint64_t now,bool allow) {
 // Long gaps cannot count as consecutive frames or prove that a result disappeared.
 if(last_&&now-last_>1000){count_=0;candidate_.clear();absent_=0;}
 last_=now;
 if(!r.visible){
  count_=0;candidate_.clear();if(!absent_)absent_=now;
  if(now-absent_>=3000){armed_=true;locked_=false;}
  return {};
 }
 absent_=0;
 auto msg=r.message();
 if(!allow||!armed_||locked_||!msg){count_=0;candidate_.clear();return {};}
 auto text=*msg;if(candidate_!=text){candidate_=text;first_=now;count_=1;return {};}
 if(++count_>=3&&now-first_>=(r.valid?250ULL:750ULL)){locked_=true;return text;}
 return {};
}
}
