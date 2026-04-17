#include <utils/identify.h>
#include <utils/operations.h>

#ifdef ARCH_ARM64
#include <arm_neon.h>
#endif

#ifdef ARCH_X8664

#pragma function(memmove)
extern "C" void* __cdecl memmove(_Out_writes_bytes_all_(_Size) void* _Dst, _In_reads_bytes_(_Size) const void* _Src,
                                 _In_ size_t _Size)
{
     return std::memcpy(_Dst, _Src, _Size);
}

#pragma function(memset)
NO_ASAN extern "C" void* __cdecl memset(_Out_ void* _Dst, _In_ int _Val, _In_ size_t _Size)
{
     auto* dst = static_cast<std::byte*>(_Dst);

     const std::byte byteVal = static_cast<std::byte>(_Val);
     const std::uint32_t val32 = static_cast<std::uint32_t>(_Val) * 0x01010101;

     while (((reinterpret_cast<std::uintptr_t>(dst) & 15) != 0) && _Size > 0)
     {
          *dst++ = static_cast<std::byte>(_Val);
          --_Size;
     }

     if (std::to_underlying(byteVal) == 0)
     {
          const __m128i zero = _mm_setzero_si128();

          while (_Size >= 64)
          {
               _mm_stream_si128(reinterpret_cast<__m128i*>(dst + 0), zero);
               _mm_stream_si128(reinterpret_cast<__m128i*>(dst + 16), zero);
               _mm_stream_si128(reinterpret_cast<__m128i*>(dst + 32), zero);
               _mm_stream_si128(reinterpret_cast<__m128i*>(dst + 48), zero);
               dst += 64;
               _Size -= 64;
          }

          while (_Size >= 16)
          {
               _mm_stream_si128(reinterpret_cast<__m128i*>(dst), zero);
               dst += 16;
               _Size -= 16;
          }

          while (_Size >= 8)
          {
               *reinterpret_cast<std::uint64_t*>(dst) = 0;
               dst += 8;
               _Size -= 8;
          }

          while (_Size-- > 0) *dst++ = std::byte{0};

          _mm_sfence();
          return _Dst;
     }

     const __m128i fill = _mm_set1_epi8(static_cast<char>(_Val));

     while (_Size >= 64)
     {
          _mm_stream_si128(reinterpret_cast<__m128i*>(dst + 0), fill);
          _mm_stream_si128(reinterpret_cast<__m128i*>(dst + 16), fill);
          _mm_stream_si128(reinterpret_cast<__m128i*>(dst + 32), fill);
          _mm_stream_si128(reinterpret_cast<__m128i*>(dst + 48), fill);
          dst += 64;
          _Size -= 64;
     }

     while (_Size >= 16)
     {
          _mm_stream_si128(reinterpret_cast<__m128i*>(dst), fill);
          dst += 16;
          _Size -= 16;
     }

     while (_Size >= 4)
     {
          *reinterpret_cast<std::uint32_t*>(dst) = val32;
          dst += 4;
          _Size -= 4;
     }

     while (_Size-- > 0) { *dst++ = static_cast<std::byte>(_Val); }

     _mm_sfence();

     return _Dst;
}

