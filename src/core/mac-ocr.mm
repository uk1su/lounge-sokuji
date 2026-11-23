#include "mac-ocr.hpp"

#import <CoreGraphics/CoreGraphics.h>
#import <Foundation/Foundation.h>
#import <Vision/Vision.h>

#include <vector>

namespace quickscore {

std::string macRecognizeBgra(const uint8_t *bgra, int width, int height) {
  if (!bgra || width <= 0 || height <= 0)
    return {};
  @autoreleasepool {
    CGColorSpaceRef colorSpace = CGColorSpaceCreateDeviceRGB();
    CGContextRef ctx = CGBitmapContextCreate(
        const_cast<uint8_t *>(bgra), static_cast<size_t>(width), static_cast<size_t>(height), 8,
        static_cast<size_t>(width) * 4, colorSpace,
        kCGBitmapByteOrder32Little | kCGImageAlphaPremultipliedFirst);
    CGColorSpaceRelease(colorSpace);
    if (!ctx)
      return {};

    CGImageRef cgImage = CGBitmapContextCreateImage(ctx);
    CGContextRelease(ctx);
    if (!cgImage)
      return {};

    __block std::string text;
    VNRecognizeTextRequest *request = [[VNRecognizeTextRequest alloc]
        initWithCompletionHandler:^(VNRequest *req, NSError *err) {
          if (err)
            return;
          for (VNRecognizedTextObservation *obs in req.results) {
            VNRecognizedText *top = [[obs topCandidates:1] firstObject];
            if (top && top.string) {
              if (!text.empty())
                text += ' ';
              text += [top.string UTF8String];
            }
          }
        }];
    request.recognitionLanguages = @[ @"ja-JP", @"en-US" ];
    request.recognitionLevel = VNRequestTextRecognitionLevelAccurate;
    request.usesLanguageCorrection = YES;

    VNImageRequestHandler *handler =
        [[VNImageRequestHandler alloc] initWithCGImage:cgImage options:@{}];
    [handler performRequests:@[ request ] error:nil];
    CGImageRelease(cgImage);
    return text;
  }
}

std::string macRecognizeGray(const uint8_t *gray, int width, int height) {
  if (!gray || width <= 0 || height <= 0)
    return {};
  std::vector<uint8_t> bgra(static_cast<size_t>(width) * static_cast<size_t>(height) * 4);
  for (size_t i = 0; i < static_cast<size_t>(width) * static_cast<size_t>(height); ++i) {
    const uint8_t v = gray[i];
    bgra[i * 4 + 0] = v;
    bgra[i * 4 + 1] = v;
    bgra[i * 4 + 2] = v;
    bgra[i * 4 + 3] = 255;
  }
  return macRecognizeBgra(bgra.data(), width, height);
}

} // namespace quickscore
