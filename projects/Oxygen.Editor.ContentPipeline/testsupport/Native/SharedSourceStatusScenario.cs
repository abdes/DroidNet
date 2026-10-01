// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Diagnostics;
using System.Text.Json.Nodes;
using AwesomeAssertions;
using Microsoft.Extensions.Logging.Abstractions;
using Oxygen.Editor.ContentPipeline.Status;
using static Oxygen.Editor.ContentPipeline.TestSupport.CookScenario;
using static Oxygen.Editor.ContentPipeline.TestSupport.RetainedModelScenario;

namespace Oxygen.Editor.ContentPipeline.TestSupport;

internal static class SharedSourceStatusScenario
{
    internal static async Task<TimeSpan> RunAsync(TestContext testContext, int meshCount)
    {
        using var workspace = new CookWorkspace();
        var source = await WriteRetainedModelAsync(workspace, "Many", "gltf", testContext.CancellationToken).ConfigureAwait(false);
        var path = Path.Combine(workspace.Root, "Content/SourceMedia/DCC/Many/model.gltf");
        var model = JsonNode.Parse(await File.ReadAllTextAsync(path, testContext.CancellationToken).ConfigureAwait(false))!;
        var prototype = model["meshes"]![0]!.DeepClone();
        var meshes = new JsonArray();
        var nodes = new JsonArray();
        var sceneNodes = new JsonArray();
        for (var index = 0; index < meshCount; index++)
        {
            var mesh = prototype.DeepClone();
            var name = "Part" + index.ToString("D3", System.Globalization.CultureInfo.InvariantCulture);
            mesh["name"] = name;
            meshes.Add(mesh);
            nodes.Add(new JsonObject { ["name"] = name, ["mesh"] = index });
            sceneNodes.Add(index);
        }

        model["meshes"] = meshes;
        model["nodes"] = nodes;
        model["scenes"]![0]!["nodes"] = sceneNodes;
        await File.WriteAllTextAsync(path, model.ToJsonString(), testContext.CancellationToken).ConfigureAwait(false);
        using var compatibility = Oxygen.Testing.TemporaryNativeArtifacts.ForInstalledEngine();
        var runner = new CountingSourceRunner();
        var api = new ImportToolContentPipelineApi(new EngineContentPipelineToolLocator(), runner, NullLogger<ImportToolContentPipelineApi>.Instance, compatibility);
        var service = CreateService(workspace, new SceneDescriptorGenerator(new ProceduralGeometryDescriptorService(api)), api, compatibility);
        var cooked = await service.CookAssetAsync(source, testContext.CancellationToken).ConfigureAwait(false);
        _ = cooked.IsPublished.Should().BeTrue(string.Join(Environment.NewLine, cooked.Diagnostics.Select(static issue => issue.Message)));
        _ = cooked.CookedAssets.Count(static asset => asset.Kind == ContentCookAssetKind.Geometry).Should().Be(meshCount);
        var geometry = cooked.CookedAssets.First(static asset => asset.Kind == ContentCookAssetKind.Geometry);
        var missing = new Uri(geometry.CookedAssetUri, "Missing.ogeo");
        var requested = cooked.CookedAssets.Select(static asset => asset.CookedAssetUri).Prepend(source).Append(missing).ToArray();
        var workers = runner.Count;
        var watch = Stopwatch.StartNew();
        var states = await service.ReadAsync(workspace.ProjectContext, requested, testContext.CancellationToken).ConfigureAwait(false);
        watch.Stop();
        _ = states.Select(static state => state.AssetUri).Should().BeEquivalentTo(requested);
        _ = states.Where(state => state.AssetUri != missing).Should().OnlyContain(static state => state.Freshness == AssetCookFreshness.Current && state.HasAvailableOutput);
        _ = states.Single(state => state.AssetUri == missing).HasPublishedOutput.Should().BeFalse();
        _ = states.Single(state => state.AssetUri == missing).Freshness.Should().Be(AssetCookFreshness.NeedsCooking);
        _ = runner.Count.Should().Be(workers);
        return watch.Elapsed;
    }
}
