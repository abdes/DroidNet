//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <string>

#include <Oxygen/Base/Logging.h>
#include <Oxygen/Base/NoStd.h>
#include <Oxygen/Content/PakFile.h>
#include <Oxygen/Data/AssetKey.h>

using namespace oxygen::content;
using namespace oxygen::data;
using namespace oxygen::data::pak;

void PrintGuidReadable(const std::array<uint8_t, 16>& guid)
{
  for (size_t i = 0; i < guid.size(); ++i) {
    uint8_t c = guid[i];
    if (c >= 32 && c <= 126) {
      std::cout << static_cast<char>(c);
    } else {
      std::cout << "\\x" << std::hex << std::setw(2) << std::setfill('0')
                << static_cast<int>(c) << std::dec;
    }
  }
}

void PrintAssetKey(const AssetKey& key)
{
  std::cout << "    --- asset key ---\n";
  std::cout << "    GUID        : " << nostd::to_string(key) << "\n";
}

void PrintAssetEntry(const AssetDirectoryEntry& entry, size_t idx)
{
  std::cout << "Asset #" << idx << ":\n";
  PrintAssetKey(entry.asset_key);
  std::cout << "    --- asset metadata ---\n";
  std::cout << "    Asset Type   : " << entry.asset_type << "\n";
  std::cout << "    Entry Offset : " << entry.entry_offset << "\n";
  std::cout << "    Desc Offset  : " << entry.desc_offset << "\n";
  std::cout << "    Desc Size    : " << entry.desc_size << "\n";
}

int main(int argc, char* argv[])
{
  if (argc < 2) {
    std::cerr << "Usage: AssetDump <pakfile>\n";
    return 1;
  }

  loguru::g_preamble_date = false;
  loguru::g_preamble_file = true;
  loguru::g_preamble_verbose = false;
  loguru::g_preamble_time = false;
  loguru::g_preamble_uptime = false;
  loguru::g_preamble_thread = false;
  loguru::g_preamble_header = false;
  loguru::g_stderr_verbosity = loguru::Verbosity_0;
  loguru::g_colorlogtostderr = true;
  // Optional, but useful to time-stamp the start of the log.
  // Will also detect verbosity level on command line as -v.
  loguru::init(argc, argv);
  loguru::set_thread_name("main");

  std::filesystem::path pak_path(argv[1]);
  if (!std::filesystem::exists(pak_path)) {
    std::cerr << "File not found: " << pak_path << "\n";
    return 1;
  }
  try {
    PakFile pak(pak_path);
    auto dir = pak.Directory();
    std::cout << "PAK: " << pak_path << "\n";
    std::cout << "Asset count: " << dir.size() << "\n\n";
    for (size_t i = 0; i < dir.size(); ++i) {
      PrintAssetEntry(dir[i], i);
    }
  } catch (const std::exception& ex) {
    std::cerr << "Error: " << ex.what() << "\n";
    return 2;
  }
  return 0;
}
