#include "usb.h"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <span>
#include <vector>

#include "../cpu/interrupts.h"
#include "../kinit.h"
#include "../process/taskScheduler.h"
#include "pcie.h"
#include "utils/kdbg.h"

namespace uhci::reg
{
     constexpr std::uintptr_t Command = 0x00;     // USBCMD   (16-bit)
     constexpr std::uintptr_t Status = 0x02;      // USBSTS   (16-bit)
     constexpr std::uintptr_t InterruptEn = 0x04; // USBINTR  (16-bit)
     constexpr std::uintptr_t FrameNum = 0x06;    // FRNUM    (16-bit)
     constexpr std::uintptr_t FrameBase = 0x08;   // FRBASEADD (32-bit)
     constexpr std::uintptr_t SOFModify = 0x0C;   // SOFMOD   (8-bit)
     constexpr std::uintptr_t Port0 = 0x10;       // PORTSC1  (16-bit), Port1 at +2
} // namespace uhci::reg

namespace uhci::cmd
{
     constexpr std::uint16_t RunStop = 1 << 0;
     constexpr std::uint16_t HCReset = 1 << 1;
     constexpr std::uint16_t GlobalReset = 1 << 2;
     constexpr std::uint16_t MaxPacket64 = 1 << 7;
} // namespace uhci::cmd

namespace uhci::sts
{
     constexpr std::uint16_t UsbInterrupt = 1 << 0;
     constexpr std::uint16_t ErrorInterrupt = 1 << 1;
     constexpr std::uint16_t ResumeDetect = 1 << 2;
     constexpr std::uint16_t HostSysError = 1 << 3;
     constexpr std::uint16_t HCProcessErr = 1 << 4;
     constexpr std::uint16_t HCHalted = 1 << 5;      // RO
     constexpr std::uint16_t ClearableMask = 0x001F; // bits 0-4 only
} // namespace uhci::sts

namespace uhci::intr
{
     constexpr std::uint16_t TimeoutCRC = 1 << 0;
     constexpr std::uint16_t Resume = 1 << 1;
     constexpr std::uint16_t IOC = 1 << 2;
     constexpr std::uint16_t ShortPacket = 1 << 3;
} // namespace uhci::intr

namespace uhci::port
{
     constexpr std::uint16_t Connected = 1 << 0;
     constexpr std::uint16_t ConnectChange = 1 << 1; // W1C
     constexpr std::uint16_t Enabled = 1 << 2;
     constexpr std::uint16_t EnableChange = 1 << 3; // W1C
     constexpr std::uint16_t ResumeDetect = 1 << 6;
     constexpr std::uint16_t AlwaysOne = 1 << 7;
     constexpr std::uint16_t LowSpeed = 1 << 8;
     constexpr std::uint16_t Reset = 1 << 9;
     constexpr std::uint16_t Suspend = 1 << 12;
} // namespace uhci::port

namespace usb::pid
{
     constexpr std::uint8_t Setup = 0x2D;
     constexpr std::uint8_t In = 0x69;
     constexpr std::uint8_t Out = 0xE1;
} // namespace usb::pid

namespace uhci::ptr
{
     constexpr std::uint32_t Terminate = 1 << 0;
     constexpr std::uint32_t QueueHead = 1 << 1;  // 0 = TD
     constexpr std::uint32_t DepthFirst = 1 << 2; // TD linkPointer only
} // namespace uhci::ptr

struct alignas(16) UHCITransferDescriptor
{
     std::uint32_t linkPointer;
     std::uint32_t controlStatus;
     std::uint32_t token;
     std::uint32_t bufferPointer;
};

namespace uhci::td
{
     constexpr std::uint32_t BitStuffError = 1 << 17;
     constexpr std::uint32_t CRCTimeout = 1 << 18;
     constexpr std::uint32_t NAKed = 1 << 19;
     constexpr std::uint32_t Babble = 1 << 20;
     constexpr std::uint32_t DataBufferError = 1 << 21;
     constexpr std::uint32_t Stalled = 1 << 22;
     constexpr std::uint32_t Active = 1 << 23;
     constexpr std::uint32_t IOC = 1 << 24;
     constexpr std::uint32_t IsochronousTD = 1 << 25;
     constexpr std::uint32_t LowSpeed = 1 << 26;
     constexpr std::uint32_t ErrorMask = BitStuffError | CRCTimeout | Babble | DataBufferError | Stalled;
} // namespace uhci::td

struct alignas(16) UHCIQueueHead
{
     std::uint32_t headLinkPointer;    // horizontal: next QH (or terminate)
     std::uint32_t elementLinkPointer; // vertical: first TD (or terminate)
};

