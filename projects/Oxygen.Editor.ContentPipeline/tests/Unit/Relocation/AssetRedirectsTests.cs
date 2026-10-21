// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using AwesomeAssertions;
using DroidNet.TimeMachine;
using Oxygen.Editor.ContentPipeline.Relocation;
using Oxygen.Managed.Core;

namespace Oxygen.Editor.ContentPipeline.Unit.Tests.Relocation;

[TestClass]
[System.Diagnostics.CodeAnalysis.SuppressMessage("Maintainability", "CA1515:Consider making public types internal", Justification = "MSTest discovers public test classes with the repository discovery configuration.")]
public sealed class AssetRedirectsTests
{
    public TestContext TestContext { get; set; } = null!;

    /// <summary>Captured references follow every later rename, keep their form, and come back with Undo.</summary>
    /// <returns>The asynchronous test.</returns>
    [TestMethod]
    public async Task CapturedReferencesFollowRenamesInOrder()
    {
        using var workspace = new RelocationWorkspace();
        var redirects = new AssetRedirects(workspace.Session);
        var service = workspace.CreateService(redirects);
        _ = workspace.Write("Content/Materials/A.omat.json", "{}");

        _ = await service.RelocateAsync(Move("/Content/Materials/A.omat.json", "/Content/Materials/B.omat.json"), "Move", new HistoryKeeper(this), this.TestContext.CancellationToken).ConfigureAwait(false);
        var second = await service.RelocateAsync(Move("/Content/Materials/B.omat.json", "/Content/Materials/C.omat.json"), "Move", new HistoryKeeper(this), this.TestContext.CancellationToken).ConfigureAwait(false);

        _ = redirects.Resolve(AssetPath.Parse("/Content/Materials/A.omat")).Should().Be(AssetPath.Parse("/Content/Materials/C.omat"));
        _ = redirects.Resolve(AssetPath.Parse("/Content/Materials/Other.omat")).Should().Be(AssetPath.Parse("/Content/Materials/Other.omat"));

        _ = await service.RelocateAsync(second.Reverse, "Move", new HistoryKeeper(this), this.TestContext.CancellationToken).ConfigureAwait(false);
        _ = redirects.Resolve(AssetPath.Parse("/Content/Materials/A.omat")).Should().Be(AssetPath.Parse("/Content/Materials/B.omat"));
    }

    /// <summary>A reference whose asset exists again at its old path is never redirected.</summary>
    /// <returns>The asynchronous test.</returns>
    [TestMethod]
    public async Task ExistingAssetIsNotRedirected()
    {
        using var workspace = new RelocationWorkspace();
        var redirects = new AssetRedirects(workspace.Session);
        var service = workspace.CreateService(redirects);
        _ = workspace.Write("Content/Materials/A.omat.json", "{}");
        _ = await service.RelocateAsync(Move("/Content/Materials/A.omat.json", "/Content/Materials/B.omat.json"), "Move", new HistoryKeeper(this), this.TestContext.CancellationToken).ConfigureAwait(false);
        _ = workspace.Write("Content/Materials/A.omat.json", "{}");

        _ = redirects.Resolve(AssetPath.Parse("/Content/Materials/A.omat")).Should().Be(AssetPath.Parse("/Content/Materials/A.omat"));
    }

    /// <summary>Deleted assets are reported, until something exists at that path again.</summary>
    /// <returns>The asynchronous test.</returns>
    [TestMethod]
    public async Task DeletedAssetsAreReported()
    {
        using var workspace = new RelocationWorkspace();
        var redirects = new AssetRedirects(workspace.Session);
        var service = workspace.CreateService(redirects);
        var path = workspace.Write("Content/Materials/A.omat.json", "{}");

        await service.DeleteAsync(
            [AssetPath.Parse("/Content/Materials/A.omat.json")],
            paths =>
            {
                workspace.FileSystem.File.Delete(path);
                return Task.CompletedTask;
            },
            this.TestContext.CancellationToken).ConfigureAwait(false);

        _ = redirects.WasDeleted(AssetPath.Parse("/Content/Materials/A.omat.json")).Should().BeTrue();
        _ = redirects.WasDeleted(AssetPath.Parse("/Content/Materials/B.omat.json")).Should().BeFalse();
        _ = workspace.Write("Content/Materials/A.omat.json", "{}");
        _ = redirects.WasDeleted(AssetPath.Parse("/Content/Materials/A.omat.json")).Should().BeFalse();
    }

    /// <summary>Participants follow the committed change, with the written versions, before the relocation returns.</summary>
    /// <returns>The asynchronous test.</returns>
    [TestMethod]
    public async Task ParticipantsFollowBeforeRelocationReturns()
    {
        using var workspace = new RelocationWorkspace();
        var service = workspace.CreateService(redirects: null);
        _ = workspace.Write("Content/Materials/A.omat.json", "{}");
        var scene = workspace.FileOf("Content/Scenes/Main.oscene.json");
        _ = workspace.Write("Content/Scenes/Main.oscene.json", """{ "RootNodes": [ { "Components": [ { "OverrideSlots": [ { "MaterialUri": "asset:///Content/Materials/A.omat" } ] } ] } ] }""");
        AssetRelocationChange? followed = null;
        using var registration = service.AddParticipant(new Participant(change => followed = change));

        _ = await service.RelocateAsync(Move("/Content/Materials/A.omat.json", "/Content/Materials/B.omat.json"), "Move", new HistoryKeeper(this), this.TestContext.CancellationToken).ConfigureAwait(false);

        _ = followed.Should().NotBeNull();
        var rewrite = followed!.FindRewrite(scene);
        _ = rewrite.Should().NotBeNull();
        _ = rewrite!.Written.Should().Be((await workspace.Files.ReadAsync(scene.FullPath, this.TestContext.CancellationToken).ConfigureAwait(false)).Version);
        _ = followed.MapAsset(AssetPath.Parse("/Content/Materials/A.omat")).Should().Be(AssetPath.Parse("/Content/Materials/B.omat"));
        _ = followed.MapPath(workspace.FileOf("Content/Materials/A.omat.json")).Should().Be(workspace.FileOf("Content/Materials/B.omat.json"));
    }

    private static AssetRelocationRequest Move(string source, string target) => new() { Moves = [new(AssetPath.Parse(source), AssetPath.Parse(target))] };

    private sealed class Participant(Action<AssetRelocationChange> follow) : IAssetRelocationParticipant
    {
        public Task FollowAsync(AssetRelocationChange change)
        {
            follow(change);
            return Task.CompletedTask;
        }
    }
}
