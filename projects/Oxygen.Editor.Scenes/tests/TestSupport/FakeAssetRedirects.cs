// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using Oxygen.Editor.ContentPipeline.Relocation;
using Oxygen.Managed.Core;

namespace Oxygen.Editor.Scenes.Tests.TestSupport;

/// <summary>
/// The project's asset moves and deletions. Both <see cref="Resolve(AssetPath)"/> overloads and <see cref="WasDeleted"/>
/// read the same state, which a test may change between two edits.
/// </summary>
public sealed class FakeAssetRedirects : IAssetRedirects
{
    private readonly Lock gate = new();
    private readonly Dictionary<AssetPath, AssetPath> moves = [];
    private readonly HashSet<AssetPath> deleted = [];

    /// <summary>Records that an asset moved. A later move of the new location is followed too.</summary>
    /// <param name="from">The old location.</param>
    /// <param name="to">The new location.</param>
    public void Move(AssetPath from, AssetPath to)
    {
        lock (this.gate)
        {
            this.moves[from] = to;
            _ = this.deleted.Remove(to);
        }
    }

    /// <summary>Records that an asset was deleted.</summary>
    /// <param name="asset">The asset.</param>
    public void Delete(AssetPath asset)
    {
        lock (this.gate)
        {
            _ = this.deleted.Add(asset);
        }
    }

    /// <inheritdoc/>
    public AssetPath Resolve(AssetPath reference)
    {
        ArgumentNullException.ThrowIfNull(reference);
        lock (this.gate)
        {
            var current = reference;
            for (var hops = 0; hops <= this.moves.Count && this.moves.TryGetValue(current, out var next); hops++)
            {
                current = next;
            }

            return current;
        }
    }

    /// <inheritdoc/>
    public string Resolve(string reference)
    {
        ArgumentNullException.ThrowIfNull(reference);
        var isUri = reference.StartsWith("asset:", StringComparison.Ordinal);
        AssetPath? parsed;
        if (!(isUri ? AssetPath.TryParseUri(reference, out parsed) : AssetPath.TryParse(reference, out parsed)))
        {
            return reference;
        }

        var resolved = this.Resolve(parsed);
        return resolved == parsed ? reference : isUri ? resolved.ToUriString() : resolved.VirtualPath;
    }

    /// <inheritdoc/>
    public bool WasDeleted(AssetPath reference)
    {
        lock (this.gate)
        {
            return this.deleted.Contains(reference);
        }
    }
}
