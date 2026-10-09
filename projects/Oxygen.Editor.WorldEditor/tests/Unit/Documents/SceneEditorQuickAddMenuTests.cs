// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using AwesomeAssertions;
using CommunityToolkit.Mvvm.Messaging;
using DroidNet.Controls.Menus;
using DroidNet.Documents;
using DryIoc;
using Microsoft.UI;
using Moq;
using Oxygen.Editor.ContentPipeline;
using Oxygen.Editor.Documents;
using Oxygen.Editor.Projects;
using Oxygen.Editor.Runtime.Engine;
using Oxygen.Editor.World.Documents;
using Oxygen.Editor.World.SceneEditor;
using Oxygen.Editor.World.Services;
using Oxygen.Editor.WorldEditor.Documents.Commands;
using Oxygen.Managed.Core.Diagnostics;

namespace Oxygen.Editor.WorldEditor.Unit.Tests.Documents;

/// <summary>Verifies the scene editor Quick Add menu: an empty node, the canonical primitives, lights and cameras.</summary>
[TestClass]
[System.Diagnostics.CodeAnalysis.SuppressMessage("Maintainability", "CA1515:Consider making public types internal", Justification = "MSTest discovers public test classes with the repository's discovery configuration.")]
public sealed class SceneEditorQuickAddMenuTests
{
    /// <summary>The menu offers an empty node, then shapes, lights and cameras.</summary>
    [TestMethod]
    public void QuickAddMenuOffersEmptyNodeShapesLightsCamerasAndLocalFog()
    {
        using var fixture = new Fixture();
        var items = fixture.Editor.QuickAddMenu.Items;

        _ = items.Where(item => !item.IsSeparator).Select(item => item.Text).Should().Equal("Empty node", "Shapes", "Lights", "Cameras", "Local fog volume");
        _ = Submenu(items, "Lights").Select(item => item.Text).Should().Equal("Directional light", "Point light", "Spot light");
        _ = Submenu(items, "Cameras").Select(item => item.Text).Should().Equal("Perspective camera", "Orthographic camera");
    }

    /// <summary>The Shapes submenu lists exactly the ten engine built-in authoring geometries.</summary>
    [TestMethod]
    public void QuickAddMenuShapesOfferEveryCanonicalPrimitiveKind()
    {
        using var fixture = new Fixture();
        var shapes = Submenu(fixture.Editor.QuickAddMenu.Items, "Shapes");

        _ = shapes.Select(item => item.Text).Should().Equal(
            "Sphere",
            "Cube",
            "Cylinder",
            "Cone",
            "Plane",
            "Capsule",
            "Icosphere",
            "Torus",
            "Quad",
            "Subdivided cube");
        _ = shapes.Should().OnlyContain(item => !item.IsSeparator && item.Command != null);
    }

    private static IEnumerable<MenuItemData> Submenu(IEnumerable<MenuItemData> items, string text)
        => items.Single(item => string.Equals(item.Text, text, StringComparison.Ordinal)).SubItems;

    private sealed class Fixture : IDisposable
    {
        private readonly Container container = new();

        public Fixture()
        {
            this.Metadata = new SceneDocumentMetadata { Title = "Quick Add" };
            var documents = new Mock<IDocumentService>();
            _ = documents.Setup(value => value.GetActiveDocumentId(It.IsAny<WindowId>())).Returns(this.Metadata.DocumentId);
            var input = new Mock<IDocumentInputCommitter>();
            _ = input.Setup(value => value.CommitAsync(It.IsAny<WindowId>())).Returns(Task.CompletedTask);
            this.Editor = new(
                this.Metadata,
                documents.Object,
                default,
                Mock.Of<IEngineService>(),
                Mock.Of<ISceneEngineSync>(),
                input.Object,
                Mock.Of<IOperationResultPublisher>(),
                new OperationStatusReducer(),
                Mock.Of<ISceneDocumentCommandService>(),
                Mock.Of<IContentPipelineService>(),
                this.container,
                this.Messenger,
                new SceneCookInputRegistrar(
                    new Oxygen.Editor.ContentPipeline.Snapshots.CookDocumentRegistry(),
                    Mock.Of<IProjectManagerService>(),
                    new DroidNet.Hosting.WinUI.HostingContext { Dispatcher = null!, Application = null!, DispatcherScheduler = null! },
                    documents.Object),
                new Oxygen.Editor.World.Workspace.PreviewSettingsService(
                    Mock.Of<IEngineService>(),
                    Mock.Of<Oxygen.Editor.Data.Services.IEditorSettingsManager>(),
                    Mock.Of<IProjectContextService>(),
                    Mock.Of<IOperationResultPublisher>(),
                    new OperationStatusReducer()));
        }

        public SceneDocumentMetadata Metadata { get; }

        public SceneEditorViewModel Editor { get; }

        public StrongReferenceMessenger Messenger { get; } = new();

        public void Dispose()
        {
            this.Editor.Dispose();
            this.Messenger.Reset();
            this.container.Dispose();
        }
    }
}
