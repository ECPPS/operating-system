#include <utils/identify.h>
#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <numbers>

#define DLL DLLEXPORT
#include <BootVideo.h>

#if defined(ARCH_X8664)
#include <immintrin.h>
#if defined(COMPILER_MSVC)
#include <intrin.h>
#else
#include <cpuid.h>
#endif
#elifdef ARCH_ARM64
#include <arm_neon.h>
#endif

#pragma pack(push, 1)
struct BmpFileHeader
{
     std::uint16_t type;
     std::uint32_t size;
     std::uint16_t reserved1;
     std::uint16_t reserved2;
     std::uint32_t offBits;
};

struct BmpInfoHeader
{
     std::uint32_t size;
     std::int32_t width;
     std::int32_t height;
     std::uint16_t planes;
     std::uint16_t bitCount;
     std::uint32_t compression;
     std::uint32_t sizeImage;
     std::int32_t xPelsPerMeter;
     std::int32_t yPelsPerMeter;
     std::uint32_t clrUsed;
     std::uint32_t clrImportant;
};
#pragma pack(pop)

template <typename T> [[nodiscard]] constexpr T VidMin(T a, T b) noexcept { return a < b ? a : b; }

template <typename T> [[nodiscard]] constexpr T VidMax(T a, T b) noexcept { return a > b ? a : b; }

template <typename T> constexpr void VidSwap(T& a, T& b) noexcept
{
     T tmp = a;
     a = b;
     b = tmp;
}

template <typename T> static void VidFill(T* first, T* last, T value) noexcept
{
     for (; first != last; ++first) *first = value;
}

template <typename T, typename Cmp> static void VidInsertionSort(T* first, T* last, Cmp cmp) noexcept
{
     for (T* i = first + 1; i < last; ++i)
     {
          T key = *i;
          T* j = i - 1;
          while (j >= first && cmp(key, *j))
          {
               *(j + 1) = *j;
               --j;
          }
          *(j + 1) = key;
     }
}

[[nodiscard]] static float VidFabs(float x) noexcept
{
     std::uint32_t bits{};
     memcpy(&bits, &x, sizeof(bits));
     bits &= 0x7FFF'FFFFu;
     memcpy(&x, &bits, sizeof(x));
     return x;
}

[[nodiscard]] static float VidFloor(float x) noexcept
{
     const auto trunc = static_cast<float>(static_cast<std::int32_t>(x));
     return (x < trunc) ? trunc - 1.0f : trunc;
}

[[nodiscard]] static float VidRound(float x) noexcept
{
     return (x >= 0.0f) ? VidFloor(x + 0.5f) : -VidFloor(-x + 0.5f);
}

[[nodiscard]] static float VidCos(float x) noexcept
{
     constexpr float kTwoPi = 2.0f * std::numbers::pi_v<float>;
     constexpr float kPi = std::numbers::pi_v<float>;
     x -= kTwoPi * VidFloor(x / kTwoPi);
     if (x > kPi) x = kTwoPi - x;
     const float x2 = x * x;
     return 1.0f + (x2 * (-0.4999999963f +
                          x2 * (0.0416666418f + x2 * (-0.0013888397f + x2 * (0.0000247609f + x2 * (-0.0000002605f))))));
}
[[nodiscard]] static float VidSin(float x) noexcept
{
     constexpr float kHalfPi = std::numbers::pi_v<float> / 2.0f;
     return VidCos(x - kHalfPi);
}

[[nodiscard]] static float VidAtan2(float y, float x) noexcept
{
     constexpr float kPi = std::numbers::pi_v<float>;
     constexpr float kHalfPi = 1.57079632f;
     if (x == 0.0f) return (y > 0.0f) ? kHalfPi : (y < 0.0f) ? -kHalfPi : 0.0f;
     const bool negX = x < 0.0f;
     const bool negY = y < 0.0f;
     float ay = VidFabs(y);
     float ax = VidFabs(x);
     bool swapped = false;
     if (ay > ax)
     {
          VidSwap(ax, ay);
          swapped = true;
     }
     const float t = ay / ax;
     const float t2 = t * t;
     float a = t * (0.99997726f + t2 * (-0.33262347f + t2 * (0.19354346f + t2 * (-0.11643287f + t2 * 0.05265332f))));
     if (swapped) a = kHalfPi - a;
     if (negX) a = kPi - a;
     if (negY) a = -a;
     return a;
}

[[nodiscard]] static float VidSqrt(float x) noexcept
{
#if defined(ARCH_X8664)
     float result{};
#if defined(COMPILER_CLANG) || defined(COMPILER_GCC)
     asm("sqrtss %1, %0" : "=x"(result) : "x"(x));
#else // COMPILER_MSVC
     const __m128 v = _mm_set_ss(x);
     result = _mm_cvtss_f32(_mm_sqrt_ss(v));
#endif
     return result;

#elif defined(ARCH_ARM64)
     float result{};
#if defined(COMPILER_CLANG) || defined(COMPILER_GCC)
     asm("fsqrt %s0, %s1" : "=w"(result) : "w"(x));
#else
     result =
#endif
     return result;
#endif
}

struct VdiFrameBufferInternal
{
     std::uint32_t* framebuffer{};
     std::uint32_t width{};
     std::uint32_t height{};
     std::uint32_t size{};
     std::uint32_t scanlineSize{};
     std::uint32_t* vram{};
};

static VdiFrameBufferInternal g_buffer;

struct Rect
{
     std::uint32_t x0{}, y0{}, x1{}, y1{}; // inclusive-exclusive [x0,x1) [y0,y1)

     [[nodiscard]] constexpr bool IsEmpty() const noexcept { return x0 >= x1 || y0 >= y1; }

     [[nodiscard]] constexpr Rect Union(const Rect& o) const noexcept
     {
          return {.x0 = VidMin(x0, o.x0), .y0 = VidMin(y0, o.y0), .x1 = VidMax(x1, o.x1), .y1 = VidMax(y1, o.y1)};
     }

     [[nodiscard]] constexpr std::uint64_t MergedArea(const Rect& o) const noexcept
     {
          const std::uint64_t w = VidMax(x1, o.x1) - VidMin(x0, o.x0);
          const std::uint64_t h = VidMax(y1, o.y1) - VidMin(y0, o.y0);
          return w * h;
     }
};

static constexpr std::size_t kMaxDirtyRects = 16;

struct DirtyList
{
     Rect* rects{};
     std::size_t count{};

     void Reset() noexcept { count = 0; }

     void MergeBestPair() noexcept
     {
          std::size_t bestI = 0;
          std::size_t bestJ = 1;
          std::uint64_t bestArea = rects[0].MergedArea(rects[1]);
          for (std::size_t i = 0; i < count; ++i)
               for (std::size_t j = i + 1; j < count; ++j)
               {
                    const std::uint64_t area = rects[i].MergedArea(rects[j]);
                    if (area < bestArea)
                    {
                         bestArea = area;
                         bestI = i;
                         bestJ = j;
                    }
               }
          rects[bestI] = rects[bestI].Union(rects[bestJ]);
          if (bestJ != count - 1) rects[bestJ] = rects[count - 1];
          --count;
     }

     void Add(Rect r, std::uint32_t screenW, std::uint32_t screenH) noexcept
     {
          if (r.IsEmpty()) return;
          r.x1 = VidMin(r.x1, screenW);
          r.y1 = VidMin(r.y1, screenH);
          if (r.IsEmpty()) return;
          if (count == kMaxDirtyRects) MergeBestPair();
          rects[count++] = r;
     }
};

static DirtyList g_dirty;

enum class CmdType : std::uint8_t
{
     Clear,
     Rect,
     Pixel,
     Char,
     Bitmap,
     Line,
     Ellipse,
     RoundedRect,
     Triangle,
     Spinner
};

