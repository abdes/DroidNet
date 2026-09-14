// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using AwesomeAssertions;
using DroidNet.Storage;
using DroidNet.Storage.Native;
using Microsoft.Extensions.Logging.Abstractions;
using Oxygen.Editor.ContentPipeline.Cooking;
using Oxygen.Editor.ContentPipeline.Import;
using Oxygen.Editor.ContentPipeline.Snapshots;
using Oxygen.Managed.Core.Diagnostics;
using Testably.Abstractions;

namespace Oxygen.Editor.ContentPipeline.Tests;

/// <summary>Exercises the explicit source-import operation and retained-source recovery.</summary>
public sealed partial class ContentPipelineServiceTests
{
    /// <summary>One explicit import retains source, creates settings and publishes through one coordinator run.</summary>
    /// <returns>The asynchronous complete import test.</returns>
    [TestMethod]
    [TestCategory("NativeContent")]
    public async Task ImportWorkflowRetainsAndPublishesAsOneRun()
    {
        using var workspace = new TempWorkspace();
        var external = Directory.CreateTempSubdirectory("OxygenExternalModel-");
        try
        {
            var source = Path.Combine(external.FullName, "model.gltf");
            File.Copy(Path.Combine(AppContext.BaseDirectory, "Fixtures", "static_scalar_triangle.gltf"), source);
            using var compatibility = Oxygen.Testing.TemporaryNativeArtifacts.ForInstalledEngine();
            var api = new ImportToolContentPipelineApi(new EngineContentPipelineToolLocator(), new ContentPipelineProcessRunner(), NullLogger<ImportToolContentPipelineApi>.Instance, compatibility);
            var service = CreateService(workspace, new SceneDescriptorGenerator(new ProceduralGeometryDescriptorService(api)), api, compatibility);
            var result = await service.ImportSourceAsync(new(workspace.ProjectContext, source, "Model", new("asset:///Content/Models")), this.TestContext.CancellationToken).ConfigureAwait(false);
            _ = result.IsPublished.Should().BeTrue(string.Join(Environment.NewLine, result.Diagnostics.Select(static issue => issue.Message)));
            _ = result.RetainedSourceUri.Should().Be(new Uri("asset:///Content/SourceMedia/DCC/Model/model.gltf"));
            _ = workspace.CookCoordinator.Runs.Should().ContainSingle();
            var run = workspace.CookCoordinator.Runs.Single();
            _ = run.Request.Import.Should().BeNull();
            _ = run.Request.IsReimport.Should().BeTrue();
            _ = run.Request.ScopeUri.Should().Be(result.RetainedSourceUri);
            _ = run.Assets.Keys.Should().Contain(result.CookedAssets.Select(static asset => asset.CookedAssetUri));
            _ = run.Assets[result.RetainedSourceUri!].Kind.Should().Be(ContentCookAssetKind.ForeignSource);
            File.Delete(source);
            _ = (await service.ReimportSourceAsync(result.RetainedSourceUri!, workspace.ProjectContext, this.TestContext.CancellationToken).ConfigureAwait(false)).IsUpToDate.Should().BeTrue();
        }
        finally
        {
            external.Delete(recursive: true);
        }
    }

    /// <summary>A model already in an authoring folder is imported in place without creating another source copy.</summary>
    /// <returns>The asynchronous in-project source test.</returns>
    [TestMethod]
    [TestCategory("NativeContent")]
    public async Task ImportWorkflowKeepsProjectSourceIdentity()
    {
        using var workspace = new TempWorkspace();
        var path = Path.Combine(workspace.Root, "Content/SourceMedia/DCC/model.fbx");
        _ = Directory.CreateDirectory(Path.GetDirectoryName(path)!);
        File.Copy(Path.Combine(AppContext.BaseDirectory, "Fixtures", "static_scalar_triangle.fbx"), path);
        using var compatibility = Oxygen.Testing.TemporaryNativeArtifacts.ForInstalledEngine();
        var api = new ImportToolContentPipelineApi(new EngineContentPipelineToolLocator(), new ContentPipelineProcessRunner(), NullLogger<ImportToolContentPipelineApi>.Instance, compatibility);
        var service = CreateService(workspace, new SceneDescriptorGenerator(new ProceduralGeometryDescriptorService(api)), api, compatibility);
        var result = await service.ImportSourceAsync(new(workspace.ProjectContext, path, "Model", new("asset:///Content/Models")), this.TestContext.CancellationToken).ConfigureAwait(false);
        _ = result.IsPublished.Should().BeTrue(string.Join(Environment.NewLine, result.Diagnostics.Select(static issue => issue.Message)));
        _ = result.RetainedSourceUri.Should().Be(new Uri("asset:///Content/SourceMedia/DCC/model.fbx"));
        _ = Directory.EnumerateFiles(Path.Combine(workspace.Root, "Content/SourceMedia"), "*.fbx", SearchOption.AllDirectories).Should().ContainSingle();
    }

