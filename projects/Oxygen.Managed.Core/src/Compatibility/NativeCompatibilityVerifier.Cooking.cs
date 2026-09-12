// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Collections.Immutable;
using System.Security.Cryptography;

namespace Oxygen.Managed.Core.Compatibility;

/// <summary>Checks cooker inputs and retains their content identity through native worker completion.</summary>
public static partial class NativeCompatibilityVerifier
{
    /// <summary>Captures the current cooker producer files and checks matching editor/native base schemas.</summary>
    /// <param name="operationId">The diagnostic correlation identity.</param>
    /// <param name="configuration">The build configuration.</param>
    /// <param name="locations">The actual producer files used by the operation.</param>
    /// <param name="cancellationToken">Cancels acquisition.</param>
    /// <returns>The leased producer files or schema mismatch diagnostics.</returns>
    [System.Diagnostics.CodeAnalysis.SuppressMessage("Reliability", "CA2000:Dispose objects before losing scope", Justification = "The returned artifact lease owns every acquired stream; failure disposes all streams in finally.")]
    public static async Task<NativeCompatibilityResult> CaptureCookingAsync(Guid operationId, string configuration, IReadOnlyList<NativeArtifactLocation> locations, CancellationToken cancellationToken)
    {
        ValidateRequirements(configuration, locations);
        var streams = new List<FileStream>();
        var transferred = false;
        try
        {
            var artifacts = new List<NativeArtifact>();
            foreach (var location in locations.OrderBy(static value => value.Id, StringComparer.Ordinal))
            {
                cancellationToken.ThrowIfCancellationRequested();
                var stream = new FileStream(location.FullPath, FileMode.Open, FileAccess.Read, FileShare.Read, 65536, FileOptions.Asynchronous | FileOptions.SequentialScan);
                streams.Add(stream);
                var hash = Convert.ToHexString(await SHA256.HashDataAsync(stream, cancellationToken).ConfigureAwait(false));
                artifacts.Add(new(location.Id, stream.Length, hash, location.SchemaId));
            }

            foreach (var schema in artifacts.Where(static value => value.Id.StartsWith("engine/schemas/", StringComparison.Ordinal)))
            {
                var editorId = "editor/Schemas/" + Path.GetFileName(schema.Id);
                if (artifacts.Find(value => string.Equals(value.Id, editorId, StringComparison.Ordinal)) is not { } editorSchema
                    || !string.Equals(editorSchema.Sha256, schema.Sha256, StringComparison.Ordinal)
                    || !string.Equals(editorSchema.SchemaId, schema.SchemaId, StringComparison.Ordinal))
                {
                    return new(Artifacts: null, [Failure(operationId, NativeCompatibilityDiagnosticCodes.ArtifactMismatch, $"Editor and cooker schemas differ: {Path.GetFileName(schema.Id)}. Rebuild the editor against the installed SDK.", editorId)]);
                }
            }

            cancellationToken.ThrowIfCancellationRequested();
            var lease = new NativeArtifactLease(ComputeFingerprint(new(1, configuration, [.. artifacts])), locations.ToImmutableDictionary(static value => value.Id, static value => value.FullPath, StringComparer.Ordinal), [.. streams]);
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
}
