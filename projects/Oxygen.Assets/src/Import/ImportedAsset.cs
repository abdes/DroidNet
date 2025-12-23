// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using Oxygen.Assets.Persistence.LooseCooked.V1;

namespace Oxygen.Assets.Import;

/// <summary>
/// Canonical (editor/tooling) representation of an imported asset.
/// </summary>
/// <param name="AssetKey">Stable runtime-facing identity.</param>
/// <param name="VirtualPath">Canonical virtual path (for example <c>/Content/Textures/Wood.otex</c>).</param>
/// <param name="AssetType">Logical asset type (for example <c>Texture</c>, <c>Material</c>).</param>
/// <param name="Source">Source fingerprint information.</param>
/// <param name="Dependencies">Discovered dependencies for incremental rebuild.</param>
/// <param name="Payload">Importer-specific payload (strongly typed by importer).</param>
public sealed record ImportedAsset(
    AssetKey AssetKey,
    string VirtualPath,
    string AssetType,
    ImportedAssetSource Source,
    IReadOnlyList<ImportedDependency> Dependencies,
    object Payload);