    /// <summary>A failed cook retains its source and changes Retry to the retained identity, even after the original disappears.</summary>
    /// <returns>The asynchronous failed-import recovery test.</returns>
    [TestMethod]
    [TestCategory("NativeContent")]
    public async Task ImportFailureRetriesRetainedSourceWithoutRecopying()
    {
        using var workspace = new TempWorkspace();
        var external = Directory.CreateTempSubdirectory("OxygenFailedImport-");
        try
        {
            var path = Path.Combine(external.FullName, "model.gltf");
            File.Copy(Path.Combine(AppContext.BaseDirectory, "Fixtures", "static_scalar_triangle.gltf"), path);
            using var compatibility = Oxygen.Testing.TemporaryNativeArtifacts.ForInstalledEngine();
            var runner = new FailImportBatchRunner();
            var api = new ImportToolContentPipelineApi(new EngineContentPipelineToolLocator(), runner, NullLogger<ImportToolContentPipelineApi>.Instance, compatibility);
            var service = CreateService(workspace, new SceneDescriptorGenerator(new ProceduralGeometryDescriptorService(api)), api, compatibility);
            var result = await service.ImportSourceAsync(new(workspace.ProjectContext, path, "Model", new("asset:///Content/Models")), this.TestContext.CancellationToken).ConfigureAwait(false);
            _ = result.Status.Should().Be(OperationStatus.Failed);
            _ = result.RetainedSourceUri.Should().NotBeNull();
            var retry = workspace.CookCoordinator.Runs.Single().Request;
            _ = retry.Import.Should().BeNull();
            _ = retry.ScopeUri.Should().Be(result.RetainedSourceUri);
            File.Delete(path);
            runner.Fail = false;
            var recovered = await service.ReimportSourceAsync(retry.ScopeUri!, workspace.ProjectContext, this.TestContext.CancellationToken).ConfigureAwait(false);
            _ = recovered.IsPublished.Should().BeTrue();
        }
        finally
        {
            external.Delete(recursive: true);
        }
    }

    /// <summary>Changes to the reviewed project prevent source writes and native work.</summary>
    /// <returns>The asynchronous reviewed-context test.</returns>
    [TestMethod]
    public async Task ImportRejectsChangedReviewedProject()
    {
        using var workspace = new TempWorkspace();
        var request = new SceneImportRequest(workspace.ProjectContext, Path.Combine(workspace.Root, "model.gltf"), "Model", new("asset:///Content/Models"));
        workspace.ContextService.Activate(workspace.ProjectContext with { Name = "Changed" });
        var api = new CapturingEngineContentPipelineApi(new(Path.Combine(workspace.Root, ".cooked/Content"), Succeeded: true, []), SucceededInspection(workspace));
        var service = CreateService(workspace, new CapturingSceneDescriptorGenerator(workspace, []), api);
        var result = await service.ImportSourceAsync(request, this.TestContext.CancellationToken).ConfigureAwait(false);
        _ = result.Status.Should().Be(OperationStatus.Failed);
        _ = result.RetainedSourceUri.Should().BeNull();
        _ = api.ImportedManifest.Should().BeNull();
    }

    /// <summary>Retry after settings persistence fails keeps the retained source instead of repeating the copy.</summary>
    /// <returns>The asynchronous partial-retention recovery test.</returns>
    [TestMethod]
    [TestCategory("NativeContent")]
    public async Task ImportSettingsFailureRetriesWithoutOriginalSource()
    {
        using var workspace = new TempWorkspace();
        var external = Directory.CreateTempSubdirectory("OxygenImportSettingsFailure-");
        try
        {
            var source = Path.Combine(external.FullName, "model.gltf");
            File.Copy(Path.Combine(AppContext.BaseDirectory, "Fixtures", "static_scalar_triangle.gltf"), source);
            using var compatibility = Oxygen.Testing.TemporaryNativeArtifacts.ForInstalledEngine();
            var api = new ImportToolContentPipelineApi(new EngineContentPipelineToolLocator(), new ContentPipelineProcessRunner(), NullLogger<ImportToolContentPipelineApi>.Instance, compatibility);
            var files = new FailSettingsStore();
            var service = new ContentPipelineService(
                workspace.ContextService,
                workspace.CookCoordinator,
                new FixedCookScopeProvider(workspace.Root),
                new SceneDescriptorGenerator(new ProceduralGeometryDescriptorService(api)),
                new ContentImportManifestBuilder(),
                new ContentImportManifestValidator(),
                api,
                workspace.Documents,
                compatibility,
                files);
            var result = await service.ImportSourceAsync(new(workspace.ProjectContext, source, "Model", new("asset:///Content/Models")), this.TestContext.CancellationToken).ConfigureAwait(false);
            _ = result.Status.Should().Be(OperationStatus.Failed);
            _ = result.RetainedSourceUri.Should().NotBeNull();
            var retry = workspace.CookCoordinator.Runs.Single().Request.Import;
            _ = retry.Should().NotBeNull();
            _ = retry!.RetainedSource.Should().NotBeNull();
            File.Delete(source);
            files.Fail = false;
            var recovered = await service.ImportSourceAsync(retry, this.TestContext.CancellationToken).ConfigureAwait(false);
            _ = recovered.IsPublished.Should().BeTrue();
            _ = recovered.RetainedSourceUri.Should().Be(result.RetainedSourceUri);
        }
        finally
        {
            external.Delete(recursive: true);
        }
    }

