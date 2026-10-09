// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

namespace Oxygen.Editor.ContentPipeline.Relocation;

/// <summary>Moves or renames one authored file or folder.</summary>
/// <param name="SourcePath">The current virtual path.</param>
/// <param name="TargetPath">The new virtual path.</param>
public sealed record AssetRelocationMove(string SourcePath, string TargetPath);