struct CmdClear
{
     std::uint32_t colour;
};
struct CmdRect
{
     std::uint32_t x, y, width, height, colour;
};
struct CmdPixel
{
     std::uint32_t x, y, colour;
};
struct CmdChar
{
     std::uint32_t x, y, colour;
     char c;
     std::uint8_t scale;
};
struct CmdLine
{
     std::uint32_t x0, y0, x1, y1, colour;
};
struct CmdEllipse
{
     std::uint32_t cx, cy, rx, ry, colour;
};
struct CmdRoundedRect
{
     std::uint32_t x, y, width, height, radius, colour;
};
struct CmdTriangle
{
     std::int32_t x0, y0, x1, y1, x2, y2;
     std::uint32_t colour;
};
struct CmdBitmap
{
     std::uint32_t x, y;
     const std::uint8_t* data;
     std::uint32_t width, height, pitch;
     std::uint8_t bytesPerPixel;
     BitmapOrientation orientation;
     bool bottomUp;
};
struct CmdSpinner
{
     std::uint32_t cx, cy;
     std::uint32_t radius;
     std::uint32_t colour;
     std::uint32_t tick;
     SpinnerStyle style;
};

struct DrawCommand
{
     CmdType type{};
     union
     {
          CmdClear clear;
          CmdRect rect;
          CmdPixel pixel;
          CmdChar chr;
          CmdLine line;
          CmdEllipse ellipse;
          CmdRoundedRect roundedRect;
          CmdTriangle triangle;
          CmdBitmap bitmap;
          CmdSpinner spinner;
     } data{};
};

static constexpr std::size_t kMeshCapacity = 4096;

struct MeshBuffer
{
     DrawCommand* commands{};
     std::size_t count{};

     void Reset() noexcept { count = 0; }

     bool Push(const DrawCommand& cmd) noexcept
     {
          if (count >= kMeshCapacity) return false;
          commands[count++] = cmd;
          return true;
     }
};

static MeshBuffer g_mesh;

struct MsaaBuffer
{
     std::uint32_t* pixels{};
     std::uint32_t msaaW{};
     std::uint32_t msaaH{};
};

static MsaaBuffer g_msaa;

static constexpr std::size_t kGlyphW = 8;
static constexpr std::size_t kGlyphH = 16;
static constexpr std::size_t kSdfGlyphs = 256;

struct SdfAtlas
{
     float* data{};
};
static SdfAtlas g_sdf;

extern "C" const std::uint8_t kFontData[256][16]; // NOLINT

static void BuildGlyphSdf(std::size_t g) noexcept
{
     float bmp[kGlyphH][kGlyphW]{}; // NOLINT
     for (std::size_t row = 0; row < kGlyphH; ++row)
     {
          const std::uint8_t byte = kFontData[g][row];
          for (std::size_t col = 0; col < kGlyphW; ++col) bmp[row][col] = (byte >> (7u - col)) & 1u ? 1.0f : 0.0f;
     }

     constexpr float kMaxDist = static_cast<float>(kGlyphW + kGlyphH);

     for (std::size_t row = 0; row < kGlyphH; ++row)
     {
          for (std::size_t col = 0; col < kGlyphW; ++col)
          {
               const float inside = bmp[row][col];
               float minDist = kMaxDist;

               for (std::size_t sr = 0; sr < kGlyphH; ++sr)
                    for (std::size_t sc = 0; sc < kGlyphW; ++sc)
                    {
                         if (bmp[sr][sc] == inside) continue;
                         const float dr = static_cast<float>(row) - static_cast<float>(sr);
                         const float dc = static_cast<float>(col) - static_cast<float>(sc);
                         const float d = VidSqrt((dr * dr) + (dc * dc));
                         minDist = std::min(d, minDist);
                    }

               const float sign = (inside > 0.5f) ? 1.0f : -1.0f;
               g_sdf.data[((g * kGlyphH + row) * kGlyphW) + col] = sign * minDist / kMaxDist;
          }
     }
}

static constexpr std::size_t kOffMesh = 0;
static constexpr std::size_t kOffDirty = kOffMesh + (sizeof(DrawCommand) * kMeshCapacity);
static constexpr std::size_t kOffSdf = kOffDirty + (sizeof(Rect) * kMaxDirtyRects);
static constexpr std::size_t kOffMsaa = kOffSdf + (sizeof(float) * kSdfGlyphs * kGlyphH * kGlyphW);

DLLEXPORT std::size_t VidGetRequiredMemory(std::uint32_t width, std::uint32_t height)
{
     const std::size_t msaaPixels = static_cast<std::size_t>(width * 2u) * static_cast<std::size_t>(height * 2u);
     return kOffMsaa + (sizeof(std::uint32_t) * msaaPixels);
}

static std::uint32_t g_cursorX = 0;
static std::uint32_t g_cursorY = 0;

#if defined(ARCH_X8664)

enum class X86SimdLevel : std::uint8_t
{
     SSE2 = 1,
     AVX2 = 2
};

static X86SimdLevel DetectX86Simd() noexcept
{
#ifdef COMPILER_MSVC
     int info[4]{};
     __cpuid(info, 1);
     if (!(info[2] & (1 << 27))) return X86SimdLevel::SSE2;
     if ((_xgetbv(0) & 0x6u) != 0x6u) return X86SimdLevel::SSE2;
     __cpuidex(info, 7, 0);
     if (info[1] & (1 << 5)) return X86SimdLevel::AVX2;
     return X86SimdLevel::SSE2;
#else
     unsigned eax{};
     unsigned ebx{};
     unsigned ecx{};
     unsigned edx{};
     if (!__get_cpuid(1, &eax, &ebx, &ecx, &edx)) return X86SimdLevel::SSE2;
     if (!(ecx & (1u << 27))) return X86SimdLevel::SSE2;
     std::uint32_t xcr0Lo{};
     std::uint32_t xcr0Hi{};
#ifdef COMPILER_CLANG
     asm volatile("xgetbv" : "=a"(xcr0Lo), "=d"(xcr0Hi) : "c"(0u));
#else
     xcr0Lo = static_cast<std::uint32_t>(_xgetbv(0));
#endif
     if ((xcr0Lo & 0x6u) != 0x6u) return X86SimdLevel::SSE2;
     if (!__get_cpuid_count(7, 0, &eax, &ebx, &ecx, &edx)) return X86SimdLevel::SSE2;
     if (ebx & (1u << 5)) return X86SimdLevel::AVX2;
     return X86SimdLevel::SSE2;
#endif
}

static const X86SimdLevel g_x86simd = DetectX86Simd();
static constexpr std::size_t kPrefetchAhead = 256;

static void CopyAVX2(const void* __restrict src, void* __restrict dst, std::size_t byteCount) noexcept
{
     const auto* s = static_cast<const std::uint8_t*>(src);
     auto* d = static_cast<std::uint8_t*>(dst);
     const std::size_t vec64 = byteCount / 64;
     const std::size_t tail = byteCount % 64;
#if defined(COMPILER_CLANG)
     for (std::size_t i = 0; i < vec64; ++i, s += 64, d += 64)
          asm volatile("prefetchnta %[pfr]          \n\t"
                       "vmovdqu   (%[s]),  %%ymm0   \n\t"
                       "vmovdqu 32(%[s]),  %%ymm1   \n\t"
                       "vmovntdq  %%ymm0, (%[d])    \n\t"
                       "vmovntdq  %%ymm1, 32(%[d])  \n\t"
                       :
                       : [s] "r"(s), [d] "r"(d), [pfr] "m"(*(s + kPrefetchAhead))
                       : "memory", "ymm0", "ymm1");
     asm volatile("sfence" ::: "memory");
     asm volatile("vzeroupper" ::: "memory");
#else
     for (std::size_t i = 0; i < vec64; ++i, s += 64, d += 64)
     {
          _mm_prefetch(reinterpret_cast<const char*>(s + kPrefetchAhead), _MM_HINT_NTA);
          const __m256i v0 = _mm256_loadu_si256(reinterpret_cast<const __m256i*>(s));
          const __m256i v1 = _mm256_loadu_si256(reinterpret_cast<const __m256i*>(s + 32));
          _mm256_stream_si256(reinterpret_cast<__m256i*>(d), v0);
          _mm256_stream_si256(reinterpret_cast<__m256i*>(d + 32), v1);
     }
     _mm_sfence();
     _mm256_zeroupper();
#endif
     if (tail) std::memcpy(d, s, tail);
}

