// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using Oxygen.Managed.Assets.Diagnostics;
using Oxygen.Managed.Core;

namespace Oxygen.Editor.Input;

/// <summary>An input source file as last read.</summary>
/// <param name="File">The file.</param>
/// <param name="Container">The source, or <see langword="null"/> when it is not JSON or does not match the schema.</param>
/// <param name="FormatProblems">What the format found in the file alone.</param>
/// <param name="Hash">The hash of the bytes read, which tells an unchanged file from a changed one.</param>
internal sealed record InputSourceEntry(FileSystemPath File, InputContainer? Container, IReadOnlyList<AssetProblem> FormatProblems, string Hash);
