#pragma once

#include <cstdint>
namespace device
{
     enum struct DeviceType : std::uint16_t // NOLINT
     {
          Unknown = 0,
          PCI = 1,
          USB = 2,
          ACPI = 3,
          HID = 4,
          ROOT = 5
     };
     struct Device
     {
          DeviceType type{};
          Device* parent{};
     };
} // namespace device
