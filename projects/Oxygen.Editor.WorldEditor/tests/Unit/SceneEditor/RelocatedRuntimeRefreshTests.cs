// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using AwesomeAssertions;
using Moq;
using Oxygen.Editor.Projects;
using Oxygen.Editor.World;
using Oxygen.Editor.World.Components;
using Oxygen.Editor.World.SceneEditor;
using Oxygen.Editor.WorldEditor.Documents.Commands;
using Oxygen.Managed.Assets.Model;

namespace Oxygen.Editor.WorldEditor.Unit.Tests.SceneEditor;

[TestClass]
public sealed class RelocatedRuntimeRefreshTests
{
    private static readonly Uri Box = new("asset:///Content/Geometry/Box.ogeo.json");
    private static readonly Uri Crate = new("asset:///Content/Geometry/Crate.ogeo.json");

    /// <summary>A relocated node waits until every asset it references is mounted, then is taken once.</summary>
    [TestMethod]
    public void NodeIsTakenOnceItsAssetsAreMounted()
    {
        var scene = CreateScene();
        var box = AddGeometry(scene, Box);
        var crate = AddGeometry(scene, Crate);
        var refresh = new RelocatedRuntimeRefresh();
        refresh.Add(new SceneRelocationResult(Succeeded: true, [box.Id, crate.Id], RuntimeEnvironment: false));
        var mounted = new HashSet<Uri> { Box };

        var first = refresh.TakeReady(scene, mounted.Contains);
        _ = first.Nodes.Should().Equal(box.Id);
        _ = refresh.IsPending.Should().BeTrue("the crate is not mounted yet");

        _ = mounted.Add(Crate);
        var second = refresh.TakeReady(scene, mounted.Contains);
        _ = second.Nodes.Should().Equal(crate.Id);
        _ = refresh.IsPending.Should().BeFalse();
    }

    /// <summary>A node removed from the scene while it waited is dropped, not refreshed.</summary>
    [TestMethod]
    public void RemovedNodeIsDropped()
    {
        var scene = CreateScene();
        var box = AddGeometry(scene, Box);
        var refresh = new RelocatedRuntimeRefresh();
        refresh.Add(new SceneRelocationResult(Succeeded: true, [box.Id], RuntimeEnvironment: false));
        _ = scene.RootNodes.Remove(box);

        var ready = refresh.TakeReady(scene, static _ => false);

        _ = ready.Nodes.Should().BeEmpty();
        _ = refresh.IsPending.Should().BeFalse();
    }

    /// <summary>An environment with no referenced assets is ready at once.</summary>
    [TestMethod]
    public void EnvironmentWithoutAssetsIsReady()
    {
        var scene = CreateScene();
        var refresh = new RelocatedRuntimeRefresh();
        refresh.Add(new SceneRelocationResult(Succeeded: true, [], RuntimeEnvironment: true));

        var ready = refresh.TakeReady(scene, static _ => false);

        _ = ready.Environment.Should().BeTrue();
        _ = refresh.IsPending.Should().BeFalse();
    }

    private static Scene CreateScene()
    {
        var project = new Mock<IProject>();
        _ = project.SetupGet(value => value.ProjectInfo).Returns(new ProjectInfo("Relocated runtime tests", Category.Games, Path.Combine(Path.GetTempPath(), "Oxygen-Relocated-Runtime-Tests")));
        return new Scene(project.Object) { Name = "Main" };
    }

    private static SceneNode AddGeometry(Scene scene, Uri geometry)
    {
        var node = new SceneNode(scene) { Name = Path.GetFileName(geometry.AbsolutePath) };
        _ = node.AddComponent(new GeometryComponent { Name = "Geometry", Geometry = new AssetReference<GeometryAsset>(geometry) });
        scene.RootNodes.Add(node);
        return node;
    }
}
