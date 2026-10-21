// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using AwesomeAssertions;
using Moq;
using Oxygen.Editor.Shell.Commands;

namespace Oxygen.Editor.SceneExplorer.Unit.Tests;

/// <summary>
/// The Explorer as a command handler: it runs its commands, including cut, copy and paste, only while it shows the
/// window's active document.
/// </summary>
public sealed partial class SceneExplorerSelectionTests
{
    [TestMethod]
    public void CommandHandler_RegistersWithTheCommandServiceAndReleasesOnDispose()
    {
        using var harness = new SelectionHarness(out _, out _, out _);
        var registration = new Mock<IDisposable>();
        var commands = new Mock<ICommandService>();
        _ = commands.Setup(service => service.Register(It.IsAny<ICommandHandler>())).Returns(registration.Object);

        var explorer = harness.Build(commands.Object);
        commands.Verify(service => service.Register(explorer), Times.Once);

        explorer.Dispose();
        registration.Verify(value => value.Dispose(), Times.Once);
    }

    [TestMethod]
    public async Task CommandHandler_RenameAndFindRunOnlyForTheActiveDocument()
    {
        using var harness = new SelectionHarness(out var scene, out var node, out _);
        using var explorer = harness.Build();
        await explorer.HandleDocumentOpenedAsync(scene).ConfigureAwait(false);
        _ = await explorer.SetSelectedNodes([node.Id]).ConfigureAwait(false);
        var renames = 0;
        explorer.RenameRequested += (_, _) => renames++;
        var finds = 0;
        explorer.FindRequested += (_, _) => finds++;

        _ = explorer.CanExecute(SceneExplorerCommands.Rename.Id).Should().BeTrue();
        _ = explorer.CanExecute(SceneExplorerCommands.FindInExplorer.Id).Should().BeTrue();
        await explorer.ExecuteAsync(SceneExplorerCommands.Rename.Id).ConfigureAwait(false);
        await explorer.ExecuteAsync(SceneExplorerCommands.FindInExplorer.Id).ConfigureAwait(false);
        _ = renames.Should().Be(1);
        _ = finds.Should().Be(1);

        harness.Documents.Activate(document: null);

        _ = explorer.CanExecute(SceneExplorerCommands.Rename.Id).Should().BeFalse("the Explorer shows a document the window does not have active");
        _ = explorer.CanExecute(SceneExplorerCommands.FindInExplorer.Id).Should().BeFalse();
        _ = explorer.CanExecute(ShellCommands.Delete.Id).Should().BeFalse();
        await explorer.ExecuteAsync(SceneExplorerCommands.Rename.Id).ConfigureAwait(false);
        _ = renames.Should().Be(1);
    }

    [TestMethod]
    public async Task CommandHandler_IgnoresCommandsItDoesNotOwn()
    {
        using var harness = new SelectionHarness(out var scene, out _, out _);
        using var explorer = harness.Build();
        await explorer.HandleDocumentOpenedAsync(scene).ConfigureAwait(false);

        _ = explorer.CanExecute("Scene.Tool.Select").Should().BeFalse();
    }

    [TestMethod]
    public async Task CommandHandler_CopyThenPaste_PastesTheNodeOnce()
    {
        // Arrange
        using var harness = new SelectionHarness(out var scene, out var node, out _);
        using var explorer = harness.Build();
        await explorer.HandleDocumentOpenedAsync(scene).ConfigureAwait(false);
        _ = await explorer.SetSelectedNodes([node.Id]).ConfigureAwait(false);
        var before = scene.AllNodes.Count();

        // Act: the window's Ctrl+C and Ctrl+V run the explorer's commands.
        _ = explorer.CanExecute(ShellCommands.Copy.Id).Should().BeTrue();
        await explorer.ExecuteAsync(ShellCommands.Copy.Id).ConfigureAwait(false);
        _ = explorer.CanExecute(ShellCommands.Paste.Id).Should().BeTrue();
        await explorer.ExecuteAsync(ShellCommands.Paste.Id).ConfigureAwait(false);

        // Assert
        _ = scene.AllNodes.Count().Should().Be(before + 1);
    }
}
