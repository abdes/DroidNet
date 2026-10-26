// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Text.Json;
using Oxygen.Managed.Core;

namespace Oxygen.Editor.Models;

/// <summary>One model the import dialog imports.</summary>
/// <param name="SourcePath">The primary source file.</param>
/// <param name="Name">The model's name: its folder in <paramref name="DestinationFolder"/> for a source from outside
/// the project.</param>
/// <param name="DestinationFolder">The authoring folder the model goes into.</param>
/// <param name="Settings">The model import settings, its name included, written into the model's import record.</param>
public sealed record ModelImportChoice(string SourcePath, string Name, AssetPath DestinationFolder, JsonElement Settings)
{
    /// <summary>Gets the model this import replaces, reviewed in the dialog, or <see langword="null"/> for a new model.
    /// The replaced model keeps its record, which takes <see cref="Settings"/>.</summary>
    public AssetPath? Replaces { get; init; }
}