    /// <summary>Saving a blocker resumes the same run from retained source even when the external file is gone.</summary>
    /// <returns>The asynchronous save-and-resume regression.</returns>
    [TestMethod]
    [TestCategory("NativeContent")]
    public async Task ImportResumesAfterSaveWithoutRepeatingSourceRetention()
    {
        using var workspace = new TempWorkspace();
        var external = Directory.CreateTempSubdirectory("OxygenImportResume-");
        try
        {
            var source = Path.Combine(external.FullName, "model.gltf");
            File.Copy(Path.Combine(AppContext.BaseDirectory, "Fixtures", "static_scalar_triangle.gltf"), source);
            var settingsPath = Path.Combine(workspace.Root, "Content/SourceMedia/DCC/Model/model.gltf.import.json");
            var state = new CookDocumentState(Guid.NewGuid(), settingsPath, "Model import settings", 1, 0, IsDirty: true, string.Empty);
            var dirty = true;
            using var registration = workspace.Documents.Register(settingsPath, _ => Task.FromResult<CookDocumentReadLease?>(dirty ? new(state, static () => { }) : null));
            var blocked = new TaskCompletionSource<Guid>(TaskCreationOptions.RunContinuationsAsynchronously);
            workspace.CookCoordinator.RunChanged += (sender, args) =>
            {
                if (args.Run.State == CookRunState.NeedsSave)
                {
                    _ = blocked.TrySetResult(args.Run.OperationId);
                }
            };
            using var compatibility = Oxygen.Testing.TemporaryNativeArtifacts.ForInstalledEngine();
            var api = new ImportToolContentPipelineApi(new EngineContentPipelineToolLocator(), new ContentPipelineProcessRunner(), NullLogger<ImportToolContentPipelineApi>.Instance, compatibility);
            var service = CreateService(workspace, new SceneDescriptorGenerator(new ProceduralGeometryDescriptorService(api)), api, compatibility);
            var pending = service.ImportSourceAsync(new(workspace.ProjectContext, source, "Model", new("asset:///Content/Models")), this.TestContext.CancellationToken);
            var operationId = await blocked.Task.WaitAsync(TimeSpan.FromSeconds(15), this.TestContext.CancellationToken).ConfigureAwait(false);
            File.Delete(source);
            dirty = false;
            _ = workspace.CookCoordinator.ResumeAfterSave(operationId).Should().BeTrue();
            var result = await pending.WaitAsync(TimeSpan.FromSeconds(15), this.TestContext.CancellationToken).ConfigureAwait(false);
            _ = result.IsPublished.Should().BeTrue(string.Join(Environment.NewLine, result.Diagnostics.Select(static issue => issue.Message)));
            _ = result.OperationId.Should().Be(operationId);
            _ = workspace.CookCoordinator.Runs.Should().ContainSingle();
        }
        finally
        {
            external.Delete(recursive: true);
        }
    }

    private sealed class FailSettingsStore : IAtomicFileStore
    {
        private readonly NativeAtomicFileStore inner = new(new RealFileSystem());

        public bool Fail { get; set; } = true;

        public Task<FileSnapshot> ReadAsync(string path, CancellationToken cancellationToken = default) => this.inner.ReadAsync(path, cancellationToken);

        public Task<FileVersion> WriteAsync(string path, ReadOnlyMemory<byte> content, FileVersion expected, CancellationToken cancellationToken = default)
            => this.Fail && path.EndsWith(NativeSceneImportSettings.SidecarSuffix, StringComparison.Ordinal)
                ? Task.FromException<FileVersion>(new IOException("Settings write failed"))
                : this.inner.WriteAsync(path, content, expected, cancellationToken);
    }

    private sealed class FailImportBatchRunner : IContentPipelineProcessRunner
    {
        private readonly ContentPipelineProcessRunner inner = new();

        public bool Fail { get; set; } = true;

        public Task<ContentPipelineProcessResult> RunAsync(ContentPipelineProcessRequest request, CancellationToken cancellationToken)
            => this.Fail && request.Arguments.Contains("batch", StringComparer.Ordinal)
                ? Task.FromResult(new ContentPipelineProcessResult(1, string.Empty, "Simulated cook failure"))
                : this.inner.RunAsync(request, cancellationToken);
    }
}
