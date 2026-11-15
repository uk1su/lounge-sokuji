#include "image.hpp"
#include <algorithm>
#include <cmath>
#include <stdexcept>

#ifdef _WIN32
#include <windows.h>
#include <wincodec.h>
#include <wrl/client.h>
using Microsoft::WRL::ComPtr;

namespace mk {
static void check(HRESULT h) { if(FAILED(h)) throw std::runtime_error("Image operation failed: "+std::to_string(uint32_t(h))); }
std::wstring wide(const std::string& s) {
 int n=MultiByteToWideChar(CP_UTF8,MB_ERR_INVALID_CHARS,s.data(),int(s.size()),nullptr,0);
 if(!n && !s.empty()) throw std::runtime_error("Invalid UTF-8");
 std::wstring r(n,0); MultiByteToWideChar(CP_UTF8,0,s.data(),int(s.size()),r.data(),n); return r;
}
Image read_image(const std::wstring& path) {
 ComPtr<IWICImagingFactory> f; check(CoCreateInstance(CLSID_WICImagingFactory,nullptr,CLSCTX_INPROC_SERVER,IID_PPV_ARGS(&f)));
 ComPtr<IWICBitmapDecoder> d; check(f->CreateDecoderFromFilename(path.c_str(),nullptr,GENERIC_READ,WICDecodeMetadataCacheOnLoad,&d));
 ComPtr<IWICBitmapFrameDecode> b; check(d->GetFrame(0,&b));
 UINT w,h; check(b->GetSize(&w,&h)); if(!w||!h||w>16384||h>16384) throw std::runtime_error("Invalid image size");
 ComPtr<IWICFormatConverter> c; check(f->CreateFormatConverter(&c));
 check(c->Initialize(b.Get(),GUID_WICPixelFormat32bppBGRA,WICBitmapDitherTypeNone,nullptr,0,WICBitmapPaletteTypeCustom));
 Image r(w,h); check(c->CopyPixels(nullptr,w*4,UINT(r.pixels.size()),r.pixels.data())); return r;
}
#endif
Image resize(const Image& s,int w,int h) {
 if(s.w==w&&s.h==h) return s;
 if(s.w<=0||s.h<=0||w<=0||h<=0) throw std::runtime_error("Invalid resize");
 Image out(w,h);
 for(int y=0;y<h;y++) for(int x=0;x<w;x++) {
  float sx=std::clamp((x+.5f)*s.w/w-.5f,0.f,float(s.w-1));
  float sy=std::clamp((y+.5f)*s.h/h-.5f,0.f,float(s.h-1));
  int x0=int(sx),y0=int(sy),x1=std::min(x0+1,s.w-1),y1=std::min(y0+1,s.h-1);
  for(int c=0;c<4;c++) out.at(x,y)[c]=uint8_t(std::lround((1-(sy-y0))*((1-(sx-x0))*s.at(x0,y0)[c]+(sx-x0)*s.at(x1,y0)[c])+(sy-y0)*((1-(sx-x0))*s.at(x0,y1)[c]+(sx-x0)*s.at(x1,y1)[c])));
 } return out;
}
Image crop(const Image& s,int x,int y,int w,int h) {
 if(x<0||y<0||x+w>s.w||y+h>s.h) throw std::runtime_error("Crop out of bounds");
 Image r(w,h); for(int j=0;j<h;j++) std::copy_n(s.at(x,y+j),w*4,r.at(0,j)); return r;
}
#ifdef _WIN32
void save_image(const Image& s,const std::wstring& path) {
 ComPtr<IWICImagingFactory> f; check(CoCreateInstance(CLSID_WICImagingFactory,nullptr,CLSCTX_INPROC_SERVER,IID_PPV_ARGS(&f)));
 ComPtr<IWICStream> stream; check(f->CreateStream(&stream)); check(stream->InitializeFromFilename(path.c_str(),GENERIC_WRITE));
 ComPtr<IWICBitmapEncoder> e; check(f->CreateEncoder(GUID_ContainerFormatPng,nullptr,&e)); check(e->Initialize(stream.Get(),WICBitmapEncoderNoCache));
 ComPtr<IWICBitmapFrameEncode> frame; check(e->CreateNewFrame(&frame,nullptr)); check(frame->Initialize(nullptr)); check(frame->SetSize(s.w,s.h));
 WICPixelFormatGUID fmt=GUID_WICPixelFormat32bppBGRA; check(frame->SetPixelFormat(&fmt));
 check(frame->WritePixels(s.h,s.w*4,UINT(s.pixels.size()),const_cast<BYTE*>(s.pixels.data()))); check(frame->Commit()); check(e->Commit());
}
#endif
}
