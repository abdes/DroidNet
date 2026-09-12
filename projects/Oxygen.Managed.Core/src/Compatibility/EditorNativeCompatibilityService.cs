// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Runtime.Versioning;
using System.Security.Cryptography;
using System.Text.Json;
using Oxygen.Managed.Core.Diagnostics;

namespace Oxygen.Managed.Core.Compatibility;

/// <summary>Checks build compatibility using existing files, without building or running tests.</summary>
/// <param name="installation">The editor and installed SDK paths.</param>
/// <param name="cooking">Whether to check cooking inputs instead of the runtime bridge.</param>
[SupportedOSPlatform("windows")]
public sealed class EditorNativeCompatibilityService(EditorNativeInstallation installation, bool cooking = false) : INativeCompatibilityService
{
    /// <summary>The executing build configuration.</summary>
    public const string CurrentConfiguration =
#if DEBUG
        "Debug";
#else
        "Release";
#endif

    /// <summary>Creates the runtime startup check.</summary>
    /// <returns>The runtime compatibility service.</returns>
    public static EditorNativeCompatibilityService ForCurrentProcess()
        => new(EditorNativeInstallation.Discover(AppContext.BaseDirectory, CurrentConfiguration));

    /// <summary>Creates the cooking check, independent of Interop or editor startup.</summary>
    /// <returns>The cooking compatibility service.</returns>
    public static EditorNativeCompatibilityService ForCooking()
        => new(EditorNativeInstallation.Discover(AppContext.BaseDirectory, CurrentConfiguration), cooking: true);

    /// <inheritdoc />
    public async Task<NativeCompatibilityResult> VerifyAsync(Guid operationId, CancellationToken cancellationToken)
    {
        try
        {
            if (cooking)
            {
                return await NativeCompatibilityVerifier.CaptureCookingAsync(operationId, installation.Configuration, NativeArtifactInventory.CreateCooking(installation), cancellationToken).ConfigureAwait(false);
            }

            var interop = new FileStream(installation.InteropPath, FileMode.Open, FileAccess.Read, FileShare.Read, 65536, FileOptions.Asynchronous | FileOptions.SequentialScan);
            await using var interopLifetime = interop.ConfigureAwait(false);
            var receipt = NativeSdkMetadata.Read(interop);
            NativeCompatibilityVerifier.ValidateReceipt(receipt);
            interop.Position = 0;
            var hash = Convert.ToHexString(await SHA256.HashDataAsync(interop, cancellationToken).ConfigureAwait(false));
            receipt = receipt with { Artifacts = receipt.Artifacts.Add(new(NativeArtifactInventory.InteropId, interop.Length, hash)) };
            return await NativeCompatibilityVerifier.VerifyAsync(operationId, receipt, installation.Configuration, NativeArtifactInventory.CreateRuntime(installation), cancellationToken).ConfigureAwait(false);
        }
        catch (Exception ex) when (ex is IOException or UnauthorizedAccessException or JsonException or InvalidDataException or BadImageFormatException)
        {
            return new(
                Artifacts: null,
                [
                    new DiagnosticRecord
                    {
                        OperationId = operationId,
                        Domain = FailureDomain.RuntimeDiscovery,
                        Severity = DiagnosticSeverity.Error,
                        Code = NativeCompatibilityDiagnosticCodes.ArtifactMismatch,
                        Message = cooking ? "The cooker or its schemas are missing or invalid. Check the installed engine SDK." : "Interop does not match the installed engine SDK. Rebuild Interop.",
                        TechnicalMessage = ex.ToString(),
                        AffectedPath = cooking ? installation.EngineRoot : installation.InteropPath,
                    },
                ]);
        }
    }
}
