// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Text.Json;
using System.Text.Json.Nodes;
using AwesomeAssertions;
using Microsoft.Extensions.Logging.Abstractions;
using Oxygen.Editor.ContentPipeline.TestSupport;
using Oxygen.Editor.Projects.Mounts;
using Oxygen.Managed.Core;
using Oxygen.Managed.Core.Diagnostics;
using static Oxygen.Editor.ContentPipeline.TestSupport.CookScenario;
using static Oxygen.Editor.ContentPipeline.TestSupport.DependencyScenario;
using static Oxygen.Editor.ContentPipeline.TestSupport.SavedSourceScenario;

namespace Oxygen.Editor.ContentPipeline.Integration.Tests.Cooking;

[TestClass]
[System.Diagnostics.CodeAnalysis.SuppressMessage("Maintainability", "CA1515:Consider making public types internal", Justification = "MSTest discovers public test classes with the repository discovery configuration.")]
public sealed class CapturedInputCookTests
{
    public TestContext TestContext { get; set; } = null!;

    /// <summary>All project mounts are captured before the first native worker can observe a later save.</summary>
    /// <returns>The asynchronous snapshot test.</returns>
    [TestMethod]
    public async Task ProjectWorkersShareOneSnapshotAcrossMounts()
    {
        using var workspace = new CookWorkspace([new("Content", MountKind.Source, "Content"), new("Extra", MountKind.Source, "Extra")]);
        workspace.WriteMaterial("Content/Materials/Red.omat.json", "Red");
        workspace.WriteMaterial("Extra/Materials/Blue.omat.json", "Blue");
        var invocation = 0;
        var names = new List<string>();
        var api = new CapturingEngineContentPipelineApi(new(workspace.Root, Succeeded: true, []), SucceededInspection(workspace))
        {
            BeforeImport = async (execution, token) =>
            {
                if (++invocation == 1)
                {
                    workspace.WriteMaterial("Extra/Materials/Blue.omat.json", "Saved later");
                }

                foreach (var job in execution.Manifest.Jobs)
                {
                    using var document = JsonDocument.Parse(await File.ReadAllBytesAsync(CapturedPath(execution, job.AttributedSource), token).ConfigureAwait(false));
                    names.Add(document.RootElement.GetProperty("name").GetString()!);
                }
            },
            SourceAnalysis = NativeSourceFactsFixture.AnalyzeAsync,
        };
        var service = CreateService(workspace, api, new CapturingSceneDescriptorGenerator([]));
        var result = await service.CookProjectAsync(this.TestContext.CancellationToken).ConfigureAwait(false);

        _ = result.Status.Should().Be(OperationStatus.Succeeded);
        _ = names.Should().BeEquivalentTo("Red", "Blue");
        _ = api.Executions.Should().HaveCount(2);
        _ = api.Executions.Select(static execution => execution.InputRoot).Distinct(StringComparer.Ordinal).Should().ContainSingle();
        _ = result.InputSnapshot.Should().NotBeNull();
        _ = api.Executions[0].InputRoot.Should().Be(workspace.Root);
        _ = api.Executions.SelectMany(static execution => execution.CapturedInputs!.Inputs).Where(static input => input.File is not null)
            .Should().OnlyContain(input => input.File!.Path.StartsWith(result.InputSnapshot!.InputRoot, StringComparison.OrdinalIgnoreCase));
        _ = result.InputSnapshot.Inputs.Count(static input => input.AssetUri is not null).Should().Be(2);
        _ = result.InputsAreCurrent.Should().BeFalse();
    }

