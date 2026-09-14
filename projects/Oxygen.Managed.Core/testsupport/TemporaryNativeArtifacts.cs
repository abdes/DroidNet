// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Collections.Immutable;
using System.Reflection.PortableExecutable;
using System.Security.Cryptography;
using System.Text.Json;
using Oxygen.Managed.Core.Compatibility;

namespace Oxygen.Testing;

/// <summary>Captures real or synthetic native inputs for isolated tests.</summary>
internal sealed partial class TemporaryNativeArtifacts : INativeCompatibilityService, IDisposable
{
    private static readonly JsonSerializerOptions JsonOptions = new() { PropertyNamingPolicy = JsonNamingPolicy.CamelCase };
    private readonly string root = Path.Combine(Path.GetTempPath(), "OxygenArtifactFixtures", Guid.NewGuid().ToString("N"));
    private readonly ImmutableArray<NativeArtifactLocation> locations;
    private readonly string receiptPath;

    /// <summary>Initializes a new instance of the <see cref="TemporaryNativeArtifacts"/> class.</summary>
    /// <param name="locations">The real or synthetic files used by the test.</param>
    public TemporaryNativeArtifacts(IEnumerable<NativeArtifactLocation> locations)
    {
        this.locations = [.. locations];
        Directory.CreateDirectory(this.root);
        this.receiptPath = Path.Combine(this.root, "compatibility.json");
        var artifacts = this.locations.Select(static artifact =>
        {
            var bytes = File.ReadAllBytes(artifact.FullPath);
            return new NativeArtifact(artifact.Id, bytes.Length, Convert.ToHexString(SHA256.HashData(bytes)), artifact.SchemaId);
        }).ToImmutableArray();
        File.WriteAllText(this.receiptPath, JsonSerializer.Serialize(
            new NativeBuildReceipt(1, EditorNativeCompatibilityService.CurrentConfiguration, artifacts),
            JsonOptions));
    }

    /// <summary>Creates a private test receipt for the installed native binaries and current Interop copy.</summary>
    /// <param name="additional">Optional fixture-owned producer inputs.</param>
    /// <returns>The fixture-owned compatibility service.</returns>
    public static TemporaryNativeArtifacts ForInstalledEngine(IEnumerable<NativeArtifactLocation>? additional = null)
    {
        var installation = EditorNativeInstallation.Discover(AppContext.BaseDirectory, EditorNativeCompatibilityService.CurrentConfiguration);
        var bin = Path.Combine(installation.EngineRoot, "bin");
        if (!Directory.Exists(bin))
        {
            bin = AppContext.BaseDirectory;
        }

        var nativeFiles = Directory.EnumerateFiles(bin, "*.dll").Where(IsNativeBinary).Select(static path => new NativeArtifactLocation("engine/bin/" + Path.GetFileName(path), Path.GetFullPath(path))).ToList();
        var tool = Path.Combine(bin, "Oxygen.Cooker.ImportTool.exe");
        if (File.Exists(tool))
        {
            nativeFiles.Add(new(NativeArtifactInventory.ImportToolId, tool));
        }

        var interop = Path.Combine(AppContext.BaseDirectory, "DroidNet.Oxygen.Editor.Interop.dll");
        if (File.Exists(interop))
        {
            nativeFiles.Add(new(NativeArtifactInventory.InteropId, interop));
        }

        if (additional is not null)
        {
            nativeFiles.AddRange(additional);
        }

        return new(nativeFiles);
    }

    /// <inheritdoc />
    public Task<NativeCompatibilityResult> VerifyAsync(Guid operationId, CancellationToken cancellationToken)
        => NativeCompatibilityVerifier.VerifyAsync(operationId, this.receiptPath, EditorNativeCompatibilityService.CurrentConfiguration, this.locations, cancellationToken);

    /// <inheritdoc />
    public void Dispose() => Directory.Delete(this.root, recursive: true);

    private static bool IsNativeBinary(string path)
    {
        using var stream = File.OpenRead(path);
        using var image = new PEReader(stream);
        return !image.HasMetadata;
    }
}