#pragma function(memcpy)
NO_ASAN extern "C" void* __cdecl memcpy(_Out_writes_bytes_all_(_Size) void* _Dst,
                                        _In_reads_bytes_(_Size) void const* _Src, _In_ size_t _Size)
{
     auto* dst = static_cast<std::byte*>(_Dst);
     const auto* src = static_cast<const std::byte*>(_Src);

     std::size_t i = 0;

     while (i < _Size && (reinterpret_cast<std::uintptr_t>(dst + i) & 0xF) != 0)
     {
          dst[i] = src[i];
          ++i;
     }

     const bool alignedSrc = (reinterpret_cast<std::uintptr_t>(src + i) & 15) == 0;

#ifdef STREAMING_MEMCPY
     for (; i + 64 <= _Size; i += 64)
     {
          _mm_prefetch(reinterpret_cast<const char*>(src + i + 64), _MM_HINT_T0);

          _mm_stream_si128(reinterpret_cast<__m128i*>(dst + i + 0),
                           _mm_stream_load_si128(reinterpret_cast<const __m128i*>(src + i + 0)));
          _mm_stream_si128(reinterpret_cast<__m128i*>(dst + i + 16),
                           _mm_stream_load_si128(reinterpret_cast<const __m128i*>(src + i + 16)));
          _mm_stream_si128(reinterpret_cast<__m128i*>(dst + i + 32),
                           _mm_stream_load_si128(reinterpret_cast<const __m128i*>(src + i + 32)));
          _mm_stream_si128(reinterpret_cast<__m128i*>(dst + i + 48),
                           _mm_stream_load_si128(reinterpret_cast<const __m128i*>(src + i + 48)));
     }
     for (; i + 16 <= _Size; i += 16)
     {
          _mm_stream_si128(reinterpret_cast<__m128i*>(dst + i),
                           _mm_stream_load_si128(reinterpret_cast<const __m128i*>(src + i)));
     }
#else
     const bool alignedDst = (reinterpret_cast<std::uintptr_t>(dst + i) & 15) == 0;

     if (alignedSrc && alignedDst)
     {
          for (; i + 64 <= _Size; i += 64)
          {
               _mm_prefetch(reinterpret_cast<const char*>(src + i + 64), _MM_HINT_T0);

               _mm_store_si128(reinterpret_cast<__m128i*>(dst + i + 0),
                               _mm_load_si128(reinterpret_cast<const __m128i*>(src + i + 0)));
               _mm_store_si128(reinterpret_cast<__m128i*>(dst + i + 16),
                               _mm_load_si128(reinterpret_cast<const __m128i*>(src + i + 16)));
               _mm_store_si128(reinterpret_cast<__m128i*>(dst + i + 32),
                               _mm_load_si128(reinterpret_cast<const __m128i*>(src + i + 32)));
               _mm_store_si128(reinterpret_cast<__m128i*>(dst + i + 48),
                               _mm_load_si128(reinterpret_cast<const __m128i*>(src + i + 48)));
          }
          for (; i + 16 <= _Size; i += 16)
          {
               _mm_store_si128(reinterpret_cast<__m128i*>(dst + i),
                               _mm_load_si128(reinterpret_cast<const __m128i*>(src + i)));
          }
     }
     else
     {
          for (; i + 64 <= _Size; i += 64)
          {
               _mm_storeu_si128(reinterpret_cast<__m128i*>(dst + i + 0),
                                _mm_loadu_si128(reinterpret_cast<const __m128i*>(src + i + 0)));
               _mm_storeu_si128(reinterpret_cast<__m128i*>(dst + i + 16),
                                _mm_loadu_si128(reinterpret_cast<const __m128i*>(src + i + 16)));
               _mm_storeu_si128(reinterpret_cast<__m128i*>(dst + i + 32),
                                _mm_loadu_si128(reinterpret_cast<const __m128i*>(src + i + 32)));
               _mm_storeu_si128(reinterpret_cast<__m128i*>(dst + i + 48),
                                _mm_loadu_si128(reinterpret_cast<const __m128i*>(src + i + 48)));
          }
          for (; i + 16 <= _Size; i += 16)
          {
               _mm_storeu_si128(reinterpret_cast<__m128i*>(dst + i),
                                _mm_loadu_si128(reinterpret_cast<const __m128i*>(src + i)));
          }
     }
#endif

     for (; i < _Size; ++i) dst[i] = src[i];

     return _Dst;
}

#pragma function(memcmp)
extern "C" int __cdecl memcmp(_In_reads_bytes_(_Size) const void* _Buf1, _In_reads_bytes_(_Size) const void* _Buf2,
                              _In_ size_t _Size)
{
     const auto* buf1 = static_cast<const unsigned char*>(_Buf1);
     const auto* buf2 = static_cast<const unsigned char*>(_Buf2);
     std::size_t i = 0;

     for (; i + 16 <= _Size; i += 16)
     {
          __m128i v1 = _mm_loadu_si128(reinterpret_cast<const __m128i*>(buf1 + i));
          __m128i v2 = _mm_loadu_si128(reinterpret_cast<const __m128i*>(buf2 + i));
          __m128i cmp = _mm_cmpeq_epi8(v1, v2);
          int mask = _mm_movemask_epi8(cmp);

          if (mask == 0xFFFF) continue;

          for (size_t j = i; j < i + 16; ++j)
               if (buf1[j] != buf2[j]) return buf1[j] < buf2[j] ? -1 : 1;
     }

     for (; i < _Size; ++i)
     {
          if (buf1[i] != buf2[i]) { return buf1[i] < buf2[i] ? -1 : 1; }
     }

     return 0;
}

