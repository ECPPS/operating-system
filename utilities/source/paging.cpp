#include <utils/identify.h>
#include <utils/memory.h>
#include <utils/operations.h>
#include <utility>

#if defined(ARCH_X8664)
#include <immintrin.h>
#if defined(COMPILER_MSVC)
#include <intrin.h>
#include <memory>
#endif
#elifdef ARCH_ARM64
#include <arm_neon.h>
#endif

#ifdef ARCH_X8664

inline void ZeroPage(void* ptr) noexcept
{
#if defined(COMPILER_CLANG)
     asm volatile("pxor %%xmm0, %%xmm0          \n\t"
                  "movntdq %%xmm0,    0(%[p])   \n\t"
                  "movntdq %%xmm0,   16(%[p])   \n\t"
                  "movntdq %%xmm0,   32(%[p])   \n\t"
                  "movntdq %%xmm0,   48(%[p])   \n\t"
                  "movntdq %%xmm0,   64(%[p])   \n\t"
                  "movntdq %%xmm0,   80(%[p])   \n\t"
                  "movntdq %%xmm0,   96(%[p])   \n\t"
                  "movntdq %%xmm0,  112(%[p])   \n\t"
                  "movntdq %%xmm0,  128(%[p])   \n\t"
                  "movntdq %%xmm0,  144(%[p])   \n\t"
                  "movntdq %%xmm0,  160(%[p])   \n\t"
                  "movntdq %%xmm0,  176(%[p])   \n\t"
                  "movntdq %%xmm0,  192(%[p])   \n\t"
                  "movntdq %%xmm0,  208(%[p])   \n\t"
                  "movntdq %%xmm0,  224(%[p])   \n\t"
                  "movntdq %%xmm0,  240(%[p])   \n\t"
                  "movntdq %%xmm0,  256(%[p])   \n\t"
                  "movntdq %%xmm0,  272(%[p])   \n\t"
                  "movntdq %%xmm0,  288(%[p])   \n\t"
                  "movntdq %%xmm0,  304(%[p])   \n\t"
                  "movntdq %%xmm0,  320(%[p])   \n\t"
                  "movntdq %%xmm0,  336(%[p])   \n\t"
                  "movntdq %%xmm0,  352(%[p])   \n\t"
                  "movntdq %%xmm0,  368(%[p])   \n\t"
                  "movntdq %%xmm0,  384(%[p])   \n\t"
                  "movntdq %%xmm0,  400(%[p])   \n\t"
                  "movntdq %%xmm0,  416(%[p])   \n\t"
                  "movntdq %%xmm0,  432(%[p])   \n\t"
                  "movntdq %%xmm0,  448(%[p])   \n\t"
                  "movntdq %%xmm0,  464(%[p])   \n\t"
                  "movntdq %%xmm0,  480(%[p])   \n\t"
                  "movntdq %%xmm0,  496(%[p])   \n\t"
                  "movntdq %%xmm0,  512(%[p])   \n\t"
                  "movntdq %%xmm0,  528(%[p])   \n\t"
                  "movntdq %%xmm0,  544(%[p])   \n\t"
                  "movntdq %%xmm0,  560(%[p])   \n\t"
                  "movntdq %%xmm0,  576(%[p])   \n\t"
                  "movntdq %%xmm0,  592(%[p])   \n\t"
                  "movntdq %%xmm0,  608(%[p])   \n\t"
                  "movntdq %%xmm0,  624(%[p])   \n\t"
                  "movntdq %%xmm0,  640(%[p])   \n\t"
                  "movntdq %%xmm0,  656(%[p])   \n\t"
                  "movntdq %%xmm0,  672(%[p])   \n\t"
                  "movntdq %%xmm0,  688(%[p])   \n\t"
                  "movntdq %%xmm0,  704(%[p])   \n\t"
                  "movntdq %%xmm0,  720(%[p])   \n\t"
                  "movntdq %%xmm0,  736(%[p])   \n\t"
                  "movntdq %%xmm0,  752(%[p])   \n\t"
                  "movntdq %%xmm0,  768(%[p])   \n\t"
                  "movntdq %%xmm0,  784(%[p])   \n\t"
                  "movntdq %%xmm0,  800(%[p])   \n\t"
                  "movntdq %%xmm0,  816(%[p])   \n\t"
                  "movntdq %%xmm0,  832(%[p])   \n\t"
                  "movntdq %%xmm0,  848(%[p])   \n\t"
                  "movntdq %%xmm0,  864(%[p])   \n\t"
                  "movntdq %%xmm0,  880(%[p])   \n\t"
                  "movntdq %%xmm0,  896(%[p])   \n\t"
                  "movntdq %%xmm0,  912(%[p])   \n\t"
                  "movntdq %%xmm0,  928(%[p])   \n\t"
                  "movntdq %%xmm0,  944(%[p])   \n\t"
                  "movntdq %%xmm0,  960(%[p])   \n\t"
                  "movntdq %%xmm0,  976(%[p])   \n\t"
                  "movntdq %%xmm0,  992(%[p])   \n\t"
                  "movntdq %%xmm0, 1008(%[p])   \n\t"
                  "movntdq %%xmm0, 1024(%[p])   \n\t"
                  "movntdq %%xmm0, 1040(%[p])   \n\t"
                  "movntdq %%xmm0, 1056(%[p])   \n\t"
                  "movntdq %%xmm0, 1072(%[p])   \n\t"
                  "movntdq %%xmm0, 1088(%[p])   \n\t"
                  "movntdq %%xmm0, 1104(%[p])   \n\t"
                  "movntdq %%xmm0, 1120(%[p])   \n\t"
                  "movntdq %%xmm0, 1136(%[p])   \n\t"
                  "movntdq %%xmm0, 1152(%[p])   \n\t"
                  "movntdq %%xmm0, 1168(%[p])   \n\t"
                  "movntdq %%xmm0, 1184(%[p])   \n\t"
                  "movntdq %%xmm0, 1200(%[p])   \n\t"
                  "movntdq %%xmm0, 1216(%[p])   \n\t"
                  "movntdq %%xmm0, 1232(%[p])   \n\t"
                  "movntdq %%xmm0, 1248(%[p])   \n\t"
                  "movntdq %%xmm0, 1264(%[p])   \n\t"
                  "movntdq %%xmm0, 1280(%[p])   \n\t"
                  "movntdq %%xmm0, 1296(%[p])   \n\t"
                  "movntdq %%xmm0, 1312(%[p])   \n\t"
                  "movntdq %%xmm0, 1328(%[p])   \n\t"
                  "movntdq %%xmm0, 1344(%[p])   \n\t"
                  "movntdq %%xmm0, 1360(%[p])   \n\t"
                  "movntdq %%xmm0, 1376(%[p])   \n\t"
                  "movntdq %%xmm0, 1392(%[p])   \n\t"
                  "movntdq %%xmm0, 1408(%[p])   \n\t"
                  "movntdq %%xmm0, 1424(%[p])   \n\t"
                  "movntdq %%xmm0, 1440(%[p])   \n\t"
                  "movntdq %%xmm0, 1456(%[p])   \n\t"
                  "movntdq %%xmm0, 1472(%[p])   \n\t"
                  "movntdq %%xmm0, 1488(%[p])   \n\t"
                  "movntdq %%xmm0, 1504(%[p])   \n\t"
                  "movntdq %%xmm0, 1520(%[p])   \n\t"
                  "movntdq %%xmm0, 1536(%[p])   \n\t"
                  "movntdq %%xmm0, 1552(%[p])   \n\t"
                  "movntdq %%xmm0, 1568(%[p])   \n\t"
                  "movntdq %%xmm0, 1584(%[p])   \n\t"
                  "movntdq %%xmm0, 1600(%[p])   \n\t"
                  "movntdq %%xmm0, 1616(%[p])   \n\t"
                  "movntdq %%xmm0, 1632(%[p])   \n\t"
                  "movntdq %%xmm0, 1648(%[p])   \n\t"
                  "movntdq %%xmm0, 1664(%[p])   \n\t"
                  "movntdq %%xmm0, 1680(%[p])   \n\t"
                  "movntdq %%xmm0, 1696(%[p])   \n\t"
                  "movntdq %%xmm0, 1712(%[p])   \n\t"
                  "movntdq %%xmm0, 1728(%[p])   \n\t"
                  "movntdq %%xmm0, 1744(%[p])   \n\t"
                  "movntdq %%xmm0, 1760(%[p])   \n\t"
                  "movntdq %%xmm0, 1776(%[p])   \n\t"
                  "movntdq %%xmm0, 1792(%[p])   \n\t"
                  "movntdq %%xmm0, 1808(%[p])   \n\t"
                  "movntdq %%xmm0, 1824(%[p])   \n\t"
                  "movntdq %%xmm0, 1840(%[p])   \n\t"
                  "movntdq %%xmm0, 1856(%[p])   \n\t"
                  "movntdq %%xmm0, 1872(%[p])   \n\t"
                  "movntdq %%xmm0, 1888(%[p])   \n\t"
                  "movntdq %%xmm0, 1904(%[p])   \n\t"
                  "movntdq %%xmm0, 1920(%[p])   \n\t"
                  "movntdq %%xmm0, 1936(%[p])   \n\t"
                  "movntdq %%xmm0, 1952(%[p])   \n\t"
                  "movntdq %%xmm0, 1968(%[p])   \n\t"
                  "movntdq %%xmm0, 1984(%[p])   \n\t"
                  "movntdq %%xmm0, 2000(%[p])   \n\t"
                  "movntdq %%xmm0, 2016(%[p])   \n\t"
                  "movntdq %%xmm0, 2032(%[p])   \n\t"
                  "movntdq %%xmm0, 2048(%[p])   \n\t"
                  "movntdq %%xmm0, 2064(%[p])   \n\t"
                  "movntdq %%xmm0, 2080(%[p])   \n\t"
                  "movntdq %%xmm0, 2096(%[p])   \n\t"
                  "movntdq %%xmm0, 2112(%[p])   \n\t"
                  "movntdq %%xmm0, 2128(%[p])   \n\t"
                  "movntdq %%xmm0, 2144(%[p])   \n\t"
                  "movntdq %%xmm0, 2160(%[p])   \n\t"
                  "movntdq %%xmm0, 2176(%[p])   \n\t"
                  "movntdq %%xmm0, 2192(%[p])   \n\t"
                  "movntdq %%xmm0, 2208(%[p])   \n\t"
                  "movntdq %%xmm0, 2224(%[p])   \n\t"
                  "movntdq %%xmm0, 2240(%[p])   \n\t"
                  "movntdq %%xmm0, 2256(%[p])   \n\t"
                  "movntdq %%xmm0, 2272(%[p])   \n\t"
                  "movntdq %%xmm0, 2288(%[p])   \n\t"
                  "movntdq %%xmm0, 2304(%[p])   \n\t"
                  "movntdq %%xmm0, 2320(%[p])   \n\t"
                  "movntdq %%xmm0, 2336(%[p])   \n\t"
                  "movntdq %%xmm0, 2352(%[p])   \n\t"
                  "movntdq %%xmm0, 2368(%[p])   \n\t"
                  "movntdq %%xmm0, 2384(%[p])   \n\t"
                  "movntdq %%xmm0, 2400(%[p])   \n\t"
                  "movntdq %%xmm0, 2416(%[p])   \n\t"
                  "movntdq %%xmm0, 2432(%[p])   \n\t"
                  "movntdq %%xmm0, 2448(%[p])   \n\t"
                  "movntdq %%xmm0, 2464(%[p])   \n\t"
                  "movntdq %%xmm0, 2480(%[p])   \n\t"
                  "movntdq %%xmm0, 2496(%[p])   \n\t"
                  "movntdq %%xmm0, 2512(%[p])   \n\t"
                  "movntdq %%xmm0, 2528(%[p])   \n\t"
                  "movntdq %%xmm0, 2544(%[p])   \n\t"
                  "movntdq %%xmm0, 2560(%[p])   \n\t"
                  "movntdq %%xmm0, 2576(%[p])   \n\t"
                  "movntdq %%xmm0, 2592(%[p])   \n\t"
                  "movntdq %%xmm0, 2608(%[p])   \n\t"
                  "movntdq %%xmm0, 2624(%[p])   \n\t"
                  "movntdq %%xmm0, 2640(%[p])   \n\t"
                  "movntdq %%xmm0, 2656(%[p])   \n\t"
                  "movntdq %%xmm0, 2672(%[p])   \n\t"
                  "movntdq %%xmm0, 2688(%[p])   \n\t"
                  "movntdq %%xmm0, 2704(%[p])   \n\t"
                  "movntdq %%xmm0, 2720(%[p])   \n\t"
                  "movntdq %%xmm0, 2736(%[p])   \n\t"
                  "movntdq %%xmm0, 2752(%[p])   \n\t"
                  "movntdq %%xmm0, 2768(%[p])   \n\t"
                  "movntdq %%xmm0, 2784(%[p])   \n\t"
                  "movntdq %%xmm0, 2800(%[p])   \n\t"
                  "movntdq %%xmm0, 2816(%[p])   \n\t"
                  "movntdq %%xmm0, 2832(%[p])   \n\t"
                  "movntdq %%xmm0, 2848(%[p])   \n\t"
                  "movntdq %%xmm0, 2864(%[p])   \n\t"
                  "movntdq %%xmm0, 2880(%[p])   \n\t"
                  "movntdq %%xmm0, 2896(%[p])   \n\t"
                  "movntdq %%xmm0, 2912(%[p])   \n\t"
                  "movntdq %%xmm0, 2928(%[p])   \n\t"
                  "movntdq %%xmm0, 2944(%[p])   \n\t"
                  "movntdq %%xmm0, 2960(%[p])   \n\t"
                  "movntdq %%xmm0, 2976(%[p])   \n\t"
                  "movntdq %%xmm0, 2992(%[p])   \n\t"
                  "movntdq %%xmm0, 3008(%[p])   \n\t"
                  "movntdq %%xmm0, 3024(%[p])   \n\t"
                  "movntdq %%xmm0, 3040(%[p])   \n\t"
                  "movntdq %%xmm0, 3056(%[p])   \n\t"
                  "movntdq %%xmm0, 3072(%[p])   \n\t"
                  "movntdq %%xmm0, 3088(%[p])   \n\t"
                  "movntdq %%xmm0, 3104(%[p])   \n\t"
                  "movntdq %%xmm0, 3120(%[p])   \n\t"
                  "movntdq %%xmm0, 3136(%[p])   \n\t"
                  "movntdq %%xmm0, 3152(%[p])   \n\t"
                  "movntdq %%xmm0, 3168(%[p])   \n\t"
                  "movntdq %%xmm0, 3184(%[p])   \n\t"
                  "movntdq %%xmm0, 3200(%[p])   \n\t"
                  "movntdq %%xmm0, 3216(%[p])   \n\t"
                  "movntdq %%xmm0, 3232(%[p])   \n\t"
                  "movntdq %%xmm0, 3248(%[p])   \n\t"
                  "movntdq %%xmm0, 3264(%[p])   \n\t"
                  "movntdq %%xmm0, 3280(%[p])   \n\t"
                  "movntdq %%xmm0, 3296(%[p])   \n\t"
                  "movntdq %%xmm0, 3312(%[p])   \n\t"
                  "movntdq %%xmm0, 3328(%[p])   \n\t"
                  "movntdq %%xmm0, 3344(%[p])   \n\t"
                  "movntdq %%xmm0, 3360(%[p])   \n\t"
                  "movntdq %%xmm0, 3376(%[p])   \n\t"
                  "movntdq %%xmm0, 3392(%[p])   \n\t"
                  "movntdq %%xmm0, 3408(%[p])   \n\t"
                  "movntdq %%xmm0, 3424(%[p])   \n\t"
                  "movntdq %%xmm0, 3440(%[p])   \n\t"
                  "movntdq %%xmm0, 3456(%[p])   \n\t"
                  "movntdq %%xmm0, 3472(%[p])   \n\t"
                  "movntdq %%xmm0, 3488(%[p])   \n\t"
                  "movntdq %%xmm0, 3504(%[p])   \n\t"
                  "movntdq %%xmm0, 3520(%[p])   \n\t"
                  "movntdq %%xmm0, 3536(%[p])   \n\t"
                  "movntdq %%xmm0, 3552(%[p])   \n\t"
                  "movntdq %%xmm0, 3568(%[p])   \n\t"
                  "movntdq %%xmm0, 3584(%[p])   \n\t"
                  "movntdq %%xmm0, 3600(%[p])   \n\t"
                  "movntdq %%xmm0, 3616(%[p])   \n\t"
                  "movntdq %%xmm0, 3632(%[p])   \n\t"
                  "movntdq %%xmm0, 3648(%[p])   \n\t"
                  "movntdq %%xmm0, 3664(%[p])   \n\t"
                  "movntdq %%xmm0, 3680(%[p])   \n\t"
                  "movntdq %%xmm0, 3696(%[p])   \n\t"
                  "movntdq %%xmm0, 3712(%[p])   \n\t"
                  "movntdq %%xmm0, 3728(%[p])   \n\t"
                  "movntdq %%xmm0, 3744(%[p])   \n\t"
                  "movntdq %%xmm0, 3760(%[p])   \n\t"
                  "movntdq %%xmm0, 3776(%[p])   \n\t"
                  "movntdq %%xmm0, 3792(%[p])   \n\t"
                  "movntdq %%xmm0, 3808(%[p])   \n\t"
                  "movntdq %%xmm0, 3824(%[p])   \n\t"
                  "movntdq %%xmm0, 3840(%[p])   \n\t"
                  "movntdq %%xmm0, 3856(%[p])   \n\t"
                  "movntdq %%xmm0, 3872(%[p])   \n\t"
                  "movntdq %%xmm0, 3888(%[p])   \n\t"
                  "movntdq %%xmm0, 3904(%[p])   \n\t"
                  "movntdq %%xmm0, 3920(%[p])   \n\t"
                  "movntdq %%xmm0, 3936(%[p])   \n\t"
                  "movntdq %%xmm0, 3952(%[p])   \n\t"
                  "movntdq %%xmm0, 3968(%[p])   \n\t"
                  "movntdq %%xmm0, 3984(%[p])   \n\t"
                  "movntdq %%xmm0, 4000(%[p])   \n\t"
                  "movntdq %%xmm0, 4016(%[p])   \n\t"
                  "movntdq %%xmm0, 4032(%[p])   \n\t"
                  "movntdq %%xmm0, 4048(%[p])   \n\t"
                  "movntdq %%xmm0, 4064(%[p])   \n\t"
                  "movntdq %%xmm0, 4080(%[p])   \n\t"
                  "sfence                        \n\t"
                  :
                  : [p] "r"(ptr)
                  : "memory", "xmm0");
#elifdef COMPILER_MSVC
     auto* p = static_cast<__m128i*>(std::assume_aligned<64>(ptr));
     const __m128i z = _mm_setzero_si128();
#define ZP_LINE(base)                                                                                                  \
     _mm_stream_si128(p + (base) + 0, z);                                                                              \
     _mm_stream_si128(p + (base) + 1, z);                                                                              \
     _mm_stream_si128(p + (base) + 2, z);                                                                              \
     _mm_stream_si128(p + (base) + 3, z)

     ZP_LINE(0);
     ZP_LINE(4);
     ZP_LINE(8);
     ZP_LINE(12);
     ZP_LINE(16);
     ZP_LINE(20);
     ZP_LINE(24);
     ZP_LINE(28);
     ZP_LINE(32);
     ZP_LINE(36);
     ZP_LINE(40);
     ZP_LINE(44);
     ZP_LINE(48);
     ZP_LINE(52);
     ZP_LINE(56);
     ZP_LINE(60);
     ZP_LINE(64);
     ZP_LINE(68);
     ZP_LINE(72);
     ZP_LINE(76);
     ZP_LINE(80);
     ZP_LINE(84);
     ZP_LINE(88);
     ZP_LINE(92);
     ZP_LINE(96);
     ZP_LINE(100);
     ZP_LINE(104);
     ZP_LINE(108);
     ZP_LINE(112);
     ZP_LINE(116);
     ZP_LINE(120);
     ZP_LINE(124);
     ZP_LINE(128);
     ZP_LINE(132);
     ZP_LINE(136);
     ZP_LINE(140);
     ZP_LINE(144);
     ZP_LINE(148);
     ZP_LINE(152);
     ZP_LINE(156);
     ZP_LINE(160);
     ZP_LINE(164);
     ZP_LINE(168);
     ZP_LINE(172);
     ZP_LINE(176);
     ZP_LINE(180);
     ZP_LINE(184);
     ZP_LINE(188);
     ZP_LINE(192);
     ZP_LINE(196);
     ZP_LINE(200);
     ZP_LINE(204);
     ZP_LINE(208);
     ZP_LINE(212);
     ZP_LINE(216);
     ZP_LINE(220);
     ZP_LINE(224);
     ZP_LINE(228);
     ZP_LINE(232);
     ZP_LINE(236);
     ZP_LINE(240);
     ZP_LINE(244);
     ZP_LINE(248);
     ZP_LINE(252);
#undef ZP_LINE
     _mm_sfence();
#endif
}

