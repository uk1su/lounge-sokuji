#pragma once
#include <cstdint>
#include <string>
#include <vector>
namespace mk {
struct Image {
 int w=0,h=0;
 std::vector<uint8_t> pixels; // BGRA
 Image()=default;
 Image(int width,int height):w(width),h(height),pixels(size_t(width)*height*4){}
 const uint8_t* at(int x,int y) const { return pixels.data()+(size_t(y)*w+x)*4; }
 uint8_t* at(int x,int y) { return pixels.data()+(size_t(y)*w+x)*4; }
};
Image read_image(const std::wstring& path);
Image read_image(const std::string& path);
Image resize(const Image& src,int w,int h);
Image crop(const Image& src,int x,int y,int w,int h);
void save_image(const Image& image,const std::wstring& path);
void save_image(const Image& image,const std::string& path);
std::wstring wide(const std::string& s);
#ifdef _WIN32
inline Image read_image(const std::string& path) { return read_image(wide(path)); }
inline void save_image(const Image& image, const std::string& path) { save_image(image, wide(path)); }
#endif
}
