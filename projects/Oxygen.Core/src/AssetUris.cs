// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

namespace Oxygen.Core;

/// <summary>
/// Provides centralized constants and helpers for building asset URIs.
/// </summary>
public static class AssetUris
{
    /// <summary>
    /// The URI scheme for assets.
    /// </summary>
    public const string Scheme = "asset";

    /// <summary>
    /// The mount point for engine-provided assets.
    /// </summary>
    /// <remarks>
    /// Results in URIs like <c>asset:///Engine/...</c>.
    /// </remarks>
    public const string EngineMountPoint = "Engine";

    /// <summary>
    /// The mount point for project-specific content assets.
    /// </summary>
    /// <remarks>
    /// Results in URIs like <c>asset:///Content/...</c>.
    /// </remarks>
    public const string ContentMountPoint = "Content";

    /// <summary>
    /// The path prefix for generated basic shapes.
    /// </summary>
    public const string GeneratedPath = "Generated/BasicShapes";

    /// <summary>
    /// Builds an engine asset URI.
    /// </summary>
    /// <param name="path">The path within the engine mount point.</param>
    /// <returns>The full asset URI.</returns>
    public static string BuildEngineUri(string path) => $"{Scheme}:///{EngineMountPoint}/{path.TrimStart('/')}";

    /// <summary>
    /// Builds a content asset URI.
    /// </summary>
    /// <param name="path">The path within the content mount point.</param>
    /// <returns>The full asset URI.</returns>
    public static string BuildContentUri(string path) => $"{Scheme}:///{ContentMountPoint}/{path.TrimStart('/')}";

    /// <summary>
    /// Builds a URI for a generated basic shape.
    /// </summary>
    /// <param name="shapeName">The name of the shape (e.g., "Cube").</param>
    /// <returns>The full asset URI.</returns>
    public static string BuildGeneratedUri(string shapeName) => BuildEngineUri($"{GeneratedPath}/{shapeName}");
}