#elifdef ARCH_X8632

#pragma function(memmove)
extern "C" void* __cdecl memmove(_Out_writes_bytes_all_(_Size) void* _Dst, _In_reads_bytes_(_Size) const void* _Src,
                                 _In_ size_t _Size)
{
     return std::memcpy(_Dst, _Src, _Size);
}

#pragma function(memset)
extern "C" void* __cdecl memset(_Out_ void* pTarget, _In_ int value, _In_ size_t cbTarget)
{
     auto* dst = static_cast<std::byte*>(pTarget);

     const std::byte byteVal = static_cast<std::byte>(value);
     const std::uint32_t val32 = value * 0x01010101;

     while (((reinterpret_cast<std::uintptr_t>(dst) & 15) != 0) && cbTarget > 0)
     {
          *dst++ = static_cast<std::byte>(value);
          --cbTarget;
     }

     if (std::to_underlying(byteVal) == 0)
     {
          const __m128i zero = _mm_setzero_si128();

          while (cbTarget >= 64)
          {
               _mm_stream_si128(reinterpret_cast<__m128i*>(dst + 0), zero);
               _mm_stream_si128(reinterpret_cast<__m128i*>(dst + 16), zero);
               _mm_stream_si128(reinterpret_cast<__m128i*>(dst + 32), zero);
               _mm_stream_si128(reinterpret_cast<__m128i*>(dst + 48), zero);
               dst += 64;
               cbTarget -= 64;
          }

          while (cbTarget >= 16)
          {
               _mm_stream_si128(reinterpret_cast<__m128i*>(dst), zero);
               dst += 16;
               cbTarget -= 16;
          }

          while (cbTarget >= 4)
          {
               *reinterpret_cast<std::uint32_t*>(dst) = 0;
               dst += 4;
               cbTarget -= 4;
          }

          while (cbTarget-- > 0) *dst++ = std::byte{0};

          _mm_sfence();
          return pTarget;
     }

     const __m128i fill = _mm_set1_epi8(static_cast<char>(value));

     while (cbTarget >= 64)
     {
          _mm_stream_si128(reinterpret_cast<__m128i*>(dst + 0), fill);
          _mm_stream_si128(reinterpret_cast<__m128i*>(dst + 16), fill);
          _mm_stream_si128(reinterpret_cast<__m128i*>(dst + 32), fill);
          _mm_stream_si128(reinterpret_cast<__m128i*>(dst + 48), fill);
          dst += 64;
          cbTarget -= 64;
     }

     while (cbTarget >= 16)
     {
          _mm_stream_si128(reinterpret_cast<__m128i*>(dst), fill);
          dst += 16;
          cbTarget -= 16;
     }

     while (cbTarget >= 4)
     {
          *reinterpret_cast<std::uint32_t*>(dst) = val32;
          dst += 4;
          cbTarget -= 4;
     }

     // Final tail
     while (cbTarget-- > 0) { *dst++ = static_cast<std::byte>(value); }

     _mm_sfence();

     return pTarget;
}

