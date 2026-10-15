// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Numerics;
using System.Text.Json.Nodes;
using AwesomeAssertions;
using Oxygen.Editor.Assets;
using Oxygen.Editor.ContentPipeline;
using Oxygen.Editor.ContentPipeline.Import;
using Oxygen.Editor.WorldEditor.Documents.Commands;
using Oxygen.Editor.WorldEditor.TestSupport;
using static Oxygen.Editor.WorldEditor.TestSupport.ModelImportScenario;
using static Oxygen.Editor.WorldEditor.TestSupport.NativeSceneAssertions;
using static Oxygen.Editor.WorldEditor.TestSupport.NativeSceneData;
using static Oxygen.Editor.WorldEditor.TestSupport.PublicationWorkflows;

namespace Oxygen.Editor.WorldEditor.Integration.UI.Tests.Workspace;

/// <summary>Checks that replacing an imported model's source reaches the open scene's existing consumers.</summary>
[TestClass]
public sealed class ImportReplacementSyncTests : DroidNet.Tests.VisualUserInterfaceTests
{
    public TestContext TestContext { get; set; } = null!;

    /// <summary>A replaced source updates the material its consumer already uses, without an edit, history entry or reload.</summary>
    /// <param name="format">The model source format.</param>
    /// <returns>The asynchronous native replacement journey.</returns>
    [TestMethod]
    [DataRow("gltf")]
    [DataRow("fbx")]
    public Task ReplacedImportUpdatesItsLiveMaterialConsumer(string format) => EnqueueAsync(async () =>
    {
        var fixture = new NativeSceneFixture(automatic: false, scene => AddGeometryNode(scene, "Cube"));
        await using var lifetime = fixture.ConfigureAwait(true);
        using var timeout = CancellationTokenSource.CreateLinkedTokenSource(this.TestContext.CancellationToken);
        timeout.CancelAfter(TimeSpan.FromSeconds(60));
        using var services = new CatalogWorkloadServices(fixture);
        await using var catalog = new AssetCatalog(services.Projects, services.Storage, services.Builtins, services.Publication);
        await fixture.InitializeAsync(timeout.Token).ConfigureAwait(true);
        using var publication = fixture.RegisterWorkspacePublication(services, catalog);

        var first = await ImportTypedModelAsync(fixture, services, format, timeout.Token).ConfigureAwait(true);
        var material = first.CookedAssets.Single(static asset => asset.Kind == ContentCookAssetKind.Material).CookedAssetUri;
        var node = fixture.Source.RootNodes[0].Id;
        var slot = await fixture.ReadSingleMaterialSlotAsync(node, timeout.Token).ConfigureAwait(true);
        _ = (await fixture.Commands.EditMaterialSlotAsync(fixture.Context, [node], slot, material, EditSessionToken.OneShot).ConfigureAwait(true)).Succeeded.Should().BeTrue();
        var before = await WaitForNodeAsync(fixture, node, static value => value.MaterialBaseColors.Length == 1 && Vector4.Distance(value.MaterialBaseColors[0], new(0.8f, 0.2f, 0.1f, 1)) < 0.001f, timeout.Token).ConfigureAwait(true);
        var revision = fixture.Context.Metadata.ChangeVersion;
        var history = fixture.Context.History.UndoStack.Count;
        var dirty = fixture.Context.Metadata.IsDirty;

        var path = Path.Combine(fixture.ProjectRoot, "Incoming", "Triangle." + format);
        await File.WriteAllTextAsync(path, Recolor(await File.ReadAllTextAsync(path, timeout.Token).ConfigureAwait(true), format), timeout.Token).ConfigureAwait(true);
        var project = services.Projects.ActiveProject!;
        var review = await SceneImportReplacement.ReviewAsync(project, "Triangle", timeout.Token).ConfigureAwait(true);
        var replaced = await services.Pipeline.ImportSourceAsync(new(project, path, "Triangle", new("asset:///Content/Models")) { Replacement = review }, timeout.Token).ConfigureAwait(true);
        _ = replaced.IsPublished.Should().BeTrue(string.Join(Environment.NewLine, replaced.Diagnostics.Select(static issue => issue.Message)));
        _ = replaced.CookedAssets.Select(static asset => asset.CookedAssetUri).Should().BeEquivalentTo(first.CookedAssets.Select(static asset => asset.CookedAssetUri));

        var after = await WaitForNodeAsync(fixture, node, static value => value.MaterialBaseColors.Length == 1 && Vector4.Distance(value.MaterialBaseColors[0], new(0.1f, 0.8f, 0.2f, 1)) < 0.001f, timeout.Token).ConfigureAwait(true);
        _ = after.MaterialKeys.Should().Equal(before.MaterialKeys);
        _ = fixture.Context.Metadata.ChangeVersion.Should().Be(revision);
        _ = fixture.Context.History.UndoStack.Should().HaveCount(history);
        _ = fixture.Context.Metadata.IsDirty.Should().Be(dirty);
    });

    private static string Recolor(string source, string format)
    {
        if (string.Equals(format, "gltf", StringComparison.Ordinal))
        {
            var document = JsonNode.Parse(source)!;
            document["materials"]![0]!["pbrMetallicRoughness"]!["baseColorFactor"] = new JsonArray(0.1f, 0.8f, 0.2f, 1f);
            return document.ToJsonString();
        }

        return source.Replace("0.8,0.2,0.1", "0.1,0.8,0.2", StringComparison.Ordinal);
    }
}
