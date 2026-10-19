// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using Oxygen.Managed.Core;

namespace Oxygen.Managed.Assets;

/// <summary>The engine content pak's assets and folders the editor names directly.</summary>
/// <remarks>
/// The engine build cooks these into <c>engine.pak</c> (native <c>EngineContent</c>); they are ordinary assets with
/// file types, read from the pak like any other packaged asset.
/// </remarks>
public static class EngineAssets
{
    /// <summary>Gets the folder of the assets pickers offer, <c>/Engine/Generated</c>.</summary>
    public static AssetPath Generated { get; } = AssetPath.Engine.Combine("Generated");

    /// <summary>Gets the folder of engine-internal assets, <c>/Engine/Internal</c>, which pickers hide.</summary>
    public static AssetPath Internal { get; } = AssetPath.Engine.Combine("Internal");

    /// <summary>Gets the default material every geometry slot without an assignment uses.</summary>
    public static AssetPath DefaultMaterial { get; } = Generated.Combine("Materials/Default" + AssetFileTypes.Material);

    /// <summary>Returns a basic shape of the engine, such as <c>/Engine/Generated/BasicShapes/Cube.ogeo</c>.</summary>
    /// <param name="name">The shape's name, such as <c>Cube</c>.</param>
    /// <returns>The shape's geometry.</returns>
    public static AssetPath BasicShape(string name)
    {
        ArgumentException.ThrowIfNullOrWhiteSpace(name);
        return Generated.Combine("BasicShapes/" + name + AssetFileTypes.Geometry);
    }
}
