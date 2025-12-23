// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

namespace Oxygen.Assets.Resolvers;

/// <summary>
/// Resolves assets from the "Content" mount point (user project folder).
/// </summary>
/// <remarks>
/// <para>
/// This is a stub implementation for Phase 4. Full implementation will be added in later phases
/// when file system asset loading and serialization are required.
/// </para>
/// <para>
/// Future implementation will handle:
/// <list type="bullet">
/// <item>Mapping URIs to file system paths under <c>{ProjectRoot}/Content/</c></item>
/// <item>Deserializing asset metadata from JSON or binary formats</item>
/// <item>Caching loaded assets</item>
/// </list>
/// </para>
/// </remarks>
public sealed class FileSystemAssetResolver : IAssetResolver
{
    /// <inheritdoc/>
    public bool CanResolve(string authority)
        => string.Equals(authority, "Content", StringComparison.OrdinalIgnoreCase);

    /// <inheritdoc/>
    public Task<Asset?> ResolveAsync(Uri uri)
    {
        // Stub implementation for Phase 4
        // TODO: Implement file system asset loading in future phases
        return Task.FromResult<Asset?>(null);
    }
}
