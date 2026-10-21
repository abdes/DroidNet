// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using AwesomeAssertions;
using DroidNet.TimeMachine;
using Moq;
using Oxygen.Editor.Shell.Commands;
using Oxygen.Editor.Shell.Contributions;
using Oxygen.Editor.Shell.Documents;
using Oxygen.Editor.Shell.Unit.Tests.Documents;

namespace Oxygen.Editor.Shell.Unit.Tests.Commands;

/// <summary>
/// A command goes to the active document's editor, then to the registered handlers; Undo and Redo that none handles go to
/// the active document's history, and to the global history when no document is active.
/// </summary>
[TestClass]
public sealed class CommandServiceTests
{
    /// <summary>With no document active, Undo and Redo run the global history.</summary>
    /// <returns>The asynchronous test.</returns>
    [TestMethod]
    public async Task WithoutADocumentUndoRunsTheGlobalHistory()
    {
        using var histories = new HistoryService();
        var undone = 0;
        histories.GlobalHistory.AddChange("Rename", Rename);
        var service = new CommandService(Mock.Of<IDocumentArea>(), histories, NoContributions());

        _ = (await service.ExecuteAsync(ShellCommands.Undo.Id, context: null).ConfigureAwait(false)).Should().BeTrue();

        _ = undone.Should().Be(1);
        _ = histories.GlobalHistory.CanRedo.Should().BeTrue();
        _ = (await service.ExecuteAsync(ShellCommands.Undo.Id, context: null).ConfigureAwait(false)).Should().BeFalse("nothing is left to undo");

        // Undoing a rename records the rename that redoes it.
        void Rename()
        {
            undone++;
            histories.GlobalHistory.AddChange("Rename", Rename);
        }
    }

    /// <summary>The active document's editor runs Undo when it handles it; the histories are left alone.</summary>
    /// <returns>The asynchronous test.</returns>
    [TestMethod]
    public async Task TheActiveEditorRunsTheCommandsItHandles()
    {
        using var histories = new HistoryService();
        histories.GlobalHistory.AddChange("Rename", static () => { });
        var editor = new Mock<ICommandHandler>();
        _ = editor.Setup(value => value.CanExecute(ShellCommands.Undo.Id)).Returns(value: true);
        _ = editor.Setup(value => value.ExecuteAsync(ShellCommands.Undo.Id)).Returns(Task.CompletedTask);
        var area = new Mock<IDocumentArea>();
        _ = area.SetupGet(value => value.ActiveEditor).Returns(editor.Object);
        var service = new CommandService(area.Object, histories, NoContributions());

        _ = (await service.ExecuteAsync(ShellCommands.Undo.Id, context: null).ConfigureAwait(false)).Should().BeTrue();

        editor.Verify(value => value.ExecuteAsync(ShellCommands.Undo.Id), Times.Once);
        _ = histories.GlobalHistory.CanUndo.Should().BeTrue();
    }

    /// <summary>An active document whose editor does not handle Undo has its own history undone, not the global one.</summary>
    /// <returns>The asynchronous test.</returns>
    [TestMethod]
    public async Task UndoRunsTheActiveDocumentsHistoryBeforeTheGlobalOne()
    {
        using var histories = new HistoryService();
        histories.GlobalHistory.AddChange("Rename", static () => { });
        using var notes = new Notes();
        var document = await notes.OpenAsync("Intro", CancellationToken.None).ConfigureAwait(false);
        document.History.AddChange("Edit", static () => { });
        var area = new Mock<IDocumentArea>();
        _ = area.SetupGet(value => value.ActiveDocument).Returns(document);
        var service = new CommandService(area.Object, histories, NoContributions());

        _ = (await service.ExecuteAsync(ShellCommands.Undo.Id, context: null).ConfigureAwait(false)).Should().BeTrue();

        _ = document.IsDirty.Should().BeFalse();
        _ = histories.GlobalHistory.CanUndo.Should().BeTrue();
    }

    /// <summary>A registered handler runs a command no editor handles, without a control to start from, until it is removed.</summary>
    /// <returns>The asynchronous test.</returns>
    [TestMethod]
    public async Task ARegisteredHandlerRunsTheCommandUntilItIsRemoved()
    {
        using var histories = new HistoryService();
        var handler = new Mock<ICommandHandler>();
        _ = handler.Setup(value => value.CanExecute("Scene.Rename")).Returns(value: true);
        _ = handler.Setup(value => value.ExecuteAsync("Scene.Rename")).Returns(Task.CompletedTask);
        var service = new CommandService(Mock.Of<IDocumentArea>(), histories, NoContributions());

        var registration = service.Register(handler.Object);
        _ = service.CanExecute("Scene.Rename", context: null).Should().BeTrue();
        _ = (await service.ExecuteAsync("Scene.Rename", context: null).ConfigureAwait(false)).Should().BeTrue();
        registration.Dispose();

        _ = service.CanExecute("Scene.Rename", context: null).Should().BeFalse();
        _ = (await service.ExecuteAsync("Scene.Rename", context: null).ConfigureAwait(false)).Should().BeFalse();
        handler.Verify(value => value.ExecuteAsync("Scene.Rename"), Times.Once);
    }

    /// <summary>The active document's editor comes before the registered handlers.</summary>
    /// <returns>The asynchronous test.</returns>
    [TestMethod]
    public async Task TheActiveEditorComesBeforeTheRegisteredHandlers()
    {
        using var histories = new HistoryService();
        var editor = new Mock<ICommandHandler>();
        _ = editor.Setup(value => value.CanExecute("Scene.Duplicate")).Returns(value: true);
        _ = editor.Setup(value => value.ExecuteAsync("Scene.Duplicate")).Returns(Task.CompletedTask);
        var area = new Mock<IDocumentArea>();
        _ = area.SetupGet(value => value.ActiveEditor).Returns(editor.Object);
        var registered = new Mock<ICommandHandler>();
        _ = registered.Setup(value => value.CanExecute("Scene.Duplicate")).Returns(value: true);
        var service = new CommandService(area.Object, histories, NoContributions());
        using var registration = service.Register(registered.Object);

        _ = (await service.ExecuteAsync("Scene.Duplicate", context: null).ConfigureAwait(false)).Should().BeTrue();

        editor.Verify(value => value.ExecuteAsync("Scene.Duplicate"), Times.Once);
        registered.Verify(value => value.ExecuteAsync(It.IsAny<string>()), Times.Never);
    }

    /// <summary>The window's commands are the Shell's and the contributed ones.</summary>
    [TestMethod]
    public void TheCommandsAreTheShellsAndTheContributedOnes()
    {
        using var histories = new HistoryService();
        var rename = new CommandDescriptor("Scene.Rename", "Rename");
        var service = new CommandService(Mock.Of<IDocumentArea>(), histories, new ContributionRegistry([], [rename]));

        _ = service.Commands.Should().Contain([ShellCommands.Undo, rename]);
    }

    private static ContributionRegistry NoContributions() => new([], []);
}