static void CopySSE2(const void* __restrict src, void* __restrict dst, std::size_t byteCount) noexcept
{
     const auto* s = static_cast<const std::uint8_t*>(src);
     auto* d = static_cast<std::uint8_t*>(dst);
     const std::size_t vec32 = byteCount / 32;
     const std::size_t tail = byteCount % 32;
#if defined(COMPILER_CLANG)
     for (std::size_t i = 0; i < vec32; ++i, s += 32, d += 32)
          asm volatile("prefetchnta %[pfr]          \n\t"
                       "movdqu   (%[s]),  %%xmm0    \n\t"
                       "movdqu 16(%[s]),  %%xmm1    \n\t"
                       "movntdq  %%xmm0, (%[d])     \n\t"
                       "movntdq  %%xmm1, 16(%[d])   \n\t"
                       :
                       : [s] "r"(s), [d] "r"(d), [pfr] "m"(*(s + kPrefetchAhead))
                       : "memory", "xmm0", "xmm1");
     asm volatile("sfence" ::: "memory");
#else
     for (std::size_t i = 0; i < vec32; ++i, s += 32, d += 32)
     {
          _mm_prefetch(reinterpret_cast<const char*>(s + kPrefetchAhead), _MM_HINT_NTA);
          const __m128i v0 = _mm_loadu_si128(reinterpret_cast<const __m128i*>(s));
          const __m128i v1 = _mm_loadu_si128(reinterpret_cast<const __m128i*>(s + 16));
          _mm_stream_si128(reinterpret_cast<__m128i*>(d), v0);
          _mm_stream_si128(reinterpret_cast<__m128i*>(d + 16), v1);
     }
     _mm_sfence();
#endif
     if (tail) std::memcpy(d, s, tail);
}

#elifdef ARCH_ARM64

static constexpr std::size_t kArmPrefetchAhead = 256;

static void CopyNEON(const void* __restrict src, void* __restrict dst, std::size_t byteCount) noexcept
{
     const auto* s = static_cast<const std::uint8_t*>(src);
     auto* d = static_cast<std::uint8_t*>(dst);
     const std::size_t vec64 = byteCount / 64;
     const std::size_t tail = byteCount % 64;
#if defined(COMPILER_CLANG)
     for (std::size_t i = 0; i < vec64; ++i, s += 64, d += 64)
          asm volatile("prfm  pldl1strm, [%[s], %[pf]] \n\t"
                       "ldp   q0, q1, [%[s]]           \n\t"
                       "ldp   q2, q3, [%[s], #32]      \n\t"
                       "stnp  q0, q1, [%[d]]           \n\t"
                       "stnp  q2, q3, [%[d], #32]      \n\t"
                       :
                       : [s] "r"(s), [d] "r"(d), [pf] "r"(static_cast<std::ptrdiff_t>(kArmPrefetchAhead))
                       : "memory", "v0", "v1", "v2", "v3");
     asm volatile("dsb st" ::: "memory");
#else
     for (std::size_t i = 0; i < vec64; ++i, s += 64, d += 64)
     {
          __prefetch2(s + kArmPrefetchAhead, 0);
          const uint8x16x4_t v = vld1q_u8_x4(s);
          vst1q_u8(d, v.val[0]);
          vst1q_u8(d + 16, v.val[1]);
          vst1q_u8(d + 32, v.val[2]);
          vst1q_u8(d + 48, v.val[3]);
     }
     __dsb(_ARM64_BARRIER_ISHST);
#endif
     if (tail) std::memcpy(d, s, tail);
}

#endif // ARCH_ARM64

static void SimdMemcpyToVRAM(const void* __restrict src, void* __restrict dst, std::size_t byteCount) noexcept
{
#if defined(ARCH_X8664)
     if (g_x86simd >= X86SimdLevel::AVX2) CopyAVX2(src, dst, byteCount);
     else
          CopySSE2(src, dst, byteCount);
#elif defined(ARCH_ARM64)
     CopyNEON(src, dst, byteCount);
#else
     std::memcpy(dst, src, byteCount);
#endif
}

static void BitBltRect(const Rect& r) noexcept
{
     if (g_buffer.vram == nullptr) return;
     const std::size_t stride = g_buffer.scanlineSize;
     const std::size_t rowBytes = stride * sizeof(std::uint32_t);
     for (std::uint32_t y = r.y0; y < r.y1; ++y)
     {
          const std::uint32_t* src = g_buffer.framebuffer + (y * stride);
          std::uint32_t* dst = g_buffer.vram + (y * stride);
          SimdMemcpyToVRAM(src, dst, rowBytes);
     }
}

struct Rgb
{
     std::uint8_t r, g, b;
};

[[nodiscard]] static constexpr Rgb Unpack(std::uint32_t c) noexcept
{
     return {.r = static_cast<std::uint8_t>((c >> 16) & 0xFFu),
             .g = static_cast<std::uint8_t>((c >> 8) & 0xFFu),
             .b = static_cast<std::uint8_t>(c & 0xFFu)};
}

[[nodiscard]] static constexpr std::uint32_t Pack(Rgb c) noexcept
{
     return (static_cast<std::uint32_t>(c.r) << 16) | (static_cast<std::uint32_t>(c.g) << 8) |
            static_cast<std::uint32_t>(c.b);
}

[[nodiscard]] static std::uint32_t Blend(std::uint32_t dst, std::uint32_t src, float t) noexcept
{
     if (t <= 0.0f) return dst;
     if (t >= 1.0f) return src;
     const Rgb d = Unpack(dst);
     const Rgb s = Unpack(src);
     const auto lerp = [t](std::uint8_t a, std::uint8_t b) -> std::uint8_t
     {
          return static_cast<std::uint8_t>(static_cast<float>(a) +
                                           (t * (static_cast<float>(b) - static_cast<float>(a))));
     };
     return Pack({.r = lerp(d.r, s.r), .g = lerp(d.g, s.g), .b = lerp(d.b, s.b)});
}

static void MsaaSeedPixel(std::uint32_t lx, std::uint32_t ly) noexcept
{
     const std::uint32_t bg = g_buffer.framebuffer[(ly * g_buffer.scanlineSize) + lx];
     const std::uint32_t sx = lx * 2;
     const std::uint32_t sy = ly * 2;
     const std::uint32_t w = g_msaa.msaaW;
     g_msaa.pixels[((sy)*w) + (sx)] = bg;
     g_msaa.pixels[((sy)*w) + (sx + 1)] = bg;
     g_msaa.pixels[((sy + 1) * w) + (sx)] = bg;
     g_msaa.pixels[((sy + 1) * w) + (sx + 1)] = bg;
}

static void MsaaResolvePixel(std::uint32_t lx, std::uint32_t ly) noexcept
{
     const std::uint32_t sx = lx * 2;
     const std::uint32_t sy = ly * 2;
     const std::uint32_t w = g_msaa.msaaW;
     const Rgb s00 = Unpack(g_msaa.pixels[((sy)*w) + (sx)]);
     const Rgb s10 = Unpack(g_msaa.pixels[((sy)*w) + (sx + 1)]);
     const Rgb s01 = Unpack(g_msaa.pixels[((sy + 1) * w) + (sx)]);
     const Rgb s11 = Unpack(g_msaa.pixels[((sy + 1) * w) + (sx + 1)]);
     const Rgb avg = {.r = static_cast<std::uint8_t>((s00.r + s10.r + s01.r + s11.r) >> 2u),
                      .g = static_cast<std::uint8_t>((s00.g + s10.g + s01.g + s11.g) >> 2u),
                      .b = static_cast<std::uint8_t>((s00.b + s10.b + s01.b + s11.b) >> 2u)};
     g_buffer.framebuffer[(ly * g_buffer.scanlineSize) + lx] = Pack(avg);
}

