#pragma once

#include <array>
#include <charconv>
#include <cstddef>
#include <cstdint>
#include <span>
#include <string>
#include <string_view>
#include <tuple>
#include <type_traits>
#include "operations.h"

namespace debugging
{
     constexpr std::size_t MaxSerialLine = 256;
     enum struct FormatKind : std::uint8_t
     {
          Default,
          Decimal,
          HexLower,
          HexUpper,
          Base2
     };

     struct FormatSpec
     {
          FormatKind kind{FormatKind::Default};
     };

     template <typename T> struct SerialFormatter
     {
          NO_ASAN static void Write(const T& unnamed, const FormatSpec& spec = {})
          {
               static_assert(sizeof(unnamed) == 0, "SerialFormatter not implemented for this type");
          }
     };

     template <> struct SerialFormatter<char8_t>
     {
          NO_ASAN static void Write(char8_t value, const FormatSpec& spec = {})
          {
               operations::WriteSerialCharacter(static_cast<char>(value));
          }
     };

     template <> struct SerialFormatter<char8_t*>
     {
          NO_ASAN static void Write(const char8_t* value, const FormatSpec& spec = {})
          {
               while (value[0] != 0) operations::WriteSerialCharacter(static_cast<char>(*value++));
          }
     };

     template <> struct SerialFormatter<std::u8string>
     {
          NO_ASAN static void Write(const std::u8string& value, const FormatSpec& spec = {})
          {
               for (char8_t c : value) operations::WriteSerialCharacter(static_cast<char>(c));
          }
     };

     template <> struct SerialFormatter<std::string>
     {
          NO_ASAN static void Write(const std::string& value, const FormatSpec& spec = {})
          {
               for (char c : value) operations::WriteSerialCharacter(c);
          }
     };

     template <> struct SerialFormatter<const char8_t*>
     {
          NO_ASAN static void Write(const char8_t* value, const FormatSpec& spec = {})
          {
               while (value[0] != 0) operations::WriteSerialCharacter(static_cast<char>(*value++));
          }
     };

     template <> struct SerialFormatter<std::string_view>
     {
          NO_ASAN static void Write(const std::string_view value, const FormatSpec& spec = {})
          {
               operations::WriteSerialString(value.data(), value.size());
          }
     };

     template <std::integral TInteger> struct SerialFormatter<TInteger>
     {
          NO_ASAN static void Write(TInteger value, const FormatSpec& spec = {})
          {
               std::array<char, 64> buffer{};

               TInteger base = 10;
               if (spec.kind == FormatKind::HexLower || spec.kind == FormatKind::HexUpper) base = TInteger(16);
               else if (spec.kind == FormatKind::Base2) base = TInteger(2);

               auto result = std::to_chars(buffer.data(), buffer.data() + buffer.size(), value, base);

               if (spec.kind == FormatKind::HexUpper)
               {
                    for (auto& c : std::span(buffer.data(), result.ptr))
                    {
                         if (c >= 'a' && c <= 'f') c -= 32;
                    }
               }

               operations::WriteSerialString(buffer.data(), result.ptr - buffer.data());
          }
     };

     template <> struct SerialFormatter<bool>
     {
          NO_ASAN static void Write(bool value, const FormatSpec& spec = {})
          {
               operations::WriteSerialString(value ? "true" : "false", value ? 4 : 5);
          }
     };

     template <typename T> struct SerialFormatter<T*>
     {
          NO_ASAN static void Write(T* ptr, const FormatSpec& spec = {})
          {
               std::array<char, 2 + (sizeof(std::uintptr_t) * 2) + 1> buffer{};
               auto* destination = buffer.data();
               *destination++ = '0';
               *destination++ = 'x';

               constexpr auto hexDigits = "0123456789ABCDEF";
               std::uintptr_t value = reinterpret_cast<std::uintptr_t>(ptr);

               for (int i = static_cast<int>((sizeof(std::uintptr_t) * 2) - 1); i >= 0; --i)
               {
                    const auto shift = i * 4;
                    *destination++ = hexDigits[(value >> shift) & 0xF];
               }
               *destination = 0;
               SerialFormatter<char8_t*>::Write(reinterpret_cast<char8_t*>(buffer.data()), spec);
          }
     };

     template <> struct SerialFormatter<std::byte>
     {
          NO_ASAN static void Write(std::byte value, const FormatSpec& spec = {})
          {
               std::array<char, 5> buffer{};
               auto* destination = buffer.data();
               constexpr auto hexDigits = "0123456789ABCDEF";

               for (int i = 1; i >= 0; --i)
               {
                    const auto shift = i * 4;
                    *destination++ = hexDigits[(std::to_integer<int>(value) >> shift) & 0xF];
               }
               *destination = 0;
               SerialFormatter<char8_t*>::Write(reinterpret_cast<char8_t*>(buffer.data()), spec);
          }
     };

     template <std::size_t N> struct SerialFormatter<char8_t[N]> // NOLINT
     {
          NO_ASAN static void Write(const char8_t (&value)[N], const FormatSpec& spec = {}) // NOLINT
          {
               operations::WriteSerialString(value, N - 1);
          }
     };

     template <typename... TArgs> NO_ASAN void DbgWrite(const char8_t* format, TArgs&&... args)
     {
          const char8_t* ptr = format;
          std::tuple<TArgs...> tupleArgs(std::forward<TArgs>(args)...);
          std::size_t argIndex = 0;

          auto printArg = [&](std::size_t index, const FormatSpec& spec = {})
          {
               std::apply(
                   [&]<typename... T>(T&&... unpackedArgs)
                   {
                        std::size_t i = 0;
                        ((i++ == index ? (void)SerialFormatter<typename std::remove_cvref_t<T>>::Write(
                                             std::forward<T>(unpackedArgs), spec)
                                       : (void)0),
                         ...);
                   },
                   tupleArgs);
          };

          while (*ptr)
          {
               if (ptr[0] == '{')
               {
                    ptr++;

                    FormatSpec spec{};

                    if (*ptr == ':')
                    {
                         ptr++;

                         switch (*ptr)
                         {
                         case 'x': spec.kind = FormatKind::HexLower; break;
                         case 'X': spec.kind = FormatKind::HexUpper; break;
                         case 'd': spec.kind = FormatKind::Decimal; break;
                         case 'b': spec.kind = FormatKind::Base2; break;
                         default: break;
                         }

                         ptr++;
                    }

                    if (*ptr == '}')
                    {
                         printArg(argIndex++, spec);
                         ptr++;
                         continue;
                    }
               }

               SerialFormatter<char8_t>::Write(static_cast<char8_t>(*ptr++), {});
          }
     }

     inline std::size_t ReadSerialLine(char* buffer, std::size_t maxLength)
     {
          std::size_t count = 0;
          while (count + 1 < maxLength)
          {
               char c = operations::ReadSerialCharacter();

               if (c == 127)
               {
                    if (count > 0)
                    {
                         count--;
                         operations::WriteSerialCharacter('\b');
                         operations::WriteSerialCharacter(' ');
                         operations::WriteSerialCharacter('\b');
                    }
                    continue;
               }
               if (c < 127) operations::WriteSerialCharacter(c);
               // else DbgWrite(u8"{}", int(c));

               if (c == '\n' || c == '\r')
               {
                    buffer[count] = '\0';
                    return count;
               }

               buffer[count++] = c;
          }

          buffer[count] = '\0';
          return count;
     }
} // namespace debugging
