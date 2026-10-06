// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Numerics;
using AwesomeAssertions;
using Oxygen.Editor.Projects;
using Oxygen.Editor.World;
using Oxygen.Editor.WorldEditor.Documents.Commands;
using Oxygen.Editor.WorldEditor.TestSupport;
using static Oxygen.Editor.WorldEditor.TestSupport.NativeSceneAssertions;
using static Oxygen.Editor.WorldEditor.TestSupport.NativeSceneData;

namespace Oxygen.Editor.WorldEditor.Integration.UI.Tests.Workspace;

[TestClass]
public sealed partial class ContentPriorityTests : DroidNet.Tests.VisualUserInterfaceTests
{
    public TestContext TestContext { get; set; } = null!;

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
        _ = libraryKey.Should().Be(key, "stable asset keys preserve the same logical identity while source precedence selects its content");
        var project = ProjectContext.FromProject(fixture.Source.Project) with
        {
            AuthoringMounts = [new("Content", "Content")],
            LocalFolderMounts = [new("Library", await fixture.GetCookedRootAsync(libraryProject, timeout.Token).ConfigureAwait(true))],
        };
        await fixture.InitializeAsync(timeout.Token).ConfigureAwait(true);
        await fixture.ApplyContentPriorityAsync(project, timeout.Token).ConfigureAwait(true);
        var nodeId = fixture.Source.RootNodes.Single().Id;
        _ = (await fixture.Commands.EditMaterialSlotAsync(fixture.Context, [nodeId], await fixture.ReadSingleMaterialSlotAsync(nodeId, this.TestContext.CancellationToken).ConfigureAwait(true), uri, EditSessionToken.OneShot).ConfigureAwait(true)).Succeeded.Should().BeTrue();
        _ = await WaitForNodeAsync(fixture, nodeId, value => value.MaterialBaseColors.Length == 1 && Vector4.Distance(value.MaterialBaseColors[0], red) < 0.001f, timeout.Token).ConfigureAwait(true);
        var revision = fixture.Context.Metadata.ChangeVersion;
        var history = fixture.Context.History.UndoStack.Count;
        var source = fixture.Source;
        var overriding = project with
        {
            CookedContentOrder = [new(CookedContentSourceKind.ProjectOutput), new(CookedContentSourceKind.LocalFolder, "Library")],
        };
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
}
