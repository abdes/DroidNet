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

/// <summary>Verifies a build receipt against the host's complete required artifact inventory.</summary>
[SupportedOSPlatform("windows")]
public static partial class NativeCompatibilityVerifier
{
    private static readonly JsonSerializerOptions ReceiptOptions = new()
    {
        PropertyNamingPolicy = JsonNamingPolicy.CamelCase,
        UnmappedMemberHandling = JsonUnmappedMemberHandling.Disallow,
        RespectNullableAnnotations = true,
        RespectRequiredConstructorParameters = true,
    };

    /// <summary>Verifies all required files and retains read leases through the caller's native operation.</summary>
    /// <param name="operationId">The diagnostic correlation identity.</param>
    /// <param name="receiptPath">The Interop SDK build receipt to read.</param>
    /// <param name="configuration">The running build configuration.</param>
    /// <param name="requiredArtifacts">The complete inventory required by the host, independently of the receipt.</param>
    /// <param name="cancellationToken">Cancels verification and releases all acquired file handles.</param>
    /// <param name="progress">Optional progress for the host's startup or cooking operation.</param>
    /// <returns>The owned compatible set or failures that prevent native work.</returns>
    public static async Task<NativeCompatibilityResult> VerifyAsync(
        Guid operationId,
        string receiptPath,
        string configuration,
        IReadOnlyList<NativeArtifactLocation> requiredArtifacts,
        CancellationToken cancellationToken,
        IProgress<NativeCompatibilityProgress>? progress = null)
    {
        if (!OperatingSystem.IsWindows())
        {
            throw new PlatformNotSupportedException("Artifact compatibility requires Windows file-sharing protection.");
        }

        ArgumentException.ThrowIfNullOrWhiteSpace(receiptPath);
        ArgumentNullException.ThrowIfNull(requiredArtifacts);
        cancellationToken.ThrowIfCancellationRequested();
        var requirements = requiredArtifacts.ToImmutableArray();
        ValidateRequirements(configuration, requirements);
        NativeBuildReceipt receipt;
        try
        {
            var bytes = await File.ReadAllBytesAsync(receiptPath, cancellationToken).ConfigureAwait(false);
            using var document = JsonDocument.Parse(bytes);
            ValidateUniqueProperties(document.RootElement);
            receipt = document.RootElement.Deserialize<NativeBuildReceipt>(ReceiptOptions)
                ?? throw new InvalidDataException("The compatibility receipt is empty.");
            ValidateReceipt(receipt);
        }
        catch (Exception ex) when (ex is IOException or UnauthorizedAccessException or JsonException or InvalidDataException)
        {
            return new(Artifacts: null, [Failure(operationId, NativeCompatibilityDiagnosticCodes.ReceiptInvalid, "The Interop SDK build receipt is missing or invalid.", receiptPath, ex.ToString())]);
        }

        return await VerifyAsync(operationId, receipt, configuration, requirements, cancellationToken, progress).ConfigureAwait(false);
    }

    /// <summary>Verifies native files against the SDK receipt embedded by the Interop build.</summary>
    /// <param name="operationId">The diagnostic correlation identity.</param>
    /// <param name="receipt">The build's recorded SDK inputs.</param>
    /// <param name="configuration">The running configuration.</param>
    /// <param name="requiredArtifacts">The native files required by the operation.</param>
    /// <param name="cancellationToken">Cancels verification.</param>
    /// <param name="progress">Optional verification progress.</param>
    /// <returns>The protected native files or incompatibility diagnostics.</returns>
    public static Task<NativeCompatibilityResult> VerifyAsync(Guid operationId, NativeBuildReceipt receipt, string configuration, IReadOnlyList<NativeArtifactLocation> requiredArtifacts, CancellationToken cancellationToken, IProgress<NativeCompatibilityProgress>? progress = null)
    {
        ValidateRequirements(configuration, requiredArtifacts);
        ValidateReceipt(receipt);
        if (!string.Equals(receipt.Configuration, configuration, StringComparison.Ordinal))
        {
            var mismatch = $"Interop was built for {receipt.Configuration}; this build is {configuration}. Rebuild Interop in {configuration}.";
            return Task.FromResult(new NativeCompatibilityResult(Artifacts: null, [Failure(operationId, NativeCompatibilityDiagnosticCodes.ConfigurationMismatch, mismatch, string.Empty)]));
        }

        return requiredArtifacts.Select(static artifact => artifact.Id).ToHashSet(StringComparer.Ordinal).SetEquals(receipt.Artifacts.Select(static artifact => artifact.Id))
            ? VerifyFilesAsync(operationId, receipt, requiredArtifacts, progress, cancellationToken)
            : Task.FromResult(new NativeCompatibilityResult(Artifacts: null, [Failure(operationId, NativeCompatibilityDiagnosticCodes.ArtifactSetMismatch, "The installed engine SDK has changed. Rebuild Interop against this SDK.", string.Empty)]));
    }