#elifdef ARCH_ARM64

static std::uint32_t ZpDcZvaBlockSize() noexcept
{
#if defined(COMPILER_CLANG)
     std::uint64_t dczid{};
     asm volatile("mrs %0, dczid_el0" : "=r"(dczid));
     if (dczid & (1u << 4)) return 0; // DZP=1: prohibited
     return 4u << (static_cast<std::uint32_t>(dczid) & 0xFu);
#elif defined(COMPILER_MSVC)
     const std::uint64_t dczid = _ReadStatusReg(18);
     if (dczid & (1u << 4)) return 0;
     return 4u << (static_cast<std::uint32_t>(dczid) & 0xFu);
#else
     return 0;
#endif
}

static const std::uint32_t g_dcZvaBlockSize = ZpDcZvaBlockSize();

inline void ZeroPage(void* ptr) noexcept
{
     if (g_dcZvaBlockSize != 0)
     {
#if defined(COMPILER_CLANG)
          auto addr = reinterpret_cast<std::uintptr_t>(ptr);
          const auto end = addr + 4096u;
          const auto bs = static_cast<std::uintptr_t>(g_dcZvaBlockSize);
          asm volatile("1:                        \n\t"
                       "dc  zva, %[a]             \n\t"
                       "add %[a], %[a], %[bs]     \n\t"
                       "cmp %[a], %[end]          \n\t"
                       "b.lo 1b                   \n\t"
                       : [a] "+r"(addr)
                       : [end] "r"(end), [bs] "r"(bs)
                       : "memory");
          asm volatile("dsb ishst" ::: "memory");
#elif defined(COMPILER_MSVC)
          std::memset(ptr, 0, 4096); // honestly, faster than an indirect call anyway...
#endif
     }
     else
     {
#if defined(COMPILER_CLANG)
          auto* p = static_cast<std::uint8_t*>(ptr);
          const auto* end = p + 4096u;
          asm volatile("movi v0.16b, #0              \n\t"
                       "1:                           \n\t"
                       "stnp q0, q0, [%[p]]          \n\t"
                       "stnp q0, q0, [%[p], #32]     \n\t"
                       "add  %[p], %[p], #64         \n\t"
                       "cmp  %[p], %[end]            \n\t"
                       "b.lo 1b                      \n\t"
                       : [p] "+r"(p)
                       : [end] "r"(end)
                       : "memory", "v0");
          asm volatile("dsb ishst" ::: "memory");
#elif defined(COMPILER_MSVC)
          auto* p = static_cast<std::uint64_t*>(ptr);
          const uint64x2_t z = vdupq_n_u64(0);
          for (int i = 0; i < 64; ++i, p += 8)
          {
               vst1q_u64(p, z);
               vst1q_u64(p + 2, z);
               vst1q_u64(p + 4, z);
               vst1q_u64(p + 6, z);
          }
          __dmb(_ARM64_BARRIER_ISHST);
#endif
     }
}
#endif

