// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Text.Json.Nodes;
using AwesomeAssertions;
using Microsoft.Extensions.Logging.Abstractions;
using Oxygen.Editor.ContentPipeline.TestSupport;
using Oxygen.Managed.Core.Compatibility;
using static Oxygen.Editor.ContentPipeline.TestSupport.ImportAdapterScenario;

namespace Oxygen.Editor.ContentPipeline.Integration.Tests.Import;

[TestClass]
[System.Diagnostics.CodeAnalysis.SuppressMessage("Maintainability", "CA1515:Consider making public types internal", Justification = "MSTest discovers public test classes with the repository discovery configuration.")]
[System.Diagnostics.CodeAnalysis.SuppressMessage("Naming", "CA1707:Identifiers should not contain underscores", Justification = "Scenario-based MSTest method names separate the operation and expected behavior.")]
public sealed class ImportExecutionPathTests
{
    public TestContext TestContext { get; set; } = null!;

    /// <summary>The real tool reads private input and produces staging output without touching published content.</summary>
    /// <returns>The asynchronous native test operation.</returns>
    [TestMethod]
    [TestCategory("NativeContent")]
    public async Task NativeImportReadsSnapshotAndWritesIndependentStagingRoot()
    {
        using var workspace = new ImportAdapterWorkspace();
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
        using var compatibility = EditorNativeCompatibilityService.ForCooking();
        var api = new ImportToolContentPipelineApi(new EngineContentPipelineToolLocator(), new ContentPipelineProcessRunner(), NullLogger<ImportToolContentPipelineApi>.Instance, compatibility);

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
}