    /// <summary>Rejects incomplete or malformed SDK metadata before resolving its artifact paths.</summary>
    /// <param name="receipt">The SDK build receipt.</param>
    internal static void ValidateReceipt(NativeBuildReceipt receipt)
    {
        var ids = new HashSet<string>(StringComparer.Ordinal);
        if (receipt.Version != 1 || receipt.Configuration is not ("Debug" or "Release")
            || receipt.Artifacts.IsDefaultOrEmpty
            || receipt.Artifacts.Any(artifact => artifact is null || !IsValidId(artifact.Id) || !ids.Add(artifact.Id)
                || artifact.Size < 0 || artifact.Sha256.Length != SHA256.HashSizeInBytes * 2 || !artifact.Sha256.All(Uri.IsHexDigit)
                || (artifact.SchemaId is not null && string.IsNullOrWhiteSpace(artifact.SchemaId))))
        {
            throw new InvalidDataException("The compatibility receipt contains invalid or duplicate artifact identities, hashes, schema identifiers or build metadata.");
        }
    }

    private static void ValidateRequirements(string configuration, IReadOnlyList<NativeArtifactLocation> requirements)
    {
        if (configuration is not ("Debug" or "Release"))
        {
            throw new ArgumentException("Compatibility requires an explicit Debug or Release configuration.", nameof(configuration));
        }

        var ids = new HashSet<string>(StringComparer.Ordinal);
        var paths = new HashSet<string>(OperatingSystem.IsWindows() ? StringComparer.OrdinalIgnoreCase : StringComparer.Ordinal);
        if (requirements.Count == 0 || requirements.Any(artifact => artifact is null
            || !IsValidId(artifact.Id) || !ids.Add(artifact.Id)
            || !Path.IsPathFullyQualified(artifact.FullPath) || !paths.Add(Path.GetFullPath(artifact.FullPath))))
        {
            throw new ArgumentException("Compatibility requires a nonempty inventory of unique identities and absolute file paths.", nameof(requirements));
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
                    throw new InvalidDataException($"The compatibility receipt repeats property '{property.Name}'.");
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
    private static async Task<NativeCompatibilityResult> VerifyFilesAsync(
        Guid operationId,
        NativeBuildReceipt receipt,
        IReadOnlyList<NativeArtifactLocation> requiredArtifacts,
        IProgress<NativeCompatibilityProgress>? progress,
        CancellationToken cancellationToken)
    {
        var expected = receipt.Artifacts.ToDictionary(static artifact => artifact.Id, StringComparer.Ordinal);
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

            var lease = new NativeArtifactLease(
                ComputeFingerprint(receipt),
                requiredArtifacts.ToImmutableDictionary(static artifact => artifact.Id, static artifact => Path.GetFullPath(artifact.FullPath), StringComparer.Ordinal),
                [.. streams]);
            var result = new NativeCompatibilityResult(lease, []);
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
        NativeArtifactLocation artifact,
        NativeArtifact expected,
        List<FileStream> streams,
        List<DiagnosticRecord> diagnostics,
        CancellationToken cancellationToken)
    {
        if (!string.Equals(artifact.SchemaId, expected.SchemaId, StringComparison.Ordinal))
        {
            diagnostics.Add(Failure(operationId, NativeCompatibilityDiagnosticCodes.ArtifactMismatch, $"The required schema identifier differs from the SDK used to build Interop; rebuild Interop: {artifact.Id}.", artifact.FullPath));
            return;
        }

        try
        {
            var stream = new FileStream(artifact.FullPath, FileMode.Open, FileAccess.Read, FileShare.Read, 65536, FileOptions.Asynchronous | FileOptions.SequentialScan);
            streams.Add(stream);
            if (stream.Length != expected.Size
                || !string.Equals(Convert.ToHexString(await SHA256.HashDataAsync(stream, cancellationToken).ConfigureAwait(false)), expected.Sha256, StringComparison.OrdinalIgnoreCase))
            {
                diagnostics.Add(Failure(operationId, NativeCompatibilityDiagnosticCodes.ArtifactMismatch, $"The installed file differs from the SDK used to build Interop; rebuild Interop: {artifact.Id}.", artifact.FullPath));
            }
        }
        catch (Exception ex) when (ex is IOException or UnauthorizedAccessException)
        {
            diagnostics.Add(Failure(operationId, NativeCompatibilityDiagnosticCodes.ArtifactMismatch, $"The compatible file is missing or inaccessible: {artifact.Id}.", artifact.FullPath, ex.ToString()));
        }
    }

    private static string ComputeFingerprint(NativeBuildReceipt receipt)
    {
        var bytes = JsonSerializer.SerializeToUtf8Bytes(new
        {
            receipt.Version,
            receipt.Configuration,
            Artifacts = receipt.Artifacts.OrderBy(static artifact => artifact.Id, StringComparer.Ordinal)
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
