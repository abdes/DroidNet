// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Text.Json.Nodes;
using AwesomeAssertions;
using Oxygen.Editor.ContentPipeline.Publication;
using Oxygen.Managed.Core;
using static Oxygen.Editor.ContentPipeline.TestSupport.RetainedModelScenario;

namespace Oxygen.Editor.ContentPipeline.TestSupport;

internal static class CookScenario
{
    /// <summary>Writes an imported model in the <c>Art</c> mount, beside scenes authored in <c>Content</c>.</summary>
    /// <param name="workspace">A workspace with <c>Content</c> and <c>Art</c> mounts.</param>
    /// <param name="cancellationToken">Cancels the writes.</param>
    /// <returns>The model's identity.</returns>
    internal static Task<AssetPath> WriteCrossMountModelAsync(CookWorkspace workspace, CancellationToken cancellationToken)
        => WriteRetainedModelAsync(workspace, "Model", "gltf", cancellationToken, mount: "Art");

    /// <summary>Creates the content pipeline service of a workspace over a native pipeline API.</summary>
    /// <param name="workspace">The workspace whose project is cooked.</param>
    /// <param name="api">The native pipeline API, real or controlled.</param>
    /// <param name="generator">Writes scene descriptors; the real generator when none is given.</param>
    /// <param name="compatibility">Checks the native tools; the workspace's own when none is given.</param>
    /// <param name="publication">Publishes the cooked roots; the workspace's own when none is given.</param>
    /// <returns>The service.</returns>
    internal static ContentPipelineService CreateService(CookWorkspace workspace, IEngineContentPipelineApi api, ISceneDescriptorGenerator? generator = null, Managed.Core.Compatibility.INativeCompatibilityService? compatibility = null, CookPublicationService? publication = null) => new(workspace.ContextService, workspace.CookCoordinator, new FixedCookScopeProvider(workspace.Root), generator ?? new SceneDescriptorGenerator(), new ContentImportManifestBuilder(), new ContentImportManifestValidator(), api, compatibility ?? workspace.Compatibility, workspace.Files, publication ?? workspace.Publication, workspace.EngineContent);

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
