// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Collections.Immutable;
using Oxygen.Editor.Projects;
using Oxygen.Managed.Core;

namespace Oxygen.Editor.ContentPipeline.Relocation;

/// <summary>Reads the direct references of the session project's saved authored descriptors.</summary>
/// <param name="session">The project session, whose writable mounts hold the descriptors.</param>
public sealed class AuthoredReferences(IProjectSession session) : IAuthoredReferences
{
    private ImmutableDictionary<AssetPath, IReadOnlyList<AssetPath>> references = [];

    /// <inheritdoc/>
    public IReadOnlyList<AssetPath> Of(AssetPath asset)
    {
        ArgumentNullException.ThrowIfNull(asset);
        return Volatile.Read(ref this.references).GetValueOrDefault(asset, []);
    }

    /// <inheritdoc/>
    public async Task RefreshAsync(CancellationToken cancellationToken)
    {
        var index = await AuthoredReferenceIndex.BuildAsync(session.Context, cancellationToken).ConfigureAwait(false);
        var read = index.Descriptors
            .GroupBy(static descriptor => descriptor.Identity)
            .ToImmutableDictionary(
                static group => group.Key,
                static group => (IReadOnlyList<AssetPath>)[.. group.SelectMany(static descriptor => descriptor.References).Distinct()]);
        Volatile.Write(ref this.references, read);
    }
}
