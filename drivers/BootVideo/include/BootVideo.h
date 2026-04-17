#pragma once

#include <utils/identify.h>
#include <cstdint>

struct VdiFrameBuffer
{
     std::uint32_t* framebuffer{};
     std::uint32_t width{};
     std::uint32_t height{};
     std::uint32_t scalineSize{};
     std::uint32_t* optionalBackbuffer{};
};

#ifndef DLL
#define DLL DLLIMPORT
#endif

enum struct BitmapOrientation : std::uint8_t
{
     o0,
     o90,
     o180,
     o270
};
enum class SpinnerStyle : std::uint8_t
{
     Win10Dots,
     Win11Ring,
};

DLL void VidInitialise(VdiFrameBuffer buffer, void* (*allocator)(std::size_t));
DLL void VidClearScreen(std::uint32_t colour);
DLL void VidExchangeBuffers();
DLL void VidSetPixel(std::uint32_t x, std::uint32_t y, std::uint32_t colour);
DLL void VidDrawRect(std::uint32_t x, std::uint32_t y, std::uint32_t width, std::uint32_t height, std::uint32_t colour);
DLL void VidDrawChar(std::uint32_t x, std::uint32_t y, char c, std::uint32_t colour, std::uint8_t scale);
DLL void VidGetDimensions(std::uint32_t& width, std::uint32_t& height);
DLL void VidSetCursorPosition(std::uint32_t x, std::uint32_t y);
DLLEXPORT void VidDrawEllipse(std::uint32_t cx, std::uint32_t cy, std::uint32_t rx, std::uint32_t ry,
                              std::uint32_t colour);

DLLEXPORT void VidDrawLine(std::uint32_t x0, std::uint32_t y0, std::uint32_t x1, std::uint32_t y1,
                           std::uint32_t colour);
DLLEXPORT void VidDrawRoundedRect(std::uint32_t x, std::uint32_t y, std::uint32_t width, std::uint32_t height,
                                  std::uint32_t radius, std::uint32_t colour);
DLLEXPORT void VidDrawTriangle(std::int32_t x0, std::int32_t y0, std::int32_t x1, std::int32_t y1, std::int32_t x2,
                               std::int32_t y2, std::uint32_t colour);
DLLEXPORT void VidDrawBitmap(std::uint32_t x, std::uint32_t y, const void* bmpData,
                             BitmapOrientation orientation = BitmapOrientation::o0, std::size_t bmpSize = 0);
DLLEXPORT void VidDrawSpinner(std::uint32_t cx, std::uint32_t cy, std::uint32_t radius, std::uint32_t colour,
                              std::uint32_t tick, SpinnerStyle style);
#undef DLL