static Rect RasteriseClear(const CmdClear& cmd) noexcept
{
     if (g_buffer.framebuffer == nullptr) return {};
     for (std::uint32_t y = 0; y < g_buffer.height; ++y)
     {
          auto* row = g_buffer.framebuffer + (static_cast<std::size_t>(y) * g_buffer.scanlineSize);
          VidFill(row, row + g_buffer.width, cmd.colour);
     }
     VidFill(g_msaa.pixels, g_msaa.pixels + (static_cast<std::size_t>(g_msaa.msaaW) * g_msaa.msaaH), cmd.colour);
     return {.x0 = 0, .y0 = 0, .x1 = g_buffer.width, .y1 = g_buffer.height};
}

static Rect RasteriseRect(const CmdRect& cmd) noexcept
{
     if (g_buffer.framebuffer == nullptr) return {};
     const std::uint32_t x1 = VidMin(cmd.x + cmd.width, g_buffer.width);
     const std::uint32_t y1 = VidMin(cmd.y + cmd.height, g_buffer.height);
     for (std::uint32_t ly = cmd.y; ly < y1; ++ly)
          for (std::uint32_t lx = cmd.x; lx < x1; ++lx)
          {
               const std::uint32_t sx = lx * 2;
               const std::uint32_t sy = ly * 2;
               const std::uint32_t mw = g_msaa.msaaW;
               g_msaa.pixels[((sy)*mw) + (sx)] = cmd.colour;
               g_msaa.pixels[((sy)*mw) + (sx + 1)] = cmd.colour;
               g_msaa.pixels[((sy + 1) * mw) + (sx)] = cmd.colour;
               g_msaa.pixels[((sy + 1) * mw) + (sx + 1)] = cmd.colour;
               MsaaResolvePixel(lx, ly);
          }
     return {.x0 = cmd.x, .y0 = cmd.y, .x1 = x1, .y1 = y1};
}

static Rect RasterisePixel(const CmdPixel& cmd) noexcept
{
     if (g_buffer.framebuffer == nullptr) return {};
     if (cmd.x >= g_buffer.width || cmd.y >= g_buffer.height) return {};
     const std::uint32_t sx = cmd.x * 2;
     const std::uint32_t sy = cmd.y * 2;
     const std::uint32_t mw = g_msaa.msaaW;
     g_msaa.pixels[((sy)*mw) + (sx)] = cmd.colour;
     g_msaa.pixels[((sy)*mw) + (sx + 1)] = cmd.colour;
     g_msaa.pixels[((sy + 1) * mw) + (sx)] = cmd.colour;
     g_msaa.pixels[((sy + 1) * mw) + (sx + 1)] = cmd.colour;
     MsaaResolvePixel(cmd.x, cmd.y);
     return {.x0 = cmd.x, .y0 = cmd.y, .x1 = cmd.x + 1, .y1 = cmd.y + 1};
}

static constexpr float kSdfLo = -0.08f;
static constexpr float kSdfHi = 0.08f;

static Rect RasteriseChar(const CmdChar& cmd) noexcept
{
     if (g_buffer.framebuffer == nullptr) return {};

     const float* glyphSdf = g_sdf.data + (static_cast<std::uint8_t>(cmd.c) * kGlyphH * kGlyphW);

     for (std::uint32_t row = 0; row < static_cast<std::uint32_t>(kGlyphH); ++row)
     {
          const std::uint32_t py = cmd.y + (row * cmd.scale);
          if (py >= g_buffer.height) break;

          for (std::uint32_t col = 0; col < static_cast<std::uint32_t>(kGlyphW); ++col)
          {
               const std::uint32_t px = cmd.x + (col * cmd.scale);
               if (px >= g_buffer.width) continue;

               const float dist = glyphSdf[(row * kGlyphW) + col];

               float coverage{};
               if (dist >= kSdfHi) coverage = 1.0f;
               else if (dist <= kSdfLo)
                    coverage = 0.0f;
               else
                    coverage = (dist - kSdfLo) / (kSdfHi - kSdfLo);

               if (coverage <= 0.0f) continue;

               for (std::uint32_t sy = 0; sy < cmd.scale && (py + sy) < g_buffer.height; ++sy)
                    for (std::uint32_t sx = 0; sx < cmd.scale && (px + sx) < g_buffer.width; ++sx)
                    {
                         std::uint32_t& dst = g_buffer.framebuffer[((py + sy) * g_buffer.scanlineSize) + (px + sx)];
                         dst = Blend(dst, cmd.colour, coverage);
                    }
          }
     }

     return {.x0 = cmd.x,
             .y0 = cmd.y,
             .x1 = cmd.x + (static_cast<std::uint32_t>(kGlyphW) * cmd.scale),
             .y1 = cmd.y + (static_cast<std::uint32_t>(kGlyphH) * cmd.scale)};
}

[[nodiscard]] static float Frac(float x) noexcept { return x - VidFloor(x); }
[[nodiscard]] static float RFrac(float x) noexcept { return 1.0f - Frac(x); }

static void WuPlotSuper(std::int32_t sx, std::int32_t sy, std::uint32_t colour, float coverage) noexcept
{
     if (sx < 0 || sy < 0) return;
     const auto usx = static_cast<std::uint32_t>(sx);
     const auto usy = static_cast<std::uint32_t>(sy);
     if (usx >= g_msaa.msaaW || usy >= g_msaa.msaaH) return;
     std::uint32_t& p = g_msaa.pixels[(usy * g_msaa.msaaW) + usx];
     p = Blend(p, colour, coverage);
}

static Rect RasteriseLine(const CmdLine& cmd) noexcept
{
     if (g_buffer.framebuffer == nullptr) return {};

     const std::uint32_t bx0 = VidMin(cmd.x0, cmd.x1);
     const std::uint32_t by0 = VidMin(cmd.y0, cmd.y1);
     const std::uint32_t bx1 = VidMin(VidMax(cmd.x0, cmd.x1) + 1u, g_buffer.width);
     const std::uint32_t by1 = VidMin(VidMax(cmd.y0, cmd.y1) + 1u, g_buffer.height);

     for (std::uint32_t ly = by0; ly < by1; ++ly)
          for (std::uint32_t lx = bx0; lx < bx1; ++lx) MsaaSeedPixel(lx, ly);

     float x0f = static_cast<float>(cmd.x0);
     float y0f = static_cast<float>(cmd.y0);
     float x1f = static_cast<float>(cmd.x1);
     float y1f = static_cast<float>(cmd.y1);

     const bool steep = VidFabs(y1f - y0f) > VidFabs(x1f - x0f);
     if (steep)
     {
          VidSwap(x0f, y0f);
          VidSwap(x1f, y1f);
     }
     if (x0f > x1f)
     {
          VidSwap(x0f, x1f);
          VidSwap(y0f, y1f);
     }

     const float dx = x1f - x0f;
     const float dy = y1f - y0f;
     const float grad = (dx == 0.0f) ? 1.0f : dy / dx;

     const auto plot = [&](float lx, float ly, float cov) noexcept
     {
          const auto ipx = static_cast<std::int32_t>(lx) * 2;
          const auto ipy = static_cast<std::int32_t>(ly) * 2;
          if (steep)
          {
               WuPlotSuper(ipy, ipx, cmd.colour, cov);
               WuPlotSuper(ipy + 1, ipx, cmd.colour, cov);
               WuPlotSuper(ipy, ipx + 1, cmd.colour, cov);
               WuPlotSuper(ipy + 1, ipx + 1, cmd.colour, cov);
          }
          else
          {
               WuPlotSuper(ipx, ipy, cmd.colour, cov);
               WuPlotSuper(ipx + 1, ipy, cmd.colour, cov);
               WuPlotSuper(ipx, ipy + 1, cmd.colour, cov);
               WuPlotSuper(ipx + 1, ipy + 1, cmd.colour, cov);
          }
     };

     const auto iX0 = static_cast<std::int32_t>(VidRound(x0f));
     const auto iX1 = static_cast<std::int32_t>(VidRound(x1f));
     float intery = y0f + (grad * (static_cast<float>(iX0) - x0f));

     for (std::int32_t x = iX0; x <= iX1; ++x, intery += grad)
     {
          const auto iY = static_cast<std::int32_t>(VidFloor(intery));
          plot(static_cast<float>(x), static_cast<float>(iY), RFrac(intery));
          plot(static_cast<float>(x), static_cast<float>(iY + 1), Frac(intery));
     }

     for (std::uint32_t ly = by0; ly < by1; ++ly)
          for (std::uint32_t lx = bx0; lx < bx1; ++lx)
               if (lx < g_buffer.width && ly < g_buffer.height) MsaaResolvePixel(lx, ly);

     return {.x0 = bx0, .y0 = by0, .x1 = bx1, .y1 = by1};
}

