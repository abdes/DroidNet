//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

// Offline ETW export. Build with the Windows SDK: cl /EHsc /std:c++20
// ExportExposureCpuSchedule.cpp /link advapi32.lib
// Input must use the QPC clock and PROC_THREAD+CSWITCH kernel events.
// CSwitch payload: https://learn.microsoft.com/windows/win32/etw/cswitch

#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <Windows.h>
#include <evntcons.h>
#include <evntrace.h>

#include <cstdint>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <string>

namespace {
constexpr GUID kThreadProvider { 0x3d6fa8d1, 0xfe05, 0x11d0,
  { 0x9d, 0xda, 0x00, 0xc0, 0x4f, 0xd7, 0xba, 0x7c } };

struct Export {
  std::ofstream output;
  ULONG thread {};
  std::uint64_t records {};
  bool invalid {};
};

void WINAPI OnEvent(EVENT_RECORD* event)
{
  if (event->EventHeader.ProviderId != kThreadProvider
    || event->EventHeader.EventDescriptor.Opcode != 36U) {
    return;
  }
  auto& result = *static_cast<Export*>(event->UserContext);
  if (event->UserDataLength < 16U) {
    result.invalid = true;
    return;
  }
  const auto* data = static_cast<const unsigned char*>(event->UserData);
  std::uint32_t next {}, previous {};
  std::memcpy(&next, data, sizeof(next));
  std::memcpy(&previous, data + 4, sizeof(previous));
  if (next != result.thread && previous != result.thread) {
    return;
  }
  result.output << event->EventHeader.TimeStamp.QuadPart << ','
                << static_cast<unsigned>(event->BufferContext.ProcessorNumber)
                << ',' << previous << ',' << next << ','
                << static_cast<unsigned>(data[12]) << ','
                << static_cast<unsigned>(data[14]) << '\n';
  ++result.records;
}
} // namespace

int wmain(int argc, wchar_t** argv)
{
  if (argc != 4) {
    std::fprintf(stderr,
      "Usage: ExportExposureCpuSchedule input.etl output.csv thread-id\n");
    return 2;
  }
  const auto output = std::filesystem::path(argv[2]);
  if (std::filesystem::exists(output)) {
    std::fprintf(stderr, "Output already exists\n");
    return 2;
  }
  Export result { .output = std::ofstream(output, std::ios::binary),
    .thread = static_cast<ULONG>(std::stoul(argv[3])) };
  if (!result.output) {
    return 2;
  }
  result.output << "qpc,cpu,old_thread,new_thread,old_wait_reason,old_state\n";
  EVENT_TRACE_LOGFILEW logfile {};
  logfile.LogFileName = argv[1];
  logfile.ProcessTraceMode
    = PROCESS_TRACE_MODE_EVENT_RECORD | PROCESS_TRACE_MODE_RAW_TIMESTAMP;
  logfile.EventRecordCallback = OnEvent;
  logfile.Context = &result;
  auto trace = OpenTraceW(&logfile);
  if (trace == INVALID_PROCESSTRACE_HANDLE) {
    std::fprintf(stderr, "OpenTrace failed: %lu\n", GetLastError());
    return 1;
  }
  const auto status = ProcessTrace(&trace, 1, nullptr, nullptr);
  CloseTrace(trace);
  result.output.close();
  const auto& header = logfile.LogfileHeader;
  std::printf("{\"status\":%lu,\"qpc_frequency\":%lld,\"clock_type\":%lu,"
              "\"events_lost\":%lu,\"buffers_lost\":%lu,\"records\":%llu}\n",
    status, header.PerfFreq.QuadPart, header.ReservedFlags, header.EventsLost,
    header.BuffersLost, result.records);
  return status == ERROR_SUCCESS && header.ReservedFlags == 1U
      && header.EventsLost == 0U && header.BuffersLost == 0U && !result.invalid
      && result.output.good() && result.records != 0U
    ? 0
    : 1;
}