#if defined(ARCH_ARM64) && defined(COMPILER_MSVC)
#include <arm64intr.h>
#include <intrin.h>
#endif
namespace memory
{
     std::uintptr_t virtualOffset{};
}

namespace memory::paging
{
#ifdef ARCH_X8664 // vvv x86-64

     constexpr std::size_t GetPageSize() noexcept { return 0x1000; } // 4KB pages

     struct X64PageEntry
     {
          std::uint64_t present : 1;
          std::uint64_t writable : 1;
          std::uint64_t userAccessible : 1;
          std::uint64_t writeThrough : 1;
          std::uint64_t cacheDisable : 1;
          std::uint64_t accessed : 1;
          std::uint64_t dirty : 1;
          std::uint64_t largePage : 1;
          std::uint64_t global : 1;
          std::uint64_t available : 3;
          std::uint64_t physicalAddress : 40;
          std::uint64_t available2 : 11;
          std::uint64_t noExecute : 1;
     };

     std::uintptr_t CreatePageTable(void* (*allocator)(std::size_t))
     {
          auto* pml4 = static_cast<X64PageEntry*>(allocator(0x1000));
          if (pml4 == nullptr) return 0;

          std::memset(pml4, 0, 0x1000);

          return reinterpret_cast<std::uintptr_t>(pml4);
     }