    /// <summary>Descriptor-relative paths select captured buffers without rewriting their descriptor.</summary>
    /// <returns>The asynchronous snapshot test.</returns>
    [TestMethod]
    public async Task GeometryJobsReadCapturedBuffersAfterSourceChanges()
    {
        using var workspace = new CookWorkspace();
        WriteAuthoredGeometry(workspace, withBuffer: true);
        workspace.WriteMaterial("Content/Materials/Red.omat.json", "Red");
        const string source = "Content/Geometry/AuthoredCube.ogeo.json";

        var authored = workspace.ReadText(source);
        var api = new CapturingEngineContentPipelineApi(new(workspace.Root, Succeeded: true, []), SucceededInspection(workspace))
        {
            BeforeImport = async (execution, token) =>
            {
                workspace.WriteText("Content/Geometry/mesh.bin", "changed live buffer");
                var job = execution.Manifest.Jobs.Single(static job => string.Equals(job.Type, "geometry-descriptor", StringComparison.Ordinal));
                var logicalDescriptor = execution.MountsFile.Resolve(execution.InputRoot, job.AttributedSource);
                var descriptorPath = CapturedPath(execution, job.AttributedSource);
                using var descriptor = JsonDocument.Parse(await File.ReadAllBytesAsync(descriptorPath, token).ConfigureAwait(false));
                var relative = descriptor.RootElement.GetProperty("buffers")[0].GetProperty("uri").GetString()!;
                var capturedBuffer = CapturedPath(execution, Path.GetFullPath(relative, Path.GetDirectoryName(logicalDescriptor)!));
                _ = capturedBuffer.Should().NotBe(Path.Combine(execution.InputRoot, "Content", "Geometry", "mesh.bin"));
                _ = (await File.ReadAllTextAsync(capturedBuffer, token).ConfigureAwait(false)).Should().Be("saved buffer bytes");
            },
            SourceAnalysis = NativeSourceFactsFixture.AnalyzeAsync,
        };
        var service = CreateService(workspace, api, new CapturingSceneDescriptorGenerator([]));

        var result = await service.CookAssetAsync(AssetPath.Parse("/" + source[..^5]), this.TestContext.CancellationToken).ConfigureAwait(false);

        _ = result.Status.Should().Be(OperationStatus.Succeeded);
        _ = result.InputsAreCurrent.Should().BeFalse();
        _ = workspace.ReadText(source).Should().Be(authored);
        _ = (await File.ReadAllTextAsync(Path.Combine(result.InputSnapshot!.InputRoot, CookPaths.StoragePathOf("/" + source)), this.TestContext.CancellationToken).ConfigureAwait(false)).Should().Be(authored);
    }

    /// <summary>
    /// A descriptor that names its file by an absolute path is refused before anything cooks, as the engine refuses
    /// every absolute committed path; the descriptor is left as authored.
    /// </summary>
    /// <param name="geometry">Whether the descriptor is a geometry's buffer rather than a texture's image.</param>
    /// <returns>The asynchronous snapshot test.</returns>
    [TestMethod]
    [DataRow(true, DisplayName = "A geometry's buffer")]
    [DataRow(false, DisplayName = "A texture's image")]
    public async Task DescriptorsNamingAnAbsolutePathAreRefused(bool geometry)
    {
        using var workspace = new CookWorkspace();
        string source;
        if (geometry)
        {
            WriteAuthoredGeometry(workspace, withBuffer: true);
            workspace.WriteMaterial("Content/Materials/Red.omat.json", "Red");
            source = "Content/Geometry/AuthoredCube.ogeo.json";
            var descriptor = JsonNode.Parse(workspace.ReadText(source))!;
            descriptor["buffers"]![0]!["uri"] = Path.Combine(workspace.Root, "Content", "Geometry", "mesh.bin");
            workspace.WriteText(source, descriptor.ToJsonString());
        }
        else
        {
            source = "Content/Textures/Meter.otex.json";
            workspace.WriteText("Content/Textures/meter.png", "saved mask image bytes");
            workspace.WriteText(source, JsonSerializer.Serialize(new { source = Path.Combine(workspace.Root, "Content", "Textures", "meter.png") }));
        }

        var authored = workspace.ReadText(source);
        var api = new CapturingEngineContentPipelineApi(new(workspace.Root, Succeeded: true, []), SucceededInspection(workspace))
        {
            SourceAnalysis = NativeSourceFactsFixture.AnalyzeAsync,
        };
        var service = CreateService(workspace, api, new CapturingSceneDescriptorGenerator([]));

        var result = await service.CookAssetAsync(AssetPath.Parse("/" + source[..^5]), this.TestContext.CancellationToken).ConfigureAwait(false);

        _ = result.Status.Should().Be(OperationStatus.Failed);
        _ = result.Diagnostics.Should().Contain(issue => issue.Code == "path.absolute_committed" && issue.Severity == DiagnosticSeverity.Error);
        _ = workspace.ReadText(source).Should().Be(authored);
    }

