// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using AwesomeAssertions;
using DroidNet.TimeMachine;
using Oxygen.Editor.ContentPipeline.Relocation;
using Oxygen.Managed.Core;

namespace Oxygen.Editor.ContentPipeline.Unit.Tests.Relocation;

/// <summary>The relocation service turns a paste into moves, and undoes and redoes the history it records in.</summary>
[TestClass]
[System.Diagnostics.CodeAnalysis.SuppressMessage("Maintainability", "CA1515:Consider making public types internal", Justification = "MSTest discovers public test classes with the repository discovery configuration.")]
public sealed class RelocationReplayTests
{
    private const string Material = """{ "name": "Red" }""";

    public TestContext TestContext { get; set; } = null!;

    /// <summary>Undo and Redo replay a recorded relocation and name the entry they replayed.</summary>
    /// <returns>The asynchronous test.</returns>
    [TestMethod]
    public async Task UndoAndRedoReplayTheRelocationAndNameIt()
    {
        using var workspace = new RelocationWorkspace();
        _ = workspace.Write("Content/Materials/Red.omat.json", Material);
        var history = new HistoryKeeper(this);
        _ = await workspace.Service.RelocateAsync(Move("/Content/Materials/Red.omat.json", "/Content/Materials/Blue.omat.json"), "Rename", history, this.TestContext.CancellationToken).ConfigureAwait(false);

        var undone = await workspace.Service.UndoAsync(history, this.TestContext.CancellationToken).ConfigureAwait(false);

        _ = undone.Should().Be("Rename");
        _ = workspace.Exists("Content/Materials/Red.omat.json").Should().BeTrue();
        _ = history.CanRedo.Should().BeTrue();

        var redone = await workspace.Service.RedoAsync(history, this.TestContext.CancellationToken).ConfigureAwait(false);

        _ = redone.Should().Be("Rename");
        _ = workspace.Exists("Content/Materials/Blue.omat.json").Should().BeTrue();
        _ = workspace.Exists("Content/Materials/Red.omat.json").Should().BeFalse();
        _ = history.CanRedo.Should().BeFalse();
    }

    /// <summary>An undo that a later change made impossible is rejected, changes nothing and stays in the history.</summary>
    /// <returns>The asynchronous test.</returns>
    [TestMethod]
    public async Task RejectedUndoStaysInTheHistory()
    {
        using var workspace = new RelocationWorkspace();
        _ = workspace.Write("Content/Materials/Red.omat.json", Material);
        var history = new HistoryKeeper(this);
        _ = await workspace.Service.RelocateAsync(Move("/Content/Materials/Red.omat.json", "/Content/Materials/Blue.omat.json"), "Rename", history, this.TestContext.CancellationToken).ConfigureAwait(false);
        _ = workspace.Write("Content/Materials/Red.omat.json", Material);

        Func<Task> undo = () => workspace.Service.UndoAsync(history, this.TestContext.CancellationToken);

        _ = await undo.Should().ThrowAsync<AssetRelocationException>().ConfigureAwait(false);
        _ = workspace.Exists("Content/Materials/Blue.omat.json").Should().BeTrue();
        _ = history.CanUndo.Should().BeTrue();
    }

    /// <summary>With nothing recorded there is nothing to undo or redo.</summary>
    /// <returns>The asynchronous test.</returns>
    [TestMethod]
    public async Task EmptyHistoryReplaysNothing()
    {
        using var workspace = new RelocationWorkspace();
        var history = new HistoryKeeper(this);

        _ = (await workspace.Service.UndoAsync(history, this.TestContext.CancellationToken).ConfigureAwait(false)).Should().BeNull();
        _ = (await workspace.Service.RedoAsync(history, this.TestContext.CancellationToken).ConfigureAwait(false)).Should().BeNull();
    }

    /// <summary>A paste of cut items moves those not already in the folder, under their names.</summary>
    [TestMethod]
    public void PasteMovesTheItemsThatAreElsewhere()
    {
        using var workspace = new RelocationWorkspace();

        var request = workspace.Service.CreatePasteRequest([AssetPath.Parse("/Content/Materials"), AssetPath.Parse("/Content/Props/Box.ogeo.json")], AssetPath.Parse("/Content/Props"));

        _ = request.Should().NotBeNull();
        var move = request!.Moves.Should().ContainSingle().Subject;
        _ = move.Source.Should().Be(AssetPath.Parse("/Content/Materials"));
        _ = move.Target.Should().Be(AssetPath.Parse("/Content/Props/Materials"));
    }

    /// <summary>A paste of items that are all in the folder already moves nothing.</summary>
    [TestMethod]
    public void PasteIntoTheFolderTheItemsAreInMovesNothing()
    {
        using var workspace = new RelocationWorkspace();

        var request = workspace.Service.CreatePasteRequest([AssetPath.Parse("/Content/Props/Box.ogeo.json")], AssetPath.Parse("/Content/Props"));

        _ = request.Should().BeNull();
    }

    private static AssetRelocationRequest Move(string source, string target) => new() { Moves = [new(AssetPath.Parse(source), AssetPath.Parse(target))] };
}
