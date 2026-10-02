// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Buffers.Binary;
using System.Diagnostics.CodeAnalysis;
using System.Security.Cryptography;
using System.Text.Json;
using System.Text;
using AwesomeAssertions;
using Microsoft.Extensions.Logging.Abstractions;
using Oxygen.Editor.ContentPipeline.TestSupport;
using Oxygen.Managed.Assets.Persistence.LooseCooked.V3;
using Oxygen.Managed.Core.Diagnostics;
using static Oxygen.Editor.ContentPipeline.TestSupport.ImportAdapterScenario;

namespace Oxygen.Editor.ContentPipeline.Unit.Tests.Import;

[TestClass]
[System.Diagnostics.CodeAnalysis.SuppressMessage("Maintainability", "CA1515:Consider making public types internal", Justification = "MSTest discovers public test classes with the repository discovery configuration.")]
[System.Diagnostics.CodeAnalysis.SuppressMessage("Naming", "CA1707:Identifiers should not contain underscores", Justification = "Scenario-based MSTest method names separate the operation and expected behavior.")]
public sealed class ImportAdapterTests
{
    public TestContext TestContext { get; set; } = null!;
    /// <summary>Verifies the workflow can invoke Import Tool With Temporary Manifest Under Operation Directory.</summary>
    /// <returns>The asynchronous test operation.</returns>
    [TestMethod]
    public async Task ImportAsync_ShouldInvokeImportToolWithTemporaryManifestUnderOperationDirectory()
    {
        using var workspace = new ImportAdapterWorkspace();
        var manifest = CreateManifest(workspace);
        var runner = new CapturingRunner(new ContentPipelineProcessResult(0, string.Empty, string.Empty));
        var api = new ImportToolContentPipelineApi(
            new FixedToolLocator(Path.Combine(workspace.Root, "Oxygen.Cooker.ImportTool.exe")),
            runner,
            NullLogger<ImportToolContentPipelineApi>.Instance,
            workspace.Compatibility);

        var execution = CreateExecution(workspace, manifest);
        var result = await api.ImportAsync(execution, CancellationToken.None).ConfigureAwait(false);

        _ = result.Succeeded.Should().BeTrue();
        _ = runner.Request.Should().NotBeNull();
        _ = runner.Request!.WorkingDirectory.Should().Be(workspace.Root);
        _ = runner.Request.Arguments.Should().ContainInOrder(
            "--no-tui",
            "--no-color",
            "--cooked-root",
            manifest.Output,
            "batch",
            "--manifest");
        _ = runner.Request.Arguments.Should().ContainInOrder("--manifest", runner.ManifestPath!, "--root", workspace.Root);
        var manifestPath = runner.ManifestPath!;
        _ = manifestPath.Should().StartWith(Path.Combine(execution.OperationRoot, "manifests"));
        _ = runner.ManifestJson.Should().Contain("\"jobs\"");
        _ = runner.ManifestJson.Should().NotContain("\"output\":null");
        _ = File.Exists(manifestPath).Should().BeFalse("the fallback adapter owns and cleans up its temporary manifest");
    }

    /// <summary>Verifies the workflow can return Single Import Diagnostic.</summary>
    /// <returns>The asynchronous test operation.</returns>
    [TestMethod]
    public async Task ImportAsync_WhenToolFails_ShouldReturnSingleImportDiagnostic()
    {
        using var workspace = new ImportAdapterWorkspace();
        var runner = new CapturingRunner(new ContentPipelineProcessResult(1, "stdout", "stderr"));
        var api = new ImportToolContentPipelineApi(
            new FixedToolLocator(Path.Combine(workspace.Root, "Oxygen.Cooker.ImportTool.exe")),
            runner,
            NullLogger<ImportToolContentPipelineApi>.Instance,
            workspace.Compatibility);

        var execution = CreateExecution(workspace, CreateManifest(workspace));
        var result = await api.ImportAsync(execution, CancellationToken.None).ConfigureAwait(false);

        _ = result.Succeeded.Should().BeFalse();
        _ = result.Diagnostics.Should().ContainSingle();
        _ = result.Diagnostics[0].OperationId.Should().Be(execution.OperationId);
        _ = result.Diagnostics[0].Code.Should().Be(AssetImportDiagnosticCodes.ImportFailed);
        _ = result.Diagnostics[0].Domain.Should().Be(FailureDomain.AssetImport);
        _ = result.Diagnostics[0].TechnicalMessage.Should().Contain("stderr");
    }

