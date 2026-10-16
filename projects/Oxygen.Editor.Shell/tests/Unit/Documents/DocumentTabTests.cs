// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using AwesomeAssertions;
using DroidNet.Storage;
using DroidNet.TimeMachine.Changes;
using Oxygen.Editor.Documents;
using Oxygen.Editor.Shell.Documents;

namespace Oxygen.Editor.Shell.Unit.Tests.Documents;

/// <summary>Checks that a tab shows its document's state without anybody pushing it.</summary>
[TestClass]
public sealed class DocumentTabTests
{
    public TestContext TestContext { get; set; } = null!;

    /// <summary>An edit makes the tab dirty, and undoing back to the saved point makes it clean again.</summary>
    /// <returns>The asynchronous test.</returns>
    [TestMethod]
    public async Task TheTabFollowsEditsAndUndoToTheSavedPoint()
    {
        using var notes = new Notes();
        var document = await notes.OpenAsync("Intro", this.TestContext.CancellationToken).ConfigureAwait(false);
        using var tab = new DocumentTab(document, new DocumentEditorDescriptor(notes.Kind, static _ => new object()));
        var changes = new List<string?>();
        tab.PropertyChanged += (_, args) => changes.Add(args.PropertyName);

        document.History.AddChange(new SimpleAction(static () => { }) { Key = "edit" });
        _ = tab.IsDirty.Should().BeTrue();

        document.History.Undo();
        _ = tab.IsDirty.Should().BeFalse();
        _ = changes.Should().Equal(nameof(tab.IsDirty), nameof(tab.IsDirty));
    }

    /// <summary>A document whose file disappears shows as unsaved, and a reload of the file shows as saved.</summary>
    /// <returns>The asynchronous test.</returns>
    [TestMethod]
    public async Task TheTabFollowsAMissingFileAndAReload()
    {
        using var notes = new Notes();
        var document = await notes.OpenAsync("Intro", this.TestContext.CancellationToken).ConfigureAwait(false);
        using var tab = new DocumentTab(document, new DocumentEditorDescriptor(notes.Kind, static _ => new object()));
        var changes = new List<string?>();
        tab.PropertyChanged += (_, args) => changes.Add(args.PropertyName);

        notes.Files.Delete(document.File.Path);
        _ = await document.CheckFileAsync(this.TestContext.CancellationToken).ConfigureAwait(false);
        _ = tab.IsDirty.Should().BeTrue("a missing file is unsaved");

        _ = await notes.Files.WriteAsync(document.File.Path, "outside"u8.ToArray(), FileVersion.Missing, this.TestContext.CancellationToken).ConfigureAwait(false);
        _ = await document.ReloadAsync(this.TestContext.CancellationToken).ConfigureAwait(false);
        _ = tab.IsDirty.Should().BeFalse("the reload took the file as it is now");
        _ = changes.Should().Contain(nameof(tab.IsDirty)).And.HaveCountGreaterThanOrEqualTo(2);
    }

    /// <summary>A rename in the editor changes the tab's title.</summary>
    /// <returns>The asynchronous test.</returns>
    [TestMethod]
    public async Task TheTabFollowsARename()
    {
        using var notes = new Notes();
        var document = await notes.OpenAsync("Intro", this.TestContext.CancellationToken).ConfigureAwait(false);
        using var tab = new DocumentTab(document, new DocumentEditorDescriptor(notes.Kind, static _ => new object()));
        var moved = Path.Combine(Path.GetDirectoryName(document.File.Path)!, "Opening.note");

        await document.FollowRelocationAsync(
            new DocumentRelocation(new DocumentFile(moved, new Uri("asset:///Content/Opening.note")), document.Version, static _ => null),
            this.TestContext.CancellationToken).ConfigureAwait(false);

        _ = tab.Title.Should().Be("Opening");
        _ = tab.DocumentId.Should().Be(document.Id);
    }

    /// <summary>The tab takes how it looks from its kind's editor, and stops following its document once disposed.</summary>
    /// <returns>The asynchronous test.</returns>
    [TestMethod]
    public async Task TheTabLooksAsItsEditorSaysAndStopsWhenDisposed()
    {
        using var notes = new Notes();
        var document = await notes.OpenAsync("Intro", this.TestContext.CancellationToken).ConfigureAwait(false);
        var tab = new DocumentTab(document, new DocumentEditorDescriptor(notes.Kind, static _ => new object()) { IsClosable = false });
        var raised = false;
        tab.PropertyChanged += (_, _) => raised = true;

        tab.Dispose();
        document.History.AddChange(new SimpleAction(static () => { }) { Key = "edit" });

        _ = tab.IsClosable.Should().BeFalse();
        _ = raised.Should().BeFalse();
    }
}