     enum struct PatIndex : std::uint8_t
     {
          WB = 0,
          WT = 1,
          UCMinus = 2,
          UC = 3,
          WC = 4,
          WP = 5
     };

     struct CacheEncoding
     {
          bool pwt;
          bool pcd;
          PatIndex pat;
     };

     constexpr CacheEncoding KiEncodeCachePolicy(CachePolicy p)
     {
          switch (p)
          {
          case CachePolicy::WriteBack: return {.pwt = false, .pcd = false, .pat = PatIndex::WB};

          case CachePolicy::WriteThrough: return {.pwt = true, .pcd = false, .pat = PatIndex::WT};

          case CachePolicy::Uncacheable: return {.pwt = false, .pcd = true, .pat = PatIndex::UC};

          case CachePolicy::WriteCombining: return {.pwt = false, .pcd = false, .pat = PatIndex::WC};

          case CachePolicy::WriteProtected: return {.pwt = false, .pcd = false, .pat = PatIndex::WP};
          }

          return {.pwt = false, .pcd = true, .pat = PatIndex::UC};
     }

     bool MapPage(std::uintptr_t pageTableRoot, const PageMapping& mapping, void* (*allocator)(std::size_t))
     {
          auto* pml4 = reinterpret_cast<X64PageEntry*>(pageTableRoot + virtualOffset);
          const auto encoding = KiEncodeCachePolicy(mapping.cachePolicy);

          for (std::uintptr_t offset = 0; offset < mapping.size; offset += 0x1000)
          {
               std::uintptr_t vAddr = mapping.virtualAddress + offset;
               std::uintptr_t pAddr = mapping.physicalAddress + offset;

               std::uint64_t pml4Index = (vAddr >> 39) & 0x1FF;
               std::uint64_t pdptIndex = (vAddr >> 30) & 0x1FF;
               std::uint64_t pdIndex = (vAddr >> 21) & 0x1FF;
               std::uint64_t ptIndex = (vAddr >> 12) & 0x1FF;

               if (!pml4[pml4Index].present)
               {
                    auto* pdpt = static_cast<X64PageEntry*>(allocator(0x1000));
                    if (!pdpt) return false;
                    ZeroPage(pdpt);

                    pml4[pml4Index].physicalAddress = (reinterpret_cast<std::uintptr_t>(pdpt) - virtualOffset) >> 12;
                    pml4[pml4Index].present = 1;
                    pml4[pml4Index].writable = 1;
               }

               auto* pdpt = reinterpret_cast<X64PageEntry*>(
                   virtualOffset + (static_cast<std::uintptr_t>(pml4[pml4Index].physicalAddress) << 12));

               if (!pdpt[pdptIndex].present)
               {
                    auto* pd = static_cast<X64PageEntry*>(allocator(0x1000));
                    if (!pd) return false;
                    ZeroPage(pd);

                    pdpt[pdptIndex].physicalAddress = (reinterpret_cast<std::uintptr_t>(pd) - virtualOffset) >> 12;
                    pdpt[pdptIndex].present = 1;
                    pdpt[pdptIndex].writable = 1;
               }

               auto* pd = reinterpret_cast<X64PageEntry*>(
                   virtualOffset + (static_cast<std::uintptr_t>(pdpt[pdptIndex].physicalAddress) << 12));

               if (!pd[pdIndex].present || pd[pdIndex].largePage)
               {
                    auto* pt = static_cast<X64PageEntry*>(allocator(0x1000));
                    if (!pt) return false;
                    ZeroPage(pt);

                    pd[pdIndex].physicalAddress = (reinterpret_cast<std::uintptr_t>(pt) - virtualOffset) >> 12;
                    pd[pdIndex].present = 1;
                    pd[pdIndex].writable = 1;
                    pd[pdIndex].largePage = 0;
               }

               auto* pt = reinterpret_cast<X64PageEntry*>(
                   virtualOffset + (static_cast<std::uintptr_t>(pd[pdIndex].physicalAddress) << 12));

               pt[ptIndex].physicalAddress = pAddr >> 12;
               pt[ptIndex].present = 1;
               pt[ptIndex].writable = mapping.writable ? 1 : 0;
               pt[ptIndex].userAccessible = mapping.userAccessible ? 1 : 0;
               pt[ptIndex].cacheDisable = encoding.pcd ? 1 : 0;
               pt[ptIndex].writeThrough = encoding.pwt ? 1 : 0;
               pt[ptIndex].noExecute = mapping.executable ? 0 : 1;
          }

          return true;
     }
     bool UnmapPage(std::uintptr_t pageTableRoot, std::uintptr_t virtualAddress, bool invalidate)
     {
          auto* pml4 = reinterpret_cast<X64PageEntry*>(pageTableRoot + virtualOffset);

          std::uint64_t pml4Index = (virtualAddress >> 39) & 0x1FF;
          std::uint64_t pdptIndex = (virtualAddress >> 30) & 0x1FF;
          std::uint64_t pdIndex = (virtualAddress >> 21) & 0x1FF;
          std::uint64_t ptIndex = (virtualAddress >> 12) & 0x1FF;

          if (!pml4[pml4Index].present) return false;
          auto* pdpt = reinterpret_cast<X64PageEntry*>(
              virtualOffset + (static_cast<std::uintptr_t>(pml4[pml4Index].physicalAddress) << 12));

          if (!pdpt[pdptIndex].present) return false;
          auto* pd = reinterpret_cast<X64PageEntry*>(
              virtualOffset + (static_cast<std::uintptr_t>(pdpt[pdptIndex].physicalAddress) << 12));

          if (!pd[pdIndex].present || pd[pdIndex].largePage) return false;
          auto* pt = reinterpret_cast<X64PageEntry*>(virtualOffset +
                                                     (static_cast<std::uintptr_t>(pd[pdIndex].physicalAddress) << 12));

          if (!pt[ptIndex].present) return false;

          pt[ptIndex].present = 0;

          if (invalidate) InvalidatePage(virtualAddress);

          return true;
     }
     bool ProtectPage(std::uintptr_t pageTableRoot, std::uintptr_t virtualAddress, bool isWritable,
                      bool isUserAccessible, bool isExecutable, void* (*allocator)(std::size_t))
     {
          auto* pml4 = reinterpret_cast<X64PageEntry*>(pageTableRoot + virtualOffset);
          std::uint64_t pml4Index = (virtualAddress >> 39) & 0x1FF;
          std::uint64_t pdptIndex = (virtualAddress >> 30) & 0x1FF;
          std::uint64_t pdIndex = (virtualAddress >> 21) & 0x1FF;
          std::uint64_t ptIndex = (virtualAddress >> 12) & 0x1FF;

          if (!pml4[pml4Index].present) return false;
          auto* pdpt = reinterpret_cast<X64PageEntry*>(
              virtualOffset + (static_cast<std::uintptr_t>(pml4[pml4Index].physicalAddress) << 12));
          if (!pdpt[pdptIndex].present) return false;
          auto* pd = reinterpret_cast<X64PageEntry*>(
              virtualOffset + (static_cast<std::uintptr_t>(pdpt[pdptIndex].physicalAddress) << 12));
          if (!pd[pdIndex].present) return false;
          if (pd[pdIndex].largePage)
          {
               // split
               auto* pt = static_cast<X64PageEntry*>(allocator(0x1000));
               if (!pt) return false;
               ZeroPage(pt);
               for (std::size_t i = 0; i < 512; ++i)
               {
                    pt[i].physicalAddress = ((pd[pdIndex].physicalAddress << 12) + (i << 12)) >> 12;
                    pt[i].present = pd[pdIndex].present;
                    pt[i].writable = pd[pdIndex].writable;
                    pt[i].userAccessible = pd[pdIndex].userAccessible;
                    pt[i].writeThrough = pd[pdIndex].writeThrough;
                    pt[i].cacheDisable = pd[pdIndex].cacheDisable;
                    pt[i].largePage = 0;
                    pt[i].global = pd[pdIndex].global;
                    pt[i].noExecute = pd[pdIndex].noExecute;
               }
               pd[pdIndex].physicalAddress = (reinterpret_cast<std::uintptr_t>(pt) - virtualOffset) >> 12;
               pd[pdIndex].largePage = 0;
               pd[pdIndex].present = 1;
          }
          auto* pt = reinterpret_cast<X64PageEntry*>(virtualOffset +
                                                     (static_cast<std::uintptr_t>(pd[pdIndex].physicalAddress) << 12));
          if (!pt[ptIndex].present) return false;

          pt[ptIndex].writable = isWritable ? 1 : 0;
          pt[ptIndex].userAccessible = isUserAccessible ? 1 : 0;
          pt[ptIndex].noExecute = isExecutable ? 0 : 1;
          return true;
     }
     bool MapPhysicalMemoryDirect(std::uintptr_t pageTableRoot, std::size_t maxPhysicalAddress,
                                  void* (*allocator)(std::size_t), std::size_t startOffset)
     {
          constexpr std::uintptr_t DirectMapOffset = 0xFFFF800000000000ULL;
          constexpr std::size_t LargePageSize = 0x200000; // 2MB

          auto* pml4 = reinterpret_cast<X64PageEntry*>(pageTableRoot);

          // TODO: Check 2MiB support (maybe add 1GiB support?)
          for (std::uintptr_t pAddr = startOffset; pAddr < maxPhysicalAddress; pAddr += LargePageSize)
          {
               std::uintptr_t vAddr = pAddr + DirectMapOffset;

               std::uint64_t pml4Index = (vAddr >> 39) & 0x1FF;
               std::uint64_t pdptIndex = (vAddr >> 30) & 0x1FF;
               std::uint64_t pdIndex = (vAddr >> 21) & 0x1FF;

               if (!pml4[pml4Index].present)
               {
                    auto* pdpt = static_cast<X64PageEntry*>(allocator(0x1000));
                    if (!pdpt) return false;
                    for (std::size_t i = 0; i < 512; ++i) { reinterpret_cast<std::uint64_t*>(pdpt)[i] = 0; }

                    pml4[pml4Index].physicalAddress = reinterpret_cast<std::uintptr_t>(pdpt) >> 12;
                    pml4[pml4Index].present = 1;
                    pml4[pml4Index].writable = 1;
                    pml4[pml4Index].noExecute = 1;
               }

               auto* pdpt =
                   reinterpret_cast<X64PageEntry*>(static_cast<std::uintptr_t>(pml4[pml4Index].physicalAddress) << 12);

               if (!pdpt[pdptIndex].present)
               {
                    auto* pd = static_cast<X64PageEntry*>(allocator(0x1000));
                    if (!pd) return false;
                    for (std::size_t i = 0; i < 512; ++i) { reinterpret_cast<std::uint64_t*>(pd)[i] = 0; }

                    pdpt[pdptIndex].physicalAddress = reinterpret_cast<std::uintptr_t>(pd) >> 12;
                    pdpt[pdptIndex].present = 1;
                    pdpt[pdptIndex].writable = 1;
                    pdpt[pdptIndex].noExecute = 1;
               }

               auto* pd =
                   reinterpret_cast<X64PageEntry*>(static_cast<std::uintptr_t>(pdpt[pdptIndex].physicalAddress) << 12);

               pd[pdIndex].physicalAddress = pAddr >> 12;
               pd[pdIndex].present = 1;
               pd[pdIndex].writable = 1;
               pd[pdIndex].writeThrough = 1;
               pd[pdIndex].cacheDisable = 0;
               pd[pdIndex].largePage = 1;
               pd[pdIndex].global = 1;
               pd[pdIndex].noExecute = 1;
               pd[pdIndex].userAccessible = 0;
          }

          return true;
     }

