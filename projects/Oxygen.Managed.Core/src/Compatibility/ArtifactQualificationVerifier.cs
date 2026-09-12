// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Collections.Immutable;
using System.Runtime.Versioning;
using System.Security.Cryptography;
using System.Text.Json;
using System.Text.Json.Serialization;
using Oxygen.Managed.Core.Diagnostics;

namespace Oxygen.Managed.Core.Compatibility;

/// <summary>Verifies a fixed manifest against the host's complete required artifact inventory.</summary>
[SupportedOSPlatform("windows")]
public static class ArtifactQualificationVerifier
{
    private static readonly JsonSerializerOptions ManifestOptions = new()
    {
        PropertyNamingPolicy = JsonNamingPolicy.CamelCase,
        UnmappedMemberHandling = JsonUnmappedMemberHandling.Disallow,
        RespectNullableAnnotations = true,
        RespectRequiredConstructorParameters = true,
    };

    /// <summary>Verifies all required files and retains read leases through the caller's native operation.</summary>
    /// <param name="operationId">The diagnostic correlation identity.</param>
    /// <param name="manifestPath">The fixed qualification manifest to read.</param>
    /// <param name="configuration">The running build configuration.</param>
    /// <param name="requiredArtifacts">The complete inventory required by the host, independently of the manifest.</param>
    /// <param name="cancellationToken">Cancels verification and releases all acquired file handles.</param>
    /// <param name="progress">Optional progress for the host's startup or cooking operation.</param>
    /// <returns>The owned qualified set or failures that prevent native work.</returns>
    public static async Task<ArtifactQualificationResult> VerifyAsync(
        Guid operationId,
        string manifestPath,
        string configuration,
        IReadOnlyList<QualificationArtifactLocation> requiredArtifacts,
        CancellationToken cancellationToken,
        IProgress<ArtifactQualificationProgress>? progress = null)
    {
        if (!OperatingSystem.IsWindows())
        {
            throw new PlatformNotSupportedException("Artifact qualification requires Windows file-sharing protection.");
        }

        ArgumentException.ThrowIfNullOrWhiteSpace(manifestPath);
        ArgumentNullException.ThrowIfNull(requiredArtifacts);
        cancellationToken.ThrowIfCancellationRequested();
        var requirements = requiredArtifacts.ToImmutableArray();
        ValidateRequirements(configuration, requirements);
        ArtifactQualificationManifest manifest;
        try
        {
            var bytes = await File.ReadAllBytesAsync(manifestPath, cancellationToken).ConfigureAwait(false);
            using var document = JsonDocument.Parse(bytes);
            ValidateUniqueProperties(document.RootElement);
            manifest = document.RootElement.Deserialize<ArtifactQualificationManifest>(ManifestOptions)
                ?? throw new InvalidDataException("The qualification manifest is empty.");
            ValidateManifest(manifest);
        }
        catch (Exception ex) when (ex is IOException or UnauthorizedAccessException or JsonException or InvalidDataException)
        {
            return new(Artifacts: null, [Failure(operationId, ArtifactQualificationDiagnosticCodes.ManifestInvalid, "The fixed qualification manifest is missing or invalid.", manifestPath, ex.ToString())]);
        }

        if (!string.Equals(manifest.Configuration, configuration, StringComparison.Ordinal))
        {
            var mismatch = $"The qualification manifest is for {manifest.Configuration}; this build is {configuration}.";
            return new(Artifacts: null, [Failure(operationId, ArtifactQualificationDiagnosticCodes.ConfigurationMismatch, mismatch, manifestPath)]);
        }

        return requirements.Select(static artifact => artifact.Id).ToHashSet(StringComparer.Ordinal).SetEquals(manifest.Artifacts.Select(static artifact => artifact.Id))
            ? await VerifyFilesAsync(operationId, manifest, requirements, progress, cancellationToken).ConfigureAwait(false)
            : new(Artifacts: null, [Failure(operationId, ArtifactQualificationDiagnosticCodes.ArtifactSetMismatch, "The installed artifact set differs from the fixed qualification manifest.", manifestPath)]);
    }

    private static void ValidateRequirements(string configuration, IReadOnlyList<QualificationArtifactLocation> requirements)
    {
        if (configuration is not ("Debug" or "Release"))
        {
            throw new ArgumentException("Qualification requires an explicit Debug or Release configuration.", nameof(configuration));
        }

        var ids = new HashSet<string>(StringComparer.Ordinal);
        var paths = new HashSet<string>(OperatingSystem.IsWindows() ? StringComparer.OrdinalIgnoreCase : StringComparer.Ordinal);
        if (requirements.Count == 0 || requirements.Any(artifact => artifact is null
            || !IsValidId(artifact.Id) || !ids.Add(artifact.Id)
            || !Path.IsPathFullyQualified(artifact.FullPath) || !paths.Add(Path.GetFullPath(artifact.FullPath))))
        {
            throw new ArgumentException("Qualification requires a nonempty inventory of unique identities and absolute file paths.", nameof(requirements));
        }
    }

    private static void ValidateManifest(ArtifactQualificationManifest manifest)
    {
        var ids = new HashSet<string>(StringComparer.Ordinal);
        if (manifest.Version != 1 || manifest.Configuration is not ("Debug" or "Release")
            || manifest.SourceRevision.Length is not (40 or 64) || !manifest.SourceRevision.All(Uri.IsHexDigit)
            || manifest.Artifacts.IsDefaultOrEmpty
            || manifest.Artifacts.Any(artifact => artifact is null || !IsValidId(artifact.Id) || !ids.Add(artifact.Id)
                || artifact.Size < 0 || artifact.Sha256.Length != SHA256.HashSizeInBytes * 2 || !artifact.Sha256.All(Uri.IsHexDigit)
                || (artifact.SchemaId is not null && string.IsNullOrWhiteSpace(artifact.SchemaId))))
        {
            throw new InvalidDataException("The qualification manifest contains invalid or duplicate artifact identities, hashes, schema identifiers or build metadata.");
        }
    }

