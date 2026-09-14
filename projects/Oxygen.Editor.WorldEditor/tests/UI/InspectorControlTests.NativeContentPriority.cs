// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Numerics;
using AwesomeAssertions;
using DroidNet.Storage.Native;
using Microsoft.Extensions.Logging.Abstractions;
using Oxygen.Editor.ContentPipeline;
using Oxygen.Editor.ContentPipeline.Mounting;
using Oxygen.Editor.ContentPipeline.Publication;
using Oxygen.Editor.Projects;
using Oxygen.Editor.WorldEditor.Documents.Commands;
using Testably.Abstractions;

namespace Oxygen.Editor.World.Tests;

/// <summary>Checks the visible material selected by the editor's source order against the running engine.</summary>
public sealed partial class InspectorControlTests
{
    /// <summary>Priority changes refresh existing bindings, preserve edits/history, and remain effective after scene Save/reopen.</summary>
    /// <returns>The asynchronous native source-priority regression.</returns>
    [TestMethod]
    public Task LibraryPriorityChangesExistingMaterialWithoutReloadOrAuthoredEdits() => EnqueueAsync(async () =>
    {
        var fixture = new NativeSceneFixture(automatic: false, scene => AddGeometryNode(scene, "Cube"));
        await using var lifetime = fixture.ConfigureAwait(true);
        using var timeout = CancellationTokenSource.CreateLinkedTokenSource(this.TestContext.CancellationToken);
        timeout.CancelAfter(TimeSpan.FromSeconds(60));
        var red = new Vector4(1, 0, 0, 1);
        var blue = new Vector4(0, 0, 1, 1);
        var (uri, key) = await fixture.CookTestMaterialAsync("Shared", timeout.Token, red).ConfigureAwait(true);
        var libraryProject = Path.Combine(fixture.ProjectRoot, "LibraryProject");
        var (_, libraryKey) = await fixture.CookTestMaterialAsync("Shared", timeout.Token, blue, libraryProject).ConfigureAwait(true);
        _ = libraryKey.Should().NotBe(key, "the independent library defines a different native identity at the same virtual path");
        var project = ProjectContext.FromProject(fixture.Source.Project) with
        {
            AuthoringMounts = [new("Content", "Content")],
            LocalFolderMounts = [new("Library", Path.Combine(libraryProject, ".cooked", "Content"))],
        };
        await fixture.InitializeAsync(timeout.Token).ConfigureAwait(true);
        await fixture.ApplyContentPriorityAsync(project, timeout.Token).ConfigureAwait(true);
        var nodeId = fixture.Source.RootNodes.Single().Id;
        _ = (await fixture.Commands.EditMaterialSlotAsync(fixture.Context, [nodeId], 0, uri, EditSessionToken.OneShot).ConfigureAwait(true)).Succeeded.Should().BeTrue();
        _ = await WaitForNodeAsync(fixture, nodeId, value => value.MaterialBaseColors.Length == 1 && Vector4.Distance(value.MaterialBaseColors[0], red) < 0.001f, timeout.Token).ConfigureAwait(true);
        var revision = fixture.Context.Metadata.ChangeVersion;
        var history = fixture.Context.History.UndoStack.Count;
        var source = fixture.Source;
        var overriding = project with { CookedContentOrder = [new(CookedContentSourceKind.ProjectOutput), new(CookedContentSourceKind.LocalFolder, "Library")] };
        await fixture.ApplyContentPriorityAsync(overriding, timeout.Token).ConfigureAwait(true);
        var overridden = await WaitForNodeAsync(fixture, nodeId, value => value.MaterialBaseColors.Length == 1 && Vector4.Distance(value.MaterialBaseColors[0], blue) < 0.001f, timeout.Token).ConfigureAwait(true);
        _ = overridden.MaterialKeys.Should().ContainSingle().Which.Should().Be(libraryKey);
        _ = fixture.Source.Should().BeSameAs(source);
        _ = fixture.Context.Metadata.ChangeVersion.Should().Be(revision);
        _ = fixture.Context.History.UndoStack.Should().HaveCount(history);
        await fixture.SaveAndReopenAsync(timeout.Token).ConfigureAwait(true);
        _ = await WaitForNodeAsync(fixture, nodeId, value => value.MaterialBaseColors.Length == 1 && Vector4.Distance(value.MaterialBaseColors[0], blue) < 0.001f, timeout.Token).ConfigureAwait(true);
        await fixture.ApplyContentPriorityAsync(project, timeout.Token).ConfigureAwait(true);
        _ = await WaitForNodeAsync(fixture, nodeId, value => value.MaterialBaseColors.Length == 1 && Vector4.Distance(value.MaterialBaseColors[0], red) < 0.001f, timeout.Token).ConfigureAwait(true);
    });

    private sealed partial class NativeSceneFixture
    {
        public async Task ApplyContentPriorityAsync(ProjectContext project, CancellationToken cancellationToken)
        {
            var api = new ImportToolContentPipelineApi(new EngineContentPipelineToolLocator(), new ContentPipelineProcessRunner(), NullLogger<ImportToolContentPipelineApi>.Instance, this.compatibility);
            var service = new CookedContentMountService(new NativeStorageProvider(new RealFileSystem()), api);
            var mounts = await service.PrepareAsync(project, CookedContentMountService.FindProjectRoots(project), CookOutputLease.AcquireRead(project.ProjectRoot), cancellationToken).ConfigureAwait(true);
            await this.engine.RefreshProjectCookedRootsAsync(mounts.Roots, mounts).ConfigureAwait(true);
        }
    }
}