     NO_ASAN void LoadPageTable(std::uintptr_t pageTableRoot)
     {
#ifdef COMPILER_MSVC
          __writecr3(pageTableRoot);
#else
          asm volatile("mov %0, %%cr3" ::"r"(pageTableRoot) : "memory");
#endif
     }

     NO_ASAN std::uintptr_t GetCurrentPageTable()
     {
#ifdef COMPILER_MSVC
          return __readcr3();
#else
          std::uintptr_t cr3 = 0;
          asm volatile("mov %%cr3, %0" : "=r"(cr3));
          return cr3;
#endif
     }

     NO_ASAN void InvalidatePage(std::uintptr_t virtualAddress)
     {
#ifdef COMPILER_MSVC
          __invlpg(reinterpret_cast<void*>(virtualAddress));
#else
          asm volatile("invlpg (%0)" ::"r"(virtualAddress) : "memory");
#endif
     }

     NO_ASAN void FlushTLB()
     {
          std::uintptr_t cr3 = GetCurrentPageTable();
          LoadPageTable(cr3);
     }

     NO_ASAN void EnablePaging()
     {
#ifdef COMPILER_MSVC
          std::uint64_t cr4 = __readcr4();
          cr4 |= (1ULL << 5); // CR4.PAE
          cr4 |= (1ULL << 4); // CR4.PSE
          cr4 |= (1ULL << 7); // CR4.PGE (global pages)
          cr4 &= ~(1ULL << 12);
          __writecr4(cr4);

          std::uint64_t efer = __readmsr(0xC0000080); // IA32_EFER
          efer |= (1ULL << 8);                        // EFER.LME
          __writemsr(0xC0000080, efer);

          std::uint64_t cr0 = __readcr0();
          cr0 |= (1ULL << 31); // CR0.PG
          __writecr0(cr0);
#else
          std::uint64_t cr4 = 0;
          asm volatile("mov %%cr4, %0" : "=r"(cr4));
          cr4 |= (1ULL << 4); // CR4.PSE
          cr4 |= (1ULL << 5); // CR4.PAE
          cr4 |= (1ULL << 7); // CR4.PGE (global pages)
          cr4 &= ~(1ULL << 12);
          cr4 &= ~(1ULL << 17);
          cr4 &= ~(1ULL << 22);
          asm volatile("mov %0, %%cr4" ::"r"(cr4) : "memory");

          std::uint32_t eferLow{};
          std::uint32_t eferHigh{};
          asm volatile("rdmsr" : "=a"(eferLow), "=d"(eferHigh) : "c"(0xC0000080));
          eferLow |= (1 << 8);  // EFER.LME
          eferLow |= (1 << 11); // NX
          asm volatile("wrmsr" ::"c"(0xC0000080), "a"(eferLow), "d"(eferHigh) : "memory");

          std::uint64_t cr0 = 0;
          asm volatile("mov %%cr0, %0" : "=r"(cr0));
          cr0 |= (1ULL << 31); // CR0.PG
          asm volatile("mov %0, %%cr0" ::"r"(cr0) : "memory");
#endif
     }

#elifdef ARCH_X8632 // ^^^ x86-64 / x86-32 vvv

