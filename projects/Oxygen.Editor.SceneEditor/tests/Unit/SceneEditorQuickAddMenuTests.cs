// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using AwesomeAssertions;
using DroidNet.Controls.Menus;
using DroidNet.TestHelpers;
using DryIoc;
using Moq;
using Oxygen.Editor.ContentPipeline;
using Oxygen.Editor.Projects.Testing;
using Oxygen.Editor.Runtime.Engine;
using Oxygen.Editor.SceneEditor.Settings;
using Oxygen.Editor.Scenes.Testing;
using Oxygen.Editor.Shell.Documents;
using Oxygen.Editor.Shell.Testing;
using Oxygen.Editor.Shell.Windows;
using Oxygen.Editor.World;
using Oxygen.Managed.Core.Diagnostics;
using Testably.Abstractions.Testing;

namespace Oxygen.Editor.SceneEditor.Unit.Tests;

/// <summary>Verifies the scene editor Quick Add menu: an empty node, the canonical primitives, lights and cameras.</summary>
[TestClass]
[System.Diagnostics.CodeAnalysis.SuppressMessage("Maintainability", "CA1515:Consider making public types internal", Justification = "MSTest discovers public test classes with the repository's discovery configuration.")]
public sealed partial class SceneEditorQuickAddMenuTests
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

    private sealed partial class Fixture : IDisposable
    {
        private readonly IContainer container = HostContainer.Create();

        public Fixture()
        {
            this.Metadata = TestScenes.Open(new Scene { Name = "Quick Add" });
            this.Tab = new DocumentTab(this.Metadata.Document, new DocumentEditorDescriptor(this.Metadata.Document.Kind, static _ => new object()) { IsClosable = false });
            var input = new Mock<IDocumentInputCommitter>();
            _ = input.Setup(value => value.CommitAsync(It.IsAny<DroidNet.Documents.DocumentId>())).Returns(Task.CompletedTask);
            this.Editor = new(
                this.Tab,
                new TestDocumentArea(this.Metadata.Document),
                Mock.Of<IEngineService>(),
                Mock.Of<Oxygen.Editor.LivePreview.Scenes.IScenePreview>(),
                input.Object,
                Mock.Of<IOperationResultPublisher>(),
                new OperationStatusReducer(),
                Mock.Of<IContentPipelineService>(),
                this.container,
                Mock.Of<IAssetReveal>(),
                new PreviewSettingsService(
                    Mock.Of<IEngineService>(),
                    Mock.Of<Data.Services.IEditorSettingsManager>(),
                    new TestSession(TestProject.Create(new MockFileSystem()).Context),
                    Mock.Of<IOperationResultPublisher>(),
                    new OperationStatusReducer()));
        }

        public TestScene Metadata { get; }

        public DocumentTab Tab { get; }

        public SceneEditorViewModel Editor { get; }

        public void Dispose()
        {
            this.Editor.Dispose();
            this.container.Dispose();
            this.Tab.Dispose();
            this.Metadata.Dispose();
        }
    }
}