    /// <summary>Texture jobs consume the captured image even when the source changes during cooking.</summary>
    /// <returns>The asynchronous snapshot test.</returns>
    [TestMethod]
    public async Task TextureJobsReadCapturedImagesAfterSourceChanges()
    {
        using var workspace = new CookWorkspace();
        const string source = "Content/Textures/Meter.otex.json";
        const string image = "Content/Textures/meter.png";
        workspace.WriteText(image, "saved mask image bytes");
        workspace.WriteText(source, JsonSerializer.Serialize(new { source = "meter.png" }));

        var authored = workspace.ReadText(source);
        var inspection = SucceededInspection(workspace) with
        {
            Assets = [new("/Content/Textures/Meter.otex", ContentCookAssetKind.Texture) { DescriptorRelativePath = "Textures/Meter.otex" }],
        };
        var api = new CapturingEngineContentPipelineApi(new(workspace.Root, Succeeded: true, []), inspection)
        {
            BeforeImport = async (execution, token) =>
            {
                workspace.WriteText(image, "changed live image");
                var job = execution.Manifest.Jobs.Single(static job => string.Equals(job.Type, "texture-descriptor", StringComparison.Ordinal));
                var logicalDescriptor = execution.MountsFile.Resolve(execution.InputRoot, job.AttributedSource);
                var descriptorPath = CapturedPath(execution, job.AttributedSource);
                using var descriptor = JsonDocument.Parse(await File.ReadAllBytesAsync(descriptorPath, token).ConfigureAwait(false));
                var capturedImage = CapturedPath(execution, Path.GetFullPath(descriptor.RootElement.GetProperty("source").GetString()!, Path.GetDirectoryName(logicalDescriptor)!));
                _ = capturedImage.Should().NotBe(Path.Combine(execution.InputRoot, image));
                _ = (await File.ReadAllTextAsync(capturedImage, token).ConfigureAwait(false)).Should().Be("saved mask image bytes");
            },
            SourceAnalysis = NativeSourceFactsFixture.AnalyzeAsync,
        };
        var service = CreateService(workspace, api, new CapturingSceneDescriptorGenerator([]));

        var result = await service.CookAssetAsync(AssetPath.Parse("/" + source[..^5]), this.TestContext.CancellationToken).ConfigureAwait(false);
        _ = result.Status.Should().Be(OperationStatus.Succeeded, string.Join("; ", result.Diagnostics.Select(static issue => issue.Message)));
        _ = result.InputsAreCurrent.Should().BeFalse();
        _ = workspace.ReadText(source).Should().Be(authored);
    }