struct FrameEntry
{
     std::uint32_t value = uhci::ptr::Terminate;
};

//  Bits  7:0  = PID
//  Bits 14:8  = device address
//  Bits 18:15 = endpoint
//  Bit  19    = data toggle
//  Bits 31:21 = MaximumLength  (packet_size - 1); 0x7FF = zero-length packet

static constexpr std::uint32_t MakeToken(std::uint8_t pid, std::uint8_t addr, std::uint8_t ep, std::uint16_t len,
                                         bool toggle)
{
     const std::uint32_t maxLen = (len == 0) ? 0x7FFu : static_cast<std::uint32_t>(len - 1);
     return static_cast<std::uint32_t>(pid) | (static_cast<std::uint32_t>(addr) << 8) |
            (static_cast<std::uint32_t>(ep) << 15) | (static_cast<std::uint32_t>(toggle ? 1u : 0u) << 19) |
            (maxLen << 21);
}

static constexpr std::uint32_t MakeControlStatus(bool lowSpeed, bool ioc, bool active)
{
     return (active ? uhci::td::Active : 0u) | (ioc ? uhci::td::IOC : 0u) | (lowSpeed ? uhci::td::LowSpeed : 0u);
}

enum struct TransferType // NOLINT
{
     Control,
     Bulk,
     Interrupt,
     Isochronous
};

struct UHCITransfer
{
     enum struct State // NOLINT
     {
          Pending,
          Complete,
          Error
     };

     std::vector<std::unique_ptr<UHCITransferDescriptor>> tds;
     std::unique_ptr<UHCIQueueHead> qh;
     volatile State state = State::Pending;
     std::uint32_t errorStatus = 0;
     TransferType type = TransferType::Control;
     int scheduleSlot = 0;
     std::uint8_t devAddr = 0;
     std::uint8_t devEp = 0;
};

static bool KiIsrUsbUHCI(cpu::IInterruptFrame& frame, void* controller);

struct UHCIController
{
     void Initialise(std::uintptr_t ioBase, volatile PCIHeader* hdr, std::uint8_t busNum, std::uint8_t functionNum)
     {
          _ioBase = ioBase;
          _hdr = hdr;
          _bus = busNum;
          _function = functionNum;

          // KeRegisterInterruptHandler(hdr->interruptLine, hdr->interruptLine + 0x20, KiIsrUsbUHCI, this);
          DisableLegacyUSB();
          SetBusMaster();
          ResetController();
          AllocateFrameList();
          EnableInterrupts();
          StartController();
          DetectAndEnablePorts();
     }

     /// setupPacket : exactly 8 bytes
     /// dataBuffer  : may be empty
     /// dataIn      : true => device -> host, false => host -> device
     [[nodiscard]] std::shared_ptr<UHCITransfer> SubmitControlTransfer(std::uint8_t addr, std::uint8_t ep,
                                                                       bool lowSpeed,
                                                                       std::span<const std::uint8_t> setupPacket,
                                                                       std::span<std::uint8_t> dataBuffer, bool dataIn)
     {
          auto transfer = std::make_shared<UHCITransfer>();
          transfer->type = TransferType::Control;

          const std::size_t maxPacket = lowSpeed ? 8u : 64u;

          auto& setupTD = AllocTD(transfer);
          setupTD.token = MakeToken(usb::pid::Setup, addr, ep, 8, false);
          setupTD.controlStatus = MakeControlStatus(lowSpeed, false, true);
          setupTD.bufferPointer = PhysAddr(setupPacket.data());

          bool toggle = true;
          const std::uint8_t dataPID = dataIn ? usb::pid::In : usb::pid::Out;
          for (std::size_t offset = 0; offset < dataBuffer.size();)
          {
               const std::size_t len = std::min(dataBuffer.size() - offset, maxPacket);
               auto& dataTD = AllocTD(transfer);
               dataTD.token = MakeToken(dataPID, addr, ep, static_cast<std::uint16_t>(len), toggle);
               dataTD.controlStatus = MakeControlStatus(lowSpeed, false, true);
               dataTD.bufferPointer = PhysAddr(dataBuffer.data() + offset);
               toggle = !toggle;
               offset += len;
          }

          const std::uint8_t statusPID = dataIn ? usb::pid::Out : usb::pid::In;
          auto& statusTD = AllocTD(transfer);
          statusTD.token = MakeToken(statusPID, addr, ep, 0, true);
          statusTD.controlStatus = MakeControlStatus(lowSpeed, true, true); // IOC on last TD only
          debugging::DbgWrite(u8"UHCI: status TD token={:x}\n", statusTD.token);

          ChainAndInsert(transfer, 0 /* 1 ms slot */);
          return transfer;
     }