     constexpr std::size_t GetPageSize() noexcept { return 0x1000; }

     struct X86PageEntry
     {
          std::uint64_t present : 1;
          std::uint64_t writable : 1;
          std::uint64_t userAccessible : 1;
          std::uint64_t writeThrough : 1;
          std::uint64_t cacheDisable : 1;
          std::uint64_t accessed : 1;
          std::uint64_t dirty : 1;
          std::uint64_t largePage : 1;
          std::uint64_t global : 1;
          std::uint64_t available : 3;
          std::uint64_t physicalAddress : 40;
          std::uint64_t available2 : 11;
          std::uint64_t noExecute : 1;
     };

     NO_ASAN std::uintptr_t CreatePageTable(void* (*allocator)(std::size_t))
     {
          auto* pdpt = static_cast<X86PageEntry*>(allocator(0x1000));
          if (!pdpt) return 0;

          for (std::size_t i = 0; i < 512; ++i) { reinterpret_cast<std::uint64_t*>(pdpt)[i] = 0; }

          return reinterpret_cast<std::uintptr_t>(pdpt);
     }

     NO_ASAN bool MapPage(std::uintptr_t pageTableRoot, const PageMapping& mapping, void* (*allocator)(std::size_t))
     {
          auto* pdpt = reinterpret_cast<X86PageEntry*>(pageTableRoot);

          for (std::uintptr_t offset = 0; offset < mapping.size; offset += 0x1000)
          {
               std::uintptr_t vAddr = mapping.virtualAddress + offset;
               std::uintptr_t pAddr = mapping.physicalAddress + offset;

               std::uint32_t pdptIndex = (vAddr >> 30) & 0x3; // 2 bits
               std::uint32_t pdIndex = (vAddr >> 21) & 0x1FF; // 9 bits
               std::uint32_t ptIndex = (vAddr >> 12) & 0x1FF; // 9 bits

               if (!pdpt[pdptIndex].present)
               {
                    auto* pd = static_cast<X86PageEntry*>(allocator(0x1000));
                    if (!pd) return false;
                    for (std::size_t i = 0; i < 512; ++i) { reinterpret_cast<std::uint64_t*>(pd)[i] = 0; }

                    pdpt[pdptIndex].physicalAddress = reinterpret_cast<std::uintptr_t>(pd) >> 12;
                    pdpt[pdptIndex].present = 1;
                    pdpt[pdptIndex].writable = 1;
               }

               auto* pd =
                   reinterpret_cast<X86PageEntry*>(static_cast<std::uintptr_t>(pdpt[pdptIndex].physicalAddress) << 12);

               if (!pd[pdIndex].present || pd[pdIndex].largePage)
               {
                    auto* pt = static_cast<X86PageEntry*>(allocator(0x1000));
                    if (!pt) return false;
                    for (std::size_t i = 0; i < 512; ++i) { reinterpret_cast<std::uint64_t*>(pt)[i] = 0; }

                    pd[pdIndex].physicalAddress = reinterpret_cast<std::uintptr_t>(pt) >> 12;
                    pd[pdIndex].present = 1;
                    pd[pdIndex].writable = 1;
                    pd[pdIndex].largePage = 0;
               }

               auto* pt =
                   reinterpret_cast<X86PageEntry*>(static_cast<std::uintptr_t>(pd[pdIndex].physicalAddress) << 12);

               pt[ptIndex].physicalAddress = pAddr >> 12;
               pt[ptIndex].present = 1;
               pt[ptIndex].writable = mapping.writable ? 1 : 0;
               pt[ptIndex].userAccessible = mapping.userAccessible ? 1 : 0;
               pt[ptIndex].cacheDisable = mapping.cacheDisable ? 1 : 0;
          }

          return true;
     }

     NO_ASAN bool MapPhysicalMemoryDirect(std::uintptr_t pageTableRoot, std::size_t maxPhysicalAddress,
                                          void* (*allocator)(std::size_t))
     {
          // For x86-32, use top of 32-bit address space
          constexpr std::uintptr_t DIRECT_MAP_OFFSET = 0xC0000000UL;
          constexpr std::size_t LARGE_PAGE_SIZE = 0x200000; // 2MB

          auto* pdpt = reinterpret_cast<X86PageEntry*>(pageTableRoot);

          // Map in 2MB chunks
          for (std::uintptr_t pAddr = 0; pAddr < maxPhysicalAddress && pAddr < (0x100000000ULL - DIRECT_MAP_OFFSET);
               pAddr += LARGE_PAGE_SIZE)
          {
               std::uintptr_t vAddr = pAddr + DIRECT_MAP_OFFSET;

               std::uint32_t pdptIndex = (vAddr >> 30) & 0x3;
               std::uint32_t pdIndex = (vAddr >> 21) & 0x1FF;

               // Ensure PDPT entry exists
               if (!pdpt[pdptIndex].present)
               {
                    auto* pd = static_cast<X86PageEntry*>(allocator(0x1000));
                    if (!pd) return false;
                    for (std::size_t i = 0; i < 512; ++i) { reinterpret_cast<std::uint64_t*>(pd)[i] = 0; }

                    pdpt[pdptIndex].physicalAddress = reinterpret_cast<std::uintptr_t>(pd) >> 12;
                    pdpt[pdptIndex].present = 1;
                    pdpt[pdptIndex].writable = 1;
                    pdpt[pdptIndex].noExecute = 1;
               }

               auto* pd =
                   reinterpret_cast<X86PageEntry*>(static_cast<std::uintptr_t>(pdpt[pdptIndex].physicalAddress) << 12);

               // Create 2MB page entry with: WT, G, RW, XD
               pd[pdIndex].physicalAddress = pAddr >> 12;
               pd[pdIndex].present = 1;
               pd[pdIndex].writable = 1;
               pd[pdIndex].writeThrough = 1; // WT
               pd[pdIndex].cacheDisable = 0;
               pd[pdIndex].largePage = 1; // 2MB page
               pd[pdIndex].global = 1;    // G
               pd[pdIndex].noExecute = 1; // XD
               pd[pdIndex].userAccessible = 0;
          }

          return true;
     }