    /// <summary>Verifies the workflow can read Loose Cooked Index.</summary>
    /// <param name="assetType">The native type, independent of the virtual filename.</param>
    /// <param name="expectedKind">The report kind.</param>
    /// <returns>The asynchronous test operation.</returns>
    [TestMethod]
    [DataRow((byte)1, ContentCookAssetKind.Material)]
    [DataRow((byte)2, ContentCookAssetKind.Geometry)]
    [DataRow((byte)3, ContentCookAssetKind.Scene)]
    [DataRow((byte)4, ContentCookAssetKind.Unknown)]
    [DataRow((byte)255, ContentCookAssetKind.Unknown)]
    public async Task InspectLooseCookedRootAsync_ShouldReadLooseCookedIndex(byte assetType, ContentCookAssetKind expectedKind)
    {
        using var workspace = new ImportAdapterWorkspace();
        var cookedRoot = Path.Combine(workspace.Root, ".cooked", "Content");
        WriteLooseCookedIndex(cookedRoot, assetType: assetType);
        var api = CreateApi(workspace);

        var result = await api.InspectLooseCookedRootAsync(cookedRoot, CancellationToken.None).ConfigureAwait(false);

        _ = result.Succeeded.Should().BeTrue(string.Join(Environment.NewLine, result.Diagnostics.Select(static diagnostic => diagnostic.TechnicalMessage ?? diagnostic.Message)));
        _ = result.Diagnostics.Should().BeEmpty();
        _ = result.CookedRoot.Should().Be(cookedRoot);
        _ = result.Assets.Should().ContainSingle(asset =>
            asset.VirtualPath == "/Content/Materials/Red.omat" && asset.Kind == expectedKind);
        _ = result.Files.Should().ContainSingle(file =>
            file.RelativePath == "materials.bin" && file.Size == 8);
    }

    /// <summary>Verifies the workflow can return Diagnostic.</summary>
    /// <returns>The asynchronous test operation.</returns>
    [TestMethod]
    public async Task InspectLooseCookedRootAsync_WhenDescriptorSizeMismatchesIndex_ShouldReturnDiagnostic()
    {
        using var workspace = new ImportAdapterWorkspace();
        var cookedRoot = Path.Combine(workspace.Root, ".cooked", "Content");
        WriteLooseCookedIndex(cookedRoot, descriptorSize: 128, actualDescriptorBytes: new byte[256], fileSize: 8, actualFileBytes: new byte[8]);
        var api = CreateApi(workspace);

        var result = await api.InspectLooseCookedRootAsync(cookedRoot, CancellationToken.None).ConfigureAwait(false);

        _ = result.Succeeded.Should().BeFalse();
        _ = result.Diagnostics.Should().ContainSingle();
        _ = result.Diagnostics[0].Code.Should().Be(ContentPipelineDiagnosticCodes.ValidateFailed);
        _ = result.Diagnostics[0].AffectedPath.Should().Be(Path.Combine(cookedRoot, "Content/Materials/Red.omat"));
        _ = result.Diagnostics[0].Message.Should().Contain("size_mismatch");
    }