static Rect RasteriseEllipse(const CmdEllipse& cmd) noexcept
{
     if (g_buffer.framebuffer == nullptr) return {};

     const float cx = static_cast<float>(cmd.cx);
     const float cy = static_cast<float>(cmd.cy);
     const float rx = static_cast<float>(cmd.rx);
     const float ry = static_cast<float>(cmd.ry);

     const std::uint32_t bx0 = static_cast<std::uint32_t>(VidMax(0.0f, cx - rx - 1.0f));
     const std::uint32_t by0 = static_cast<std::uint32_t>(VidMax(0.0f, cy - ry - 1.0f));
     const std::uint32_t bx1 = VidMin(static_cast<std::uint32_t>(cx + rx + 2.0f), g_buffer.width);
     const std::uint32_t by1 = VidMin(static_cast<std::uint32_t>(cy + ry + 2.0f), g_buffer.height);

     for (std::uint32_t ly = by0; ly < by1; ++ly)
          for (std::uint32_t lx = bx0; lx < bx1; ++lx) MsaaSeedPixel(lx, ly);

     constexpr float kEdgeHalf = 0.6f;

     for (std::uint32_t ly = by0; ly < by1; ++ly)
     {
          for (std::uint32_t lx = bx0; lx < bx1; ++lx)
          {
               const float dx = static_cast<float>(lx) + 0.5f - cx;
               const float dy = static_cast<float>(ly) + 0.5f - cy;

               const float ex = dx / rx;
               const float ey = dy / ry;
               const float sdf = VidSqrt((ex * ex) + (ey * ey)) - 1.0f;

               if (sdf < -kEdgeHalf || sdf > kEdgeHalf) continue;

               const float coverage = 1.0f - ((sdf + kEdgeHalf) / (2.0f * kEdgeHalf));

               const std::uint32_t sx = lx * 2;
               const std::uint32_t sy = ly * 2;
               const std::uint32_t mw = g_msaa.msaaW;
               auto blendSlot = [&](std::uint32_t six, std::uint32_t siy)
               {
                    std::uint32_t& slot = g_msaa.pixels[(siy * mw) + six];
                    slot = Blend(slot, cmd.colour, coverage);
               };
               blendSlot(sx, sy);
               blendSlot(sx + 1, sy);
               blendSlot(sx, sy + 1);
               blendSlot(sx + 1, sy + 1);

               MsaaResolvePixel(lx, ly);
          }
     }

     return {.x0 = bx0, .y0 = by0, .x1 = bx1, .y1 = by1};
}

static Rect RasteriseRoundedRect(const CmdRoundedRect& cmd) noexcept
{
     if (g_buffer.framebuffer == nullptr) return {};

     const std::uint32_t maxR = VidMin(cmd.width, cmd.height) / 2u;
     const std::uint32_t r = VidMin(cmd.radius, maxR);
     const float rf = static_cast<float>(r);

     const std::uint32_t x1 = VidMin(cmd.x + cmd.width, g_buffer.width);
     const std::uint32_t y1 = VidMin(cmd.y + cmd.height, g_buffer.height);

     constexpr float kEdge = 0.6f;

     const auto solidPixel = [&](std::uint32_t lx, std::uint32_t ly)
     {
          const std::uint32_t sx = lx * 2;
          const std::uint32_t sy = ly * 2;
          const std::uint32_t mw = g_msaa.msaaW;
          g_msaa.pixels[(sy * mw) + (sx)] = cmd.colour;
          g_msaa.pixels[(sy * mw) + (sx + 1)] = cmd.colour;
          g_msaa.pixels[((sy + 1) * mw) + (sx)] = cmd.colour;
          g_msaa.pixels[((sy + 1) * mw) + (sx + 1)] = cmd.colour;
          MsaaResolvePixel(lx, ly);
     };

     const auto cornerPixel = [&](std::uint32_t lx, std::uint32_t ly, float ccx, float ccy)
     {
          const float dx = static_cast<float>(lx) + 0.5f - ccx;
          const float dy = static_cast<float>(ly) + 0.5f - ccy;
          const float sdf = VidSqrt((dx * dx) + (dy * dy)) - rf; // + = outside

          if (sdf > kEdge) return; // fully outside, skip

          const float coverage = (sdf < -kEdge) ? 1.0f : 1.0f - ((sdf + kEdge) / (2.0f * kEdge));

          if (coverage <= 0.0f) return;

          const std::uint32_t sx = lx * 2;
          const std::uint32_t sy = ly * 2;
          const std::uint32_t mw = g_msaa.msaaW;
          auto blendSlot = [&](std::uint32_t six, std::uint32_t siy)
          {
               std::uint32_t& slot = g_msaa.pixels[(siy * mw) + six];
               slot = Blend(slot, cmd.colour, coverage);
          };
          blendSlot(sx, sy);
          blendSlot(sx + 1, sy);
          blendSlot(sx, sy + 1);
          blendSlot(sx + 1, sy + 1);
          MsaaResolvePixel(lx, ly);
     };

     const float ccTLx = static_cast<float>(cmd.x) + rf;
     const float ccTLy = static_cast<float>(cmd.y) + rf;
     const float ccTRx = static_cast<float>(x1) - rf;
     const float ccBLy = static_cast<float>(y1) - rf;

     for (std::uint32_t ly = cmd.y; ly < y1; ++ly)
          for (std::uint32_t lx = cmd.x; lx < x1; ++lx) MsaaSeedPixel(lx, ly);

     for (std::uint32_t ly = cmd.y; ly < y1; ++ly)
     {
          for (std::uint32_t lx = cmd.x; lx < x1; ++lx)
          {
               const bool inCornerCols = (lx < cmd.x + r) || (lx >= x1 - r);
               const bool inCornerRows = (ly < cmd.y + r) || (ly >= y1 - r);

               if (!inCornerCols || !inCornerRows) { solidPixel(lx, ly); }
               else
               {
                    const float ccx = (lx < cmd.x + r) ? ccTLx : ccTRx;
                    const float ccy = (ly < cmd.y + r) ? ccTLy : ccBLy;
                    cornerPixel(lx, ly, ccx, ccy);
               }
          }
     }

     return {.x0 = cmd.x, .y0 = cmd.y, .x1 = x1, .y1 = y1};
}

[[nodiscard]] static std::int64_t TriArea2(std::int32_t ax, std::int32_t ay, std::int32_t bx, std::int32_t by,
                                           std::int32_t cx, std::int32_t cy) noexcept
{
     return (static_cast<std::int64_t>(bx - ax) * static_cast<std::int64_t>(cy - ay)) -
            (static_cast<std::int64_t>(by - ay) * static_cast<std::int64_t>(cx - ax));
}

