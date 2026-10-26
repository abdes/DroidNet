// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Collections.Immutable;
using System.Security.Cryptography;

namespace Oxygen.Managed.Core.Compatibility;

/// <summary>Checks cooker inputs and keeps them in place through native worker completion.</summary>
public static partial class NativeCompatibilityVerifier
{
    /// <summary>
    /// Opens the current cooker files and checks that the editor's copy of each cooking schema matches the engine's.
    /// The tools are not hashed: cooked output is judged by the format versions the engine reads, not by the binaries
    /// that wrote it.
    /// </summary>
    /// <param name="operationId">The diagnostic correlation identity.</param>
    /// <param name="configuration">The build configuration.</param>
    /// <param name="locations">The actual cooker files used by the operation.</param>
    /// <param name="cancellationToken">Cancels acquisition.</param>
    /// <returns>The leased cooker files or schema mismatch diagnostics.</returns>
    [System.Diagnostics.CodeAnalysis.SuppressMessage("Reliability", "CA2000:Dispose objects before losing scope", Justification = "The returned artifact lease owns every acquired stream; failure disposes all streams in finally.")]
    public static async Task<NativeCompatibilityResult> CaptureCookingAsync(Guid operationId, string configuration, IReadOnlyList<NativeArtifactLocation> locations, CancellationToken cancellationToken)
    {
        ValidateRequirements(configuration, locations);
        var streams = new List<FileStream>();
        var transferred = false;
        try
        {
            var schemas = new Dictionary<string, (string Sha256, string? SchemaId)>(StringComparer.Ordinal);
            foreach (var location in locations.OrderBy(static value => value.Id, StringComparer.Ordinal))
            {
                cancellationToken.ThrowIfCancellationRequested();
                var stream = new FileStream(location.FullPath, FileMode.Open, FileAccess.Read, FileShare.Read, 65536, FileOptions.Asynchronous | FileOptions.SequentialScan);
                streams.Add(stream);
                if (IsSchema(location.Id))
                {
                    schemas[location.Id] = (Convert.ToHexString(await SHA256.HashDataAsync(stream, cancellationToken).ConfigureAwait(false)), location.SchemaId);
                }
            }

            foreach (var (id, schema) in schemas.Where(static entry => entry.Key.StartsWith("engine/schemas/", StringComparison.Ordinal)))
            {
                var editorId = "editor/Schemas/" + Path.GetFileName(id);
                if (!schemas.TryGetValue(editorId, out var editorSchema)
                    || !string.Equals(editorSchema.Sha256, schema.Sha256, StringComparison.Ordinal)
                    || !string.Equals(editorSchema.SchemaId, schema.SchemaId, StringComparison.Ordinal))
                {
                    return new(Artifacts: null, [Failure(operationId, NativeCompatibilityDiagnosticCodes.ArtifactMismatch, $"Editor and cooker schemas differ: {Path.GetFileName(id)}. Rebuild the editor against the installed SDK.", editorId)]);
                }
            }

            cancellationToken.ThrowIfCancellationRequested();
            var lease = new NativeArtifactLease(locations.ToImmutableDictionary(static value => value.Id, static value => value.FullPath, StringComparer.Ordinal), [.. streams]);
            transferred = true;
            return new(lease, []);
        }
        finally
        {
            if (!transferred)
            {
                foreach (var stream in streams)
                {
                    await stream.DisposeAsync().ConfigureAwait(false);
                }
            }
        }
    }

    private static bool IsSchema(string id)
        => id.StartsWith("engine/schemas/", StringComparison.Ordinal) || id.StartsWith("editor/Schemas/", StringComparison.Ordinal);
}
