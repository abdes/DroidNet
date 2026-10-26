// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using Oxygen.Editor.ContentPipeline.Relocation;
using Oxygen.Managed.Core;

namespace Oxygen.Editor.ContentPipeline.Testing;

/// <summary>
/// An <see cref="IAuthoredReferences"/> for tests: the saved references a test records, such as a scene instancing
/// another. <see cref="RefreshAsync"/> reads nothing; it counts the reads asked for.
/// </summary>
public sealed class FakeAuthoredReferences : IAuthoredReferences
{
    private readonly Lock gate = new();
    private readonly Dictionary<AssetPath, IReadOnlyList<AssetPath>> references = [];

    /// <summary>Gets how many times a read of the saved descriptors was asked for.</summary>
    public int Refreshes { get; private set; }

    /// <summary>Records what a saved asset references, replacing what was recorded for it.</summary>
    /// <param name="asset">The asset's identity.</param>
    /// <param name="referenced">The identities it references.</param>
    public void Set(AssetPath asset, params IReadOnlyList<AssetPath> referenced)
    {
        lock (this.gate)
        {
            this.references[asset] = [.. referenced];
        }
    }

    /// <inheritdoc/>
    public IReadOnlyList<AssetPath> Of(AssetPath asset)
    {
        ArgumentNullException.ThrowIfNull(asset);
        lock (this.gate)
        {
            return this.references.GetValueOrDefault(asset, []);
        }
    }

    /// <inheritdoc/>
    public Task RefreshAsync(CancellationToken cancellationToken)
    {
        lock (this.gate)
        {
            this.Refreshes++;
        }

        return Task.CompletedTask;
    }
}
