// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Runtime.Versioning;
using System.Text.Json;
using Oxygen.Managed.Core.Diagnostics;

namespace Oxygen.Managed.Core.Compatibility;

/// <summary>Verifies the installed editor and engine against an explicitly promoted fixed manifest.</summary>
/// <param name="installation">The application and installed SDK paths.</param>
[SupportedOSPlatform("windows")]
public sealed class EditorArtifactQualificationService(EditorArtifactInstallation installation) : IArtifactQualificationService
{
    /// <summary>Gets the running build configuration.</summary>
    public const string CurrentConfiguration =
#if DEBUG
        "Debug";
#else
        "Release";
#endif

    /// <summary>Creates verification for the current application and SDK installation.</summary>
    /// <returns>The process-local qualification service.</returns>
    public static EditorArtifactQualificationService ForCurrentProcess()
        => new(EditorArtifactInstallation.Discover(AppContext.BaseDirectory, CurrentConfiguration));

    /// <inheritdoc />
    public async Task<ArtifactQualificationResult> VerifyAsync(Guid operationId, CancellationToken cancellationToken)
    {
        try
        {
            var inventory = EditorArtifactInventory.Create(installation);
            return await ArtifactQualificationVerifier.VerifyAsync(operationId, installation.ManifestPath, installation.Configuration, inventory, cancellationToken).ConfigureAwait(false);
        }
        catch (Exception ex) when (ex is IOException or UnauthorizedAccessException or JsonException)
        {
            return new(
                Artifacts: null,
                [
                    new DiagnosticRecord
                    {
                        OperationId = operationId,
                        Domain = FailureDomain.RuntimeDiscovery,
                        Severity = DiagnosticSeverity.Error,
                        Code = ArtifactQualificationDiagnosticCodes.ArtifactMismatch,
                        Message = "The installed artifact inventory could not be read.",
                        TechnicalMessage = ex.ToString(),
                        AffectedPath = installation.ManifestPath,
                    },
                ]);
        }
    }
}
