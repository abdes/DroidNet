// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using AwesomeAssertions;
using Oxygen.Editor.Runtime.Engine;
using Oxygen.Editor.Scenes.Edits.Assets;
using Oxygen.Editor.World;
using Oxygen.Editor.World.Components;
using Oxygen.Editor.World.Serialization;
using Oxygen.Managed.Assets;
using Oxygen.Managed.Core;
using Oxygen.Managed.Core.Diagnostics;

namespace Oxygen.Editor.LivePreview.Tests.Scenes;

public sealed partial class ScenePreviewTests
{
    [TestMethod]
    public async Task AssetFailure_PublishesCurrentOperationAndDropsStaleOrDeletedTargets()
    {
        using var fixture = new PreviewFixture();
        var (scene, _, _) = await fixture.OpenCubeAsync(0).ConfigureAwait(false);
        var node = AddCubeWithMissingMaterial(scene);
        var current = true;
        fixture.World.IsCurrentFailure = _ => current;
        _ = await fixture.Preview.SyncSceneAsync(scene, this.TestContext.CancellationToken).ConfigureAwait(false);
        var material = fixture.Requests.Last(static request => request.Command is RuntimeSetOverrides);
        var failure = new RuntimeAssetLoadFailedEventArgs(material, 71, "Asset path could not be resolved");

        fixture.World.RaiseAssetLoadFailed(failure);
        current = false;
        fixture.World.RaiseAssetLoadFailed(failure);
        current = true;
        _ = scene.RootNodes.Remove(node);
        fixture.World.RaiseAssetLoadFailed(failure);

        var result = fixture.Results.Should().ContainSingle().Subject;
        _ = result.OperationId.Should().Be(failure.Request.OperationId);
        _ = result.AffectedScope.NodeId.Should().Be(node.Id);
        _ = result.AffectedScope.SceneId.Should().Be(scene.Id);
        _ = result.Diagnostics.Should().ContainSingle().Which.Domain.Should().Be(FailureDomain.LiveSync);
    }

    /// <summary>Mask requests name the texture by its identity and report failures against the scene.</summary>
    /// <param name="mount">The authored source mount.</param>
    /// <returns>The synchronization check.</returns>
    [TestMethod]
    [DataRow("Content")]
    [DataRow("Lighting")]
    public async Task ExposureMaskUsesItsIdentityAndPublishesSceneScopedFailure(string mount)
    {
        using var fixture = new PreviewFixture();
        var generation = Path.Combine(@"C:\LivePreviewTests", ".cooked", "generations", Guid.NewGuid().ToString("N"));
        fixture.Engine.ContentStatus = new RuntimeContentSnapshot(Guid.NewGuid(), 1, RuntimeContentState.Mounted, [new(generation, RuntimeCookedRootKind.LooseFolder)]);
        var current = true;
        fixture.World.IsCurrentFailure = _ => current;
        var built = new Scene { Name = "Masked scene" };
        var mask = AssetPath.Create(mount, "Textures/Meter.otex");
        built.SetEnvironment(new SceneEnvironmentData { PostProcess = new PostProcessEnvironmentData { AutoExposureMeteringMask = mask } });
        var document = await fixture.OpenAsync(built).ConfigureAwait(false);
        var scene = PreviewFixture.SceneOf(document);

        _ = (await fixture.Preview.SyncSceneAsync(scene, this.TestContext.CancellationToken).ConfigureAwait(false)).Should().BeTrue();

        var request = fixture.Requests.Should().ContainSingle(value => value.Command is RuntimeSetEnvironment).Subject;
        var environment = (RuntimeSetEnvironment)request.Command;
        _ = environment.AutoExposureMeteringMask.Should().Be(mask);
        var failure = new RuntimeAssetLoadFailedEventArgs(request, 71, "Texture could not be loaded");
        fixture.World.RaiseAssetLoadFailed(failure);
        current = false;
        fixture.World.RaiseAssetLoadFailed(failure);

        var result = fixture.Results.Should().ContainSingle().Subject;
        _ = result.OperationId.Should().Be(request.OperationId);
        _ = result.AffectedScope.SceneId.Should().Be(scene.Id);
        _ = result.AffectedScope.NodeId.Should().BeNull();
        _ = result.AffectedScope.ComponentType.Should().Be(nameof(PostProcessEnvironmentData));
        _ = result.Diagnostics.Should().ContainSingle().Which.AffectedPath.Should().Be(mask.ToUriString());
    }

    // A cube whose one surface names a material that does not exist, so the engine reports the material's load.
    private static SceneNode AddCubeWithMissingMaterial(Scene scene)
    {
        var node = new SceneNode(scene, "Cube with a missing material");
        scene.RootNodes.Add(node);
        var target = new SurfaceTarget(EngineAssets.BasicShape("Cube"), Guid.Parse("10000000-0000-0000-0000-000000000001"), new string('a', 64));
        var geometry = new GeometryComponent { Name = "Geometry", Geometry = target.Geometry };
        geometry.Overrides = geometry.Overrides.With(SceneSchema.Geometry.Surface.Material, target.Part, target.LayoutRevision, AssetPath.Parse("/Content/Materials/Missing.omat"));
        node.Components.Add(geometry);
        return node;
    }
}