     [[nodiscard]] std::shared_ptr<UHCITransfer> SubmitInterruptTransfer(std::uint8_t addr, std::uint8_t ep,
                                                                         bool lowSpeed, std::span<std::uint8_t> buffer,
                                                                         std::uint8_t intervalMs)
     {
          auto transfer = std::make_shared<UHCITransfer>();
          transfer->type = TransferType::Interrupt;
          transfer->devAddr = addr;
          transfer->devEp = ep;

          auto& itd = AllocTD(transfer);
          itd.linkPointer = uhci::ptr::Terminate;
          itd.token = MakeToken(usb::pid::In, addr, ep, static_cast<std::uint16_t>(buffer.size()), false);
          itd.controlStatus = MakeControlStatus(lowSpeed, true, true);
          itd.bufferPointer = PhysAddr(buffer.data());

          ChainAndInsert(transfer, IntervalToSlot(intervalMs));
          return transfer;
     }

     void DispatchInterruptData(const UHCITransfer& transfer, const std::uint8_t* buf)
     {
          debugging::DbgWrite(u8"UHCI: interrupt addr={} ep={}: {:x} {:x} {:x} {:x}\n", transfer.devAddr,
                              transfer.devEp, buf[0], buf[1], buf[2], buf[3]);
     }

     void ChainAndInsertInterrupt(std::shared_ptr<UHCITransfer>& transfer, int slot, int phase)
     {
          auto& tds = transfer->tds;
          tds.back()->linkPointer = uhci::ptr::Terminate;

          transfer->qh = std::make_unique<UHCIQueueHead>();
          transfer->qh->elementLinkPointer = PhysAddr(tds.front().get()) & ~0x3u;
          transfer->qh->headLinkPointer = uhci::ptr::Terminate; // no horizontal chain
          transfer->scheduleSlot = slot;

          const int stride = 1 << (5 - slot); // slot 0=1ms, slot 5=32ms
          for (int i = phase; i < (int)FRAME_LIST_SIZE; i += stride * 2)
          {
               transfer->qh->headLinkPointer = _frameList[i].value; // daisy-chain
               _frameList[i].value = (PhysAddr(transfer->qh.get()) & ~0x3u) | uhci::ptr::QueueHead;
          }
          _pendingTransfers.push_back(transfer);
     }