#pragma function(memcpy)
extern "C" void* __cdecl memcpy(_Out_writes_bytes_all_(_Size) void* _Dst, _In_reads_bytes_(_Size) void const* _Src,
                                _In_ size_t _Size)
{
     auto* dst = static_cast<std::byte*>(_Dst);
     const auto* src = static_cast<const std::byte*>(_Src);

     std::size_t i = 0;

     while (i < _Size && (reinterpret_cast<std::uintptr_t>(dst + i) & 0xF) != 0)
     {
          dst[i] = src[i];
          ++i;
     }

     const bool alignedSrc = (reinterpret_cast<std::uintptr_t>(src + i) & 15) == 0;

#ifdef STREAMING_MEMCPY
     for (; i + 64 <= _Size; i += 64)
     {
          _mm_prefetch(reinterpret_cast<const char*>(src + i + 64), _MM_HINT_T0);

          _mm_stream_si128(reinterpret_cast<__m128i*>(dst + i + 0),
                           _mm_stream_load_si128(reinterpret_cast<const __m128i*>(src + i + 0)));
          _mm_stream_si128(reinterpret_cast<__m128i*>(dst + i + 16),
                           _mm_stream_load_si128(reinterpret_cast<const __m128i*>(src + i + 16)));
          _mm_stream_si128(reinterpret_cast<__m128i*>(dst + i + 32),
                           _mm_stream_load_si128(reinterpret_cast<const __m128i*>(src + i + 32)));
          _mm_stream_si128(reinterpret_cast<__m128i*>(dst + i + 48),
                           _mm_stream_load_si128(reinterpret_cast<const __m128i*>(src + i + 48)));
     }
     for (; i + 16 <= _Size; i += 16)
     {
          _mm_stream_si128(reinterpret_cast<__m128i*>(dst + i),
                           _mm_stream_load_si128(reinterpret_cast<const __m128i*>(src + i)));
     }
#else
     const bool alignedDst = (reinterpret_cast<std::uintptr_t>(dst + i) & 15) == 0;

     if (alignedSrc && alignedDst)
     {
          for (; i + 64 <= _Size; i += 64)
          {
               _mm_prefetch(reinterpret_cast<const char*>(src + i + 64), _MM_HINT_T0);

               _mm_store_si128(reinterpret_cast<__m128i*>(dst + i + 0),
                               _mm_load_si128(reinterpret_cast<const __m128i*>(src + i + 0)));
               _mm_store_si128(reinterpret_cast<__m128i*>(dst + i + 16),
                               _mm_load_si128(reinterpret_cast<const __m128i*>(src + i + 16)));
               _mm_store_si128(reinterpret_cast<__m128i*>(dst + i + 32),
                               _mm_load_si128(reinterpret_cast<const __m128i*>(src + i + 32)));
               _mm_store_si128(reinterpret_cast<__m128i*>(dst + i + 48),
                               _mm_load_si128(reinterpret_cast<const __m128i*>(src + i + 48)));
          }
          for (; i + 16 <= _Size; i += 16)
          {
               _mm_store_si128(reinterpret_cast<__m128i*>(dst + i),
                               _mm_load_si128(reinterpret_cast<const __m128i*>(src + i)));
          }
     }
     else
     {
          for (; i + 64 <= _Size; i += 64)
          {
               _mm_storeu_si128(reinterpret_cast<__m128i*>(dst + i + 0),
                                _mm_loadu_si128(reinterpret_cast<const __m128i*>(src + i + 0)));
               _mm_storeu_si128(reinterpret_cast<__m128i*>(dst + i + 16),
                                _mm_loadu_si128(reinterpret_cast<const __m128i*>(src + i + 16)));
               _mm_storeu_si128(reinterpret_cast<__m128i*>(dst + i + 32),
                                _mm_loadu_si128(reinterpret_cast<const __m128i*>(src + i + 32)));
               _mm_storeu_si128(reinterpret_cast<__m128i*>(dst + i + 48),
                                _mm_loadu_si128(reinterpret_cast<const __m128i*>(src + i + 48)));
          }
          for (; i + 16 <= _Size; i += 16)
          {
               _mm_storeu_si128(reinterpret_cast<__m128i*>(dst + i),
                                _mm_loadu_si128(reinterpret_cast<const __m128i*>(src + i)));
          }
     }
#endif

     for (; i + 4 <= _Size; i += 4)
          *reinterpret_cast<std::uint32_t*>(dst + i) = *reinterpret_cast<const std::uint32_t*>(src + i);
     for (; i < _Size; ++i) dst[i] = src[i];

     return _Dst;
}

