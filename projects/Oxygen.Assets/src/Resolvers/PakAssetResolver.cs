// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using Oxygen.Assets.Model;

namespace Oxygen.Assets.Resolvers;

/// <summary>
/// Resolves assets from PAK files (Engine and Package mount points).
/// </summary>
/// <remarks>
/// <para>
/// This is a stub implementation for Phase 4. Full implementation will be added in later phases
/// when PAK file integration with the Oxygen Engine is implemented.
/// </para>
/// <para>
/// Future implementation will handle:
/// <list type="bullet">
/// <item>Loading assets from <c>oxygen.pak</c> (Engine mount point)</item>
/// <item>Loading assets from package PAK files (e.g., <c>Oxygen.StandardAssets.pak</c>)</item>
/// <item>Using the engine's <c>PakFile</c> API via interop</item>
/// <item>Caching loaded assets</item>
/// </list>
/// </para>
/// </remarks>
public sealed class PakAssetResolver : IAssetResolver
{
    /// <inheritdoc/>
    public bool CanResolve(string authority)
        => string.Equals(authority, "Engine", StringComparison.OrdinalIgnoreCase)
            || (!string.Equals(authority, "Content", StringComparison.OrdinalIgnoreCase)
               && !string.Equals(authority, "Generated", StringComparison.OrdinalIgnoreCase));

    /// <inheritdoc/>
    public Task<Asset?> ResolveAsync(Uri uri)
    {
        // Stub implementation for Phase 4
        // TODO: Implement PAK file asset loading in future phases
        return Task.FromResult<Asset?>(null);
    }
}