static Rect RasteriseTriangle(const CmdTriangle& cmd) noexcept
{
     if (g_buffer.framebuffer == nullptr) return {};

     std::int32_t ax = cmd.x0;
     std::int32_t ay = cmd.y0;
     std::int32_t bx = cmd.x1;
     std::int32_t by = cmd.y1;
     std::int32_t cx = cmd.x2;
     std::int32_t cy = cmd.y2;
     if (TriArea2(ax, ay, bx, by, cx, cy) < 0)
     {
          VidSwap(bx, cx);
          VidSwap(by, cy);
     }

     const std::int32_t scW = static_cast<std::int32_t>(g_buffer.width);
     const std::int32_t scH = static_cast<std::int32_t>(g_buffer.height);

     const std::uint32_t bx0 = static_cast<std::uint32_t>(VidMax(0, VidMin(ax, VidMin(bx, cx))));
     const std::uint32_t by0 = static_cast<std::uint32_t>(VidMax(0, VidMin(ay, VidMin(by, cy))));
     const std::uint32_t bx1 = static_cast<std::uint32_t>(VidMin(scW, VidMax(ax, VidMax(bx, cx)) + 1));
     const std::uint32_t by1 = static_cast<std::uint32_t>(VidMin(scH, VidMax(ay, VidMax(by, cy)) + 1));

     if (bx0 >= bx1 || by0 >= by1) return {};

     const float abDx = static_cast<float>(bx - ax);
     const float abDy = static_cast<float>(by - ay);
     const float abLen = VidSqrt((abDx * abDx) + (abDy * abDy));

     const float bcDx = static_cast<float>(cx - bx);
     const float bcDy = static_cast<float>(cy - by);
     const float bcLen = VidSqrt((bcDx * bcDx) + (bcDy * bcDy));

     const float caDx = static_cast<float>(ax - cx);
     const float caDy = static_cast<float>(ay - cy);
     const float caLen = VidSqrt((caDx * caDx) + (caDy * caDy));

     if (abLen == 0.0f || bcLen == 0.0f || caLen == 0.0f) return {};

     const float abNx = -abDy / abLen;
     const float abNy = abDx / abLen;
     const float bcNx = -bcDy / bcLen;
     const float bcNy = bcDx / bcLen;
     const float caNx = -caDy / caLen;
     const float caNy = caDx / caLen;

     constexpr float kEdge = 0.5f;

     for (std::uint32_t ly = by0; ly < by1; ++ly)
          for (std::uint32_t lx = bx0; lx < bx1; ++lx) MsaaSeedPixel(lx, ly);

     for (std::uint32_t ly = by0; ly < by1; ++ly)
     {
          for (std::uint32_t lx = bx0; lx < bx1; ++lx)
          {
               const float px = static_cast<float>(lx) + 0.5f;
               const float py = static_cast<float>(ly) + 0.5f;

               const float dAB = (abNx * (px - static_cast<float>(ax))) + (abNy * (py - static_cast<float>(ay)));
               const float dBC = (bcNx * (px - static_cast<float>(bx))) + (bcNy * (py - static_cast<float>(by)));
               const float dCA = (caNx * (px - static_cast<float>(cx))) + (caNy * (py - static_cast<float>(cy)));

               const float minD = VidMin(dAB, VidMin(dBC, dCA));

               if (minD < -kEdge) continue;

               const float coverage = (minD >= kEdge) ? 1.0f : (minD + kEdge) / (2.0f * kEdge);

               const std::uint32_t sx = lx * 2;
               const std::uint32_t sy = ly * 2;
               const std::uint32_t mw = g_msaa.msaaW;
               auto blendSlot = [&](std::uint32_t six, std::uint32_t siy)
               {
                    std::uint32_t& slot = g_msaa.pixels[(siy * mw) + six];
                    slot = Blend(slot, cmd.colour, coverage);
               };
               blendSlot(sx, sy);
               blendSlot(sx + 1, sy);
               blendSlot(sx, sy + 1);
               blendSlot(sx + 1, sy + 1);
               MsaaResolvePixel(lx, ly);
          }
     }

     return {.x0 = bx0, .y0 = by0, .x1 = bx1, .y1 = by1};
}

