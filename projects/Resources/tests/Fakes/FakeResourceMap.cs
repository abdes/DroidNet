// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Diagnostics.CodeAnalysis;

namespace DroidNet.Resources.Tests.Fakes;

[ExcludeFromCodeCoverage]
internal sealed class FakeResourceMap(Dictionary<string, string> store) : IResourceMap
{
    private readonly Dictionary<string, string> store = store ?? new Dictionary<string, string>(StringComparer.Ordinal);

    public bool TryGetValue(string key, out string? value) => this.store.TryGetValue(key, out value);

    public IResourceMap GetSubtree(string key)
    {
        // Subtree keys are in the format 'subtree/key' in the store; return a new FakeResourceMap with trimmed keys
        var prefix = key + "/";
        var subStore = this.store
            .Where(kv => kv.Key.StartsWith(prefix, StringComparison.Ordinal))
            .ToDictionary(kv => kv.Key[prefix.Length..], kv => kv.Value, StringComparer.Ordinal);
        return subStore.Count != 0 ? new FakeResourceMap(subStore) : new EmptyResourceMap();
    }
}