     NO_ASAN void LoadPageTable(std::uintptr_t pageTableRoot)
     {
          asm volatile("mov %0, %%cr3" ::"r"(pageTableRoot) : "memory");
     }

     NO_ASAN std::uintptr_t GetCurrentPageTable()
     {
          std::uintptr_t cr3{};
          asm volatile("mov %%cr3, %0" : "=r"(cr3));
          return cr3;
     }

     NO_ASAN void InvalidatePage(std::uintptr_t virtualAddress)
     {
          asm volatile("invlpg (%0)" ::"r"(virtualAddress) : "memory");
     }

     NO_ASAN void FlushTLB()
     {
          std::uintptr_t cr3 = GetCurrentPageTable();
          LoadPageTable(cr3);
     }

     NO_ASAN void EnablePaging()
     {
          std::uint32_t cr4 = 0;
          asm volatile("mov %%cr4, %0" : "=r"(cr4));
          cr4 |= (1 << 5); // CR4.PAE
          cr4 |= (1 << 4); // CR4.PSE
          cr4 |= (1 << 7); // CR4.PGE (global pages)
          asm volatile("mov %0, %%cr4" ::"r"(cr4) : "memory");

          std::uint32_t cr0 = 0;
          asm volatile("mov %%cr0, %0" : "=r"(cr0));
          cr0 |= (1U << 31); // CR0.PG
          asm volatile("mov %0, %%cr0" ::"r"(cr0) : "memory");
     }

#elifdef ARCH_ARM64 // ^^^ x86-32 / ARM64 vvv

     constexpr std::size_t GetPageSize() noexcept { return 0x1000; } // 4KB pages

     struct ARMPageEntry
     {
          std::uint64_t valid : 1;
          std::uint64_t table : 1; // 1 = table/page, 0 = block
          std::uint64_t attrIndex : 3;
          std::uint64_t ns : 1;
          std::uint64_t ap : 2; // Access permissions
          std::uint64_t sh : 2; // Shareability
          std::uint64_t af : 1; // Access flag
          std::uint64_t ng : 1; // Not global
          std::uint64_t address : 36;
          std::uint64_t reserved : 4;
          std::uint64_t contiguous : 1;
          std::uint64_t pxn : 1; // Privileged execute never
          std::uint64_t uxn : 1; // Unprivileged execute never
          std::uint64_t ignored : 9;
     };

     NO_ASAN std::uintptr_t CreatePageTable(void* (*allocator)(std::size_t))
     {
          auto* l0 = static_cast<ARMPageEntry*>(allocator(0x1000));
          if (!l0) return 0;

          for (std::size_t i = 0; i < 512; ++i) { reinterpret_cast<std::uint64_t*>(l0)[i] = 0; }

          return reinterpret_cast<std::uintptr_t>(l0);
     }

     NO_ASAN bool MapPage(std::uintptr_t pageTableRoot, const PageMapping& mapping, void* (*allocator)(std::size_t))
     {
          auto* l0 = reinterpret_cast<ARMPageEntry*>(pageTableRoot + virtualOffset);

          for (std::uintptr_t offset = 0; offset < mapping.size; offset += 0x1000)
          {
               const std::uintptr_t vAddr = mapping.virtualAddress + offset;
               const std::uintptr_t pAddr = mapping.physicalAddress + offset;

               const std::uint64_t l0Index = (vAddr >> 39) & 0x1FF;
               const std::uint64_t l1Index = (vAddr >> 30) & 0x1FF;
               const std::uint64_t l2Index = (vAddr >> 21) & 0x1FF;
               const std::uint64_t l3Index = (vAddr >> 12) & 0x1FF;

               if (!l0[l0Index].valid)
               {
                    auto* l1 = static_cast<ARMPageEntry*>(allocator(0x1000));
                    if (!l1) return false;
                    ZeroPage(l1);

                    l0[l0Index].address = (reinterpret_cast<std::uintptr_t>(l1) - virtualOffset) >> 12;
                    l0[l0Index].valid = 1;
                    l0[l0Index].table = 1;
               }

               auto* l1 = reinterpret_cast<ARMPageEntry*>((static_cast<std::uintptr_t>(l0[l0Index].address) << 12) +
                                                          virtualOffset);

               if (!l1[l1Index].valid)
               {
                    auto* l2 = static_cast<ARMPageEntry*>(allocator(0x1000));
                    if (!l2) return false;
                    ZeroPage(l2);

                    l1[l1Index].address = (reinterpret_cast<std::uintptr_t>(l2) - virtualOffset) >> 12;
                    l1[l1Index].valid = 1;
                    l1[l1Index].table = 1;
               }

               auto* l2 = reinterpret_cast<ARMPageEntry*>((static_cast<std::uintptr_t>(l1[l1Index].address) << 12) +
                                                          virtualOffset);

               if (!l2[l2Index].valid || !l2[l2Index].table)
               {
                    auto* l3 = static_cast<ARMPageEntry*>(allocator(0x1000));
                    if (!l3) return false;
                    ZeroPage(l3);

                    l2[l2Index].address = (reinterpret_cast<std::uintptr_t>(l3) - virtualOffset) >> 12;
                    l2[l2Index].valid = 1;
                    l2[l2Index].table = 1;
               }

               auto* l3 = reinterpret_cast<ARMPageEntry*>((static_cast<std::uintptr_t>(l2[l2Index].address) << 12) +
                                                          virtualOffset);

               l3[l3Index].address = pAddr >> 12;
               l3[l3Index].valid = 1;
               l3[l3Index].table = 1;
               l3[l3Index].af = 1;
               l3[l3Index].ap = (mapping.writable ? 0 : 2) | (mapping.userAccessible ? 1 : 0);
               l3[l3Index].sh = 3;
               l3[l3Index].attrIndex = 0;
               l3[l3Index].ns = 0;
               l3[l3Index].ng = 0;
               l3[l3Index].pxn = 0;
               l3[l3Index].uxn = mapping.userAccessible ? 0 : 1;
          }

          return true;
     }

