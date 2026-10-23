// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using Moq;
using Oxygen.Editor.ContentPipeline.Inspection;
using Oxygen.Editor.ContentPipeline.Testing;
using Oxygen.Editor.LivePreview.Scenes;
using Oxygen.Editor.Projects;
using Oxygen.Editor.Projects.Testing;
using Oxygen.Editor.Scenes.Authoring;
using Oxygen.Editor.Scenes.Edits.Assets;
using Oxygen.Editor.Scenes.Interaction;
using Oxygen.Editor.Scenes.Selection;
using Oxygen.Editor.Scenes.Testing;
using Oxygen.Editor.Shell.Testing;
using Oxygen.Editor.World;
using Oxygen.Editor.World.Components;
using Oxygen.Managed.Assets;
using Oxygen.Managed.Core;
using Testably.Abstractions.Testing;

namespace Oxygen.Editor.SceneInspector.TestSupport;

/// <summary>A scene document with a camera and sun node, open in a document area, for the views and models that edit it.</summary>
internal sealed partial class SceneAuthoringFixture : IDisposable
{
    /// <summary>Initializes a new instance of the <see cref="SceneAuthoringFixture"/> class.</summary>
    /// <param name="interaction">
    /// Creates the workspace interaction state the document's edits respect, for the fixture's project session; none
    /// when <see langword="null"/>.
    /// </param>
    public SceneAuthoringFixture(Func<IProjectSession, SceneNodeInteraction>? interaction = null)
    {
        var project = TestProject.Create(new MockFileSystem());
        this.Session = new TestSession(project.Context);
        this.Interaction = interaction?.Invoke(this.Session);
        this.PublishSurface(EngineAssets.BasicShape("Cube"));
        var scene = new Scene
        {
            Name = "UI Test Scene",
        };
        var node = new SceneNode(scene, "Camera and sun");
        _ = node.AddComponent(new PerspectiveCamera
        {
            Name = "Camera",
            NearPlane = 0.1f,
            FarPlane = 1000,
        });
        _ = node.AddComponent(new DirectionalLightComponent { Name = "Sun" });
        scene.RootNodes.Add(node);
        this.TestScene = TestScenes.Open(scene, this.Interaction, project: project);
        this.Scene = this.TestScene.Scene;
        this.Node = this.TestScene.NodeOf(node);
        this.Camera = this.Node.Components.OfType<PerspectiveCamera>().Single();
        this.Area = new TestDocumentArea(this.TestScene.Document);
    }

    /// <summary>Gets the scene's document.</summary>
    public TestScene TestScene { get; }

    /// <summary>Gets the document area that shows the scene's document, active.</summary>
    public TestDocumentArea Area { get; }

    /// <summary>Gets the scene's document, whose history the edits record into.</summary>
    public Documents.Document Document => this.TestScene.Document;

    /// <summary>Gets what edits the document's scene.</summary>
    public SceneAuthoring Authoring => this.TestScene.Authoring;

    /// <summary>Gets the project session of the scene's project, which the views under test belong to.</summary>
    public TestSession Session { get; }

    /// <summary>Gets the workspace interaction state the document's edits respect, if the fixture has one.</summary>
    public SceneNodeInteraction? Interaction { get; }

    /// <summary>Gets the shared selection authority wired into the Explorer and Inspector.</summary>
    public SceneSelectionService Selection { get; } = new();

    /// <summary>Gets the surface inventories the views read; only the geometry the fixture publishes is cooked.</summary>
    public FakeSurfaceProvider Surfaces { get; } = new();

    public Scene Scene { get; }

    public SceneNode Node { get; }

    public PerspectiveCamera Camera { get; }

    /// <summary>Gets the engine preview the views read.</summary>
    public Mock<IScenePreview> Preview { get; } = new();

    public static SurfaceTarget TargetFor(AssetPath geometry) => new(geometry, Guid.Parse("10000000-0000-0000-0000-000000000001"), new string('a', 64));

    /// <summary>Publishes the inventory of a geometry as one visible, shadow-casting surface named "Surface", the one <see cref="TargetFor"/> names.</summary>
    /// <param name="geometry">The geometry.</param>
    public void PublishSurface(AssetPath geometry)
        => this.Surfaces.Publish(new GeometrySurfaceMetadata(geometry, Guid.Parse("20000000-0000-0000-0000-000000000001"), new string('a', 64), [new(TargetFor(geometry).SurfaceId, "Surface", Visible: true, CastsShadows: true, [new(0, 0, Guid.Empty)])], SceneSchema.Geometry.Defaults.LodPolicy));

    /// <summary>Writes the node selection of the fixture's document through the shared selection service, as the Explorer, a viewport or another panel does.</summary>
    /// <param name="nodes">The selected nodes, of which the last is primary; none clears the selection.</param>
    public void Select(params SceneNode[] nodes)
        => this.Selection.SetSelection(this.TestScene.DocumentId, nodes, "Test");

    public void Dispose()
    {
        this.Session.Dispose();
        this.Surfaces.Dispose();
        this.TestScene.Dispose();
    }
}
