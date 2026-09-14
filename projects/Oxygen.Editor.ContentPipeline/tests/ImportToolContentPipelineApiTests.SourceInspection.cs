// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Text.Json.Nodes;
using AwesomeAssertions;
using Microsoft.Extensions.Logging.Abstractions;
using Oxygen.Managed.Core.Compatibility;
using Oxygen.Managed.Core.Diagnostics;

namespace Oxygen.Editor.ContentPipeline.Tests;

/// <summary>Verifies source metadata, schema validation and owned query lifetime.</summary>
public sealed partial class ImportToolContentPipelineApiTests
{
    private const string SupportedSourceReport = """
        {
          "version":1,"parsed":true,"supported":true,"format":"gltf",
          "external_files":["buffers/triangle data.bin"],"source_unit_meters":1,
          "source_axes":{"right":"+X","up":"+Y","front":"+Z"},
          "source_left_handed":false,"reverses_winding":false,
          "target_axes":{"right":"+X","up":"+Z","front":"-Y"},"target_unit_meters":1,
          "mesh_count":1,"material_count":2,"node_count":3,"diagnostics":[]
        }
        """;

    /// <summary>Source inspection returns typed native facts and removes its report after reading.</summary>
    /// <returns>The asynchronous source-query test.</returns>
    [TestMethod]
    public async Task SourceInspection_ReturnsFactsAndCleansReport()
    {
        using var workspace = new TempWorkspace();
        var runner = new SourceInspectionRunner(SupportedSourceReport);
        var api = CreateSourceInspectionApi(workspace, runner);
        var operation = Path.Combine(workspace.Root, "new operation");
        var source = Path.Combine(workspace.Root, "source model.gltf");

        var result = await api.InspectSceneSourceAsync(Guid.NewGuid(), operation, source, this.TestContext.CancellationToken).ConfigureAwait(false);

        _ = result.Supported.Should().BeTrue();
        _ = result.Parsed.Should().BeTrue();
        _ = result.ExternalFiles.Should().Equal("buffers/triangle data.bin");
        _ = result.Coordinates.UnitMeters.Should().Be(1);
        _ = result.Coordinates.IsLeftHanded.Should().BeFalse();
        _ = result.MeshCount.Should().Be(1);
        _ = result.MaterialCount.Should().Be(2);
        _ = runner.Request!.Arguments.Should().ContainInOrder("inspect-source", source, "--output", runner.ReportPath!);
        _ = runner.Request.WorkingDirectory.Should().Be(operation);
        _ = File.Exists(runner.ReportPath).Should().BeFalse();
    }

    /// <summary>Unsupported content preserves native messages and source locations without throwing away the report.</summary>
    /// <returns>The asynchronous diagnostic test.</returns>
    [TestMethod]
    public async Task SourceInspection_PreservesUnsupportedContentDiagnostics()
    {
        using var workspace = new TempWorkspace();
        var json = JsonNode.Parse(SupportedSourceReport)!;
        json["supported"] = false;
        json["diagnostics"] = JsonNode.Parse("""
            [{"severity":"Error","code":"import.static_scalar.unsupported","message":"Skinning is unsupported.","source_path":"source.gltf","object_path":"/skins/0"}]
            """);
        var runner = new SourceInspectionRunner(json.ToJsonString());
        var operationId = Guid.NewGuid();
        var result = await CreateSourceInspectionApi(workspace, runner).InspectSceneSourceAsync(operationId, workspace.Root, "source.gltf", this.TestContext.CancellationToken).ConfigureAwait(false);

        _ = result.Parsed.Should().BeTrue();
        _ = result.Supported.Should().BeFalse();
        _ = result.Diagnostics.Should().ContainSingle(item => item.OperationId == operationId && item.Severity == DiagnosticSeverity.Error
            && item.Code == "import.static_scalar.unsupported" && item.Message == "Skinning is unsupported."
            && item.AffectedPath == "source.gltf" && item.TechnicalMessage == "/skins/0");
    }

