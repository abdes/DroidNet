// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using Oxygen.Editor.Assets;
using Oxygen.Managed.Core;

namespace Oxygen.Editor.Pickers;

/// <summary>
/// The sources an instance is placed from or swapped to (scene instances 6.1, 6.5): the imported models, and the
/// authored scenes that would not instance the scene the instance is in.
/// </summary>
public static class InstanceSources
{
    private const string ModelGlyph = "";
    private const string SceneGlyph = "";

    /// <summary>Creates a picker that lists models.</summary>
    /// <returns>The picker.</returns>
    public static AssetPickerModel CreateModelPicker() => new(static asset => AssetChoice.For(asset, "Model", ModelGlyph));

    /// <summary>Creates a picker that lists scenes.</summary>
    /// <returns>The picker.</returns>
    public static AssetPickerModel CreateScenePicker() => new(static asset => AssetChoice.For(asset, "Scene", SceneGlyph));

    /// <summary>Creates a picker that lists models and scenes, each named by its kind.</summary>
    /// <returns>The picker.</returns>
    public static AssetPickerModel CreatePicker()
        => new(static asset => AssetRule.For(AssetKind.Model).Accepts(asset)
            ? AssetChoice.For(asset, "Model", ModelGlyph)
            : AssetChoice.For(asset, "Scene", SceneGlyph));

    /// <summary>Gets the models of a catalog, one per imported model's source file.</summary>
    /// <param name="assets">The catalog.</param>
    /// <returns>The models.</returns>
    public static IReadOnlyList<AssetEntry> Models(IReadOnlyList<AssetEntry> assets) => AssetRule.For(AssetKind.Model).Apply(assets);

    /// <summary>Gets the authored scenes of a catalog that may be instanced.</summary>
    /// <param name="assets">The catalog.</param>
    /// <param name="wouldCycle">Whether instancing a scene, by its identity, would make a cycle.</param>
    /// <returns>The scenes.</returns>
    public static IReadOnlyList<AssetEntry> Scenes(IReadOnlyList<AssetEntry> assets, Func<AssetPath, bool> wouldCycle)
    {
        ArgumentNullException.ThrowIfNull(wouldCycle);
        return [.. AssetRule.For(AssetKind.Scene).Apply(assets).Where(asset => !wouldCycle(asset.Identity))];
    }
}
