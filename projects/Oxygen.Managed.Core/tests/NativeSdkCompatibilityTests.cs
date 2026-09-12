// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using AwesomeAssertions;
using Oxygen.Managed.Core.Compatibility;

namespace Oxygen.Managed.Core.Tests;

/// <summary>Exercises embedded SDK receipts and cooking preflight using the existing test assembly.</summary>
[TestClass]
[System.Diagnostics.CodeAnalysis.SuppressMessage("Maintainability", "CA1515:Consider making public types internal", Justification = "MSTest discovers public test classes with the repository discovery configuration.")]
public sealed class NativeSdkCompatibilityTests
{
    /// <summary>Gets or sets test cancellation.</summary>
    public TestContext TestContext { get; set; } = null!;

    /// <summary>Ordinary managed DLL changes and missing cooker tools do not block native startup.</summary>
    /// <returns>The asynchronous verification test.</returns>
    [TestMethod]
    public async Task ManagedChangesDoNotInvalidateTheInteropSdk()
    {
        using var fixture = new Fixture();
        await fixture.VerifyRuntimeAsync(this.TestContext.CancellationToken).ConfigureAwait(false);
        await File.WriteAllTextAsync(Path.Combine(fixture.Installation.EditorRoot, "Oxygen.Editor.dll"), "new UI build", this.TestContext.CancellationToken).ConfigureAwait(false);
        await fixture.VerifyRuntimeAsync(this.TestContext.CancellationToken).ConfigureAwait(false);
        _ = Directory.Exists(Path.Combine(fixture.Installation.EditorRoot, "qualification")).Should().BeFalse();
    }

    /// <summary>An updated SDK requires rebuilding only its native consumer.</summary>
    /// <returns>The asynchronous verification test.</returns>
    [TestMethod]
    public async Task ChangedNativeSdkIsRejectedBeforeLoadingInterop()
    {
        using var fixture = new Fixture();
        var path = Path.Combine(fixture.Installation.EngineRoot, "bin", Path.GetFileName(NativeArtifactInventory.RuntimeId(EditorNativeCompatibilityService.CurrentConfiguration)));
        await File.WriteAllTextAsync(path, "WXYZ", this.TestContext.CancellationToken).ConfigureAwait(false);
        var result = await new EditorNativeCompatibilityService(fixture.Installation).VerifyAsync(Guid.NewGuid(), this.TestContext.CancellationToken).ConfigureAwait(false);
        _ = result.Succeeded.Should().BeFalse();
        _ = result.Diagnostics.Should().Contain(value => value.Code == NativeCompatibilityDiagnosticCodes.ArtifactMismatch && value.AffectedPath == path);
    }

    /// <summary>Cooking needs no Interop or accepted manifest, and producer changes alter its content identity.</summary>
    /// <returns>The asynchronous producer test.</returns>
    [TestMethod]
    public async Task CookingCapturesCurrentProducerWithoutQualification()
    {
        using var fixture = new Fixture();
        fixture.CreateCookingInputs();
        File.Delete(fixture.Installation.InteropPath);
        var service = new EditorNativeCompatibilityService(fixture.Installation, cooking: true);
        var first = await service.VerifyAsync(Guid.NewGuid(), this.TestContext.CancellationToken).ConfigureAwait(false);
        _ = first.Succeeded.Should().BeTrue();
        var fingerprint = first.Artifacts!.Fingerprint;
        await first.Artifacts.DisposeAsync().ConfigureAwait(false);
        await File.WriteAllTextAsync(Path.Combine(fixture.Installation.EditorRoot, "Oxygen.Editor.ContentPipeline.dll"), "updated producer", this.TestContext.CancellationToken).ConfigureAwait(false);
        var second = await service.VerifyAsync(Guid.NewGuid(), this.TestContext.CancellationToken).ConfigureAwait(false);
        _ = second.Succeeded.Should().BeTrue();
        var artifacts = second.Artifacts!;
        await using var lease = artifacts.ConfigureAwait(false);
        _ = artifacts.Fingerprint.Should().NotBe(fingerprint);
    }

    /// <summary>Schema disagreement blocks cooking while runtime startup remains available.</summary>
    /// <returns>The asynchronous operation-scoping test.</returns>
    [TestMethod]
    public async Task CookingSchemaMismatchDoesNotBlockRuntime()
    {
        using var fixture = new Fixture();
        fixture.CreateCookingInputs();
        await File.WriteAllTextAsync(Path.Combine(fixture.Installation.EditorRoot, "Schemas", "oxygen.scene-descriptor.schema.json"), """{"$id":"different"}""", this.TestContext.CancellationToken).ConfigureAwait(false);
        var result = await new EditorNativeCompatibilityService(fixture.Installation, cooking: true).VerifyAsync(Guid.NewGuid(), this.TestContext.CancellationToken).ConfigureAwait(false);
        _ = result.Succeeded.Should().BeFalse();
        _ = result.Diagnostics.Should().ContainSingle(value => value.Message.Contains("schemas differ", StringComparison.Ordinal));
        await fixture.VerifyRuntimeAsync(this.TestContext.CancellationToken).ConfigureAwait(false);
    }

    private sealed class Fixture : IDisposable
    {
        private readonly DirectoryInfo root = Directory.CreateTempSubdirectory("OxygenSdk-");

        public Fixture()
        {
            var editor = Directory.CreateDirectory(Path.Combine(this.root.FullName, "Editor"));
            var sdk = Directory.CreateDirectory(Path.Combine(this.root.FullName, "SDK"));
            this.Installation = new(editor.FullName, sdk.FullName, EditorNativeCompatibilityService.CurrentConfiguration);
            File.Copy(typeof(NativeSdkCompatibilityTests).Assembly.Location, this.Installation.InteropPath);
            using var image = File.OpenRead(this.Installation.InteropPath);
            foreach (var artifact in NativeSdkMetadata.Read(image).Artifacts)
            {
                var path = Path.Combine(sdk.FullName, artifact.Id["engine/".Length..]);
                Directory.CreateDirectory(Path.GetDirectoryName(path)!);
                File.WriteAllText(path, "ABCD");
            }
        }

        public EditorNativeInstallation Installation { get; }

        public async Task VerifyRuntimeAsync(CancellationToken cancellationToken)
        {
            var result = await new EditorNativeCompatibilityService(this.Installation).VerifyAsync(Guid.NewGuid(), cancellationToken).ConfigureAwait(false);
            _ = result.Succeeded.Should().BeTrue(string.Join("; ", result.Diagnostics.Select(static value => value.TechnicalMessage ?? value.Message)));
            await result.Artifacts!.DisposeAsync().ConfigureAwait(false);
        }

        public void CreateCookingInputs()
        {
            foreach (var location in NativeArtifactInventory.CreateCooking(this.Installation))
            {
                if (File.Exists(location.FullPath))
                {
                    continue;
                }

                Directory.CreateDirectory(Path.GetDirectoryName(location.FullPath)!);
                File.WriteAllText(location.FullPath, location.Id.Contains("/schemas/", StringComparison.OrdinalIgnoreCase) ? """{"$id":"oxygen.test.v1"}""" : "producer");
            }
        }

        public void Dispose() => this.root.Delete(recursive: true);
    }
}
