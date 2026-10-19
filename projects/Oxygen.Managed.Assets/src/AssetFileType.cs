// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using Oxygen.Managed.Assets.Persistence.LooseCooked;
using Oxygen.Managed.Core;

namespace Oxygen.Managed.Assets;

/// <summary>
/// One file type of the asset pipeline: the suffix that names it, what it is, and what it cooks into. A source either
/// cooks into one asset of its own (<see cref="CookedExtension"/>) or is a container whose assets are named inside it
/// (<see cref="ProductExtensions"/>).
/// </summary>
/// <param name="Suffix">The file name suffix, including its leading dot, such as <c>.omat.json</c>.</param>
/// <param name="Category">What the file is.</param>
/// <param name="CookedExtension">The extension of the cooked descriptor the file is or cooks into, or null when it has none of its own.</param>
/// <param name="AssetType">The engine asset type of that cooked descriptor, or <see cref="AssetType.Unknown"/> when it is not an asset.</param>
public sealed record AssetFileType(string Suffix, AssetFileCategory Category, string? CookedExtension, AssetType AssetType)
{
    /// <summary>
    /// Gets the cooked extensions of the assets a container produces, such as <c>.oiact</c> and <c>.oimap</c> for an
    /// input file; empty for a file that is, or cooks into, at most one asset.
    /// </summary>
    public IReadOnlyList<string> ProductExtensions { get; init; } = [];

    /// <summary>
    /// Gets the cooked file kind of a descriptor that is not an engine asset, such as
    /// <see cref="FileKind.TextureDescriptor"/> for <c>.otex</c>; <see cref="FileKind.Unknown"/> otherwise.
    /// </summary>
    public FileKind CookedFileKind { get; init; }

    /// <summary>
    /// Gets the property of an authored JSON file that names the file's own identity, such as <c>virtual_path</c> in
    /// a material descriptor; null when the file's path alone names it.
    /// </summary>
    public string? IdentityField { get; init; }

    /// <summary>Gets the fields of an authored JSON file that reference other assets; empty when it references none.</summary>
    public IReadOnlyList<AssetReferenceField> ReferenceFields { get; init; } = [];

    // Oxygen's suffixes end an asset's identity and match exactly; a foreign format matches in any case, as the cooker
    // detects it (.GLB, .PNG).
    internal bool Matches(string path)
        => path.EndsWith(this.Suffix, this.Category is AssetFileCategory.Model or AssetFileCategory.Image ? StringComparison.OrdinalIgnoreCase : StringComparison.Ordinal);
}
