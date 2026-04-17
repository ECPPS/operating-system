#pragma once

#include <cstdint>
#include "pcie.h"
namespace usb
{
#pragma pack(push, 1)
     struct USBDeviceDescriptor
     {
          std::uint8_t bLength;
          std::uint8_t bDescriptorType;
          std::uint16_t bcdUSB;
          std::uint8_t bDeviceClass;
          std::uint8_t bDeviceSubClass;
          std::uint8_t bDeviceProtocol;
          std::uint8_t bMaxPacketSize0;
          std::uint16_t idVendor;
          std::uint16_t idProduct;
          std::uint16_t bcdDevice;
          std::uint8_t iManufacturer;
          std::uint8_t iProduct;
          std::uint8_t iSerialNumber;
          std::uint8_t bNumConfigurations;
     };
     struct USBConfigurationDescriptor
     {
          std::uint8_t bLength;
          std::uint8_t bDescriptorType;
          std::uint16_t wTotalLength;
          std::uint8_t bNumInterfaces;
          std::uint8_t bConfigurationValue;
          std::uint8_t iConfiguration;
          std::uint8_t bmAttributes;
          std::uint8_t bMaxPower;
     };

     struct USBInterfaceDescriptor
     {
          std::uint8_t bLength;
          std::uint8_t bDescriptorType;
          std::uint8_t bInterfaceNumber;
          std::uint8_t bAlternateSetting;
          std::uint8_t bNumEndpoints;
          std::uint8_t bInterfaceClass;
          std::uint8_t bInterfaceSubClass;
          std::uint8_t bInterfaceProtocol;
          std::uint8_t iInterface;
     };

     struct USBEndpointDescriptor
     {
          std::uint8_t bLength;
          std::uint8_t bDescriptorType;
          std::uint8_t bEndpointAddress;
          std::uint8_t bmAttributes;
          std::uint16_t wMaxPacketSize;
          std::uint8_t bInterval;
     };
#pragma pack(pop)

     void KiEnumerateUSB(volatile PCIHeader* hdr, std::uint8_t bus, std::uint8_t function);
} // namespace usb
