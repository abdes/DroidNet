// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Collections.Immutable;
using System.Reflection.PortableExecutable;
using System.Security.Cryptography;
using System.Text.Json;
using Oxygen.Managed.Core.Compatibility;

namespace Oxygen.Testing;

/// <summary>Creates private test manifests; never changes an application's accepted qualification.</summary>
internal sealed partial class TemporaryArtifactQualification : IArtifactQualificationService, IDisposable
{
    private static readonly JsonSerializerOptions JsonOptions = new() { PropertyNamingPolicy = JsonNamingPolicy.CamelCase };
    private readonly string root = Path.Combine(Path.GetTempPath(), "OxygenArtifactFixtures", Guid.NewGuid().ToString("N"));
    private readonly ImmutableArray<QualificationArtifactLocation> locations;
    private readonly string manifestPath;

    /// <summary>Initializes a new instance of the <see cref="TemporaryArtifactQualification"/> class.</summary>
    /// <param name="locations">The real or synthetic files used by the test.</param>
    public TemporaryArtifactQualification(IEnumerable<QualificationArtifactLocation> locations)
    {
        this.locations = [.. locations];
        Directory.CreateDirectory(this.root);
        this.manifestPath = Path.Combine(this.root, "qualification.json");
        var artifacts = this.locations.Select(static artifact =>
        {
            var bytes = File.ReadAllBytes(artifact.FullPath);
            return new QualifiedArtifact(artifact.Id, bytes.Length, Convert.ToHexString(SHA256.HashData(bytes)), artifact.SchemaId);
        }).ToImmutableArray();
        File.WriteAllText(this.manifestPath, JsonSerializer.Serialize(
            new ArtifactQualificationManifest(1, EditorArtifactQualificationService.CurrentConfiguration, new string('a', 40), artifacts),
            JsonOptions));
    }

    /// <summary>Creates a private test manifest for the installed native binaries and current Interop copy.</summary>
    /// <returns>The fixture-owned qualification service.</returns>
    public static TemporaryArtifactQualification ForInstalledEngine()
    {
        var installation = EditorArtifactInstallation.Discover(AppContext.BaseDirectory, EditorArtifactQualificationService.CurrentConfiguration);
        var bin = Path.Combine(installation.EngineRoot, "bin");
        if (!Directory.Exists(bin))
        {
            bin = AppContext.BaseDirectory;
        }

        var nativeFiles = Directory.EnumerateFiles(bin, "*.dll").Where(IsNativeBinary).Select(static path => new QualificationArtifactLocation("engine/bin/" + Path.GetFileName(path), Path.GetFullPath(path))).ToList();
        var tool = Path.Combine(bin, "Oxygen.Cooker.ImportTool.exe");
        if (File.Exists(tool))
        {
            nativeFiles.Add(new(EditorArtifactInventory.ImportToolId, tool));
        }

        var interop = Path.Combine(AppContext.BaseDirectory, "DroidNet.Oxygen.Editor.Interop.dll");
        if (File.Exists(interop))
        {
            nativeFiles.Add(new(EditorArtifactInventory.InteropId, interop));
        }

        return new(nativeFiles);
    }

    /// <inheritdoc />
    public Task<ArtifactQualificationResult> VerifyAsync(Guid operationId, CancellationToken cancellationToken)
        => ArtifactQualificationVerifier.VerifyAsync(operationId, this.manifestPath, EditorArtifactQualificationService.CurrentConfiguration, this.locations, cancellationToken);

    /// <inheritdoc />
    public void Dispose() => Directory.Delete(this.root, recursive: true);

    private static bool IsNativeBinary(string path)
    {
        using var stream = File.OpenRead(path);
        using var image = new PEReader(stream);
        return !image.HasMetadata;
    }
}
