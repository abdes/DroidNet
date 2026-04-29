// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using Oxygen.Assets.Import.Materials;

namespace Oxygen.Editor.MaterialEditor;

/// <summary>
/// Mutable adapter over the immutable <see cref="MaterialSource"/> record.
/// </summary>
/// <remarks>
/// <para>
/// The property pipeline writes via in-place setters; <see cref="MaterialSource"/>
/// is an immutable record. This adapter lets descriptors operate on a
/// stable target object whose internal representation can be rebuilt
/// each time a scalar field is written.
/// </para>
/// <para>
/// The adapter also serves as the boundary at which the editor's
/// glTF-style <see cref="MaterialPbrMetallicRoughness"/> field naming is
/// translated to the engine schema's JSON Pointer naming
/// (<c>/parameters/base_color/0</c>, <c>/parameters/metalness</c>,
/// etc.) — that mapping lives in
/// <see cref="MaterialDescriptors"/>.
/// </para>
/// </remarks>
public sealed class MaterialEditState
{
    private MaterialSource source;

    /// <summary>
    /// Initializes a new instance of the <see cref="MaterialEditState"/> class.
    /// </summary>
    /// <param name="source">The initial source.</param>
    public MaterialEditState(MaterialSource source)
    {
        ArgumentNullException.ThrowIfNull(source);
        this.source = source;
    }

    /// <summary>
    /// Gets the current source snapshot.
    /// </summary>
    public MaterialSource Source => this.source;

    /// <summary>
    /// Replaces the current source via a record-style transformer.
    /// </summary>
    /// <param name="transform">The transformer.</param>
    public void Replace(Func<MaterialSource, MaterialSource> transform)
    {
        ArgumentNullException.ThrowIfNull(transform);
        this.source = transform(this.source);
    }

    /// <summary>
    /// Replaces only the PBR sub-record.
    /// </summary>
    /// <param name="transform">The PBR transformer.</param>
    public void ReplacePbr(Func<MaterialPbrMetallicRoughness, MaterialPbrMetallicRoughness> transform)
    {
        ArgumentNullException.ThrowIfNull(transform);
        this.source = WithPbr(this.source, transform(this.source.PbrMetallicRoughness));
    }

    /// <summary>
    /// Replaces the alpha mode while preserving every other source field.
    /// </summary>
    /// <param name="alphaMode">The new alpha mode.</param>
    public void SetAlphaMode(MaterialAlphaMode alphaMode)
        => this.source = new MaterialSource(
            schema: this.source.Schema,
            type: this.source.Type,
            name: this.source.Name,
            pbrMetallicRoughness: this.source.PbrMetallicRoughness,
            normalTexture: this.source.NormalTexture,
            occlusionTexture: this.source.OcclusionTexture,
            alphaMode: alphaMode,
            alphaCutoff: this.source.AlphaCutoff,
            doubleSided: this.source.DoubleSided);

    private static MaterialSource WithPbr(MaterialSource source, MaterialPbrMetallicRoughness pbr) => new(
        schema: source.Schema,
        type: source.Type,
        name: source.Name,
        pbrMetallicRoughness: pbr,
        normalTexture: source.NormalTexture,
        occlusionTexture: source.OcclusionTexture,
        alphaMode: source.AlphaMode,
        alphaCutoff: source.AlphaCutoff,
        doubleSided: source.DoubleSided);
}
