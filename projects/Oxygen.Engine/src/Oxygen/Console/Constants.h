//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <cstddef>
#include <cstdint>
#include <string_view>

namespace oxygen::console {

inline constexpr size_t kDefaultHistoryCapacity = 1024;
inline constexpr size_t kMinHistoryCapacity = 1;

inline constexpr int kExitCodeGenericError = 1;
inline constexpr int kExitCodeInvalidArguments = 2;
inline constexpr int kExitCodeDenied = 13;
inline constexpr int kExitCodeNotFound = 127;

inline constexpr char kCommandChainSeparator = ';';
inline constexpr size_t kCompletionCycleStartIndex = 0;

inline constexpr std::string_view kBuiltinHelpCommand = "help";
inline constexpr std::string_view kBuiltinFindCommand = "find";
inline constexpr std::string_view kBuiltinListCommand = "list";
inline constexpr std::string_view kBuiltinExecCommand = "exec";

inline constexpr std::string_view kListModeAll = "all";
inline constexpr std::string_view kListModeCommands = "commands";
inline constexpr std::string_view kListModeCVars = "cvars";

inline constexpr std::string_view kScriptCommentPrefixHash = "#";
inline constexpr std::string_view kScriptCommentPrefixDoubleSlash = "//";
inline constexpr uint32_t kMaxScriptDepth = 8;
inline constexpr int kJsonIndentSpaces = 2;
inline constexpr std::string_view kArchiveJsonVersionKey = "version";
inline constexpr std::string_view kArchiveJsonEntriesKey = "cvars";
inline constexpr std::string_view kArchiveJsonNameKey = "name";
inline constexpr std::string_view kArchiveJsonTypeKey = "type";
inline constexpr std::string_view kArchiveJsonValueKey = "value";
inline constexpr int kArchiveJsonVersion1 = 1;
inline constexpr std::string_view kCommandLineSetPrefix = "+";
inline constexpr char kCommandLineAssignSeparator = '=';

} // namespace oxygen::console
