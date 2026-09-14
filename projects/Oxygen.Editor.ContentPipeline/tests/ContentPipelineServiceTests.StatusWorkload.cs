// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Diagnostics;
using System.Text.Json.Nodes;
using AwesomeAssertions;
using Microsoft.Extensions.Logging.Abstractions;
using Oxygen.Editor.ContentPipeline.Status;

namespace Oxygen.Editor.ContentPipeline.Tests;

/// <summary>Exercises one retained model owning many independently selectable native outputs.</summary>
public sealed partial class ContentPipelineServiceTests
{
    /// <summary>Bulk source/output status retains per-output availability while sharing source evaluation.</summary>
    /// <returns>The asynchronous native-backed status workload.</returns>
    [TestMethod]
    [TestCategory("NativeContent")]
    [TestCategory("Performance")]
    public async Task ImportedModelBulkStatusPreservesIndividualOutputAvailability()
    {
        using var workspace = new TempWorkspace();
        var source = await WriteRetainedModelAsync(workspace, "Many", "gltf", this.TestContext.CancellationToken).ConfigureAwait(false);
        var path = Path.Combine(workspace.Root, "Content/SourceMedia/DCC/Many/model.gltf");
        var model = JsonNode.Parse(await File.ReadAllTextAsync(path, this.TestContext.CancellationToken).ConfigureAwait(false))!;
        var prototype = model["meshes"]![0]!.DeepClone();
        var meshes = new JsonArray();
        var nodes = new JsonArray();
        var sceneNodes = new JsonArray();
        for (var index = 0; index < 256; index++)
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
        await File.WriteAllTextAsync(path, model.ToJsonString(), this.TestContext.CancellationToken).ConfigureAwait(false);
        using var compatibility = Oxygen.Testing.TemporaryNativeArtifacts.ForInstalledEngine();
        var runner = new CountingSourceRunner();
        var api = new ImportToolContentPipelineApi(new EngineContentPipelineToolLocator(), runner, NullLogger<ImportToolContentPipelineApi>.Instance, compatibility);
        var service = CreateService(workspace, new SceneDescriptorGenerator(new ProceduralGeometryDescriptorService(api)), api, compatibility);
        var cooked = await service.CookAssetAsync(source, this.TestContext.CancellationToken).ConfigureAwait(false);
        _ = cooked.IsPublished.Should().BeTrue(string.Join(Environment.NewLine, cooked.Diagnostics.Select(static issue => issue.Message)));
        _ = cooked.CookedAssets.Count(static asset => asset.Kind == ContentCookAssetKind.Geometry).Should().Be(256);
        var missing = new Uri("asset:///Content/Models/Many/Geometry/model/Missing.ogeo");
        var requested = cooked.CookedAssets.Select(static asset => asset.CookedAssetUri).Prepend(source).Append(missing).ToArray();
        var workers = runner.Count;
        var watch = Stopwatch.StartNew();
        var states = await service.ReadAsync(workspace.ProjectContext, requested, this.TestContext.CancellationToken).ConfigureAwait(false);
        this.TestContext.WriteLine(string.Create(System.Globalization.CultureInfo.InvariantCulture, $"Shared-source status: outputs={cooked.CookedAssets.Count}; elapsed={watch.Elapsed.TotalMilliseconds:F2}ms"));
        _ = states.Select(static state => state.AssetUri).Should().BeEquivalentTo(requested);
        _ = states.Where(state => state.AssetUri != missing).Should().OnlyContain(static state => state.Freshness == AssetCookFreshness.Current && state.HasVerifiedOutput);
        _ = states.Single(state => state.AssetUri == missing).HasPublishedOutput.Should().BeFalse();
        _ = states.Single(state => state.AssetUri == missing).Freshness.Should().Be(AssetCookFreshness.NeedsCooking);
        _ = runner.Count.Should().Be(workers);
    }
}
