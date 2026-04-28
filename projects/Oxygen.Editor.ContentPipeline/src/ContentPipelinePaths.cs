// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Text;
using Oxygen.Core;

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

        if (!string.Equals(assetUri.Scheme, AssetUris.Scheme, StringComparison.OrdinalIgnoreCase))
        {
            throw new ArgumentException($"Asset URI must use the '{AssetUris.Scheme}' scheme.", nameof(assetUri));
        }

        var path = Uri.UnescapeDataString(assetUri.AbsolutePath).Replace('\\', '/');
        if (!path.StartsWith('/') || path.Contains("//", StringComparison.Ordinal))
        {
            throw new ArgumentException("Asset URI path must be an absolute virtual path.", nameof(assetUri));
        }

        var nativePath = path.EndsWith(".json", StringComparison.OrdinalIgnoreCase)
            ? path[..^".json".Length]
            : path;

        if (!nativePath.EndsWith(expectedExtension, StringComparison.OrdinalIgnoreCase))
        {
            throw new ArgumentException(
                $"Asset URI '{assetUri}' does not normalize to '{expectedExtension}'.",
                nameof(assetUri));
        }

        if (nativePath.Contains("/../", StringComparison.Ordinal)
            || nativePath.Contains("/./", StringComparison.Ordinal)
            || nativePath.EndsWith("/..", StringComparison.Ordinal)
            || nativePath.EndsWith("/.", StringComparison.Ordinal))
        {
            throw new ArgumentException("Asset URI path must not contain relative path segments.", nameof(assetUri));
        }

        return nativePath;
    }

    /// <summary>
    /// Normalizes a scene asset file name to a native scene descriptor identifier.
    /// </summary>
    /// <param name="sceneFileName">The scene source file name or path.</param>
    /// <returns>A native descriptor identifier.</returns>
    public static string NormalizeSceneDescriptorName(string sceneFileName)
    {
        ArgumentException.ThrowIfNullOrWhiteSpace(sceneFileName);

        var name = sceneFileName;
        if (name.EndsWith(".oscene.json", StringComparison.OrdinalIgnoreCase))
        {
            name = name[..^".oscene.json".Length];
        }
        else
        {
            name = Path.GetFileNameWithoutExtension(name);
        }

        var builder = new StringBuilder(capacity: Math.Min(name.Length, 63));
        foreach (var ch in name)
        {
            builder.Append(IsIdentifierBody(ch) ? ch : '_');
            if (builder.Length == 63)
            {
                break;
            }
        }

        if (builder.Length == 0)
        {
            throw new ArgumentException("Scene descriptor name cannot be empty after normalization.", nameof(sceneFileName));
        }

        if (!IsIdentifierStart(builder[0]))
        {
            if (builder.Length == 63)
            {
                builder.Length = 62;
            }

            builder.Insert(0, '_');
        }

        return builder.ToString();
    }

    /// <summary>
    /// Creates the canonical physical cooked root for a project mount.
    /// </summary>
    /// <param name="projectRoot">The absolute project root.</param>
    /// <param name="mountName">The mount name.</param>
    /// <returns>The physical cooked root for the mount.</returns>
    public static string GetCookedMountRoot(string projectRoot, string mountName)
    {
        ArgumentException.ThrowIfNullOrWhiteSpace(projectRoot);
        ArgumentException.ThrowIfNullOrWhiteSpace(mountName);
        return Path.Combine(projectRoot, ".cooked", mountName);
    }

    /// <summary>
    /// Creates the native virtual mount root for a project mount.
    /// </summary>
    /// <param name="mountName">The mount name.</param>
    /// <returns>The native virtual mount root.</returns>
    public static string GetVirtualMountRoot(string mountName)
    {
        ArgumentException.ThrowIfNullOrWhiteSpace(mountName);
        return "/" + mountName.Trim('/');
    }

    private static bool IsIdentifierStart(char ch)
        => ch is (>= 'A' and <= 'Z') or (>= 'a' and <= 'z') or (>= '0' and <= '9') or '_';

    private static bool IsIdentifierBody(char ch)
        => IsIdentifierStart(ch) || ch == '.' || ch == '-';
}