    private static bool IsValidId(string id)
        => !string.IsNullOrWhiteSpace(id) && id.Split('/').All(static segment => segment is not ("" or "." or "..")
            && !segment.EndsWith('.') && !segment.EndsWith(' ')
            && !segment.Any(static ch => char.IsControl(ch) || "<>:\"\\|?*".Contains(ch, StringComparison.Ordinal)));

    private static void ValidateUniqueProperties(JsonElement element)
    {
        if (element.ValueKind == JsonValueKind.Object)
        {
            var names = new HashSet<string>(StringComparer.Ordinal);
            foreach (var property in element.EnumerateObject())
            {
                if (!names.Add(property.Name))
                {
                    throw new InvalidDataException($"The qualification manifest repeats property '{property.Name}'.");
                }

                ValidateUniqueProperties(property.Value);
            }
        }
        else if (element.ValueKind == JsonValueKind.Array)
        {
            foreach (var item in element.EnumerateArray())
            {
                ValidateUniqueProperties(item);
            }
        }
    }

    [System.Diagnostics.CodeAnalysis.SuppressMessage("Reliability", "CA2000:Dispose objects before losing scope", Justification = "The successful result transfers its lease to the caller; all unsuccessful paths release acquired streams in finally.")]
    private static async Task<ArtifactQualificationResult> VerifyFilesAsync(
        Guid operationId,
        ArtifactQualificationManifest manifest,
        IReadOnlyList<QualificationArtifactLocation> requiredArtifacts,
        IProgress<ArtifactQualificationProgress>? progress,
        CancellationToken cancellationToken)
    {
        var expected = manifest.Artifacts.ToDictionary(static artifact => artifact.Id, StringComparer.Ordinal);
        var streams = new List<FileStream>();
        var diagnostics = new List<DiagnosticRecord>();
        var transferred = false;
        var checkedCount = 0;
        try
        {
            foreach (var artifact in requiredArtifacts.OrderBy(static artifact => artifact.Id, StringComparer.Ordinal))
            {
                cancellationToken.ThrowIfCancellationRequested();
                await VerifyFileAsync(operationId, artifact, expected[artifact.Id], streams, diagnostics, cancellationToken).ConfigureAwait(false);
                progress?.Report(new(artifact.Id, ++checkedCount, requiredArtifacts.Count));
            }

            cancellationToken.ThrowIfCancellationRequested();
            if (diagnostics.Count != 0)
            {
                return new(Artifacts: null, [.. diagnostics]);
            }

            var lease = new QualifiedArtifactLease(
                ComputeFingerprint(manifest),
                requiredArtifacts.ToImmutableDictionary(static artifact => artifact.Id, static artifact => Path.GetFullPath(artifact.FullPath), StringComparer.Ordinal),
                [.. streams]);
            var result = new ArtifactQualificationResult(lease, []);
            transferred = true;
            return result;
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

    private static async Task VerifyFileAsync(
        Guid operationId,
        QualificationArtifactLocation artifact,
        QualifiedArtifact expected,
        List<FileStream> streams,
        List<DiagnosticRecord> diagnostics,
        CancellationToken cancellationToken)
    {
        if (!string.Equals(artifact.SchemaId, expected.SchemaId, StringComparison.Ordinal))
        {
            diagnostics.Add(Failure(operationId, ArtifactQualificationDiagnosticCodes.ArtifactMismatch, $"The required schema identifier differs from qualification: {artifact.Id}.", artifact.FullPath));
            return;
        }

        try
        {
            var stream = new FileStream(artifact.FullPath, FileMode.Open, FileAccess.Read, FileShare.Read, 65536, FileOptions.Asynchronous | FileOptions.SequentialScan);
            streams.Add(stream);
            if (stream.Length != expected.Size
                || !string.Equals(Convert.ToHexString(await SHA256.HashDataAsync(stream, cancellationToken).ConfigureAwait(false)), expected.Sha256, StringComparison.OrdinalIgnoreCase))
            {
                diagnostics.Add(Failure(operationId, ArtifactQualificationDiagnosticCodes.ArtifactMismatch, $"The installed file differs from qualification: {artifact.Id}.", artifact.FullPath));
            }
        }
        catch (Exception ex) when (ex is IOException or UnauthorizedAccessException)
        {
            diagnostics.Add(Failure(operationId, ArtifactQualificationDiagnosticCodes.ArtifactMismatch, $"The qualified file is missing or inaccessible: {artifact.Id}.", artifact.FullPath, ex.ToString()));
        }
    }

    private static string ComputeFingerprint(ArtifactQualificationManifest manifest)
    {
        var bytes = JsonSerializer.SerializeToUtf8Bytes(new
        {
            manifest.Version,
            manifest.Configuration,
            Artifacts = manifest.Artifacts.OrderBy(static artifact => artifact.Id, StringComparer.Ordinal)
                .Select(static artifact => artifact with { Sha256 = artifact.Sha256.ToUpperInvariant() }),
        });
        return Convert.ToHexString(SHA256.HashData(bytes));
    }

    private static DiagnosticRecord Failure(Guid operationId, string code, string message, string path, string? technical = null)
        => new()
        {
            OperationId = operationId,
            Domain = FailureDomain.RuntimeDiscovery,
            Severity = DiagnosticSeverity.Error,
            Code = code,
            Message = message,
            TechnicalMessage = technical,
            AffectedPath = path,
        };
}
