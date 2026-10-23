// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using AwesomeAssertions;
using Oxygen.Editor.LivePreview.Scenes;
using Oxygen.Editor.LivePreview.Tests.TestSupport;
using Oxygen.Editor.Runtime.Engine;
using Oxygen.Editor.Scenes.Edits.Assets;
using Oxygen.Editor.World;
using Oxygen.Editor.World.Components;
using Oxygen.Editor.World.Serialization;
using Oxygen.Managed.Assets;
using Oxygen.Managed.Core;

namespace Oxygen.Editor.LivePreview.Tests.Scenes;

public sealed partial class ScenePreviewTests
{
    [TestMethod]
    [DataRow(RuntimeCommandStatus.Accepted, nameof(SyncStatus.Accepted))]
    [DataRow(RuntimeCommandStatus.Rejected, nameof(SyncStatus.Rejected))]
    [DataRow(RuntimeCommandStatus.Unavailable, nameof(SyncStatus.SkippedNotRunning))]
    [DataRow(RuntimeCommandStatus.Cancelled, nameof(SyncStatus.Cancelled))]
    [DataRow(RuntimeCommandStatus.Failed, nameof(SyncStatus.Failed))]
    public async Task EngineOutcome_DrivesThePropertyOutcome(RuntimeCommandStatus runtimeStatus, string expected)
    {
        using var fixture = new PreviewFixture();
        var (scene, node, _) = await fixture.OpenCubeAsync(0).ConfigureAwait(false);
        _ = (await fixture.Preview.SyncSceneAsync(scene, this.TestContext.CancellationToken).ConfigureAwait(false)).Should().BeTrue();
        RuntimeWorldRequest? captured = null;
        fixture.World.OnExecute = request =>
        {
            captured = request;
            return FakeWorld.Result(request, runtimeStatus, "Controlled outcome");
        };

        var outcome = await fixture.Preview.UpdatePropertiesAsync(scene, node, [CreateTransformEntry(42.5f)], cancellationToken: this.TestContext.CancellationToken).ConfigureAwait(false);

        _ = outcome.Status.Should().Be(Enum.Parse<SyncStatus>(expected));
        _ = captured.Should().NotBeNull();
        _ = captured!.Target.SceneId.Should().Be(scene.Id);
        _ = captured.Target.DocumentLifetime.Should().NotBeEmpty();
        _ = captured.Target.ActivationId.Should().NotBeEmpty();
        var command = captured.Command.Should().BeOfType<RuntimeSetProperties>().Subject;
        _ = command.NodeId.Should().Be(node.Id);
        _ = command.Entries.Should().ContainSingle().Which.Value.Should().Be(42.5f);
        _ = outcome.Scope.NodeId.Should().Be(node.Id);
    }

    [TestMethod]
    [DataRow(CameraAspectMode.Auto)]
    [DataRow(CameraAspectMode.Fixed)]
    public async Task SceneProjection_PreservesCameraUnits(CameraAspectMode aspectMode)
    {
        using var fixture = new PreviewFixture();
        var built = new Scene { Name = "Test Scene" };
        var camera = new SceneNode(built, "Camera");
        built.RootNodes.Add(camera);
        camera.Components.Add(new PerspectiveCamera { Name = "Perspective Camera", FieldOfView = 60, AspectMode = aspectMode, AspectRatio = 1.5f, NearPlane = 0.25f, FarPlane = 500 });
        var document = await fixture.OpenAsync(built).ConfigureAwait(false);

        _ = (await fixture.Preview.SyncSceneAsync(PreviewFixture.SceneOf(document), this.TestContext.CancellationToken).ConfigureAwait(false)).Should().BeTrue();

        var attached = fixture.World.Commands.OfType<RuntimeAttachPerspectiveCamera>().Should().ContainSingle().Subject;
        _ = attached.NodeId.Should().Be(camera.Id);
        _ = attached.FieldOfViewYRadians.Should().BeApproximately(MathF.PI / 3, 0.00001f);
        _ = attached.AspectRatio.Should().Be(1.5f);
        _ = attached.AspectMode.Should().Be(aspectMode);
        _ = attached.NearPlane.Should().Be(0.25f);
        _ = attached.FarPlane.Should().Be(500);
    }