    /// <summary>Verifies the workflow can return Diagnostic.</summary>
    /// <returns>The asynchronous test operation.</returns>
    [TestMethod]
    public async Task ValidateLooseCookedRootAsync_WhenDescriptorSizeMismatchesIndex_ShouldReturnDiagnostic()
    {
        using var workspace = new ImportAdapterWorkspace();
        var cookedRoot = Path.Combine(workspace.Root, ".cooked", "Content");
        WriteLooseCookedIndex(cookedRoot, descriptorSize: 128, actualDescriptorBytes: new byte[256], fileSize: 8, actualFileBytes: new byte[8]);
        var api = CreateApi(workspace);

        var result = await api.ValidateLooseCookedRootAsync(cookedRoot, CancellationToken.None).ConfigureAwait(false);

        _ = result.Succeeded.Should().BeFalse();
        _ = result.Diagnostics.Should().ContainSingle();
        _ = result.Diagnostics[0].Code.Should().Be(ContentPipelineDiagnosticCodes.ValidateFailed);
        _ = result.Diagnostics[0].AffectedPath.Should().Be(Path.Combine(cookedRoot, "Content/Materials/Red.omat"));
        _ = result.Diagnostics[0].Message.Should().Contain("size_mismatch");
    }

    /// <summary>Verifies the workflow can return Synthesized Diagnostic.</summary>
    /// <returns>The asynchronous test operation.</returns>
    [TestMethod]
    public async Task InspectLooseCookedRootAsync_WhenIndexMissing_ShouldReturnSynthesizedDiagnostic()
    {
        using var workspace = new ImportAdapterWorkspace();
        var cookedRoot = Path.Combine(workspace.Root, ".cooked", "Content");
        Directory.CreateDirectory(cookedRoot);
        var api = CreateApi(workspace);

        var result = await api.InspectLooseCookedRootAsync(cookedRoot, CancellationToken.None).ConfigureAwait(false);

        _ = result.Succeeded.Should().BeFalse();
        _ = result.Diagnostics.Should().ContainSingle();
        _ = result.Diagnostics[0].Code.Should().Be(ContentPipelineDiagnosticCodes.InspectFailed);
        _ = result.Diagnostics[0].AffectedPath.Should().Be(cookedRoot);
    }

    /// <summary>Verifies the workflow can return Synthesized Diagnostic.</summary>
    /// <returns>The asynchronous test operation.</returns>
    [TestMethod]
    public async Task ValidateLooseCookedRootAsync_WhenIndexMissing_ShouldReturnSynthesizedDiagnostic()
    {
        using var workspace = new ImportAdapterWorkspace();
        var cookedRoot = Path.Combine(workspace.Root, ".cooked", "Content");
        Directory.CreateDirectory(cookedRoot);
        var api = CreateApi(workspace);

        var result = await api.ValidateLooseCookedRootAsync(cookedRoot, CancellationToken.None).ConfigureAwait(false);

        _ = result.Succeeded.Should().BeFalse();
        _ = result.Diagnostics.Should().ContainSingle();
        _ = result.Diagnostics[0].Code.Should().Be(ContentPipelineDiagnosticCodes.ValidateFailed);
        _ = result.Diagnostics[0].AffectedPath.Should().Be(cookedRoot);
    }

    /// <summary>Verifies the workflow can return Synthesized Diagnostic.</summary>
    /// <returns>The asynchronous test operation.</returns>
    [TestMethod]
    public async Task InspectLooseCookedRootAsync_WhenIndexVersionIsUnsupported_ShouldReturnSynthesizedDiagnostic()
    {
        using var workspace = new ImportAdapterWorkspace();
        var cookedRoot = Path.Combine(workspace.Root, ".cooked", "Content");
        WriteUnsupportedLooseCookedIndex(cookedRoot);
        var api = CreateApi(workspace);

        var result = await api.InspectLooseCookedRootAsync(cookedRoot, CancellationToken.None).ConfigureAwait(false);

        _ = result.Succeeded.Should().BeFalse();
        _ = result.Diagnostics.Should().ContainSingle();
        _ = result.Diagnostics[0].Code.Should().Be(ContentPipelineDiagnosticCodes.InspectFailed);
        _ = result.Diagnostics[0].TechnicalMessage.Should().Contain("version");
    }