    /// <summary>The material facade keeps the shared captured operation and its later-change status.</summary>
    /// <returns>The asynchronous material pipeline test.</returns>
    [TestMethod]
    public async Task MaterialHelperPreservesSharedSnapshotAndStaleOutcome()
    {
        using var workspace = new CookWorkspace();
        const string source = "Content/Materials/Red.omat.json";
        workspace.WriteMaterial(source, "Red");
        var inspection = SucceededInspection(workspace) with { Assets = [new("/Content/Materials/Red.omat", ContentCookAssetKind.Material)] };
        var api = new CapturingEngineContentPipelineApi(new(workspace.Root, Succeeded: true, []), inspection)
        {
            BeforeImport = (_, _) =>
            {
                workspace.WriteText(source, workspace.ReadText(source) + " ");
                return Task.CompletedTask;
            },
            SourceAnalysis = NativeSourceFactsFixture.AnalyzeAsync,
        };
        var pipeline = CreateService(workspace, api, new CapturingSceneDescriptorGenerator([]));
        var service = new MaterialCookService(pipeline, workspace.Session, NullLogger<MaterialCookService>.Instance);

        var result = await service.CookMaterialAsync(new(AssetPath.Parse("/" + source[..^5]), workspace.ProjectContext.ProjectRoot, workspace.ProjectContext.ProjectRoot.Combine(source)), this.TestContext.CancellationToken).ConfigureAwait(false);

        _ = result.State.Should().Be(MaterialCookState.Stale);
        _ = result.Cook!.InputSnapshot.Should().NotBeNull();
        _ = result.Cook.InputsAreCurrent.Should().BeFalse();

        _ = workspace.CookCoordinator.Runs.Should().ContainSingle();
        _ = result.OperationId.Should().Be(workspace.CookCoordinator.Runs.Single().OperationId);
    }

    /// <summary>An origin mismatch is rechecked after the request obtains the shared writer.</summary>
    /// <returns>The asynchronous project-scope test.</returns>
    [TestMethod]
    public async Task AssetRequestCannotRunAgainstADifferentOriginatingProject()
    {
        using var workspace = new CookWorkspace();
        workspace.WriteMaterial("Content/Materials/Red.omat.json", "Red");
        var api = CreateSuccessfulApi(workspace, NativeSourceFactsFixture.AnalyzeAsync);
        var service = CreateService(workspace, api, new CapturingSceneDescriptorGenerator([]));
        var origin = workspace.ProjectContext with { ProjectId = Guid.NewGuid() };
        var result = await service.CookAssetAsync(AssetPath.Parse("/Content/Materials/Red.omat"), this.TestContext.CancellationToken, origin).ConfigureAwait(false);
        _ = result.Status.Should().Be(OperationStatus.Failed);
        _ = result.Diagnostics.Should().ContainSingle(diagnostic => diagnostic.Message.Contains("originating project", StringComparison.Ordinal));
        _ = api.ImportedManifest.Should().BeNull();
    }

    /// <summary>Worker startup failures become correlated results for the material command.</summary>
    /// <returns>The asynchronous failure test.</returns>
    [TestMethod]
    public async Task MaterialWorkerStartupFailureReturnsTheSharedFailedOperation()
    {
        using var workspace = new CookWorkspace();
        const string source = "Content/Materials/Red.omat.json";
        workspace.WriteMaterial(source, "Red");
        var api = new CapturingEngineContentPipelineApi(new(workspace.Root, Succeeded: true, []), SucceededInspection(workspace))
        {
            BeforeImport = (_, _) => Task.FromException(new System.ComponentModel.Win32Exception(2)),
            SourceAnalysis = NativeSourceFactsFixture.AnalyzeAsync,
        };
        var pipeline = CreateService(workspace, api, new CapturingSceneDescriptorGenerator([]));
        var service = new MaterialCookService(pipeline, workspace.Session, NullLogger<MaterialCookService>.Instance);

        var result = await service.CookMaterialAsync(new(AssetPath.Parse("/" + source[..^5]), workspace.ProjectContext.ProjectRoot, workspace.ProjectContext.ProjectRoot.Combine(source)), this.TestContext.CancellationToken).ConfigureAwait(false);

        _ = result.State.Should().Be(MaterialCookState.Failed);
        _ = result.OperationId.Should().Be(workspace.CookCoordinator.Runs.Single().OperationId);
        _ = result.Cook!.Diagnostics.Should().ContainSingle(diagnostic => diagnostic.ExceptionType == typeof(System.ComponentModel.Win32Exception).FullName);
        _ = workspace.CookCoordinator.Runs.Single().State.Should().Be(global::Oxygen.Editor.ContentPipeline.Cooking.CookRunState.Failed);
    }