    [TestMethod]
    public async Task SceneProjection_AttachesOrthographicCameraAndProjectsAuthoredFlags()
    {
        using var fixture = new PreviewFixture();
        var built = new Scene { Name = "Test Scene" };
        var parent = new SceneNode(built, "Hidden parent");
        parent.Flags.Visibility = VisibilitySource.Hidden;
        parent.Flags.CastsShadows = ShadowSource.On;
        parent.Flags.ReceivesShadows = ShadowSource.Off;
        var child = new SceneNode(built, "Shown child");
        child.Flags.Visibility = VisibilitySource.Shown;
        parent.Hierarchy.AddChild(child);
        built.RootNodes.Add(parent);
        child.Components.Add(new OrthographicCamera
        {
            Name = "Top",
            OrthographicSize = 5f,
            AspectRatio = 2f,
            AspectMode = CameraAspectMode.Fixed,
            NearPlane = 0.5f,
            FarPlane = 50f,
        });
        var document = await fixture.OpenAsync(built).ConfigureAwait(false);

        _ = (await fixture.Preview.SyncSceneAsync(PreviewFixture.SceneOf(document), this.TestContext.CancellationToken).ConfigureAwait(false)).Should().BeTrue();

        var camera = fixture.World.Commands.OfType<RuntimeAttachOrthographicCamera>().Should().ContainSingle().Subject;
        _ = camera.NodeId.Should().Be(child.Id);
        _ = camera.OrthographicSize.Should().Be(5f);
        _ = camera.AspectRatio.Should().Be(2f);
        _ = camera.AspectMode.Should().Be(CameraAspectMode.Fixed);
        _ = camera.NearPlane.Should().Be(0.5f);
        _ = camera.FarPlane.Should().Be(50f);
        // Source modes go as their wire values: Inherit 0, the local on value 1 and the local off value 2.
        _ = Flags(parent.Id).Should().Equal(2f, 1f, 2f, 0f, 1f, 0f);
        _ = Flags(child.Id).Should().Equal(1f, 0f, 0f, 0f, 1f, 0f);

        IEnumerable<float> Flags(NodeId nodeId)
            => fixture.World.PropertiesOf(nodeId, RuntimeComponentId.Node).OrderBy(static entry => entry.FieldId).Select(static entry => entry.Value);
    }

    [TestMethod]
    public async Task SceneProjection_SendsAllSixAuthoredNodeFlags()
    {
        using var fixture = new PreviewFixture();
        var built = new Scene { Name = "Test Scene" };
        var node = new SceneNode(built, "Flagged");
        node.Flags.IsStatic = true;
        node.Flags.IsRayCastingSelectable = false;
        node.Flags.IgnoreParentTransform = true;
        built.RootNodes.Add(node);
        var document = await fixture.OpenAsync(built).ConfigureAwait(false);

        _ = (await fixture.Preview.SyncSceneAsync(PreviewFixture.SceneOf(document), this.TestContext.CancellationToken).ConfigureAwait(false)).Should().BeTrue();

        var sent = fixture.World.PropertiesOf(node.Id, RuntimeComponentId.Node).OrderBy(static entry => entry.FieldId).ToArray();
        _ = sent.Select(static entry => entry.FieldId).Should().Equal(
            (ushort)NodeField.Visible,
            (ushort)NodeField.CastsShadows,
            (ushort)NodeField.ReceivesShadows,
            (ushort)NodeField.Static,
            (ushort)NodeField.RayCastingSelectable,
            (ushort)NodeField.IgnoreParentTransform);
        _ = sent.Select(static entry => entry.Value).Should().Equal(0f, 0f, 0f, 1f, 0f, 1f);
    }

