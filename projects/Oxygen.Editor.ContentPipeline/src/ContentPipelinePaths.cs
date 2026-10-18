// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using Oxygen.Managed.Assets;
using Oxygen.Managed.Core;

namespace Oxygen.Editor.ContentPipeline;

/// <summary>
/// Shared path normalization helpers for native content-pipeline inputs.
/// </summary>
public static class ContentPipelinePaths
{
    /// <summary>Gets the virtual path the engine reads an asset by, checking that it is a path of the expected type.</summary>
    /// <param name="asset">The asset's identity.</param>
    /// <param name="expectedExtension">The required native extension, including the leading dot.</param>
    /// <returns>The native descriptor virtual path.</returns>
    /// <exception cref="ArgumentException">
    /// The identity is not a virtual path the engine accepts, or not of the expected type.
    /// </exception>
    public static string ToNativeDescriptorPath(AssetPath asset, string expectedExtension)
    {
        ArgumentNullException.ThrowIfNull(asset);
        ArgumentException.ThrowIfNullOrWhiteSpace(expectedExtension);
        if (VirtualPath.Validate(asset.VirtualPath) is { } error)
        {
            throw new ArgumentException($"'{asset}' is not a virtual path the engine accepts ({error.ToCode()}).", nameof(asset));
        }

        return asset.Name.EndsWith(expectedExtension, StringComparison.Ordinal)
            ? asset.VirtualPath
            : throw new ArgumentException($"'{asset}' is not a '{expectedExtension}' asset.", nameof(asset));
    }

    /// <summary>Gets the virtual path a scene cooks to, which the runtime loads it by.</summary>
    /// <param name="scene">The scene's identity, such as <c>/Content/Scenes/Level 1.oscene</c>.</param>
    /// <returns>The cooked scene's virtual path.</returns>
    public static string ToCookedScenePath(AssetPath scene)
        => ToNativeDescriptorPath(scene, AssetFileTypes.Scene);
}