static Rect RasteriseBitmap(const CmdBitmap& cmd) noexcept
{
     if (g_buffer.framebuffer == nullptr) return {};

     const std::uint32_t mw = g_msaa.msaaW;

     for (std::uint32_t row = 0; row < cmd.height; ++row)
     {
          const std::uint32_t srcRow = cmd.bottomUp ? (cmd.height - 1u - row) : row;
          const std::uint8_t* srcLine = cmd.data + (static_cast<std::size_t>(srcRow) * cmd.pitch);

          for (std::uint32_t col = 0; col < cmd.width; ++col)
          {
               std::uint32_t lx{};
               std::uint32_t ly{};

               switch (cmd.orientation)
               {
               case BitmapOrientation::o90:
                    lx = cmd.x + (cmd.height - 1u - row);
                    ly = cmd.y + col;
                    break;
               case BitmapOrientation::o180:
                    lx = cmd.x + (cmd.width - 1u - col);
                    ly = cmd.y + (cmd.height - 1u - row);
                    break;
               case BitmapOrientation::o270:
                    lx = cmd.x + row;
                    ly = cmd.y + (cmd.width - 1u - col);
                    break;
               default:
                    lx = cmd.x + col;
                    ly = cmd.y + row;
                    break;
               }

               if (lx >= g_buffer.width || ly >= g_buffer.height) continue;

               const std::uint8_t* px = srcLine + (static_cast<std::size_t>(col) * cmd.bytesPerPixel);
               const std::uint32_t colour = (static_cast<std::uint32_t>(px[2]) << 16u) |
                                            (static_cast<std::uint32_t>(px[1]) << 8u) |
                                            static_cast<std::uint32_t>(px[0]);

               const std::uint32_t sx = lx * 2u;
               const std::uint32_t sy = ly * 2u;
               g_msaa.pixels[(sy * mw) + sx] = colour;
               g_msaa.pixels[(sy * mw) + (sx + 1u)] = colour;
               g_msaa.pixels[((sy + 1u) * mw) + sx] = colour;
               g_msaa.pixels[((sy + 1u) * mw) + (sx + 1u)] = colour;
               MsaaResolvePixel(lx, ly);
          }
     }

     const bool swapDimensions =
         cmd.orientation == BitmapOrientation::o90 || cmd.orientation == BitmapOrientation::o270;
     const std::uint32_t footW = swapDimensions ? cmd.height : cmd.width;
     const std::uint32_t footH = swapDimensions ? cmd.width : cmd.height;
     return {.x0 = cmd.x,
             .y0 = cmd.y,
             .x1 = VidMin(cmd.x + footW, g_buffer.width),
             .y1 = VidMin(cmd.y + footH, g_buffer.height)};
}
static Rect RasteriseSpinner(const CmdSpinner& cmd) noexcept
{
     if (g_buffer.framebuffer == nullptr) return {};

     const std::uint32_t mw = g_msaa.msaaW;
     const float cx = static_cast<float>(cmd.cx);
     const float cy = static_cast<float>(cmd.cy);
     const float r = static_cast<float>(cmd.radius);
     const Rgb tint = Unpack(cmd.colour != 0u ? cmd.colour : 0xFFFFFFu);

     const std::uint32_t bx0 = (cmd.cx > cmd.radius + 2u) ? cmd.cx - cmd.radius - 2u : 0u;
     const std::uint32_t by0 = (cmd.cy > cmd.radius + 2u) ? cmd.cy - cmd.radius - 2u : 0u;
     const std::uint32_t bx1 = VidMin(cmd.cx + cmd.radius + 3u, g_buffer.width);
     const std::uint32_t by1 = VidMin(cmd.cy + cmd.radius + 3u, g_buffer.height);

     for (std::uint32_t ly = by0; ly < by1; ++ly)
          for (std::uint32_t lx = bx0; lx < bx1; ++lx) MsaaSeedPixel(lx, ly);

     if (cmd.style == SpinnerStyle::Win10Dots)
     {
          constexpr std::uint32_t kN = 6u;
          constexpr float kTwoPi = 6.28318530f;
          constexpr float kStep = kTwoPi / static_cast<float>(kN);
          constexpr float kAlpha[kN] = {1.0f, 0.85f, 0.75f, 0.65f, 0.55f, 0.45f};

          constexpr float kEase = 0.45f;

          const float orbitR = r * 0.70f;
          const float dotR = r * 0.18f;
          const float phase = (static_cast<float>(cmd.tick % 60u) / 60.0f) * kTwoPi;

          for (std::uint32_t d = 0u; d < kN; ++d)
          {
               const float raw = phase - (static_cast<float>(d) * kStep);

               const float eased = raw - (kEase * VidSin(raw));
               constexpr float kHalfPi = std::numbers::pi_v<float> / 2.0f;

               const float dotCx = cx + (orbitR * VidCos(eased - kHalfPi));
               const float dotCy = cy + (orbitR * VidSin(eased - kHalfPi));
               const float alpha = kAlpha[d];
               constexpr float kEdge = 0.7f;
               const auto px0 = static_cast<std::uint32_t>(VidMax(0.0f, VidFloor(dotCx - dotR - 1.0f)));
               const auto py0 = static_cast<std::uint32_t>(VidMax(0.0f, VidFloor(dotCy - dotR - 1.0f)));
               const auto px1 = VidMin(static_cast<std::uint32_t>(VidFloor(dotCx + dotR + 2.0f)), g_buffer.width);
               const auto py1 = VidMin(static_cast<std::uint32_t>(VidFloor(dotCy + dotR + 2.0f)), g_buffer.height);

               for (std::uint32_t ly = py0; ly < py1; ++ly)
               {
                    for (std::uint32_t lx = px0; lx < px1; ++lx)
                    {
                         const float dx = static_cast<float>(lx) + 0.5f - dotCx;
                         const float dy = static_cast<float>(ly) + 0.5f - dotCy;
                         const float sdf = VidSqrt((dx * dx) + (dy * dy)) - dotR;
                         if (sdf > kEdge) continue;

                         const float coverage = (sdf < -kEdge) ? 1.0f : 1.0f - ((sdf + kEdge) / (2.0f * kEdge));
                         const float blendT = coverage * alpha;
                         if (blendT <= 0.0f) continue;

                         const std::uint32_t sx = lx * 2u;
                         const std::uint32_t sy = ly * 2u;
                         auto acc = [&](std::uint32_t six, std::uint32_t siy)
                         {
                              std::uint32_t& slot = g_msaa.pixels[(siy * mw) + six];
                              slot = Blend(slot, Pack(tint), blendT);
                         };
                         acc(sx, sy);
                         acc(sx + 1u, sy);
                         acc(sx, sy + 1u);
                         acc(sx + 1u, sy + 1u);
                    }
               }
          }
     }
     else
     {
          constexpr float kTwoPi = 6.28318530f;
          constexpr float kArcSpan = kTwoPi * 0.75f; // 270°
          const float innerR = r * 0.58f;
          constexpr float kEdge = 0.8f;
          constexpr float kHalfPi = std::numbers::pi_v<float> / 2.0f;
          const float leadAngle =
              ((static_cast<float>(cmd.tick % 90u) / 90.0f) * (2.0f * std::numbers::pi_v<float>)) - kHalfPi;

          for (std::uint32_t ly = by0; ly < by1; ++ly)
          {
               for (std::uint32_t lx = bx0; lx < bx1; ++lx)
               {
                    if (lx >= g_buffer.width || ly >= g_buffer.height) continue;

                    const float dx = static_cast<float>(lx) + 0.5f - cx;
                    const float dy = static_cast<float>(ly) + 0.5f - cy;
                    const float dist = VidSqrt((dx * dx) + (dy * dy));

                    const float ringSdf = VidMax(dist - r, innerR - dist);
                    if (ringSdf > kEdge) continue;

                    const float pixAngle = VidAtan2(dy, dx);

                    float rel = leadAngle - pixAngle;
                    if (rel < 0.0f) rel += kTwoPi;
                    if (rel >= kTwoPi) rel -= kTwoPi;

                    if (rel > kArcSpan) continue;

                    const float t = rel / kArcSpan;
                    const float alpha = ((1.0f - t) * 0.95f) + 0.05f;

                    const float radialCov = (ringSdf < -kEdge) ? 1.0f : 1.0f - ((ringSdf + kEdge) / (2.0f * kEdge));
                    const float blendT = radialCov * alpha;
                    if (blendT <= 0.0f) continue;

                    const std::uint32_t sx = lx * 2u;
                    const std::uint32_t sy = ly * 2u;
                    auto acc = [&](std::uint32_t six, std::uint32_t siy)
                    {
                         std::uint32_t& slot = g_msaa.pixels[(siy * mw) + six];
                         slot = Blend(slot, Pack(tint), blendT);
                    };
                    acc(sx, sy);
                    acc(sx + 1u, sy);
                    acc(sx, sy + 1u);
                    acc(sx + 1u, sy + 1u);
               }
          }
     }

     for (std::uint32_t ly = by0; ly < by1; ++ly)
          for (std::uint32_t lx = bx0; lx < bx1; ++lx)
               if (lx < g_buffer.width && ly < g_buffer.height) MsaaResolvePixel(lx, ly);

     return {.x0 = bx0, .y0 = by0, .x1 = bx1, .y1 = by1};
}

DLLEXPORT void VidInitialise(VdiFrameBuffer buffer, void* (*allocator)(std::size_t))
{
     g_buffer.framebuffer = buffer.optionalBackbuffer != nullptr ? buffer.optionalBackbuffer : buffer.framebuffer;
     g_buffer.vram = buffer.optionalBackbuffer != nullptr ? buffer.framebuffer : nullptr;
     g_buffer.width = buffer.width;
     g_buffer.height = buffer.height;
     g_buffer.scanlineSize = buffer.scalineSize;
     g_buffer.size = g_buffer.scanlineSize * g_buffer.height;

     const std::size_t totalBytes = VidGetRequiredMemory(buffer.width, buffer.height);
     auto* block = static_cast<std::uint8_t*>(allocator(totalBytes));

     g_mesh.commands = reinterpret_cast<DrawCommand*>(block + kOffMesh);
     g_mesh.count = 0;

     g_dirty.rects = reinterpret_cast<Rect*>(block + kOffDirty);
     g_dirty.count = 0;

     g_sdf.data = reinterpret_cast<float*>(block + kOffSdf);

     g_msaa.pixels = reinterpret_cast<std::uint32_t*>(block + kOffMsaa);
     g_msaa.msaaW = buffer.width * 2u;
     g_msaa.msaaH = buffer.height * 2u;

     for (std::size_t g = 0; g < kSdfGlyphs; ++g) BuildGlyphSdf(g);

     DrawCommand cmd;
     cmd.type = CmdType::Clear;
     cmd.data.clear = {.colour = 0};
     g_mesh.Push(cmd);
}

DLLEXPORT void VidClearScreen(std::uint32_t colour)
{
     DrawCommand cmd;
     cmd.type = CmdType::Clear;
     cmd.data.clear = {.colour = colour};
     g_mesh.Push(cmd);
}

DLLEXPORT void VidDrawRect(std::uint32_t x, std::uint32_t y, std::uint32_t width, std::uint32_t height,
                           std::uint32_t colour)
{
     DrawCommand cmd;
     cmd.type = CmdType::Rect;
     cmd.data.rect = {.x = x, .y = y, .width = width, .height = height, .colour = colour};
     g_mesh.Push(cmd);
}

