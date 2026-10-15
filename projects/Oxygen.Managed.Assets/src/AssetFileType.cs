// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

namespace Oxygen.Managed.Assets;

/// <summary>One file type of the asset pipeline: the suffix that names it, what it is, and what it cooks into.</summary>
/// <param name="Suffix">The file name suffix, including its leading dot, such as <c>.omat.json</c>.</param>
/// <param name="Category">What the file is.</param>
/// <param name="CookedExtension">The extension of the cooked descriptor the file is or cooks into, or null when it has none of its own.</param>
/// <param name="AssetType">The engine asset type of that cooked descriptor, or <see cref="AssetType.Unknown"/> when it is not an asset.</param>
public sealed record AssetFileType(string Suffix, AssetFileCategory Category, string? CookedExtension, AssetType AssetType);
