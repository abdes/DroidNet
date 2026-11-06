// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Text.Json.Nodes;
using AwesomeAssertions;
using DroidNet.TimeMachine;
using Microsoft.Extensions.Logging.Abstractions;
using Oxygen.Editor.ContentPipeline.Relocation;
using Oxygen.Managed.Core;

namespace Oxygen.Editor.ContentPipeline.Unit.Tests.Relocation;

/// <summary>Retarget rewrites unresolved references through the relocation transaction, as one Undo step.</summary>
[TestClass]
internal sealed class ReferenceRetargetTests
{
    private const string Broken = "../Materials/Red.omat";
    private const string Box = "Content/Geometry/Box.ogeo.json";
    private const string Cone = "Content/Geometry/Cone.ogeo.json";
    private static readonly AssetPath Blue = AssetPath.Parse("/Content/Materials/Blue.omat");

    public TestContext TestContext { get; set; } = null!;

    [TestMethod]
    public async Task ThePickedAssetReplacesTheWrittenText_SpelledFromTheFile_AndTheCheckClears()
    {
        using var workspace = CreateProject();
        var history = new HistoryKeeper(this);
        var references = await this.FindAsync(workspace).ConfigureAwait(false);
        _ = references.Should().HaveCount(2);

        var plan = await workspace.Service.RetargetAsync(references, Blue, "Retarget", history, this.TestContext.CancellationToken).ConfigureAwait(false);

        _ = MaterialOf(workspace, Box).Should().Be("../Materials/Blue.omat");
        _ = MaterialOf(workspace, Cone).Should().Be("../Materials/Blue.omat");
        _ = plan.RewrittenFiles.Should().HaveCount(2);
        _ = plan.Moves.Should().BeEmpty();
        _ = history.UndoStack.Should().ContainSingle();
        _ = (await this.FindAsync(workspace).ConfigureAwait(false)).Should().BeEmpty();
    }

    [TestMethod]
    public async Task OneUndoRestoresEveryReferenceAndRedoRepairsThemAgain()
    {
        using var workspace = CreateProject();
        var history = new HistoryKeeper(this);
        _ = await workspace.Service.RetargetAsync(await this.FindAsync(workspace).ConfigureAwait(false), Blue, "Retarget", history, this.TestContext.CancellationToken).ConfigureAwait(false);

        _ = await workspace.Service.UndoAsync(history, this.TestContext.CancellationToken).ConfigureAwait(false);

        _ = MaterialOf(workspace, Box).Should().Be(Broken);
        _ = MaterialOf(workspace, Cone).Should().Be(Broken);

        _ = await workspace.Service.RedoAsync(history, this.TestContext.CancellationToken).ConfigureAwait(false);

        _ = MaterialOf(workspace, Box).Should().Be("../Materials/Blue.omat");
        _ = MaterialOf(workspace, Cone).Should().Be("../Materials/Blue.omat");
    }

    [TestMethod]
    public async Task OnlyTheChosenReferenceChanges()
    {
        using var workspace = CreateProject();
        var references = await this.FindAsync(workspace).ConfigureAwait(false);

        _ = await workspace.Service.RetargetAsync([references.Single(reference => reference.File.Name == "Box.ogeo.json")], Blue, "Retarget", new HistoryKeeper(this), this.TestContext.CancellationToken).ConfigureAwait(false);

        _ = MaterialOf(workspace, Box).Should().Be("../Materials/Blue.omat");
        _ = MaterialOf(workspace, Cone).Should().Be(Broken);
    }

    [TestMethod]
    public async Task AFieldThatChangedSinceTheCheckIsRefusedAndNoFileChanges()
    {
        using var workspace = CreateProject();
        var references = await this.FindAsync(workspace).ConfigureAwait(false);
        _ = workspace.Write(Cone, Geometry("../Materials/Other.omat"));

        var retarget = async () => await workspace.Service.RetargetAsync(references, Blue, "Retarget", new HistoryKeeper(this), this.TestContext.CancellationToken).ConfigureAwait(false);

        _ = await retarget.Should().ThrowAsync<AssetRelocationException>().ConfigureAwait(false);
        _ = MaterialOf(workspace, Box).Should().Be(Broken);
    }

