// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Buffers.Binary;
using System.Text;
using System.Text.Json;
using AwesomeAssertions;
using Microsoft.Extensions.Logging.Abstractions;
using Oxygen.Assets.Persistence.LooseCooked.V1;
using Oxygen.Core.Diagnostics;

namespace Oxygen.Editor.ContentPipeline.Tests;

[TestClass]
public sealed class ImportToolContentPipelineApiTests
{
    [TestMethod]
    public async Task ImportAsync_ShouldInvokeImportToolWithTemporaryManifestUnderProjectPipelineFolder()
    {
        using var workspace = new TempWorkspace();
        var manifest = CreateManifest(workspace);
        var runner = new CapturingRunner(new ContentPipelineProcessResult(0, string.Empty, string.Empty));
        var api = new ImportToolContentPipelineApi(
            new FixedToolLocator(Path.Combine(workspace.Root, "Oxygen.Cooker.ImportTool.exe")),
            runner,
            NullLogger<ImportToolContentPipelineApi>.Instance);

        var result = await api.ImportAsync(manifest, CancellationToken.None).ConfigureAwait(false);

        _ = result.Succeeded.Should().BeTrue();
        _ = runner.Request.Should().NotBeNull();
        _ = runner.Request!.WorkingDirectory.Should().Be(workspace.Root);
        _ = runner.Request.Arguments.Should().ContainInOrder(
            "--no-tui",
            "--no-color",
            "--quiet",
            "--cooked-root",
            manifest.Output,
            "batch",
            "--manifest");
        _ = runner.Request.Arguments.Should().ContainInOrder("--manifest", runner.ManifestPath!, "--root", workspace.Root);
        var manifestPath = runner.ManifestPath!;
        _ = manifestPath.Should().StartWith(Path.Combine(workspace.Root, ".pipeline", "Manifests"));
        _ = runner.ManifestJson.Should().Contain("\"jobs\"");
        _ = runner.ManifestJson.Should().NotContain("\"output\":null");
        _ = File.Exists(manifestPath).Should().BeFalse("the fallback adapter owns and cleans up its temporary manifest");
    }

    [TestMethod]
    public async Task ImportAsync_WhenToolFails_ShouldReturnSingleImportDiagnostic()
    {
        using var workspace = new TempWorkspace();
        var runner = new CapturingRunner(new ContentPipelineProcessResult(1, "stdout", "stderr"));
        var api = new ImportToolContentPipelineApi(
            new FixedToolLocator(Path.Combine(workspace.Root, "Oxygen.Cooker.ImportTool.exe")),
            runner,
            NullLogger<ImportToolContentPipelineApi>.Instance);

        var result = await api.ImportAsync(CreateManifest(workspace), CancellationToken.None).ConfigureAwait(false);

        _ = result.Succeeded.Should().BeFalse();
        _ = result.Diagnostics.Should().ContainSingle();
        _ = result.Diagnostics[0].Code.Should().Be(AssetImportDiagnosticCodes.ImportFailed);
        _ = result.Diagnostics[0].Domain.Should().Be(FailureDomain.AssetImport);
        _ = result.Diagnostics[0].TechnicalMessage.Should().Contain("stderr");
    }

    [TestMethod]
    public async Task InspectLooseCookedRootAsync_ShouldReadLooseCookedIndex()
    {
        using var workspace = new TempWorkspace();
        var cookedRoot = Path.Combine(workspace.Root, ".cooked", "Content");
        WriteLooseCookedIndex(cookedRoot);
        var api = CreateApi(workspace);

        var result = await api.InspectLooseCookedRootAsync(cookedRoot, CancellationToken.None).ConfigureAwait(false);

        _ = result.Succeeded.Should().BeTrue();
        _ = result.Diagnostics.Should().BeEmpty();
        _ = result.CookedRoot.Should().Be(cookedRoot);
        _ = result.Assets.Should().ContainSingle(asset =>
            asset.VirtualPath == "/Content/Materials/Red.omat" && asset.Kind == ContentCookAssetKind.Material);
        _ = result.Files.Should().ContainSingle(file =>
            file.RelativePath == "materials.bin" && file.Size == 8);
    }

    [TestMethod]
    public async Task InspectLooseCookedRootAsync_WhenIndexMissing_ShouldReturnSynthesizedDiagnostic()
    {
        using var workspace = new TempWorkspace();
        var cookedRoot = Path.Combine(workspace.Root, ".cooked", "Content");
        Directory.CreateDirectory(cookedRoot);
        var api = CreateApi(workspace);

        var result = await api.InspectLooseCookedRootAsync(cookedRoot, CancellationToken.None).ConfigureAwait(false);

        _ = result.Succeeded.Should().BeFalse();
        _ = result.Diagnostics.Should().ContainSingle();
        _ = result.Diagnostics[0].Code.Should().Be(ContentPipelineDiagnosticCodes.InspectFailed);
        _ = result.Diagnostics[0].AffectedPath.Should().Be(cookedRoot);
    }

    [TestMethod]
    public async Task ValidateLooseCookedRootAsync_WhenIndexMissing_ShouldReturnSynthesizedDiagnostic()
    {
        using var workspace = new TempWorkspace();
        var cookedRoot = Path.Combine(workspace.Root, ".cooked", "Content");
        Directory.CreateDirectory(cookedRoot);
        var api = CreateApi(workspace);

        var result = await api.ValidateLooseCookedRootAsync(cookedRoot, CancellationToken.None).ConfigureAwait(false);

        _ = result.Succeeded.Should().BeFalse();
        _ = result.Diagnostics.Should().ContainSingle();
        _ = result.Diagnostics[0].Code.Should().Be(ContentPipelineDiagnosticCodes.ValidateFailed);
        _ = result.Diagnostics[0].AffectedPath.Should().Be(cookedRoot);
    }

