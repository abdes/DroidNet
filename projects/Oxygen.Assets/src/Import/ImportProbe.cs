// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

namespace Oxygen.Assets.Import;

/// <summary>
/// Lightweight probe data used for importer selection.
/// </summary>
/// <param name="SourcePath">Project-relative source path.</param>
/// <param name="Extension">Lowercase extension including leading dot (for example <c>.png</c>).</param>
/// <param name="HeaderBytes">A small header slice from the source file.</param>
public sealed record ImportProbe(
    string SourcePath,
    string Extension,
    ReadOnlyMemory<byte> HeaderBytes);
