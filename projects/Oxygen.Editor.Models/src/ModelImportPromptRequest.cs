// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Collections.Immutable;
using Oxygen.Managed.Core;

namespace Oxygen.Editor.Models;

/// <summary>What the import dialog starts with.</summary>
/// <param name="DestinationFolder">The authoring folder new models go into, such as the Content Explorer's selected folder.</param>
public sealed record ModelImportPromptRequest(AssetPath DestinationFolder)
{
    /// <summary>Gets the source files the dialog lists when it opens; empty to start with none.</summary>
    public ImmutableArray<string> Files { get; init; } = [];
}