    /// <summary>Invalid report versions and impossible supported metadata fail schema validation and release ownership.</summary>
    /// <param name="wrongVersion">Whether to corrupt the version or required coordinate facts.</param>
    /// <returns>The asynchronous invalid-report test.</returns>
    [TestMethod]
    [DataRow(true)]
    [DataRow(false)]
    public async Task SourceInspection_InvalidReportReleasesArtifacts(bool wrongVersion)
    {
        using var workspace = new TempWorkspace();
        var json = JsonNode.Parse(SupportedSourceReport)!;
        json[wrongVersion ? "version" : "source_unit_meters"] = wrongVersion ? JsonValue.Create(2) : null;
        var runner = new SourceInspectionRunner(json.ToJsonString());
        var api = CreateSourceInspectionApi(workspace, runner);
        Func<Task> inspect = () => api.InspectSceneSourceAsync(Guid.NewGuid(), workspace.Root, "source.gltf", this.TestContext.CancellationToken);
        _ = await inspect.Should().ThrowAsync<InvalidDataException>().ConfigureAwait(false);
        _ = File.Exists(runner.ReportPath).Should().BeFalse();
        await File.WriteAllTextAsync(workspace.ToolPath, "Released", this.TestContext.CancellationToken).ConfigureAwait(false);
    }

    /// <summary>A failed worker termination keeps its report and native files until the worker drains.</summary>
    /// <returns>The asynchronous retained-worker test.</returns>
    [TestMethod]
    public async Task SourceInspection_TerminationFailureRetainsReportUntilDrain()
    {
        using var workspace = new TempWorkspace();
        var drain = new TaskCompletionSource(TaskCreationOptions.RunContinuationsAsynchronously);
        var runner = new SourceInspectionRunner(SupportedSourceReport, new ContentPipelineTerminationException(new IOException("Termination failed"), drain.Task));
        var api = CreateSourceInspectionApi(workspace, runner);
        Func<Task> inspect = () => api.InspectSceneSourceAsync(Guid.NewGuid(), workspace.Root, "source.gltf", this.TestContext.CancellationToken);
        _ = await inspect.Should().ThrowAsync<ContentPipelineTerminationException>().ConfigureAwait(false);
        _ = File.Exists(runner.ReportPath).Should().BeTrue();
        Action overwrite = () => File.WriteAllText(workspace.ToolPath, "replacement");
        try
        {
            _ = overwrite.Should().Throw<IOException>();
        }
        finally
        {
            drain.SetResult();
        }

        using var timeout = new CancellationTokenSource(TimeSpan.FromSeconds(5));
        while (File.Exists(runner.ReportPath))
        {
            await Task.Delay(20, timeout.Token).ConfigureAwait(false);
        }

        _ = overwrite.Should().NotThrow();
    }

    /// <summary>Compatibility failure rejects a source query before starting its native process.</summary>
    /// <returns>The asynchronous compatibility test.</returns>
    [TestMethod]
    public async Task SourceInspection_RejectsChangedArtifactsBeforeWorkerStart()
    {
        using var workspace = new TempWorkspace();
        var runner = new SourceInspectionRunner(SupportedSourceReport);
        await File.WriteAllTextAsync(workspace.ToolPath, "Changed tool", this.TestContext.CancellationToken).ConfigureAwait(false);
        Func<Task> inspect = () => CreateSourceInspectionApi(workspace, runner).InspectSceneSourceAsync(Guid.NewGuid(), workspace.Root, "source.gltf", this.TestContext.CancellationToken);
        _ = await inspect.Should().ThrowAsync<NativeCompatibilityException>().ConfigureAwait(false);
        _ = runner.Request.Should().BeNull();
    }

