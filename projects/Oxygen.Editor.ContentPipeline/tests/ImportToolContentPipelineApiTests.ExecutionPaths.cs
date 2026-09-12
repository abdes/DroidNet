// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Text.Json.Nodes;
using AwesomeAssertions;
using Microsoft.Extensions.Logging.Abstractions;

namespace Oxygen.Editor.ContentPipeline.Tests;

/// <summary>Verifies independent snapshot input, staging output, and operation manifest ownership.</summary>
public sealed partial class ImportToolContentPipelineApiTests
{
    /// <summary>Gets or sets the current test cancellation context.</summary>
    public TestContext TestContext { get; set; } = null!;

    /// <summary>The real tool reads private input and produces staging output without touching published content.</summary>
    /// <returns>The asynchronous native test operation.</returns>
    [TestMethod]
    [TestCategory("NativeContent")]
    public async Task NativeImportReadsSnapshotAndWritesIndependentStagingRoot()
    {
        using var workspace = new TempWorkspace();
        var operationId = Guid.NewGuid();
        var operationRoot = Path.Combine(workspace.Root, ".build", "cook", operationId.ToString("N"));
        var inputRoot = Path.Combine(operationRoot, "inputs with spaces");
        var manifest = CreateManifest(workspace) with { Output = Path.Combine(operationRoot, "output", "Content") };
        var source = manifest.Jobs.Single().Source;
        var capturedPath = Path.Combine(inputRoot, source);
        Directory.CreateDirectory(Path.GetDirectoryName(capturedPath)!);
        var catalog = JsonNode.Parse(await File.ReadAllTextAsync(Path.Combine(AppContext.BaseDirectory, "Fixtures", "BuiltinGeometryCatalog.json"), this.TestContext.CancellationToken).ConfigureAwait(false))!;
        var descriptor = catalog["default_material"]!["descriptor"]!;
        descriptor["name"] = "Red";
        var savedJson = descriptor.ToJsonString();
        await File.WriteAllTextAsync(capturedPath, savedJson, this.TestContext.CancellationToken).ConfigureAwait(false);

        // A mutable source at the old inferred project location must not be read.
        var authoringPath = Path.Combine(workspace.Root, source);
        Directory.CreateDirectory(Path.GetDirectoryName(authoringPath)!);
        await File.WriteAllTextAsync(authoringPath, "New incomplete authoring bytes", this.TestContext.CancellationToken).ConfigureAwait(false);
        var publishedRoot = Path.Combine(workspace.Root, ".cooked", "Content");
        Directory.CreateDirectory(publishedRoot);
        var previous = Path.Combine(publishedRoot, "previous-generation.txt");
        await File.WriteAllTextAsync(previous, "Previous publication", this.TestContext.CancellationToken).ConfigureAwait(false);
        using var qualification = Oxygen.Testing.TemporaryArtifactQualification.ForInstalledEngine();
        var api = new ImportToolContentPipelineApi(new EngineContentPipelineToolLocator(), new ContentPipelineProcessRunner(), NullLogger<ImportToolContentPipelineApi>.Instance, qualification);

        var result = await api.ImportAsync(new(operationId, inputRoot, operationRoot, manifest), this.TestContext.CancellationToken).ConfigureAwait(false);

        _ = result.Succeeded.Should().BeTrue(string.Join(Environment.NewLine, result.Diagnostics.Select(static diagnostic => diagnostic.TechnicalMessage ?? diagnostic.Message)));
        var inspection = await api.InspectLooseCookedRootAsync(manifest.Output, this.TestContext.CancellationToken).ConfigureAwait(false);
        _ = inspection.Succeeded.Should().BeTrue();
        _ = inspection.Assets.Should().ContainSingle(asset => asset.VirtualPath == "/Content/Materials/Red.omat" && asset.Kind == ContentCookAssetKind.Material);
        _ = (await api.ValidateLooseCookedRootAsync(manifest.Output, this.TestContext.CancellationToken).ConfigureAwait(false)).Succeeded.Should().BeTrue();
        var captured = await File.ReadAllTextAsync(capturedPath, this.TestContext.CancellationToken).ConfigureAwait(false);
        _ = captured.Should().Be(savedJson);
        _ = (await File.ReadAllTextAsync(previous, this.TestContext.CancellationToken).ConfigureAwait(false)).Should().Be("Previous publication");
        _ = Directory.EnumerateFiles(publishedRoot, "*", SearchOption.AllDirectories).Should().ContainSingle().Which.Should().Be(previous);
        _ = Directory.EnumerateFiles(Path.Combine(operationRoot, "manifests")).Should().BeEmpty();
    }

    /// <summary>Relative execution roots cannot silently depend on the editor's current directory.</summary>
    /// <param name="invalidPart">The execution field that lacks an absolute path or identity.</param>
    /// <returns>The asynchronous test operation.</returns>
    [TestMethod]
    [DataRow("input")]
    [DataRow("operation")]
    [DataRow("output")]
    [DataRow("identity")]
    public async Task InvalidExecutionPathsDoNotLaunchWorker(string invalidPart)
    {
        using var workspace = new TempWorkspace();
        var execution = CreateExecution(workspace, CreateManifest(workspace));
        execution = invalidPart switch
        {
            "input" => execution with { InputRoot = "inputs" },
            "operation" => execution with { OperationRoot = "operation" },
            "output" => execution with { Manifest = execution.Manifest with { Output = "output" } },
            _ => execution with { OperationId = Guid.Empty },
        };
        var runner = new CapturingRunner(new(0, string.Empty, string.Empty));
        var api = new ImportToolContentPipelineApi(new FixedToolLocator("unused.exe"), runner, NullLogger<ImportToolContentPipelineApi>.Instance);
        Func<Task> import = () => api.ImportAsync(execution, this.TestContext.CancellationToken);

        _ = await import.Should().ThrowAsync<ArgumentException>().ConfigureAwait(false);
        _ = runner.Request.Should().BeNull();
        _ = Directory.Exists(execution.OperationRoot).Should().BeFalse();
    }
}
