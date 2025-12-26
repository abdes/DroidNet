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
/// <param name="GeneratedSourcePath">Optional project-relative path to a generated source file (e.g. <c>Content/Materials/Red.omat.json</c>).</param>
/// <param name="IntermediateCachePath">Optional project-relative path to an intermediate cache file (e.g. <c>.imported/Content/Textures/Wood.png</c>).</param>
/// <param name="Payload">Legacy in-memory payload (deprecated, prefer reading from disk).</param>
public sealed record ImportedAsset(
    AssetKey AssetKey,
    string VirtualPath,
    string AssetType,
    ImportedAssetSource Source,
    IReadOnlyList<ImportedDependency> Dependencies,
    string? GeneratedSourcePath = null,
    string? IntermediateCachePath = null,
    object? Payload = null);
