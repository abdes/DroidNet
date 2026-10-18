// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using CommunityToolkit.Mvvm.Messaging;
using Moq;
using Oxygen.Editor.ContentPipeline.Inspection;
using Oxygen.Editor.LivePreview.Scenes;
using Oxygen.Editor.Projects;
using Oxygen.Editor.Scenes.Authoring;
using Oxygen.Editor.Scenes.Selection;
using Oxygen.Editor.World;
using Oxygen.Editor.World.Components;
using Oxygen.Editor.World.Slots;
using Oxygen.Editor.World.Workspace;
using Oxygen.Managed.Core;

namespace Oxygen.Editor.WorldEditor.TestSupport;

/// <summary>A scene document with a camera and sun node, open in a document area, for the views and models that edit it.</summary>
internal sealed partial class SceneAuthoringFixture : IDisposable
{
    private readonly WorkspaceInteractionService? interaction;

    /// <summary>Initializes a new instance of the <see cref="SceneAuthoringFixture"/> class.</summary>
    /// <param name="interaction">Optional workspace interaction state the document's edits respect.</param>
    public SceneAuthoringFixture(WorkspaceInteractionService? interaction = null)
    {
        this.interaction = interaction;
        this.Projects.Activate(SceneTabs.Project);
        _ = this.Slots.Setup(value => value.ReadAsync(It.IsAny<ProjectContext>(), It.IsAny<AssetPath>(), It.IsAny<CancellationToken>())).ReturnsAsync((ProjectContext _, AssetPath asset, CancellationToken _) => new GeometryMaterialSlotMetadata(asset, Guid.Parse("20000000-0000-0000-0000-000000000001"), new string('a', 64), [new(TargetFor(asset).SlotId, "Surface", [new(0, 0, Guid.Empty)])]));
        var scene = new Scene
        {
            Name = "UI Test Scene",
        };
        var node = new SceneNode(scene)
        {
            Name = "Camera and sun",
        };
        _ = node.AddComponent(new PerspectiveCamera
        {
            Name = "Camera",
            NearPlane = 0.1f,
            FarPlane = 1000,
        });
        _ = node.AddComponent(new DirectionalLightComponent { Name = "Sun" });
        scene.RootNodes.Add(node);
        this.Tab = SceneTabs.Open(scene, interaction);
        this.Scene = this.Tab.Scene;
        this.Node = this.Tab.NodeOf(node);
        this.Camera = this.Node.Components.OfType<PerspectiveCamera>().Single();
        this.Area = new TestDocumentArea(this.Tab.Document);
    }

    /// <summary>Gets the scene's document and its tab.</summary>
    public SceneTab Tab { get; }

    /// <summary>Gets the document area that shows the scene's document, active.</summary>
    public TestDocumentArea Area { get; }

    /// <summary>Gets the scene's document, whose history the edits record into.</summary>
    public Documents.Document Document => this.Tab.Document;

    /// <summary>Gets what edits the document's scene.</summary>
    public SceneAuthoring Authoring => this.Tab.Authoring;

    public ProjectContextService Projects { get; } = new();

    /// <summary>Gets the shared selection authority wired into the Explorer and Inspector.</summary>
    public SceneSelectionService Selection { get; } = new();

    public Mock<IGeometryMaterialSlotProvider> Slots { get; } = new();

    public Scene Scene { get; }

    public SceneNode Node { get; }

    public PerspectiveCamera Camera { get; }

    /// <summary>Gets the engine preview the views read.</summary>
    public Mock<IScenePreview> Preview { get; } = new();

    public StrongReferenceMessenger Messenger { get; } = new();

    public static MaterialSlotTarget TargetFor(AssetPath geometry) => new(geometry, Guid.Parse("10000000-0000-0000-0000-000000000001"), new string('a', 64));

    public void Dispose() => this.Tab.Dispose();
}
