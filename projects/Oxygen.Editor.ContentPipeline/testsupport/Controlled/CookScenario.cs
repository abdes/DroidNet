// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Diagnostics.CodeAnalysis;
using System.Text.Json.Nodes;
using AwesomeAssertions;
using Microsoft.Extensions.Logging.Abstractions;
using Oxygen.Editor.ContentPipeline.Import;
using Oxygen.Editor.ContentPipeline.Publication;
using Oxygen.Editor.ContentPipeline.Status;
using Oxygen.Editor.Projects;
using Oxygen.Editor.World.Serialization;
using Oxygen.Editor.World;
using Oxygen.Managed.Core.Diagnostics;
using static Oxygen.Editor.ContentPipeline.TestSupport.RetainedModelScenario;

namespace Oxygen.Editor.ContentPipeline.TestSupport;

internal static class CookScenario
{
    internal static async Task<Uri> WriteCrossMountModelAsync(CookWorkspace workspace, CancellationToken cancellationToken)
    {
        var source = await WriteRetainedModelAsync(workspace, "Model", "gltf", cancellationToken).ConfigureAwait(false);
        var path = Path.Combine(workspace.Root, "Content/SourceMedia/DCC/Model/model.gltf.import.json");
        var settings = NativeSceneImportSettings.Parse(await File.ReadAllBytesAsync(path, cancellationToken).ConfigureAwait(false));
        await File.WriteAllBytesAsync(path, (settings with { MountPoint = "Art" }).ToBytes(), cancellationToken).ConfigureAwait(false);
        return source;
    }

    internal static ContentPipelineService CreateService(CookWorkspace workspace, ISceneDescriptorGenerator generator, IEngineContentPipelineApi api, Oxygen.Managed.Core.Compatibility.INativeCompatibilityService? compatibility = null, CookPublicationService? publication = null) => new(workspace.ContextService, workspace.CookCoordinator, new FixedCookScopeProvider(workspace.Root), generator, new ContentImportManifestBuilder(), new ContentImportManifestValidator(), api, workspace.Documents, compatibility ?? workspace.Compatibility, workspace.Files, publication ?? workspace.Publication);
    internal static CookInspectionResult SucceededInspection(CookWorkspace workspace) => new(workspace.Root, Succeeded: true, SourceIdentity: null, Assets: [], Files: [], Diagnostics: []);
    internal async static Task AssertReaderRetainedUntilDrainAsync(TestContext testContext, Task work, CookWorkspace consumer, Action openForWrite, TaskCompletionSource drain)
    {
        Func<Task> observe = () => work.WaitAsync(TimeSpan.FromSeconds(5), testContext.CancellationToken);
        var failure = await observe.Should().ThrowAsync<ContentPipelineTerminationException>().ConfigureAwait(false);
        _ = openForWrite.Should().Throw<IOException>();
        var next = consumer.CookCoordinator.RunAsync((_, _) =>
        {
            openForWrite();
            return Task.FromResult(true);
        }, testContext.CancellationToken);
        _ = next.IsCompleted.Should().BeFalse();
        drain.SetResult();
        await failure.Which.DrainCompletion.WaitAsync(TimeSpan.FromSeconds(5), testContext.CancellationToken).ConfigureAwait(false);
        _ = await next.WaitAsync(TimeSpan.FromSeconds(5), testContext.CancellationToken).ConfigureAwait(false);
    }

    internal static void WriteMaterialRoughness(CookWorkspace workspace, string path, double value)
    {
        var source = JsonNode.Parse(workspace.ReadText(path))!;
        source["parameters"]!["roughness"] = value;
        workspace.WriteText(path, source.ToJsonString());
    }
}