#pragma function(memcmp)
extern "C" int __cdecl memcmp(_In_reads_bytes_(_Size) const void* _Buf1, _In_reads_bytes_(_Size) const void* _Buf2,
                              _In_ size_t _Size)
{
     const auto* buf1 = static_cast<const unsigned char*>(_Buf1);
     const auto* buf2 = static_cast<const unsigned char*>(_Buf2);
     std::size_t i = 0;

     for (; i + 16 <= _Size; i += 16)
     {
          __m128i v1 = _mm_loadu_si128(reinterpret_cast<const __m128i*>(buf1 + i));
          __m128i v2 = _mm_loadu_si128(reinterpret_cast<const __m128i*>(buf2 + i));
          __m128i cmp = _mm_cmpeq_epi8(v1, v2);
          int mask = _mm_movemask_epi8(cmp);

          if (mask == 0xFFFF) continue;

          for (size_t j = i; j < i + 16; ++j)
               if (buf1[j] != buf2[j]) return buf1[j] < buf2[j] ? -1 : 1;
     }

     for (; i < _Size; ++i)
     {
          if (buf1[i] != buf2[i]) { return buf1[i] < buf2[i] ? -1 : 1; }
     }

     return 0;
}

#elifdef ARCH_ARM64

#pragma function(memmove)
extern "C" void* __cdecl memmove(_Out_writes_bytes_all_(_Size) void* _Dst, _In_reads_bytes_(_Size) const void* _Src,
                                 _In_ size_t _Size)
{
     return std::memcpy(_Dst, _Src, _Size);
}

#pragma function(memset)
extern "C" void* __cdecl memset(_Out_ void* _Dst, _In_ int _Val, _In_ size_t _Size)
{
     auto* dst = static_cast<std::byte*>(_Dst);
     const std::byte byteVal = static_cast<std::byte>(_Val);
     const std::uint32_t val32 = _Val * 0x01010101;
     const std::uint64_t val64 = static_cast<std::uint64_t>(val32) | (static_cast<std::uint64_t>(val32) << 32);

     while (((reinterpret_cast<std::uintptr_t>(dst) & 15) != 0) && _Size > 0)
     {
          *dst++ = byteVal;
          --_Size;
     }

     if (_Size >= 16)
     {
          const uint8x16_t fill = vdupq_n_u8(static_cast<std::uint8_t>(_Val));

          while (_Size >= 64)
          {
               vst1q_u8(reinterpret_cast<std::uint8_t*>(dst + 0), fill);
               vst1q_u8(reinterpret_cast<std::uint8_t*>(dst + 16), fill);
               vst1q_u8(reinterpret_cast<std::uint8_t*>(dst + 32), fill);
               vst1q_u8(reinterpret_cast<std::uint8_t*>(dst + 48), fill);
               dst += 64;
               _Size -= 64;
          }

          while (_Size >= 16)
          {
               vst1q_u8(reinterpret_cast<std::uint8_t*>(dst), fill);
               dst += 16;
               _Size -= 16;
          }
     }

     while (_Size >= 8)
     {
          *reinterpret_cast<std::uint64_t*>(dst) = val64;
          dst += 8;
          _Size -= 8;
     }

     while (_Size >= 4)
     {
          *reinterpret_cast<std::uint32_t*>(dst) = val32;
          dst += 4;
          _Size -= 4;
     }

     while (_Size-- > 0) { *dst++ = byteVal; }

     return _Dst;
}