    [TestMethod]
    public async Task SharedContentCannotBeRetargetedIntoAnOwnMount()
    {
        using var workspace = new RelocationWorkspace();
        var shared = workspace.FileSystem.Path.Combine(workspace.Root, "..", "Shared");
        workspace.MountOutside("Lib", workspace.FileSystem.Path.GetFullPath(shared));
        workspace.WriteOutside(workspace.FileSystem.Path.GetFullPath(workspace.FileSystem.Path.Combine(shared, "Box.ogeo.json")), Geometry("Red.omat"));
        _ = workspace.Write("Content/Materials/Blue.omat.json", "{}");
        var references = await this.FindAsync(workspace).ConfigureAwait(false);

        var retarget = async () => await workspace.Service.RetargetAsync(references, Blue, "Retarget", new HistoryKeeper(this), this.TestContext.CancellationToken).ConfigureAwait(false);

        _ = await retarget.Should().ThrowAsync<AssetRelocationException>().WithMessage("*shared content*").ConfigureAwait(false);
    }

    [TestMethod]
    public void AModelRecordsSourceAndAFileOutsideTheMountsOfferNoRetarget()
    {
        using var workspace = CreateProject();

        _ = workspace.Service.CanRetarget(new(workspace.FileOf(Box), "$.x", Broken, Resolved: null, "code")).Should().BeTrue();
        _ = workspace.Service.CanRetarget(new(workspace.FileOf("Content/Robot.glb.import.json"), "$.source", Broken, Resolved: null, "code")).Should().BeFalse();
        _ = workspace.Service.CanRetarget(new(FileSystemPath.From(@"C:\Elsewhere\Box.ogeo.json"), "$.x", Broken, Resolved: null, "code")).Should().BeFalse();
    }

    [TestMethod]
    public async Task TheCheckRunsAgainAfterARetargetSoTheRowLeaves()
    {
        using var workspace = CreateProject();
        using var check = new ReferenceCheck(workspace.Session, NullLogger<ReferenceCheck>.Instance, workspace.Service);
        var references = await check.RunAsync(this.TestContext.CancellationToken).ConfigureAwait(false);
        _ = references.Should().HaveCount(2);
        var cleared = new TaskCompletionSource(TaskCreationOptions.RunContinuationsAsynchronously);
        check.ProblemsChanged += (_, _) =>
        {
            if (check.Unresolved.Count == 0)
            {
                cleared.TrySetResult();
            }
        };

        _ = await workspace.Service.RetargetAsync(references, Blue, "Retarget", new HistoryKeeper(this), this.TestContext.CancellationToken).ConfigureAwait(false);

        await cleared.Task.WaitAsync(TimeSpan.FromSeconds(10), this.TestContext.CancellationToken).ConfigureAwait(false);
    }

    private static string MaterialOf(RelocationWorkspace workspace, string file)
        => workspace.ReadJson(file)["lods"]![0]!["submeshes"]![0]!["material_ref"]!.GetValue<string>();

    private static string Geometry(string material)
        => new JsonObject
        {
            ["lods"] = new JsonArray(new JsonObject { ["submeshes"] = new JsonArray(new JsonObject { ["material_ref"] = material }) }),
        }.ToJsonString();

    private static RelocationWorkspace CreateProject()
    {
        var workspace = new RelocationWorkspace();
        _ = workspace.Write("Content/Materials/Blue.omat.json", "{}");
        _ = workspace.Write(Box, Geometry(Broken));
        _ = workspace.Write(Cone, Geometry(Broken));
        return workspace;
    }

    private async Task<IReadOnlyList<UnresolvedReference>> FindAsync(RelocationWorkspace workspace)
        => await ReferenceCheck.FindAsync(workspace.Project, this.TestContext.CancellationToken).ConfigureAwait(false);
}
