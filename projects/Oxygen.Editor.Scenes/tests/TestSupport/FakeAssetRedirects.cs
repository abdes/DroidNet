// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using Oxygen.Editor.ContentPipeline.Relocation;

namespace Oxygen.Editor.Scenes.Tests.TestSupport;

/// <summary>
/// The project's asset moves and deletions. Both <see cref="Resolve(Uri)"/> overloads and <see cref="WasDeleted"/>
/// read the same state, which a test may change between two edits.
/// </summary>
public sealed class FakeAssetRedirects : IAssetRedirects
{
    private readonly Lock gate = new();
    private readonly Dictionary<Uri, Uri> moves = [];
    private readonly HashSet<Uri> deleted = [];

    /// <summary>Records that an asset moved. A later move of the new location is followed too.</summary>
    /// <param name="from">The old location.</param>
    /// <param name="to">The new location.</param>
    public void Move(Uri from, Uri to)
    {
        lock (this.gate)
        {
            this.moves[from] = to;
            _ = this.deleted.Remove(to);
        }
    }

    /// <summary>Records that an asset was deleted.</summary>
    /// <param name="asset">The asset.</param>
    public void Delete(Uri asset)
    {
        lock (this.gate)
        {
            _ = this.deleted.Add(asset);
        }
    }

    /// <inheritdoc/>
    public Uri Resolve(Uri reference)
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
        => Uri.TryCreate(reference, UriKind.Absolute, out var uri) && this.Resolve(uri) is var resolved && resolved != uri
            ? resolved.ToString()
            : reference;

    /// <inheritdoc/>
    public bool WasDeleted(Uri reference)
    {
        lock (this.gate)
        {
            return this.deleted.Contains(reference);
        }
    }
}
