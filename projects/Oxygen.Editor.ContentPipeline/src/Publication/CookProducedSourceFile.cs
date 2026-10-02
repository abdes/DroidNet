// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Collections.Immutable;
using System.Security.Cryptography;
using System.Text.Json.Serialization;
using Oxygen.Editor.ContentPipeline.Snapshots;

namespace Oxygen.Editor.ContentPipeline.Publication;

/// <summary>Producer-owned source metadata emitted from, and committed after, immutable cook inputs.</summary>
/// <param name="RelativePath">The retained source settings path within the project.</param>
/// <param name="Before">The exact settings consumed by the native producer.</param>
/// <param name="After">The validated settings containing the returned native provenance.</param>
internal sealed record CookProducedSourceFile(string RelativePath, byte[] Before, byte[] After)
{
    /// <summary>Gets the consumed file identity.</summary>
    [JsonIgnore]
    public string BeforeHash => Convert.ToHexString(SHA256.HashData(this.Before));

    /// <summary>Gets the produced file identity.</summary>
    [JsonIgnore]
    public string AfterHash => Convert.ToHexString(SHA256.HashData(this.After));

    /// <summary>Builds the expected published source state without modifying consumed input records.</summary>
    /// <param name="consumed">The immutable native input set.</param>
    /// <param name="produced">Updates committed with the resulting products.</param>
    /// <returns>The expected file identities after publication.</returns>
    internal static ImmutableArray<CookSnapshotInput> ExpectedInputs(ImmutableArray<CookSnapshotInput> consumed, ImmutableArray<CookProducedSourceFile> produced)
    {
        var updates = produced.ToDictionary(static file => file.RelativePath, StringComparer.Ordinal);
        var expected = consumed.Select(input =>
        {
            if (!updates.Remove(input.RelativePath, out var update))
            {
                return input;
            }

            return input.IsAbsent || !string.Equals(input.DiscoveryHash, update.BeforeHash, StringComparison.Ordinal)
                ? throw new InvalidDataException("Produced source metadata does not identify its consumed input.")
                : input with { DiscoveryHash = update.AfterHash };
        }).ToImmutableArray();
        return updates.Count == 0 ? expected
            : throw new InvalidDataException("Produced source metadata is absent from the consumed input set.");
    }
}
