// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

namespace Oxygen.Managed.Core;

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
    /// The path prefix for generated assets.
    /// </summary>
    public const string GeneratedPath = "Generated";

    /// <summary>
    /// Builds an engine asset URI.
    /// </summary>
    /// <param name="path">The path within the engine mount point.</param>
    /// <returns>The full asset URI.</returns>
    public static Uri BuildEngineUri(string path) => new($"{Scheme}:///{EngineMountPoint}/{path.TrimStart('/')}");

    /// <summary>
    /// Builds a content asset URI.
    /// </summary>
    /// <param name="path">The path within the content mount point.</param>
    /// <returns>The full asset URI.</returns>
    public static Uri BuildContentUri(string path) => new($"{Scheme}:///{ContentMountPoint}/{path.TrimStart('/')}");

    /// <summary>
    /// Builds a URI for a generated asset.
    /// </summary>
    /// <param name="assetPath">The path of the asset relative to the generated root (e.g., "BasicShapes/Cube").</param>
    /// <returns>The full asset URI.</returns>
    public static Uri BuildGeneratedUri(string assetPath) => BuildEngineUri($"{GeneratedPath}/{assetPath}");

    /// <summary>
    /// Gets a value indicating whether a URI names an asset: an absolute URI with the <see cref="Scheme"/> scheme and a
    /// path below the root.
    /// </summary>
    /// <param name="uri">The URI to check.</param>
    /// <returns><see langword="true"/> when <paramref name="uri"/> names an asset; otherwise <see langword="false"/>.</returns>
    public static bool IsAssetUri([System.Diagnostics.CodeAnalysis.NotNullWhen(true)] Uri? uri)
        => uri is { IsAbsoluteUri: true }
            && string.Equals(uri.Scheme, Scheme, StringComparison.OrdinalIgnoreCase)
            && uri.AbsolutePath.Trim('/').Length > 0;

    /// <summary>
    /// Returns a URI that names an asset, or throws.
    /// </summary>
    /// <param name="uri">The URI to check.</param>
    /// <param name="paramName">The name of the parameter that holds <paramref name="uri"/>.</param>
    /// <returns><paramref name="uri"/>.</returns>
    /// <exception cref="ArgumentNullException"><paramref name="uri"/> is <see langword="null"/>.</exception>
    /// <exception cref="ArgumentException"><paramref name="uri"/> does not name an asset (see <see cref="IsAssetUri"/>).</exception>
    public static Uri EnsureAssetUri(Uri uri, [System.Runtime.CompilerServices.CallerArgumentExpression(nameof(uri))] string? paramName = null)
    {
        ArgumentNullException.ThrowIfNull(uri, paramName);
        return IsAssetUri(uri)
            ? uri
            : throw new ArgumentException($"'{uri}' is not an absolute '{Scheme}' URI naming an asset.", paramName);
    }
}
