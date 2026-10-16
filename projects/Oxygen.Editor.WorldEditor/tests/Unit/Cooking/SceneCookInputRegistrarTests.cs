// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Text.Json;
using AwesomeAssertions;
using DroidNet.Hosting.WinUI;
using DroidNet.TimeMachine;
using Moq;
using Oxygen.Editor.ContentPipeline.Snapshots;
using Oxygen.Editor.Documents;
using Oxygen.Editor.World.Documents;
using Oxygen.Editor.World.SceneEditor;
using Oxygen.Editor.World.Serialization;
using Oxygen.Editor.WorldEditor.Documents.Commands;
using Oxygen.Editor.WorldEditor.Unit.Tests.Documents;

namespace Oxygen.Editor.WorldEditor.Unit.Tests.Cooking;

/// <summary>An open scene document tells the cook its saved file, whether it has edits, and where its file moved.</summary>
[TestClass]
public sealed class SceneCookInputRegistrarTests
{
    public TestContext TestContext { get; set; } = null!;

    /// <summary>The registration follows the document's edits, saves and relocations until it is disposed.</summary>
    /// <returns>The asynchronous test.</returns>
    [TestMethod]
    public async Task RegistrationFollowsTheDocumentUntilDisposed()
    {
        var token = this.TestContext.CancellationToken;
        var commands = Mock.Of<ISceneDocumentCommandService>();
        using var scenes = new SceneDocumentFiles(commands);

        // The file is not as the editor writes it, so the first save changes its content hash.
        scenes.WriteText("Main", JsonSerializer.Serialize(SceneDocumentFiles.Data(Guid.NewGuid(), "Main"), SceneJsonContext.Default.SceneData) + "\n");
        var (document, _) = await scenes.OpenAsync("Main", token).ConfigureAwait(false);
        var registry = new CookDocumentRegistry();
        var registrar = new SceneCookInputRegistrar(registry, new HostingContext { Application = null!, Dispatcher = null!, DispatcherScheduler = null! });
        var registration = registrar.Register(SceneDocumentMetadata.For(document), commands);
        var saved = registry.GetState().Documents.Should().ContainSingle().Subject;
        _ = saved.IsDirty.Should().BeFalse();
        _ = saved.DocumentId.Should().Be(document.Id);

        document.History.AddChange("Edit", static () => { });
        _ = registry.GetState().Documents.Should().ContainSingle().Which.IsDirty.Should().BeTrue();
        _ = (await document.SaveAsync(token).ConfigureAwait(false)).Succeeded.Should().BeTrue();
        var resaved = registry.GetState().Documents.Should().ContainSingle().Subject;
        _ = resaved.IsDirty.Should().BeFalse();
        _ = resaved.SavedContentHash.Should().Be(document.Version.Sha256).And.NotBe(saved.SavedContentHash);

        var moved = scenes.FileOf("Demo");
        File.Move(document.File.Path, moved.Path);
        await document.FollowRelocationAsync(new DocumentRelocation(moved, document.Version, static _ => null), token).ConfigureAwait(false);
        _ = registry.GetState().Documents.Should().ContainSingle().Which.SourcePath.Should().Be(moved.Path);

        registration.Dispose();
        _ = registry.GetState().Documents.Should().BeEmpty();
    }
}
