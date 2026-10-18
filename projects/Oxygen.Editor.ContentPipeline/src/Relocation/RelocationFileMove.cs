// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using Oxygen.Managed.Core;

namespace Oxygen.Editor.ContentPipeline.Relocation;

/// <summary>One physical file or folder move of a relocation.</summary>
/// <param name="Source">The absolute current location.</param>
/// <param name="Target">The absolute new location.</param>
/// <param name="IsDirectory">Whether the location is a folder.</param>
public sealed record RelocationFileMove(FileSystemPath Source, FileSystemPath Target, bool IsDirectory);
