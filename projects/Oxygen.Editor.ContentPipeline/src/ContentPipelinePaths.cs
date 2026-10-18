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
    /// <summary>
    /// Converts an editor asset URI to the native descriptor virtual path for the expected extension.
    /// </summary>
    /// <param name="assetUri">The editor asset URI.</param>
    /// <param name="expectedExtension">The required native extension, including the leading dot.</param>
    /// <returns>The native descriptor virtual path.</returns>
    public static string ToNativeDescriptorPath(Uri assetUri, string expectedExtension)
    {
        ArgumentNullException.ThrowIfNull(assetUri);
        ArgumentException.ThrowIfNullOrWhiteSpace(expectedExtension);

        if (!assetUri.IsAbsoluteUri
            || !string.Equals(assetUri.Scheme, AssetUris.Scheme, StringComparison.OrdinalIgnoreCase)
            || assetUri.Host.Length != 0
            || assetUri.UserInfo.Length != 0
            || assetUri.Port != -1
            || assetUri.Query.Length != 0
            || assetUri.Fragment.Length != 0)
        {
            throw new ArgumentException(
                $"Asset URI must be an absolute path-only '{AssetUris.Scheme}' URI.",
                nameof(assetUri));
        }

        var path = Uri.UnescapeDataString(assetUri.AbsolutePath).Replace('\\', '/');
        var nativePath = path.EndsWith(".json", StringComparison.OrdinalIgnoreCase)
            ? path[..^".json".Length]
            : path;
        if (VirtualPath.Validate(nativePath) is { } error)
        {
            throw new ArgumentException($"Asset URI path is not a virtual path the engine accepts ({error.ToCode()}).", nameof(assetUri));
        }

        return !nativePath.EndsWith(expectedExtension, StringComparison.OrdinalIgnoreCase)
            ? throw new ArgumentException(
                $"Asset URI '{assetUri}' does not normalize to '{expectedExtension}'.",
                nameof(assetUri))
            : nativePath;
    }

    /// <summary>
    /// Creates the native virtual mount root for a project mount.
    /// </summary>
    /// <param name="mountName">The mount name.</param>
    /// <returns>The native virtual mount root.</returns>
    /// <exception cref="ArgumentException">The mount name is refused by the mount rule.</exception>
    public static string GetVirtualMountRoot(string mountName)
    {
        ArgumentNullException.ThrowIfNull(mountName);
        return VirtualPath.ValidateMountName(mountName) is { } error
            ? throw new ArgumentException($"The mount name is refused ({error.ToCode()}).", nameof(mountName))
            : "/" + mountName;
    }

    /// <summary>Gets the virtual path a scene cooks to, which the runtime loads it by.</summary>
    /// <param name="sceneAssetUri">The scene's asset identity, such as <c>asset:///Content/Scenes/Level 1.oscene.json</c>.</param>
    /// <returns>The cooked scene's virtual path, such as <c>/Content/Scenes/Level 1.oscene</c>.</returns>
    public static string ToCookedScenePath(Uri sceneAssetUri)
        => ToNativeDescriptorPath(sceneAssetUri, AssetFileTypes.Scene);
}