    /// <summary>Verifies the workflow can return Synthesized Diagnostic.</summary>
    /// <returns>The asynchronous test operation.</returns>
    [TestMethod]
    public async Task ValidateLooseCookedRootAsync_WhenIndexVersionIsUnsupported_ShouldReturnSynthesizedDiagnostic()
    {
        using var workspace = new ImportAdapterWorkspace();
        var cookedRoot = Path.Combine(workspace.Root, ".cooked", "Content");
        WriteUnsupportedLooseCookedIndex(cookedRoot);
        var api = CreateApi(workspace);

        var result = await api.ValidateLooseCookedRootAsync(cookedRoot, CancellationToken.None).ConfigureAwait(false);

        _ = result.Succeeded.Should().BeFalse();
        _ = result.Diagnostics.Should().ContainSingle();
        _ = result.Diagnostics[0].Code.Should().Be(ContentPipelineDiagnosticCodes.ValidateFailed);
        _ = result.Diagnostics[0].TechnicalMessage.Should().Contain("version");
    }

    private static ImportToolContentPipelineApi CreateApi(ImportAdapterWorkspace workspace)
        => new(
            new FixedToolLocator(Path.Combine(workspace.Root, "Oxygen.Cooker.ImportTool.exe")),
            new InventoryRunner(),
            NullLogger<ImportToolContentPipelineApi>.Instance,
            workspace.Compatibility);

    private static void WriteLooseCookedIndex(
        string cookedRoot,
        ulong descriptorSize = 128,
        byte[]? actualDescriptorBytes = null,
        ulong fileSize = 8,
        byte[]? actualFileBytes = null,
        byte assetType = 1)
    {
        Directory.CreateDirectory(cookedRoot);
        Directory.CreateDirectory(Path.Combine(cookedRoot, "Content", "Materials"));
        File.WriteAllBytes(
            Path.Combine(cookedRoot, "Content", "Materials", "Red.omat"),
            actualDescriptorBytes ?? new byte[(int)descriptorSize]);
        File.WriteAllBytes(
            Path.Combine(cookedRoot, "materials.bin"),
            actualFileBytes ?? new byte[(int)fileSize]);
        using var stream = File.Create(Path.Combine(cookedRoot, "container.index.bin"));
        Oxygen.Testing.LooseCookedIndexFixture.Write(
            stream,
            new Document(
                ContentVersion: 1,
                Flags: IndexFeatures.HasVirtualPaths,
                SourceGuid: Guid.CreateVersion7(),
                Assets:
                [
                    new AssetEntry(
                        new AssetKey(1, 2),
                        "Content/Materials/Red.omat",
                        "/Content/Materials/Red.omat",
                        AssetType: assetType,
                        DescriptorSize: descriptorSize,
                        DescriptorSha256: SHA256.HashData(actualDescriptorBytes ?? new byte[(int)descriptorSize])),
                ],
                Files:
                [
                    new FileRecord(FileKind.Auxiliary, "materials.bin", fileSize, Sha256: SHA256.HashData(actualFileBytes ?? new byte[(int)fileSize])),
                ]));
    }

    private static void WriteUnsupportedLooseCookedIndex(string cookedRoot)
    {
        Directory.CreateDirectory(cookedRoot);
        var header = new byte[LooseCookedIndex.HeaderSize];
        Encoding.ASCII.GetBytes("OXLCIDX\0").CopyTo(header, 0);
        BinaryPrimitives.WriteUInt16LittleEndian(header.AsSpan(8, 2), ushort.MaxValue);
        File.WriteAllBytes(Path.Combine(cookedRoot, "container.index.bin"), header);
    }

    private sealed class InventoryRunner : IContentPipelineProcessRunner
    {
        public async Task<ContentPipelineProcessResult> RunAsync(ContentPipelineProcessRequest request, CancellationToken cancellationToken)
        {
            _ = request.Arguments[0].Should().Be("inventory");
            try
            {
                var json = Oxygen.Testing.NativeInventoryFixture.ReadJson(request.Arguments[1]);
                var output = request.Arguments[request.Arguments.ToList().IndexOf("--output") + 1];
                await File.WriteAllTextAsync(output, json, cancellationToken).ConfigureAwait(false);
                return new(0, string.Empty, string.Empty);
            }
            catch (Exception error) when (error is IOException or InvalidDataException or NotSupportedException)
            {
                return new(1, string.Empty, error.Message);
            }
        }
    }
}
