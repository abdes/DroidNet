// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Text.Json;
using System.Text.Json.Nodes;
using AwesomeAssertions;
using Oxygen.Editor.ContentPipeline.Snapshots;
using Oxygen.Editor.Projects;
using Oxygen.Managed.Core.Diagnostics;

namespace Oxygen.Editor.ContentPipeline.Tests;

/// <summary>Verifies production cook preparation uses coherent private source files and reports later changes.</summary>
public sealed partial class ContentPipelineServiceTests
{
    /// <summary>All project mounts are captured before the first native worker can observe a later save.</summary>
    /// <returns>The asynchronous snapshot test.</returns>
    [TestMethod]
    public async Task ProjectWorkersShareOneSnapshotAcrossMounts()
    {
        using var workspace = new TempWorkspace([new("Content", "Content"), new("Extra", "Extra")]);
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
                    using var document = JsonDocument.Parse(await File.ReadAllBytesAsync(Path.Combine(execution.InputRoot, job.Source), token).ConfigureAwait(false));
                    names.Add(document.RootElement.GetProperty("name").GetString()!);
                }
            },
        };
        var service = CreateService(workspace, new CapturingSceneDescriptorGenerator(workspace, []), api);

        var result = await service.CookProjectAsync(this.TestContext.CancellationToken).ConfigureAwait(false);

        _ = result.Status.Should().Be(OperationStatus.Succeeded);
        _ = names.Should().BeEquivalentTo("Red", "Blue");
        _ = api.Executions.Should().HaveCount(2);
        _ = api.Executions.Select(static execution => execution.InputRoot).Distinct(StringComparer.Ordinal).Should().ContainSingle();
        _ = result.InputSnapshot.Should().NotBeNull();
        _ = api.Executions[0].InputRoot.Should().Be(result.InputSnapshot!.InputRoot);
        _ = result.InputSnapshot.Inputs.Count(static input => input.AssetUri is not null).Should().Be(2);
        _ = result.InputsAreCurrent.Should().BeFalse();
    }

    /// <summary>Descriptor-relative and absolute retained buffers are rewritten to their private captured copies.</summary>
    /// <param name="absolute">Whether the authored descriptor uses an absolute retained path.</param>
    /// <returns>The asynchronous snapshot test.</returns>
    [TestMethod]
    [DataRow(false)]
    [DataRow(true)]
    public async Task GeometryJobsReadCapturedBuffersAfterSourceChanges(bool absolute)
    {
        using var workspace = new TempWorkspace();
        WriteAuthoredGeometry(workspace, withBuffer: true);
        workspace.WriteMaterial("Content/Materials/Red.omat.json", "Red");
        const string source = "Content/Geometry/AuthoredCube.ogeo.json";
        if (absolute)
        {
            var geometry = JsonNode.Parse(workspace.ReadText(source))!;
            geometry["buffers"]![0]!["uri"] = Path.Combine(workspace.Root, "Content", "Geometry", "mesh.bin");
            workspace.WriteText(source, geometry.ToJsonString());
        }

        var authored = workspace.ReadText(source);
        var api = new CapturingEngineContentPipelineApi(new(workspace.Root, Succeeded: true, []), SucceededInspection(workspace))
        {
            BeforeImport = async (execution, token) =>
            {
                workspace.WriteText("Content/Geometry/mesh.bin", "changed live buffer");
                var job = execution.Manifest.Jobs.Single(static job => string.Equals(job.Type, "geometry-descriptor", StringComparison.Ordinal));
                var descriptorPath = Path.Combine(execution.InputRoot, job.Source);
                using var descriptor = JsonDocument.Parse(await File.ReadAllBytesAsync(descriptorPath, token).ConfigureAwait(false));
                var relative = descriptor.RootElement.GetProperty("buffers")[0].GetProperty("uri").GetString()!;
                var capturedBuffer = Path.GetFullPath(Path.Combine(Path.GetDirectoryName(descriptorPath)!, relative));
                _ = capturedBuffer.Should().StartWith(execution.InputRoot + Path.DirectorySeparatorChar);
                _ = (await File.ReadAllTextAsync(capturedBuffer, token).ConfigureAwait(false)).Should().Be("saved buffer bytes");
            },
        };
        var service = CreateService(workspace, new CapturingSceneDescriptorGenerator(workspace, []), api);

        var result = await service.CookAssetAsync(new("asset:///" + source), this.TestContext.CancellationToken).ConfigureAwait(false);

        _ = result.Status.Should().Be(OperationStatus.Succeeded);
        _ = result.InputsAreCurrent.Should().BeFalse();
        _ = workspace.ReadText(source).Should().Be(authored);
        _ = (await File.ReadAllTextAsync(Path.Combine(result.InputSnapshot!.InputRoot, source), this.TestContext.CancellationToken).ConfigureAwait(false)).Should().Be(authored);
    }

    /// <summary>Later unsaved edits remain dirty while a captured cook succeeds as a historical result.</summary>
    /// <returns>The asynchronous snapshot test.</returns>
    [TestMethod]
    public async Task LaterDirtyDocumentDoesNotBlockCapturedCookOrBecomeClean()
    {
        using var workspace = new TempWorkspace();
        const string source = "Content/Materials/Red.omat.json";
        workspace.WriteMaterial(source, "Red");
        var state = SavedState(Path.Combine(workspace.Root, source));
        using var registration = workspace.Documents.Register(state.SourcePath, _ => Task.FromResult<CookDocumentReadLease?>(new(state, static () => { })));
        var api = new CapturingEngineContentPipelineApi(new(workspace.Root, Succeeded: true, []), SucceededInspection(workspace))
        {
            BeforeImport = (_, _) =>
            {
                state = state with { Revision = 2, IsDirty = true };
                return Task.CompletedTask;
            },
        };
        var service = CreateService(workspace, new CapturingSceneDescriptorGenerator(workspace, []), api);

        var result = await service.CookAssetAsync(new("asset:///" + source), this.TestContext.CancellationToken).ConfigureAwait(false);

        _ = result.Status.Should().Be(OperationStatus.Succeeded);
        _ = result.InputSnapshot!.Documents.Should().ContainSingle().Which.SavedRevision.Should().Be(1);
        _ = result.InputsAreCurrent.Should().BeFalse();
        _ = state.IsDirty.Should().BeTrue();
        _ = state.Revision.Should().Be(2);
    }

    /// <summary>New folder assets make the completed captured scope visibly out of date.</summary>
    /// <returns>The asynchronous snapshot test.</returns>
    [TestMethod]
    public async Task NewFolderInputDoesNotBecomePartOfAnAlreadyCapturedCook()
    {
        using var workspace = new TempWorkspace();
        workspace.WriteMaterial("Content/Materials/Red.omat.json", "Red");
        var api = new CapturingEngineContentPipelineApi(new(workspace.Root, Succeeded: true, []), SucceededInspection(workspace))
        {
            BeforeImport = (_, _) =>
            {
                workspace.WriteMaterial("Content/Materials/Blue.omat.json", "Blue");
                return Task.CompletedTask;
            },
        };
        var service = CreateService(workspace, new CapturingSceneDescriptorGenerator(workspace, []), api);
        var result = await service.CookFolderAsync(new("asset:///Content/Materials"), this.TestContext.CancellationToken).ConfigureAwait(false);
        _ = result.InputSnapshot!.Inputs.Count(static input => input.AssetUri is not null).Should().Be(1);
        _ = result.InputsAreCurrent.Should().BeFalse();
    }

    /// <summary>Captured producer ownership remains held until failed native work and cleanup finish.</summary>
    /// <returns>The asynchronous ownership test.</returns>
    [TestMethod]
    public async Task SnapshotProducerLeaseDrainsBeforeTheNextWriterStarts()
    {
        using var workspace = new TempWorkspace();
        workspace.WriteMaterial("Content/Materials/Red.omat.json", "Red");
        var drain = new TaskCompletionSource(TaskCreationOptions.RunContinuationsAsynchronously);
        var api = new CapturingEngineContentPipelineApi(new(workspace.Root, Succeeded: true, []), SucceededInspection(workspace))
        {
            BeforeImport = (_, _) => Task.FromException(new ContentPipelineTerminationException(new IOException("Termination failed"), drain.Task)),
        };
        var service = CreateService(workspace, new CapturingSceneDescriptorGenerator(workspace, []), api);
        Func<Task> cook = () => service.CookAssetAsync(new("asset:///Content/Materials/Red.omat.json"), this.TestContext.CancellationToken);
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
}