    [TestMethod]
    public async Task SavedMaterialProjection_UsesRetainedIdentity()
    {
        using var fixture = new PreviewFixture();
        var built = new Scene { Name = "Test Scene" };
        var node = new SceneNode(built, "Saved geometry");
        built.RootNodes.Add(node);
        var target = new SurfaceTarget(
            EngineAssets.BasicShape("Cube"),
            Guid.Parse("10000000-0000-0000-0000-000000000001"),
            new string('a', 64));
        var geometry = new GeometryComponent { Name = "Geometry", Geometry = target.Geometry };
        geometry.Overrides = geometry.Overrides.With(SceneSchema.Geometry.Surface.Material, target.Part, target.LayoutRevision, AssetPath.Parse("/Content/Materials/Saved.omat"));
        node.Components.Add(geometry);
        var document = await fixture.OpenAsync(built).ConfigureAwait(false);

        _ = (await fixture.Preview.SyncSceneAsync(PreviewFixture.SceneOf(document), this.TestContext.CancellationToken).ConfigureAwait(false)).Should().BeTrue();

        var projected = fixture.World.Commands.OfType<RuntimeSetOverrides>().Should().ContainSingle().Which;
        _ = projected.Intent.Should().Be(OverrideIntent.RetainedOverride);
        _ = projected.Part.Should().Be(new RuntimeOverridePart(target.SurfaceId, target.LayoutRevision));
    }

    [TestMethod]
    public async Task OverrideProjection_SendsEachPartWithItsWholeState()
    {
        using var fixture = new PreviewFixture();
        var built = new Scene { Name = "Test Scene" };
        var node = new SceneNode(built, "Overridden geometry");
        built.RootNodes.Add(node);
        var target = new SurfaceTarget(
            EngineAssets.BasicShape("Cube"),
            Guid.Parse("10000000-0000-0000-0000-000000000001"),
            new string('a', 64));
        var policy = new LodPolicy.Distance([12f, 30f], 0.1f);
        var geometry = new GeometryComponent { Name = "Geometry", Geometry = target.Geometry };
        geometry.Overrides = geometry.Overrides
            .With(SceneSchema.Geometry.LodPolicy, (LodPolicy)policy)
            .With(SceneSchema.Geometry.Surface.Visibility, target.Part, target.LayoutRevision, false)
            .With(SceneSchema.Geometry.Surface.CastsShadows, target.Part, target.LayoutRevision, true)
            .With(SceneSchema.Geometry.Surface.Material, target.Part, target.LayoutRevision, AssetPath.Parse("/Content/Materials/Saved.omat"));
        node.Components.Add(geometry);
        var document = await fixture.OpenAsync(built).ConfigureAwait(false);

        _ = (await fixture.Preview.SyncSceneAsync(PreviewFixture.SceneOf(document), this.TestContext.CancellationToken).ConfigureAwait(false)).Should().BeTrue();

        var sent = fixture.World.Commands.OfType<RuntimeSetOverrides>().ToList();
        _ = sent.Should().HaveCount(2);
        var component = sent.Should().ContainSingle(static command => command.Part == null).Which;
        _ = component.Overrides.Should().Equal(
            new RuntimeOverride(RuntimeOverrideId.GeometryLodPolicy, new RuntimeOverrideValue.LodPolicyValue(policy)));
        var surface = sent.Should().ContainSingle(static command => command.Part != null).Which;
        _ = surface.Part.Should().Be(new RuntimeOverridePart(target.SurfaceId, target.LayoutRevision));
        _ = surface.Overrides.Should().BeEquivalentTo(
        [
            new RuntimeOverride(RuntimeOverrideId.GeometrySurfaceMaterial, new RuntimeOverrideValue.Asset("/Content/Materials/Saved.omat")),
            new RuntimeOverride(RuntimeOverrideId.GeometrySurfaceVisibility, new RuntimeOverrideValue.Flag(false)),
            new RuntimeOverride(RuntimeOverrideId.GeometrySurfaceCastsShadows, new RuntimeOverrideValue.Flag(true)),
        ]);
    }
}