     void RotateQHToTail(UHCIQueueHead* qh, int slot) const
     {
          UHCIQueueHead* anchor = _scheduleQHs[slot].get();

          const std::uint32_t anchorNext = anchor->headLinkPointer;
          if (anchorNext & uhci::ptr::Terminate) return;
          if ((anchorNext & ~0x3u) == (PhysAddr(qh) & ~0x3u))
          {
               if (qh->headLinkPointer & uhci::ptr::Terminate) return;

               anchor->headLinkPointer = qh->headLinkPointer;

               UHCIQueueHead* cur = reinterpret_cast<UHCIQueueHead*>(
                   static_cast<std::uintptr_t>(anchor->headLinkPointer & ~0x3u) + 0xffff'8000'0000'0000ULL);
               while (!(cur->headLinkPointer & uhci::ptr::Terminate))
                    cur = reinterpret_cast<UHCIQueueHead*>(static_cast<std::uintptr_t>(cur->headLinkPointer & ~0x3u) +
                                                           0xffff'8000'0000'0000ULL);

               cur->headLinkPointer = (PhysAddr(qh) & ~0x3u) | uhci::ptr::QueueHead;
               qh->headLinkPointer = uhci::ptr::Terminate;
          }
     }

     void HandleInterrupt()
     {
          const std::uint16_t status = Read16(uhci::reg::Status);
          const std::uint16_t pending = status & uhci::sts::ClearableMask;
          if (!pending) return;

          Write16(uhci::reg::Status, pending);

          if (status & uhci::sts::HCProcessErr) debugging::DbgWrite(u8"UHCI: host controller process error\n");
          if (status & uhci::sts::HostSysError) debugging::DbgWrite(u8"UHCI: host system error\n");

          for (auto it = _pendingTransfers.begin(); it != _pendingTransfers.end();)
          {
               auto& transfer = *it;
               const auto result = CheckTransfer(*transfer);

               if (result == UHCITransfer::State::Pending)
               {
                    ++it;
                    continue;
               }

               if (result == UHCITransfer::State::Error)
               {
                    debugging::DbgWrite(u8"UHCI: transfer error, TD status={:x}\n", transfer->errorStatus);
                    RemoveQH(transfer->qh.get(), transfer->scheduleSlot);
                    transfer->state = UHCITransfer::State::Error;
                    it = _pendingTransfers.erase(it);
                    continue;
               }

               if (transfer->type == TransferType::Interrupt)
               {
                    auto& td = *transfer->tds.front();
                    const auto* buf = reinterpret_cast<const std::uint8_t*>(
                        static_cast<std::uintptr_t>(td.bufferPointer) + 0xffff'8000'0000'0000ULL);

                    DispatchInterruptData(*transfer, buf);

                    td.token ^= (1u << 19);
                    td.controlStatus = (td.controlStatus & uhci::td::LowSpeed) | uhci::td::Active | uhci::td::IOC;
                    transfer->qh->elementLinkPointer = PhysAddr(transfer->tds.front().get()) & ~0x3u;
                    transfer->state = UHCITransfer::State::Pending;

                    RotateQHToTail(transfer->qh.get(), transfer->scheduleSlot);

                    ++it;
               }
               else
               {
                    debugging::DbgWrite(u8"UHCI: control transfer complete\n");
                    RemoveQH(transfer->qh.get(), transfer->scheduleSlot);
                    transfer->state = UHCITransfer::State::Complete;
                    it = _pendingTransfers.erase(it);
               }
          }
     }

     [[nodiscard]] std::uintptr_t GetIOBase() const { return _ioBase; }

private:
     void DisableLegacyUSB() const
     {
          const std::uintptr_t pciAddr =
              (static_cast<std::uintptr_t>(_bus) << 8) | (static_cast<std::uintptr_t>(_hdr->deviceId) << 3) | _function;
          auto val = KeReadPCIWord(pciAddr, 0xC0);
          val |= 0x2000;
          KeWritePCIWord(pciAddr, 0xC0, val);
     }

     void SetBusMaster() const { _hdr->command |= 1 << 2; }

     void ResetController() const
     {
          Write16(uhci::reg::Command, uhci::cmd::HCReset);
          process::KeSleepCurrentThread(10);

          Write16(uhci::reg::Command, uhci::cmd::GlobalReset);
          process::KeSleepCurrentThread(10);
          Write16(uhci::reg::Command, 0);

          Write8(uhci::reg::SOFModify, 0x40);
     }

     void AllocateFrameList()
     {
          _frameList = static_cast<FrameEntry*>(g_kernelProcess->AllocateVirtualMemory(
              nullptr, 65535,
              memory::AllocationFlags::Reserve | memory::AllocationFlags::Commit |
                  memory::AllocationFlags::ImmediatePhysical,
              memory::MemoryProtection::ReadWrite));

          for (auto& q : _scheduleQHs)
          {
               q = std::make_unique<UHCIQueueHead>();
               q->elementLinkPointer = uhci::ptr::Terminate;
          }
          for (int i = 5; i > 0; --i)
               _scheduleQHs[i]->headLinkPointer = (PhysAddr(_scheduleQHs[i - 1].get()) & ~0x3u) | uhci::ptr::QueueHead;
          _scheduleQHs[0]->headLinkPointer = uhci::ptr::Terminate;

          static constexpr std::array<int, 6> kIntervals = {32, 16, 8, 4, 2, 1};
          for (std::size_t i = 0; i < FRAME_LIST_SIZE; ++i)
          {
               int slot = 0;
               for (int j = 0; j < 6; ++j)
               {
                    if ((i % kIntervals[j]) == 0)
                    {
                         slot = 5 - j;
                         break;
                    }
               }
               _frameList[i].value = (PhysAddr(_scheduleQHs[slot].get()) & ~0x3u) | uhci::ptr::QueueHead;
          }

          Write32(uhci::reg::FrameBase, PhysAddr(_frameList));
     }

     void EnableInterrupts() const
     {
          Write16(uhci::reg::InterruptEn, uhci::intr::IOC | uhci::intr::TimeoutCRC | uhci::intr::ShortPacket);
     }

     void DetectAndEnablePorts()
     {
          for (std::uint8_t p = 0; p < 8; ++p)
          {
               const std::uintptr_t off = uhci::reg::Port0 + static_cast<std::uintptr_t>(p * 2);
               const std::uint16_t sc = Read16(off);

               if (sc == 0xFFFF || !(sc & uhci::port::AlwaysOne)) break;

               debugging::DbgWrite(u8"UHCI: port {} present, status={:x}\n", p, sc);
               if (sc & uhci::port::Connected)
               {
                    debugging::DbgWrite(u8"UHCI: port {} device present ({})\n", p,
                                        (sc & uhci::port::LowSpeed) ? u8"low speed" : u8"full speed");
                    EnablePort(p);
               }
          }
     }

     std::uint8_t _nextAddress = 1;

     void ParseAndSubmitInterruptTransfer(std::uint8_t addr, bool lowSpeed, std::span<const std::uint8_t> config)
     {
          std::size_t i = 0;
          while (i + 2 <= config.size())
          {
               const std::uint8_t len = config[i];
               const std::uint8_t type = config[i + 1];

               if (type == 0x05 && len >= 7)
               {
                    const std::uint8_t address = config[i + 2];
                    const std::uint8_t attributes = config[i + 3];
                    const std::uint16_t maxPacket = static_cast<std::uint16_t>(config[i + 4] | (config[i + 5] << 8));
                    const std::uint8_t interval = config[i + 6];

                    const bool isIn = address & 0x80;
                    const bool isInterrupt = (attributes & 0x03) == 0x03;

                    if (isIn && isInterrupt)
                    {
                         const std::uint8_t ep = address & 0x0F;
                         debugging::DbgWrite(u8"UHCI: found interrupt IN ep={} maxPacket={} interval={}\n", ep,
                                             maxPacket, interval);

                         auto* buf = static_cast<std::uint8_t*>(g_kernelProcess->AllocateVirtualMemory(
                             nullptr, maxPacket,
                             memory::AllocationFlags::Reserve | memory::AllocationFlags::Commit |
                                 memory::AllocationFlags::ImmediatePhysical,
                             memory::MemoryProtection::ReadWrite));

                         auto transfer =
                             SubmitInterruptTransfer(addr, ep, lowSpeed, std::span{buf, maxPacket}, interval);

                         _activeTransfers.push_back(std::move(transfer));

                         debugging::DbgWrite(u8"UHCI: interrupt transfer submitted for addr={}\n", addr);
                    }
               }

               if (len == 0) break;
               i += len;
          }
     }

     std::vector<std::shared_ptr<UHCITransfer>> _activeTransfers;

     static void WaitForTransfer(const std::shared_ptr<UHCITransfer>& t)
     {
          for (int i = 0; i < 1000; ++i)
          {
               if (t->state != UHCITransfer::State::Pending) return;
               process::KeSleepCurrentThread(1);
          }
          t->state = UHCITransfer::State::Error;
     }

     void EnumerateDevice(std::uint8_t port, bool lowSpeed)
     {
          debugging::DbgWrite(u8"UHCI: enumerating port {}\n", port);

          constexpr std::uint8_t defaultAddr = 0;
          constexpr std::uint8_t ep0 = 0;

          std::array<std::uint8_t, 8> setupBuf = {
              0x80,       // bmRequestType: device -> host, standard, device
              0x06,       // bRequest: GET_DESCRIPTOR
              0x00, 0x01, // wValue: Device Descriptor (type=1, index=0)
              0x00, 0x00, // wIndex: 0
              0x08, 0x00  // wLength: 8
          };
          std::array<std::uint8_t, 8> descBuf{};

          auto t = SubmitControlTransfer(defaultAddr, ep0, lowSpeed, setupBuf, descBuf, true);
          WaitForTransfer(t);
          if (t->state != UHCITransfer::State::Complete)
          {
               RemoveQH(t->qh.get(), t->scheduleSlot);
               process::KeSleepCurrentThread(2); // let HC drain its internal pipeline
               debugging::DbgWrite(u8"UHCI: GET_DESCRIPTOR(8) failed on port {}\n", port);
               return;
          }

          const std::uint8_t maxPacket0 = descBuf[7];
          const std::uint8_t devClass = descBuf[4]; // bDeviceClass
          debugging::DbgWrite(u8"UHCI: device class={:x}, maxPacket0={}\n", devClass, maxPacket0);

          const std::uint8_t newAddr = _nextAddress++;
          std::array<std::uint8_t, 8> setAddrSetup = {
              0x00,          // bmRequestType: host->device, standard, device
              0x05,          // bRequest: SET_ADDRESS
              newAddr, 0,    // wValue: new address
              0x00,    0x00, // wIndex: 0
              0x00,    0x00  // wLength: 0
          };
          std::array<std::uint8_t, 0> noData{};

          auto t2 = SubmitControlTransfer(defaultAddr, ep0, lowSpeed, setAddrSetup, noData, false);
          WaitForTransfer(t2);

          if (t2->state != UHCITransfer::State::Complete)
          {
               debugging::DbgWrite(u8"UHCI: SET_ADDRESS failed on port {}\n", port);
               return;
          }

          process::KeSleepCurrentThread(2);
          debugging::DbgWrite(u8"UHCI: device assigned address {}\n", newAddr);

          std::array<std::uint8_t, 8> fullDescSetup = {
              0x80, 0x06, 0x00, 0x01, // Device Descriptor
              0x00, 0x00, 0x12, 0x00  // wLength: 18
          };
          std::array<std::uint8_t, 18> fullDesc{};

          auto t3 = SubmitControlTransfer(newAddr, ep0, lowSpeed, fullDescSetup, fullDesc, true);
          WaitForTransfer(t3);

          if (t3->state != UHCITransfer::State::Complete)
          {
               debugging::DbgWrite(u8"UHCI: GET_DESCRIPTOR(18) failed\n");
               return;
          }

          const std::uint16_t vendorId = static_cast<std::uint16_t>(fullDesc[8] | (fullDesc[9] << 8));
          const std::uint16_t productId = static_cast<std::uint16_t>(fullDesc[10] | (fullDesc[11] << 8));
          debugging::DbgWrite(u8"UHCI: device VID={:x} PID={:x}\n", vendorId, productId);

          std::array<std::uint8_t, 8> cfgSetup = {0x80, 0x06, 0x00, 0x02, // Configuration Descriptor
                                                  0x00, 0x00, 0x09, 0x00};
          std::array<std::uint8_t, 9> cfgBuf{};

          auto t4 = SubmitControlTransfer(newAddr, ep0, lowSpeed, cfgSetup, cfgBuf, true);
          WaitForTransfer(t4);

          if (t4->state != UHCITransfer::State::Complete)
          {
               debugging::DbgWrite(u8"UHCI: GET_DESCRIPTOR(Config) failed\n");
               return;
          }

          const std::uint16_t totalLen = static_cast<std::uint16_t>(cfgBuf[2] | (cfgBuf[3] << 8));

          std::vector<std::uint8_t> fullCfg(totalLen);
          std::array<std::uint8_t, 8> fullCfgSetup = {0x80,
                                                      0x06,
                                                      0x00,
                                                      0x02,
                                                      0x00,
                                                      0x00,
                                                      static_cast<std::uint8_t>(totalLen),
                                                      static_cast<std::uint8_t>(totalLen >> 8)};
          std::span<std::uint8_t> fullCfgSpan{fullCfg};

          auto t5 = SubmitControlTransfer(newAddr, ep0, lowSpeed, fullCfgSetup, fullCfgSpan, true);
          WaitForTransfer(t5);

          if (t5->state != UHCITransfer::State::Complete)
          {
               debugging::DbgWrite(u8"UHCI: GET_DESCRIPTOR(full config) failed\n");
               return;
          }

          std::array<std::uint8_t, 8> setCfgSetup = {0x00, 0x09, 0x01, 0x00, // wValue: configuration 1
                                                     0x00, 0x00, 0x00, 0x00};

          auto t6 = SubmitControlTransfer(newAddr, ep0, lowSpeed, setCfgSetup, noData, false);
          WaitForTransfer(t6);

          ParseAndSubmitInterruptTransfer(newAddr, lowSpeed, fullCfg);
     }

     void EnablePort(std::uint8_t p)
     {
          const std::uintptr_t off = uhci::reg::Port0 + static_cast<std::uintptr_t>(p * 2);

          Write16(off, uhci::port::ConnectChange | uhci::port::EnableChange);
          process::KeSleepCurrentThread(10);

          Write16(off, uhci::port::Reset);
          process::KeSleepCurrentThread(60);

          Write16(off, 0);
          process::KeSleepCurrentThread(10);

          Write16(off, uhci::port::ConnectChange | uhci::port::EnableChange);

          Write16(off, uhci::port::Enabled);

          for (int i = 0; i < 10; ++i)
          {
               process::KeSleepCurrentThread(10);
               const std::uint16_t sc = Read16(off);
               if (sc & uhci::port::Enabled)
               {
                    debugging::DbgWrite(u8"UHCI: port {} enabled\n", p);

                    process::KeSleepCurrentThread(10);

                    const bool isLowSpeed = sc & uhci::port::LowSpeed;
                    EnumerateDevice(p, isLowSpeed);
                    return;
               }
          }
          debugging::DbgWrite(u8"UHCI: port {} failed to enable\n", p);
     }

     void StartController() const { Write16(uhci::reg::Command, uhci::cmd::RunStop | uhci::cmd::MaxPacket64); }

     static UHCITransferDescriptor& AllocTD(std::shared_ptr<UHCITransfer>& t)
     {
          auto& td = *t->tds.emplace_back(std::make_unique<UHCITransferDescriptor>());
          td = {};
          return td;
     }

     void ChainAndInsert(std::shared_ptr<UHCITransfer>& transfer, int slot)
     {
          debugging::DbgWrite(u8"UHCI: interrupt transfer addr={} ep={} slot={}\n", transfer->devAddr, transfer->devEp,
                              slot);
          auto& tds = transfer->tds;

          for (std::size_t i = 0; i + 1 < tds.size(); ++i)
               tds[i]->linkPointer = (PhysAddr(tds[i + 1].get()) & ~0x3u) | uhci::ptr::DepthFirst;
          tds.back()->linkPointer = uhci::ptr::Terminate;

          transfer->qh = std::make_unique<UHCIQueueHead>();
          transfer->qh->elementLinkPointer = PhysAddr(tds.front().get()) & ~0x3u; // bit1=0: TD
          transfer->scheduleSlot = slot;

          InsertQH(transfer->qh.get(), slot);
          _pendingTransfers.push_back(transfer);
     }

     void InsertQH(UHCIQueueHead* newQH, int slot) const
     {
          UHCIQueueHead* anchor = _scheduleQHs[slot].get();
          newQH->headLinkPointer = anchor->headLinkPointer;
          anchor->headLinkPointer = (PhysAddr(newQH) & ~0x3u) | uhci::ptr::QueueHead;
     }

     void RemoveQH(UHCIQueueHead* toRemove, int slot) const
     {
          const std::uint32_t targetPhys = PhysAddr(toRemove) & ~0x3u;

          toRemove->elementLinkPointer = uhci::ptr::Terminate;

          UHCIQueueHead* cur = _scheduleQHs[slot].get();
          while (cur)
          {
               const std::uint32_t next = cur->headLinkPointer;
               if (next & uhci::ptr::Terminate) break;
               if ((next & ~0x3u) == targetPhys)
               {
                    cur->headLinkPointer = toRemove->headLinkPointer;
                    return;
               }
               cur = reinterpret_cast<UHCIQueueHead*>(static_cast<std::uintptr_t>(next & ~0x3u) +
                                                      0xffff'8000'0000'0000ULL);
          }
     }

     UHCITransfer::State CheckTransfer(UHCITransfer& transfer) const
     {
          for (auto& tdPtr : transfer.tds)
          {
               const std::uint32_t cs = tdPtr->controlStatus;
               if (cs & uhci::td::Active) return UHCITransfer::State::Pending;
               if (cs & uhci::td::ErrorMask)
               {
                    transfer.errorStatus = cs;
                    return UHCITransfer::State::Error;
               }
          }
          return UHCITransfer::State::Complete;
     }

     void Write8(std::uintptr_t off, std::uint8_t v) const { operations::WritePort8(_ioBase + off, v); }
     void Write16(std::uintptr_t off, std::uint16_t v) const { operations::WritePort16(_ioBase + off, v); }
     void Write32(std::uintptr_t off, std::uint32_t v) const { operations::WritePort32(_ioBase + off, v); }
     std::uint16_t Read16(std::uintptr_t off) const { return operations::ReadPort16(_ioBase + off); }

     template <typename T> static std::uint32_t PhysAddr(T* ptr)
     {
          return static_cast<std::uint32_t>(memory::KeGetPhysicalAddress(ptr));
     }

     static int IntervalToSlot(std::uint8_t ms)
     {
          if (ms >= 32) return 5;
          if (ms >= 16) return 4;
          if (ms >= 8) return 3;
          if (ms >= 4) return 2;
          if (ms >= 2) return 1;
          return 0;
     }

     static constexpr std::size_t FRAME_LIST_SIZE = 1024;

     std::uintptr_t _ioBase{};
     volatile PCIHeader* _hdr{};
     std::uint8_t _bus{};
     std::uint8_t _function{};

     FrameEntry* _frameList{};
     std::array<std::unique_ptr<UHCIQueueHead>, 6> _scheduleQHs; // [0]=1ms ... [5]=32ms

     mutable std::vector<std::shared_ptr<UHCITransfer>> _pendingTransfers;
};

static bool KiIsrUsbUHCI(cpu::IInterruptFrame& /*frame*/, void* usbController)
{
     auto* controller = static_cast<UHCIController*>(usbController);

     const std::uint16_t status = operations::ReadPort16(controller->GetIOBase() + uhci::reg::Status);
     if ((status & uhci::sts::ClearableMask) == 0) return false;

     controller->HandleInterrupt();
     return true;
}

static const char8_t* UsbSpeedName(std::uint8_t speed)
{
     switch (speed)
     {
     case 1: return u8"Low";
     case 2: return u8"Full";
     case 3: return u8"High";
     case 4: return u8"Super";
     case 5: return u8"SuperPlus";
     default: return u8"Unknown";
     }
}

static void KiXhciEnumeratePorts(std::uintptr_t base)
{
     auto* cap = reinterpret_cast<volatile XhciCap*>(base);
     const std::uint8_t maxPorts = cap->hcsParams1 & 0xFF;
     const std::uintptr_t opBase = base + cap->capLength;
     const std::uintptr_t portRegs = opBase + 0x400;

     for (std::uint8_t p = 0; p < maxPorts; ++p)
     {
          const volatile auto* portSc =
              reinterpret_cast<volatile std::uint32_t*>(portRegs + static_cast<std::uintptr_t>(p * 0x10));
          if (*portSc & 1)
               debugging::DbgWrite(u8"        XHCI port {}: connected, status={:x}, speed={}\r\n", p, *portSc,
                                   UsbSpeedName((*portSc >> 10) & 0xF));
     }
}

static void KiEhciEnumeratePorts(std::uintptr_t base)
{
     auto* cap = reinterpret_cast<volatile EhciCap*>(base);
     const std::uint8_t portCount = cap->hcsParams & 0xF;
     const std::uintptr_t opBase = base + cap->capLength;
     const std::uintptr_t portBase = opBase + 0x44;

     for (std::uint8_t p = 0; p < portCount; ++p)
     {
          const volatile auto* portSc =
              reinterpret_cast<volatile std::uint32_t*>(portBase + static_cast<std::uintptr_t>(p * 4));
          if (*portSc & 1)
               debugging::DbgWrite(u8"        EHCI port {}: connected, status={:x}, speed={}\r\n", p, *portSc,
                                   UsbSpeedName((*portSc >> 10) & 0xF));
     }
}

static void KiOhciEnumeratePorts(std::uintptr_t base)
{
     const std::uint32_t rhDescA = *reinterpret_cast<volatile std::uint32_t*>(base + 0x48);
     const std::uint8_t portCount = rhDescA & 0xFF;
     const std::uintptr_t portBase = base + 0x54;

     for (std::uint8_t p = 0; p < portCount; ++p)
     {
          const volatile auto* portSc =
              reinterpret_cast<volatile std::uint32_t*>(portBase + static_cast<std::uintptr_t>(p * 4));
          if (*portSc & 1) debugging::DbgWrite(u8"        OHCI port {}: connected, status={:x}\r\n", p, *portSc);
     }
}

static void KiUhciInitialise(volatile PCIHeader* hdr, std::uint8_t bus, std::uint8_t function)
{
     PciBar bars[6]; // NOLINT
     KeGetDeviceBARs(hdr, bars);
     const std::uintptr_t baseAddr = KeFindControllerBAR(hdr, hdr->progIf, bars);
     if (!baseAddr) return;

     auto* controller = new UHCIController();
     controller->Initialise(baseAddr, hdr, bus, function);
     debugging::DbgWrite(u8"      UHCI initialised at I/O base {:x}\r\n", baseAddr);
}

void usb::KiEnumerateUSB(volatile PCIHeader* hdr, std::uint8_t bus, std::uint8_t function)
{
     PciBar bars[6]; // NOLINT
     KeGetDeviceBARs(hdr, bars);
     const std::uintptr_t baseAddr = KeFindControllerBAR(hdr, hdr->progIf, bars);
     if (!baseAddr) return;

     const char8_t* name = u8"Unknown";
     switch (hdr->progIf)
     {
     case 0x00: name = u8"UHCI"; break;
     case 0x10: name = u8"OHCI"; break;
     case 0x20: name = u8"EHCI"; break;
     case 0x30: name = u8"XHCI"; break;
     }

     const std::uintptr_t mmioBase = baseAddr + 0xffff'8000'0000'0000ULL;
     switch (hdr->progIf)
     {
     case 0x00: KiUhciInitialise(hdr, bus, function); break;
     case 0x10: KiOhciEnumeratePorts(mmioBase); break;
     case 0x20: KiEhciEnumeratePorts(mmioBase); break;
     case 0x30: KiXhciEnumeratePorts(mmioBase); break;
     }
}