    /// <summary>New folder assets make the completed captured scope visibly out of date.</summary>
    /// <returns>The asynchronous snapshot test.</returns>
    [TestMethod]
    public async Task NewFolderInputDoesNotBecomePartOfAnAlreadyCapturedCook()
    {
        using var workspace = new CookWorkspace();
        workspace.WriteMaterial("Content/Materials/Red.omat.json", "Red");
        var api = new CapturingEngineContentPipelineApi(new(workspace.Root, Succeeded: true, []), SucceededInspection(workspace))
        {
            BeforeImport = (_, _) =>
            {
                workspace.WriteMaterial("Content/Materials/Blue.omat.json", "Blue");
                return Task.CompletedTask;
            },
            SourceAnalysis = NativeSourceFactsFixture.AnalyzeAsync,
        };
        var service = CreateService(workspace, api, new CapturingSceneDescriptorGenerator([]));

        var result = await service.CookFolderAsync(AssetPath.Parse("/Content/Materials"), this.TestContext.CancellationToken).ConfigureAwait(false);
        _ = result.InputSnapshot!.Inputs.Count(static input => input.AssetUri is not null).Should().Be(1);
        _ = result.InputsAreCurrent.Should().BeFalse();
    }

    /// <summary>Captured producer ownership remains held until failed native work and cleanup finish.</summary>
    /// <returns>The asynchronous ownership test.</returns>
    [TestMethod]
    public async Task SnapshotProducerLeaseDrainsBeforeTheNextWriterStarts()
    {
        using var workspace = new CookWorkspace();
        workspace.WriteMaterial("Content/Materials/Red.omat.json", "Red");
        var drain = new TaskCompletionSource(TaskCreationOptions.RunContinuationsAsynchronously);
        var api = new CapturingEngineContentPipelineApi(new(workspace.Root, Succeeded: true, []), SucceededInspection(workspace))
        {
            BeforeImport = (_, _) => Task.FromException(new ContentPipelineTerminationException(new IOException("Termination failed"), drain.Task)),
            SourceAnalysis = NativeSourceFactsFixture.AnalyzeAsync,
        };
        var service = CreateService(workspace, api, new CapturingSceneDescriptorGenerator([]));
        Func<Task> cook = () => service.CookAssetAsync(AssetPath.Parse("/Content/Materials/Red.omat"), this.TestContext.CancellationToken);
        var failure = await cook.Should().ThrowAsync<ContentPipelineTerminationException>().ConfigureAwait(false);
        Action replaceProducer = () => File.WriteAllText(Path.Combine(workspace.Root, "producer.bin"), "replacement");
        _ = replaceProducer.Should().Throw<IOException>();
        var next = workspace.CookCoordinator.RunAsync(
            (_, _) =>
            {
                replaceProducer();
                return Task.FromResult(true);
            },
            this.TestContext.CancellationToken);
        _ = next.IsCompleted.Should().BeFalse();
        drain.SetResult();
        await failure.Which.DrainCompletion.ConfigureAwait(false);
        _ = await next.ConfigureAwait(false);
        _ = Directory.EnumerateFiles(Path.Combine(workspace.Root, ".build", "cook"), "Red.omat.json", SearchOption.AllDirectories).Should().NotBeEmpty();
    }

    private static string CapturedPath(ContentImportExecution execution, string source)
    {
        var logical = execution.MountsFile.Resolve(execution.InputRoot, source);
        return execution.CapturedInputs!.Inputs.Single(input => string.Equals(input.LogicalPath, logical, StringComparison.OrdinalIgnoreCase)).File!.Path;
    }
}