    [TestMethod]
    public async Task InspectLooseCookedRootAsync_WhenIndexVersionIsUnsupported_ShouldReturnSynthesizedDiagnostic()
    {
        using var workspace = new TempWorkspace();
        var cookedRoot = Path.Combine(workspace.Root, ".cooked", "Content");
        WriteUnsupportedLooseCookedIndex(cookedRoot);
        var api = CreateApi(workspace);

        var result = await api.InspectLooseCookedRootAsync(cookedRoot, CancellationToken.None).ConfigureAwait(false);

        _ = result.Succeeded.Should().BeFalse();
        _ = result.Diagnostics.Should().ContainSingle();
        _ = result.Diagnostics[0].Code.Should().Be(ContentPipelineDiagnosticCodes.InspectFailed);
        _ = result.Diagnostics[0].ExceptionType.Should().Be(typeof(NotSupportedException).FullName);
    }

    [TestMethod]
    public async Task ValidateLooseCookedRootAsync_WhenIndexVersionIsUnsupported_ShouldReturnSynthesizedDiagnostic()
    {
        using var workspace = new TempWorkspace();
        var cookedRoot = Path.Combine(workspace.Root, ".cooked", "Content");
        WriteUnsupportedLooseCookedIndex(cookedRoot);
        var api = CreateApi(workspace);

        var result = await api.ValidateLooseCookedRootAsync(cookedRoot, CancellationToken.None).ConfigureAwait(false);

        _ = result.Succeeded.Should().BeFalse();
        _ = result.Diagnostics.Should().ContainSingle();
        _ = result.Diagnostics[0].Code.Should().Be(ContentPipelineDiagnosticCodes.ValidateFailed);
        _ = result.Diagnostics[0].ExceptionType.Should().Be(typeof(NotSupportedException).FullName);
    }

    private static ImportToolContentPipelineApi CreateApi(TempWorkspace workspace)
        => new(
            new FixedToolLocator(Path.Combine(workspace.Root, "Oxygen.Cooker.ImportTool.exe")),
            new CapturingRunner(new ContentPipelineProcessResult(0, string.Empty, string.Empty)),
            NullLogger<ImportToolContentPipelineApi>.Instance);

    private static ContentImportManifest CreateManifest(TempWorkspace workspace)
        => new(
            Version: 1,
            Output: Path.Combine(workspace.Root, ".cooked", "Content"),
            Layout: new ContentImportLayout("/Content"),
            Jobs:
            [
                new ContentImportJob(
                    Id: "material-red",
                    Type: "material-descriptor",
                    Source: "Content/Materials/Red.omat.json",
                    DependsOn: [],
                    Output: null,
                    Name: "Red"),
            ]);

    private static void WriteLooseCookedIndex(string cookedRoot)
    {
        Directory.CreateDirectory(cookedRoot);
        using var stream = File.Create(Path.Combine(cookedRoot, "container.index.bin"));
        LooseCookedIndex.Write(
            stream,
            new Document(
                ContentVersion: 1,
                Flags: IndexFeatures.HasVirtualPaths,
                SourceGuid: Guid.NewGuid(),
                Assets:
                [
                    new AssetEntry(
                        new AssetKey(1, 2),
                        "Content/Materials/Red.omat",
                        "/Content/Materials/Red.omat",
                        AssetType: 1,
                        DescriptorSize: 128,
                        DescriptorSha256: new byte[32]),
                ],
                Files:
                [
                    new FileRecord(FileKind.BuffersData, "materials.bin", Size: 8, Sha256: new byte[32]),
                ]));
    }

    private static void WriteUnsupportedLooseCookedIndex(string cookedRoot)
    {
        Directory.CreateDirectory(cookedRoot);
        var header = new byte[LooseCookedIndex.HeaderSize];
        Encoding.ASCII.GetBytes("OXLCIDX\0").CopyTo(header, 0);
        BinaryPrimitives.WriteUInt16LittleEndian(header.AsSpan(8, 2), 2);
        File.WriteAllBytes(Path.Combine(cookedRoot, "container.index.bin"), header);
    }

    private sealed class CapturingRunner(ContentPipelineProcessResult result) : IContentPipelineProcessRunner
    {
        public ContentPipelineProcessRequest? Request { get; private set; }

        public string? ManifestJson { get; private set; }

        public string? ManifestPath { get; private set; }

        public async Task<ContentPipelineProcessResult> RunAsync(
            ContentPipelineProcessRequest request,
            CancellationToken cancellationToken)
        {
            this.Request = request;
            var manifestFlagIndex = request.Arguments.ToList().IndexOf("--manifest");
            var manifestPath = request.Arguments[manifestFlagIndex + 1];
            this.ManifestPath = manifestPath;
            this.ManifestJson = await File.ReadAllTextAsync(manifestPath, cancellationToken).ConfigureAwait(false);
            _ = JsonDocument.Parse(this.ManifestJson);
            return result;
        }
    }

    private sealed class FixedToolLocator(string toolPath) : IEngineContentPipelineToolLocator
    {
        public string GetImportToolPath() => toolPath;
    }

    private sealed class TempWorkspace : IDisposable
    {
        public TempWorkspace()
        {
            this.Root = Path.Combine(Path.GetTempPath(), "oxygen-import-tool-api-tests", Guid.NewGuid().ToString("N"));
            Directory.CreateDirectory(this.Root);
        }

        public string Root { get; }

        public void Dispose()
        {
            if (Directory.Exists(this.Root))
            {
                Directory.Delete(this.Root, recursive: true);
            }
        }
    }
}