#pragma function(memcpy)
extern "C" void* __cdecl memcpy(_Out_writes_bytes_all_(_Size) void* _Dst, _In_reads_bytes_(_Size) void const* _Src,
                                _In_ size_t _Size)
{
     auto* dst = static_cast<std::byte*>(_Dst);
     const auto* src = static_cast<const std::byte*>(_Src);
     std::size_t i = 0;

     while (i < _Size && (reinterpret_cast<std::uintptr_t>(dst + i) & 0xF) != 0)
     {
          dst[i] = src[i];
          ++i;
     }

     while (i + 64 <= _Size)
     {
          uint8x16_t v0 = vld1q_u8(reinterpret_cast<const std::uint8_t*>(src + i + 0));
          uint8x16_t v1 = vld1q_u8(reinterpret_cast<const std::uint8_t*>(src + i + 16));
          uint8x16_t v2 = vld1q_u8(reinterpret_cast<const std::uint8_t*>(src + i + 32));
          uint8x16_t v3 = vld1q_u8(reinterpret_cast<const std::uint8_t*>(src + i + 48));

          vst1q_u8(reinterpret_cast<std::uint8_t*>(dst + i + 0), v0);
          vst1q_u8(reinterpret_cast<std::uint8_t*>(dst + i + 16), v1);
          vst1q_u8(reinterpret_cast<std::uint8_t*>(dst + i + 32), v2);
          vst1q_u8(reinterpret_cast<std::uint8_t*>(dst + i + 48), v3);
          i += 64;
     }

     while (i + 16 <= _Size)
     {
          uint8x16_t v = vld1q_u8(reinterpret_cast<const std::uint8_t*>(src + i));
          vst1q_u8(reinterpret_cast<std::uint8_t*>(dst + i), v);
          i += 16;
     }

     while (i + 8 <= _Size)
     {
          *reinterpret_cast<std::uint64_t*>(dst + i) = *reinterpret_cast<const std::uint64_t*>(src + i);
          i += 8;
     }

     while (i + 4 <= _Size)
     {
          *reinterpret_cast<std::uint32_t*>(dst + i) = *reinterpret_cast<const std::uint32_t*>(src + i);
          i += 4;
     }

     while (i < _Size)
     {
          dst[i] = src[i];
          ++i;
     }

     return _Dst;
}

#pragma function(memcmp)
extern "C" int __cdecl memcmp(_In_reads_bytes_(_Size) const void* _Buf1, _In_reads_bytes_(_Size) const void* _Buf2,
                              _In_ size_t _Size)
{
     const auto* buf1 = static_cast<const unsigned char*>(_Buf1);
     const auto* buf2 = static_cast<const unsigned char*>(_Buf2);
     std::size_t i = 0;

     while (i + 16 <= _Size)
     {
          uint8x16_t v1 = vld1q_u8(buf1 + i);
          uint8x16_t v2 = vld1q_u8(buf2 + i);
          uint8x16_t cmp = vceqq_u8(v1, v2);

          uint64x2_t cmp64 = vreinterpretq_u64_u8(cmp);
          uint64_t low = vgetq_lane_u64(cmp64, 0);
          uint64_t high = vgetq_lane_u64(cmp64, 1);

          if ((low & high) != 0xFFFFFFFFFFFFFFFFULL)
          {
               for (size_t j = i; j < i + 16; ++j)
               {
                    if (buf1[j] != buf2[j]) { return buf1[j] < buf2[j] ? -1 : 1; }
               }
          }
          i += 16;
     }

     while (i < _Size)
     {
          if (buf1[i] != buf2[i]) { return buf1[i] < buf2[i] ? -1 : 1; }
          ++i;
     }

     return 0;
}

#endif
#pragma function(strlen)
_Check_return_ std::size_t __cdecl strlen(_In_z_ const char* _Str)
{
     constexpr auto k = sizeof(std::uintptr_t);
     constexpr std::uintptr_t HIMAGIC = std::uintptr_t{0x8080808080808080ull};
     constexpr std::uintptr_t LOMAGIC = std::uintptr_t{0x0101010101010101ull};

     const char* ptr = _Str;

     while (reinterpret_cast<std::uintptr_t>(ptr) % k != 0)
     {
          if (*ptr == '\0') return static_cast<std::size_t>(ptr - _Str);
          ++ptr;
     }

     const auto* wordPtr = reinterpret_cast<const std::uintptr_t*>(ptr);
     for (;; ++wordPtr)
     {
          const std::uintptr_t w = *wordPtr;

          if ((w - LOMAGIC) & ~w & HIMAGIC)
          {
               ptr = reinterpret_cast<const char*>(wordPtr);
               for (int i = 0; i < static_cast<int>(k); ++i)
               {
                    if (ptr[i] == '\0') return static_cast<std::size_t>(ptr + i - _Str);
               }
          }
     }
}