     NO_ASAN bool MapPhysicalMemoryDirect(std::uintptr_t pageTableRoot, std::size_t maxPhysicalAddress,
                                          void* (*allocator)(std::size_t), std::size_t startOffset)
     {
          constexpr std::uintptr_t DIRECT_MAP_OFFSET = 0xFFFF800000000000ULL;
          constexpr std::size_t BLOCK_SIZE = 0x200000;

          auto* l0 = reinterpret_cast<ARMPageEntry*>(pageTableRoot);

          for (std::uintptr_t pAddr = startOffset; pAddr < maxPhysicalAddress; pAddr += BLOCK_SIZE)
          {
               std::uintptr_t vAddr = pAddr + DIRECT_MAP_OFFSET;

               std::uint64_t l0Index = (vAddr >> 39) & 0x1FF;
               std::uint64_t l1Index = (vAddr >> 30) & 0x1FF;
               std::uint64_t l2Index = (vAddr >> 21) & 0x1FF;

               if (!l0[l0Index].valid)
               {
                    auto* l1 = static_cast<ARMPageEntry*>(allocator(0x1000));
                    if (!l1) return false;
                    for (std::size_t i = 0; i < 512; ++i) { reinterpret_cast<std::uint64_t*>(l1)[i] = 0; }

                    l0[l0Index].address = reinterpret_cast<std::uintptr_t>(l1) >> 12;
                    l0[l0Index].valid = 1;
                    l0[l0Index].table = 1;
               }

               auto* l1 = reinterpret_cast<ARMPageEntry*>(static_cast<std::uintptr_t>(l0[l0Index].address) << 12);

               if (!l1[l1Index].valid)
               {
                    auto* l2 = static_cast<ARMPageEntry*>(allocator(0x1000));
                    if (!l2) return false;
                    for (std::size_t i = 0; i < 512; ++i) { reinterpret_cast<std::uint64_t*>(l2)[i] = 0; }

                    l1[l1Index].address = reinterpret_cast<std::uintptr_t>(l2) >> 12;
                    l1[l1Index].valid = 1;
                    l1[l1Index].table = 1;
               }

               auto* l2 = reinterpret_cast<ARMPageEntry*>(static_cast<std::uintptr_t>(l1[l1Index].address) << 12);

               l2[l2Index].address = pAddr >> 12;
               l2[l2Index].valid = 1;
               l2[l2Index].table = 0;
               l2[l2Index].af = 1;
               l2[l2Index].ap = 0;
               l2[l2Index].sh = 3;
               l2[l2Index].attrIndex = 1;
               l2[l2Index].ns = 0;
               l2[l2Index].ng = 0;
               l2[l2Index].pxn = 1;
               l2[l2Index].uxn = 1;
          }

          return true;
     }

     NO_ASAN void LoadPageTable(std::uintptr_t pageTableRoot)
     {
#ifdef COMPILER_MSVC
          std::uint64_t tcr = 0;
          tcr |= (16ULL << 0);   // T0SZ = 16 => 48-bit VA
          tcr |= (1ULL << 8);    // IRGN0 = write-back, write-allocate
          tcr |= (1ULL << 10);   // ORGN0 = write-back, write-allocate
          tcr |= (3ULL << 12);   // SH0 = inner shareable
          tcr |= (0ULL << 14);   // TG0 = 4KB granule
          tcr |= (16ULL << 16);  // T1SZ = 16 => 48-bit VA
          tcr |= (1ULL << 24);   // IRGN1 = write-back, write-allocate
          tcr |= (1ULL << 26);   // ORGN1 = write-back, write-allocate
          tcr |= (3ULL << 28);   // SH1 = inner shareable
          tcr |= (2ULL << 30);   // TG1 = 4KB granule
          tcr |= (0x5ULL << 32); // IPS = 48-bit PA
          ::_WriteStatusReg(ARM64_SYSREG(3, 0, 2, 0, 2), tcr);

          // MAIR: Index 0 = 0xFF (write-back), Index 1 = 0xBB (write-through)
          std::uint64_t mair = 0xBBFFULL;
          ::_WriteStatusReg(ARM64_SYSREG(3, 0, 10, 2, 0), mair);

          ::__dsb(_ARM64_BARRIER_ISH);
          ::_WriteStatusReg(ARM64_SYSREG(3, 0, 2, 0, 0), pageTableRoot);
          ::_WriteStatusReg(ARM64_SYSREG(3, 0, 2, 0, 1), pageTableRoot);
          ::__isb(_ARM64_BARRIER_SY);
#else
          std::uint64_t tcr = 0;
          tcr |= (16ULL << 0);   // T0SZ = 16 => 48-bit VA
          tcr |= (1ULL << 8);    // IRGN0 = write-back, write-allocate
          tcr |= (1ULL << 10);   // ORGN0 = write-back, write-allocate
          tcr |= (3ULL << 12);   // SH0 = inner shareable
          tcr |= (0ULL << 14);   // TG0 = 4KB granule
          tcr |= (16ULL << 16);  // T1SZ = 16 => 48-bit VA
          tcr |= (1ULL << 24);   // IRGN1 = write-back, write-allocate
          tcr |= (1ULL << 26);   // ORGN1 = write-back, write-allocate
          tcr |= (3ULL << 28);   // SH1 = inner shareable
          tcr |= (2ULL << 30);   // TG1 = 4KB granule
          tcr |= (0x5ULL << 32); // IPS = 48-bit PA
          asm volatile("msr tcr_el1, %0" ::"r"(tcr));

          // MAIR: Index 0 = 0xFF (write-back), Index 1 = 0xBB (write-through)
          std::uint64_t mair = 0xBBFFULL;
          asm volatile("msr mair_el1, %0" ::"r"(mair));

          asm volatile("dsb ish");
          asm volatile("msr ttbr0_el1, %0" ::"r"(pageTableRoot));
          asm volatile("msr ttbr1_el1, %0" ::"r"(pageTableRoot));
          asm volatile("isb");
#endif
     }

     NO_ASAN std::uintptr_t GetCurrentPageTable()
     {
#ifdef COMPILER_MSVC
          return ::_ReadStatusReg(ARM64_SYSREG(3, 0, 2, 0, 0));
#else
          std::uintptr_t ttbr0 = 0;
          asm volatile("mrs %0, ttbr0_el1" : "=r"(ttbr0));
          return ttbr0;
#endif
     }

     NO_ASAN void InvalidatePage(std::uintptr_t virtualAddress)
     {
#ifdef COMPILER_MSVC
          ::__isb(_ARM64_BARRIER_SY);
#else
          asm volatile("tlbi vaae1is, %0" ::"r"(virtualAddress >> 12));
          asm volatile("dsb ish");
          asm volatile("isb");
#endif
     }

     NO_ASAN void FlushTLB()
     {
#ifdef COMPILER_MSVC
          ::__isb(_ARM64_BARRIER_SY);
#else
          asm volatile("tlbi vmalle1is");
          asm volatile("dsb ish");
          asm volatile("isb");
#endif
     }

     NO_ASAN void EnablePaging()
     {
#ifdef COMPILER_MSVC
          std::uint64_t sctlr = ::_ReadStatusReg(ARM64_SYSREG(3, 0, 1, 0, 0));

          sctlr |= (1ULL << 0);
          sctlr |= (1ULL << 2);
          sctlr |= (1ULL << 12);

          ::__dsb(_ARM64_BARRIER_ISH);
          ::_WriteStatusReg(ARM64_SYSREG(3, 0, 1, 0, 0), sctlr);
          ::__isb(_ARM64_BARRIER_SY);
#else
          std::uint64_t sctlr = 0;
          asm volatile("mrs %0, sctlr_el1" : "=r"(sctlr));

          sctlr |= (1ULL << 0);
          sctlr |= (1ULL << 2);
          sctlr |= (1ULL << 12);

          asm volatile("dsb ish");
          asm volatile("msr sctlr_el1, %0" ::"r"(sctlr));
          asm volatile("isb");
#endif
     }
#endif
} // namespace memory::paging
