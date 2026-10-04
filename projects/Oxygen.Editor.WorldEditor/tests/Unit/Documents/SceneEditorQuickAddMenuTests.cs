// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using AwesomeAssertions;
using CommunityToolkit.Mvvm.Messaging;
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
using Oxygen.Editor.World.SceneExplorer;
using Oxygen.Editor.World.Services;
using Oxygen.Editor.World;
using Oxygen.Editor.WorldEditor.Documents.Commands;
using Oxygen.Managed.Core.Diagnostics;

namespace Oxygen.Editor.WorldEditor.Unit.Tests.Documents;

/// <summary>Verifies the scene editor quick-add palette exposes the canonical primitive kinds.</summary>
[TestClass]
[System.Diagnostics.CodeAnalysis.SuppressMessage("Maintainability", "CA1515:Consider making public types internal", Justification = "MSTest discovers public test classes with the repository's discovery configuration.")]
public sealed class SceneEditorQuickAddMenuTests
{
    /// <summary>The Shapes submenu lists exactly the ten engine built-in authoring geometries.</summary>
    [TestMethod]
    public void QuickAddMenuShapesOfferEveryCanonicalPrimitiveKind()
    {
        using var fixture = new Fixture();
        var shapes = fixture.Editor.QuickAddMenu.Items.Single(item => string.Equals(item.Text, "Shapes", StringComparison.Ordinal));

        _ = shapes.SubItems.Select(item => item.Text).Should().Equal(
            "Sphere",
            "Cube",
            "Cylinder",
            "Cone",
            "Plane",
            "Capsule",
            "IcoSphere",
            "Torus",
            "Quad",
            "SubdividedCube");
        _ = shapes.SubItems.Should().OnlyContain(item => !item.IsSeparator && item.Command != null);
    }

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
