// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using AwesomeAssertions;
using DroidNet.Documents;
using DroidNet.Hosting.WinUI;
using DroidNet.Storage;
using DroidNet.TimeMachine;
using Moq;
using Oxygen.Editor.ContentPipeline.Snapshots;
using Oxygen.Editor.Projects;
using Oxygen.Editor.World.Documents;
using Oxygen.Editor.World.SceneEditor;
using Oxygen.Editor.WorldEditor.Documents.Commands;

namespace Oxygen.Editor.World.SceneExplorer.Tests;

/// <summary>Scene status reuses document metadata events without touching the native engine.</summary>
[TestClass]
public sealed class SceneCookInputRegistrarTests
{
    /// <summary>Only the registered document updates status; disposal detaches notifications.</summary>
    [TestMethod]
    public void MetadataNotificationsUpdateOnlyTheirRegisteredScene()
    {
        var scene = new Scene(Mock.Of<IProject>()) { Name = "Main" };
        var metadata = new SceneDocumentMetadata { Title = "Main" };
        var context = new SceneDocumentCommandContext(metadata.DocumentId, metadata, scene, new HistoryKeeper(metadata));
        var source = new SceneSourceVersion(Path.Combine(Path.GetTempPath(), "Main.oscene.json"), new FileVersion(Exists: true, new string('A', 64)));
        var manager = new Mock<IProjectManagerService>();
        _ = manager.Setup(value => value.GetSceneSourceVersion(scene)).Returns(() => source);
        var documents = new Mock<IDocumentService>();
        var registry = new CookDocumentRegistry();
        var registrar = new SceneCookInputRegistrar(registry, manager.Object, new HostingContext { Application = null!, Dispatcher = null!, DispatcherScheduler = null! }, documents.Object);
        using var registration = registrar.Register(context, Mock.Of<ISceneDocumentCommandService>());
        _ = registry.GetState().Documents.Should().ContainSingle().Which.IsDirty.Should().BeFalse();
        metadata.IsDirty = true;
        documents.Raise(service => service.DocumentMetadataChanged += null, new DocumentMetadataChangedEventArgs(default, new SceneDocumentMetadata()));
        _ = registry.GetState().Documents.Should().ContainSingle().Which.IsDirty.Should().BeFalse();
        documents.Raise(service => service.DocumentMetadataChanged += null, new DocumentMetadataChangedEventArgs(default, metadata));
        _ = registry.GetState().Documents.Should().ContainSingle().Which.IsDirty.Should().BeTrue();
        metadata.MarkSaved(metadata.ChangeVersion);
        source = source with { Version = new(Exists: true, new string('B', 64)) };
        documents.Raise(service => service.DocumentMetadataChanged += null, new DocumentMetadataChangedEventArgs(default, metadata));
        _ = registry.GetState().Documents.Should().ContainSingle().Which.SavedContentHash.Should().Be(new string('B', 64));
        _ = registry.GetState().Documents.Should().ContainSingle().Which.IsDirty.Should().BeFalse();
        registration!.Dispose();
        documents.Raise(service => service.DocumentMetadataChanged += null, new DocumentMetadataChangedEventArgs(default, metadata));
        _ = registry.GetState().Documents.Should().BeEmpty();
    }
}
