// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Text.Json.Nodes;
using AwesomeAssertions;
using DroidNet.TimeMachine;
using Microsoft.Extensions.Logging.Abstractions;
using Oxygen.Editor.ContentPipeline.Relocation;
using Oxygen.Editor.Projects;
using Oxygen.Editor.Projects.Mounts;
using Oxygen.Editor.Projects.Testing;
using Oxygen.Managed.Assets.Testing;
using Oxygen.Managed.Core;
using Oxygen.Managed.Core.Diagnostics;

namespace Oxygen.Editor.ContentPipeline.Unit.Tests.Relocation;

/// <summary>
/// A folder outside the project, mounted as a source mount, is relocated like <c>Content</c>; a shared asset moved while
/// the project was closed leaves its references as written, and the reference check lists them, in the Problems pane
/// and in <c>.build/references.json</c>, until they are rewritten.
/// </summary>
[TestClass]
[System.Diagnostics.CodeAnalysis.SuppressMessage("Maintainability", "CA1515:Consider making public types internal", Justification = "MSTest discovers public test classes with the repository discovery configuration.")]
public sealed class SharedMountTests
{
    private const string Studio = @"D:\Studio\Shared";

    public TestContext TestContext { get; set; } = null!;

    /// <summary>Renaming a texture in a mount outside the project moves its files there and rewrites its referrers.</summary>
    /// <returns>The asynchronous test.</returns>
    [TestMethod]
    public async Task RenamingATextureInAMountOutsideTheProject_MovesItThereAndRewritesItsReferrers()
    {
        using var workspace = CreateWorkspace();

        _ = await workspace.Service.RelocateAsync(Move("/Studio/Textures/Brick.otex.json", "/Studio/Textures/Stone.otex.json"), "Move", new HistoryKeeper(this), this.TestContext.CancellationToken).ConfigureAwait(false);

        _ = workspace.FileSystem.File.Exists(Path.Combine(Studio, "Textures", "Stone.otex.json")).Should().BeTrue();
        _ = workspace.FileSystem.File.Exists(Path.Combine(Studio, "Textures", "Stone.png")).Should().BeTrue();
        _ = workspace.FileSystem.File.Exists(Path.Combine(Studio, "Textures", "Brick.otex.json")).Should().BeFalse();
        _ = workspace.ReadJson("Content/Materials/Wall.omat.json")["textures"]!["base_color"]!["virtual_path"]!.GetValue<string>()
            .Should().Be(AuthoredFiles.SpellReference(AssetPath.Parse("/Content/Materials/Wall.omat"), AssetPath.Parse("/Studio/Textures/Stone.otex")));
        _ = JsonNode.Parse(workspace.FileSystem.File.ReadAllText(Path.Combine(Studio, "Textures", "Stone.otex.json")))!["virtual_path"]!.GetValue<string>().Should().Be("/Studio/Textures/Stone.otex");
    }

    /// <summary>
    /// A shared texture moved while the project was closed: its material keeps the old path, the check lists it with
    /// its field and code and writes it to <c>.build/references.json</c>, and after the rewrite the list is clear.
    /// </summary>
    /// <returns>The asynchronous test.</returns>
    [TestMethod]
    public async Task ASharedAssetMovedWhileTheProjectWasClosed_IsListedUntilItsReferencesAreRewritten()
    {
        using var workspace = CreateWorkspace();
        workspace.FileSystem.File.Move(Path.Combine(Studio, "Textures", "Brick.otex.json"), Path.Combine(Studio, "Textures", "Walls.otex.json"));
        workspace.FileSystem.File.Move(Path.Combine(Studio, "Textures", "Brick.png"), Path.Combine(Studio, "Textures", "Walls.png"));
        using var check = new ReferenceCheck(workspace.Session, NullLogger<ReferenceCheck>.Instance);

        var unresolved = await check.RunAsync(this.TestContext.CancellationToken).ConfigureAwait(false);

        var reference = unresolved.Should().ContainSingle().Subject;
        _ = reference.File.Should().Be(workspace.FileOf("Content/Materials/Wall.omat.json"));
        _ = reference.Property.Should().Be("$.textures.base_color.virtual_path");
        _ = reference.MountPath.Should().Be("/Studio/Textures/Brick.otex");
        _ = reference.Code.Should().Be(AssetIdentityDiagnosticCodes.ResolveMissing);
        _ = check.Problems.Should().ContainSingle().Which.Code.Should().Be(AssetIdentityDiagnosticCodes.ResolveMissing);
        var written = ReadReferences(workspace);
        _ = written["version"]!.GetValue<int>().Should().Be(1);
        var entry = written["references"]!.AsArray().Should().ContainSingle().Subject!;
        _ = entry["file"]!.GetValue<string>().Should().Be("/Content/Materials/Wall.omat.json");
        _ = entry["property"]!.GetValue<string>().Should().Be("$.textures.base_color.virtual_path");
        _ = entry["missing"]!.GetValue<string>().Should().Be("/Studio/Textures/Brick.otex");
        _ = entry["code"]!.GetValue<string>().Should().Be(AssetIdentityDiagnosticCodes.ResolveMissing);

        _ = workspace.Write("Content/Materials/Wall.omat.json", WallMaterial("/Studio/Textures/Walls.otex").Text);
        _ = await check.RunAsync(this.TestContext.CancellationToken).ConfigureAwait(false);

        _ = check.Unresolved.Should().BeEmpty();
        _ = check.Problems.Should().BeEmpty();
        _ = ReadReferences(workspace)["references"]!.AsArray().Should().BeEmpty();
    }

    /// <summary>A reference into a mount this machine has not bound is listed with the mount's code, not as missing.</summary>
    /// <returns>The asynchronous test.</returns>
    [TestMethod]
    public async Task AReferenceIntoAnUnboundMount_IsListedWithTheMountsCode()
    {
        using var workspace = CreateWorkspace();
        workspace.FileSystem.File.Delete(workspace.PathOf(ProjectMounts.FileName));
        workspace.Session.ChangeContext(TestProject.Open(TestProject.InfoOf(workspace.Project), workspace.FileSystem).Context);

        var unresolved = await ReferenceCheck.FindAsync(workspace.Project, this.TestContext.CancellationToken).ConfigureAwait(false);

        _ = unresolved.Should().ContainSingle().Which.Code.Should().Be(MountCodes.Unbound);
    }

    // Content holds a material that references a texture of Studio, a folder outside the project.
    private static RelocationWorkspace CreateWorkspace()
    {
        var workspace = new RelocationWorkspace();
        workspace.MountOutside("Studio", Studio);
        workspace.WriteOutside(Path.Combine(Studio, "Textures", "Brick.otex.json"), AuthoredFiles.Texture("/Studio/Textures/Brick.otex", "Brick.png").WithIdentityField().Set("/name", "Brick").Text);
        workspace.WriteOutside(Path.Combine(Studio, "Textures", "Brick.png"), "png");
        _ = workspace.Write("Content/Materials/Wall.omat.json", WallMaterial("/Studio/Textures/Brick.otex").Text);
        return workspace;
    }

    private static MaterialDocument WallMaterial(string texture) => AuthoredFiles.Material("/Content/Materials/Wall.omat").Texture("base_color", texture);

    private static JsonNode ReadReferences(RelocationWorkspace workspace)
        => JsonNode.Parse(workspace.FileSystem.File.ReadAllText(ProjectOutputFolders.BuildPath(workspace.Root, ReferenceCheck.FileName)))!;

    private static AssetRelocationRequest Move(string source, string target) => new() { Moves = [new(AssetPath.Parse(source), AssetPath.Parse(target))] };
}