DLLEXPORT void VidDrawChar(std::uint32_t x, std::uint32_t y, char c, std::uint32_t colour, std::uint8_t scale)
{
     DrawCommand cmd;
     cmd.type = CmdType::Char;
     cmd.data.chr = {.x = x, .y = y, .colour = colour, .c = c, .scale = scale};
     g_mesh.Push(cmd);
}

DLLEXPORT void VidSetPixel(std::uint32_t x, std::uint32_t y, std::uint32_t colour)
{
     DrawCommand cmd;
     cmd.type = CmdType::Pixel;
     cmd.data.pixel = {.x = x, .y = y, .colour = colour};
     g_mesh.Push(cmd);
}

DLLEXPORT void VidDrawLine(std::uint32_t x0, std::uint32_t y0, std::uint32_t x1, std::uint32_t y1, std::uint32_t colour)
{
     DrawCommand cmd;
     cmd.type = CmdType::Line;
     cmd.data.line = {.x0 = x0, .y0 = y0, .x1 = x1, .y1 = y1, .colour = colour};
     g_mesh.Push(cmd);
}

DLLEXPORT void VidDrawEllipse(std::uint32_t cx, std::uint32_t cy, std::uint32_t rx, std::uint32_t ry,
                              std::uint32_t colour)
{
     DrawCommand cmd;
     cmd.type = CmdType::Ellipse;
     cmd.data.ellipse = {.cx = cx, .cy = cy, .rx = rx, .ry = ry, .colour = colour};
     g_mesh.Push(cmd);
}

DLLEXPORT void VidDrawRoundedRect(std::uint32_t x, std::uint32_t y, std::uint32_t width, std::uint32_t height,
                                  std::uint32_t radius, std::uint32_t colour)
{
     DrawCommand cmd;
     cmd.type = CmdType::RoundedRect;
     cmd.data.roundedRect = {.x = x, .y = y, .width = width, .height = height, .radius = radius, .colour = colour};
     g_mesh.Push(cmd);
}

DLLEXPORT void VidDrawTriangle(std::int32_t x0, std::int32_t y0, std::int32_t x1, std::int32_t y1, std::int32_t x2,
                               std::int32_t y2, std::uint32_t colour)
{
     DrawCommand cmd;
     cmd.type = CmdType::Triangle;
     cmd.data.triangle = {.x0 = x0, .y0 = y0, .x1 = x1, .y1 = y1, .x2 = x2, .y2 = y2, .colour = colour};
     g_mesh.Push(cmd);
}

DLLEXPORT void VidPresent()
{
     const DrawCommand* const cmdBegin = g_mesh.commands;
     const DrawCommand* const cmdEnd = g_mesh.commands + g_mesh.count;
     for (const DrawCommand* it = cmdBegin; it != cmdEnd; ++it)
     {
          const DrawCommand& cmd = *it;
          Rect touched{};
          switch (cmd.type)
          {
          case CmdType::Clear: touched = RasteriseClear(cmd.data.clear); break;
          case CmdType::Rect: touched = RasteriseRect(cmd.data.rect); break;
          case CmdType::Pixel: touched = RasterisePixel(cmd.data.pixel); break;
          case CmdType::Char: touched = RasteriseChar(cmd.data.chr); break;
          case CmdType::Line: touched = RasteriseLine(cmd.data.line); break;
          case CmdType::Ellipse: touched = RasteriseEllipse(cmd.data.ellipse); break;
          case CmdType::RoundedRect: touched = RasteriseRoundedRect(cmd.data.roundedRect); break;
          case CmdType::Triangle: touched = RasteriseTriangle(cmd.data.triangle); break;
          case CmdType::Bitmap: touched = RasteriseBitmap(cmd.data.bitmap); break;
          case CmdType::Spinner: touched = RasteriseSpinner(cmd.data.spinner); break;
          }
          g_dirty.Add(touched, g_buffer.width, g_buffer.height);
     }

     g_mesh.Reset();
}

DLLEXPORT void VidExchange()
{
     if (g_buffer.vram == nullptr)
     {
          g_dirty.Reset();
          return;
     }

     if (g_dirty.count == 0) return;

     const std::size_t n = g_dirty.count;
     Rect sorted[kMaxDirtyRects]{}; // NOLINT
     for (std::size_t i = 0; i < n; ++i) sorted[i] = g_dirty.rects[i];
     VidInsertionSort(sorted, sorted + n, [](const Rect& a, const Rect& b) noexcept { return a.y0 < b.y0; });

     Rect bands[kMaxDirtyRects]{}; // NOLINT
     std::size_t bandCount = 0;
     bands[bandCount++] = sorted[0];
     for (std::size_t i = 1; i < n; ++i)
     {
          Rect& cur = bands[bandCount - 1];
          if (sorted[i].y0 <= cur.y1) cur.y1 = VidMax(cur.y1, sorted[i].y1);
          else
               bands[bandCount++] = sorted[i];
     }

     g_dirty.Reset();

     for (std::size_t i = 0; i < bandCount; ++i) BitBltRect(bands[i]);
}

DLLEXPORT void VidGetDimensions(std::uint32_t& width, std::uint32_t& height)
{
     width = g_buffer.width;
     height = g_buffer.height;
}

DLLEXPORT void VidSetCursorPosition(std::uint32_t x, std::uint32_t y)
{
     g_cursorX = x;
     g_cursorY = y;
}

DLLEXPORT void VidGetCursorPosition(std::uint32_t& x, std::uint32_t& y)
{
     x = g_cursorX;
     y = g_cursorY;
}

DLLEXPORT void VidExchangeBuffers()
{
     VidPresent();
     VidExchange();
}
DLLEXPORT void VidDrawBitmap(std::uint32_t x, std::uint32_t y, const void* bmpData, BitmapOrientation orientation,
                             std::size_t bmpSize)
{
     if (bmpData == nullptr) return;
     if (bmpSize != 0 && bmpSize < sizeof(BmpFileHeader) + sizeof(BmpInfoHeader)) return;

     const auto* base = static_cast<const std::uint8_t*>(bmpData);
     const auto* fileHdr = reinterpret_cast<const BmpFileHeader*>(base);
     const auto* infoHdr = reinterpret_cast<const BmpInfoHeader*>(base + sizeof(BmpFileHeader));

     if (fileHdr->type != 0x4D42u) return;
     if (infoHdr->compression != 0u) return;
     if (infoHdr->bitCount != 24u && infoHdr->bitCount != 32u) return;
     if (bmpSize != 0 && fileHdr->offBits >= bmpSize) return;

     const std::uint32_t width = static_cast<std::uint32_t>(infoHdr->width);
     const bool bottomUp = infoHdr->height > 0;
     const std::uint32_t height =
         bottomUp ? static_cast<std::uint32_t>(infoHdr->height) : static_cast<std::uint32_t>(-infoHdr->height);
     const std::uint32_t bytesPerPixel = infoHdr->bitCount / 8u;
     const std::uint32_t pitch = (width * bytesPerPixel + 3u) & ~3u;

     DrawCommand cmd;
     cmd.type = CmdType::Bitmap;
     cmd.data.bitmap = {.x = x,
                        .y = y,
                        .data = base + fileHdr->offBits,
                        .width = width,
                        .height = height,
                        .pitch = pitch,
                        .bytesPerPixel = static_cast<std::uint8_t>(bytesPerPixel),
                        .orientation = orientation,
                        .bottomUp = bottomUp};
     g_mesh.Push(cmd);
}
DLLEXPORT void VidDrawSpinner(std::uint32_t cx, std::uint32_t cy, std::uint32_t radius, std::uint32_t colour,
                              std::uint32_t tick, SpinnerStyle style)
{
     DrawCommand cmd;
     cmd.type = CmdType::Spinner;
     cmd.data.spinner = {.cx = cx, .cy = cy, .radius = radius, .colour = colour, .tick = tick, .style = style};
     g_mesh.Push(cmd);
}
