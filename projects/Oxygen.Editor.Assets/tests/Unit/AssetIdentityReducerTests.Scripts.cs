// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using AwesomeAssertions;
using Oxygen.Managed.Assets.Catalog;
using Oxygen.Managed.Core;

namespace Oxygen.Editor.Assets.Unit.Tests;

/// <summary>
/// Script and input sources in the Content Browser.
/// </summary>
public sealed partial class AssetIdentityReducerTests
{
    /// <summary>A script source is a Script asset identified by the cooked path it produces in its own folder.</summary>
    [TestMethod]
    public void ReduceScriptSourceIsIdentifiedByItsCookedPath()
    {
        using var workspace = new TempWorkspace();
        var source = workspace.SourcePath("Content/Player/move.lua");
        Directory.CreateDirectory(Path.GetDirectoryName(source)!);
        File.WriteAllText(source, "return {}");

        var row = Reduce(workspace, [new AssetRecord(AssetPath.Parse("/Content/Player/move.lua"))]).Single();

        _ = row.Kind.Should().Be(AssetKind.Script);
        _ = row.DisplayName.Should().Be("move");
        _ = row.Identity.Should().Be(AssetPath.Parse("/Content/Player/move.oscript"));
        _ = row.SourcePath.Should().Be(FileSystemPath.From(source));
        _ = row.DiagnosticCodes.Should().BeEmpty();
    }

    /// <summary>Two sources with the same name in one folder are one asset identity; script analysis reports the collision.</summary>
    [TestMethod]
    public void ReduceScriptsSharingANameShowOneAsset()
    {
        using var workspace = new TempWorkspace();
        var rows = Reduce(workspace,
        [
            new AssetRecord(AssetPath.Parse("/Content/Player/move.lua")),
            new AssetRecord(AssetPath.Parse("/Content/Player/move.luau")),
        ]);

        var row = rows.Should().ContainSingle().Subject;
        _ = row.IsSelectable.Should().BeTrue("the user renames one of the files to resolve the collision");
    }

    /// <summary>Hand-written input files are Input file assets named after the file.</summary>
    [TestMethod]
    [DataRow("Player.input.json")]
    [DataRow("Jump.input-action.json")]
    public void ReduceInputSourceIsAnInputFile(string fileName)
    {
        using var workspace = new TempWorkspace();

        var row = Reduce(workspace, [new AssetRecord(AssetPath.Parse("/Content/Gameplay/" + fileName))]).Single();

        _ = row.Kind.Should().Be(AssetKind.InputFile);
        _ = row.DisplayName.Should().Be(fileName[..fileName.IndexOf('.', StringComparison.Ordinal)]);
        _ = row.TypeDisplayName.Should().Be("Input file");
    }
}