    /// <summary>A nested query keeps the enclosing cook's artifact lease alive.</summary>
    /// <returns>The asynchronous borrowed-ownership test.</returns>
    [TestMethod]
    public async Task SourceInspection_PreservesBorrowedArtifactOwnership()
    {
        using var workspace = new TempWorkspace();
        var compatibility = await workspace.Compatibility.VerifyAsync(Guid.NewGuid(), this.TestContext.CancellationToken).ConfigureAwait(false);
        var artifacts = compatibility.Artifacts!;
        await using var lifetime = artifacts.ConfigureAwait(false);
        var runner = new SourceInspectionRunner(SupportedSourceReport);
        _ = await CreateSourceInspectionApi(workspace, runner).InspectSceneSourceAsync(Guid.NewGuid(), workspace.Root, "source.gltf", this.TestContext.CancellationToken, artifacts).ConfigureAwait(false);
        Action overwrite = () => File.WriteAllText(workspace.ToolPath, "replacement");
        _ = overwrite.Should().Throw<IOException>();
        _ = File.Exists(runner.ReportPath).Should().BeFalse();
    }

    /// <summary>The installed native command returns schema-valid metadata through the actual contained runner.</summary>
    /// <param name="extension">The supported source format.</param>
    /// <returns>The asynchronous native integration test.</returns>
    [TestMethod]
    [TestCategory("NativeContent")]
    [DataRow("gltf")]
    [DataRow("fbx")]
    public async Task SourceInspection_InstalledNativeToolReturnsSourceFacts(string extension)
    {
        using var workspace = new TempWorkspace();
        var source = Path.Combine(workspace.Root, "model." + extension);
        File.Copy(Path.Combine(AppContext.BaseDirectory, "Fixtures", "static_scalar_triangle." + extension), source);
        if (string.Equals(extension, "gltf", StringComparison.Ordinal))
        {
            var content = JsonNode.Parse(await File.ReadAllTextAsync(source, this.TestContext.CancellationToken).ConfigureAwait(false))!;
            content["buffers"]![0]!["uri"] = "missing%20buffer.bin";
            await File.WriteAllTextAsync(source, content.ToJsonString(), this.TestContext.CancellationToken).ConfigureAwait(false);
        }

        using var compatibility = Oxygen.Testing.TemporaryNativeArtifacts.ForInstalledEngine();
        var api = new ImportToolContentPipelineApi(new EngineContentPipelineToolLocator(), new ContentPipelineProcessRunner(), NullLogger<ImportToolContentPipelineApi>.Instance, compatibility);
        var result = await api.InspectSceneSourceAsync(Guid.NewGuid(), workspace.Root, source, this.TestContext.CancellationToken).ConfigureAwait(false);
        _ = result.Parsed.Should().BeTrue();
        _ = result.Supported.Should().BeTrue();
        _ = result.MeshCount.Should().Be(1);
        _ = result.Coordinates.UnitMeters.Should().Be(1);
        if (string.Equals(extension, "gltf", StringComparison.Ordinal))
        {
            _ = result.ExternalFiles.Should().Equal("missing buffer.bin");
        }
        else
        {
            _ = result.ExternalFiles.Should().BeEmpty();
        }

        _ = Directory.EnumerateFiles(Path.Combine(workspace.Root, "source-inspection")).Should().BeEmpty();
        _ = Directory.Exists(Path.Combine(workspace.Root, ".cooked")).Should().BeFalse();
    }

    private static ImportToolContentPipelineApi CreateSourceInspectionApi(TempWorkspace workspace, IContentPipelineProcessRunner runner)
        => new(new FixedToolLocator(workspace.ToolPath), runner, NullLogger<ImportToolContentPipelineApi>.Instance, workspace.Compatibility);

    private sealed class SourceInspectionRunner(string json, Exception? failure = null) : IContentPipelineProcessRunner
    {
        public ContentPipelineProcessRequest? Request { get; private set; }

        public string? ReportPath { get; private set; }

        public async Task<ContentPipelineProcessResult> RunAsync(ContentPipelineProcessRequest request, CancellationToken cancellationToken)
        {
            this.Request = request;
            this.ReportPath = request.Arguments[request.Arguments.ToList().IndexOf("--output") + 1];
            await File.WriteAllTextAsync(this.ReportPath, json, cancellationToken).ConfigureAwait(false);
            return failure is null ? new(0, string.Empty, string.Empty) : throw failure;
        }
    }
}
