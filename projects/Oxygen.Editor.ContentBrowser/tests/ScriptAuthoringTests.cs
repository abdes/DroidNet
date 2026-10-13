// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using AwesomeAssertions;
using Oxygen.Editor.ContentBrowser.Scripts;
using Oxygen.Editor.Projects;
using Oxygen.Editor.World;

namespace Oxygen.Editor.ContentBrowser.Tests;

[TestClass]
public sealed class ScriptAuthoringTests : IDisposable
{
    private readonly string root = Path.Combine(Path.GetTempPath(), "oxygen-script-authoring-tests", Guid.NewGuid().ToString("N"));

    public ScriptAuthoringTests() => Directory.CreateDirectory(Path.Combine(this.root, "Content", "Player"));

    private ProjectContext Project => new()
    {
        ProjectId = Guid.NewGuid(),
        Name = "Test",
        Category = Category.Games,
        ProjectRoot = this.root,
        AuthoringMounts = [new ProjectMountPoint("Content", "Content")],
        LocalFolderMounts = [],
        Scenes = [],
    };

    public void Dispose()
    {
        if (Directory.Exists(this.root))
        {
            Directory.Delete(this.root, recursive: true);
        }
    }

    [TestMethod]
    public void CreateVisualStudioCodeOpensTheProjectAtTheLine()
    {
        var start = CodeEditorLaunch.Create(new(CodeEditorPreference.CurrentVersion, CodeEditorKind.VisualStudioCode, null), @"C:\p\Content\move.lua", 12, 18, @"C:\p", @"C:\vs\Code.exe");

        _ = start.FileName.Should().Be(@"C:\vs\Code.exe");
        _ = start.ArgumentList.Should().Equal(@"C:\p", "--goto", @"C:\p\Content\move.lua:12:18");
        _ = start.UseShellExecute.Should().BeFalse();
    }

    [TestMethod]
    public void CreateVisualStudioCodeWithoutAnInstallationFails()
    {
        var create = () => CodeEditorLaunch.Create(new(CodeEditorPreference.CurrentVersion, CodeEditorKind.VisualStudioCode, null), "move.lua", null, null, null, visualStudioCode: null);

        _ = create.Should().Throw<InvalidOperationException>();
    }

    [TestMethod]
    public void CreateSystemDefaultOpensTheFileThroughTheShell()
    {
        var start = CodeEditorLaunch.Create(new(CodeEditorPreference.CurrentVersion, CodeEditorKind.SystemDefault, null), @"C:\p\move.lua", 3, 1, null, null);

        _ = start.FileName.Should().Be(@"C:\p\move.lua");
        _ = start.UseShellExecute.Should().BeTrue();
    }

    [TestMethod]
    public void CreateCustomExpandsPlaceholdersAndKeepsQuotedWordsTogether()
    {
        var preference = new CodeEditorPreference(CodeEditorPreference.CurrentVersion, CodeEditorKind.Custom, @"""C:\Tools\My Editor.exe"" -n{line} -c{column} ""{file}""");

        var start = CodeEditorLaunch.Create(preference, @"C:\p\move.lua", 7, 4, null, null);

        _ = start.FileName.Should().Be(@"C:\Tools\My Editor.exe");
        _ = start.ArgumentList.Should().Equal("-n7", "-c4", @"C:\p\move.lua");
    }

    [TestMethod]
    public void CreateCustomWithoutFilePlaceholderAppendsTheFile()
    {
        var preference = new CodeEditorPreference(CodeEditorPreference.CurrentVersion, CodeEditorKind.Custom, @"C:\Tools\edit.exe --wait");

        var start = CodeEditorLaunch.Create(preference, @"C:\p\move.lua", null, null, null, null);

        _ = start.ArgumentList.Should().Equal("--wait", @"C:\p\move.lua");
    }

    [TestMethod]
    public void PreferenceWithoutCustomCommandIsInvalid()
        => _ = new CodeEditorPreference(CodeEditorPreference.CurrentVersion, CodeEditorKind.Custom, " ").IsValid.Should().BeFalse();

    [TestMethod]
    public void DefaultFolderIsTheBrowsedFolderOrTheKindFolderAtTheMountRoot()
    {
        // The Content Browser selects folders by virtual path.
        _ = ScriptAssetCreation.GetDefaultFolder(this.Project, "/Content/Player", "Scripts").Should().Be("/Content/Player");
        _ = ScriptAssetCreation.GetDefaultFolder(this.Project, "/Content", "Scripts").Should().Be("/Content/Scripts");
        _ = ScriptAssetCreation.GetDefaultFolder(this.Project, "/", "Scripts").Should().Be("/Content/Scripts");
        _ = ScriptAssetCreation.GetDefaultFolder(this.Project, null, "Input").Should().Be("/Content/Input");
    }

    [TestMethod]
    public void ValidateRejectsANameThatWouldCookToAnExistingScript()
    {
        File.WriteAllText(Path.Combine(this.root, "Content", "Player", "move.luau"), string.Empty);

        _ = ScriptAssetCreation.Validate(this.Project, "move", "/Content/Player", [".lua", ".luau"], 63).Should().Contain("move.luau");
        _ = ScriptAssetCreation.Validate(this.Project, "jump", "/Content/Player", [".lua", ".luau"], 63).Should().BeNull();
        _ = ScriptAssetCreation.Validate(this.Project, "jump", "/Elsewhere", [".lua", ".luau"], 63).Should().NotBeNull();
    }

    [TestMethod]
    public void InputSourceIndexFindsTheDeclaringFileAndLine()
    {
        var file = Path.Combine(this.root, "Content", "Player", "Player.input.json");
        File.WriteAllText(file, ScriptTemplates.InputFile("Gameplay"));

        var action = InputSourceIndex.Find(Path.GetDirectoryName(file)!, "Jump", isContext: false);
        var context = InputSourceIndex.Find(Path.GetDirectoryName(file)!, "Gameplay", isContext: true);

        _ = action.Should().Be(new InputDeclaration(file, 4));
        _ = context!.SourcePath.Should().Be(file);
        _ = InputSourceIndex.Find(Path.GetDirectoryName(file)!, "Gameplay", isContext: false).Should().BeNull();
    }

    [TestMethod]
    public void SnapshotDescribesProblemCounts()
    {
        _ = ScriptAnalysisSnapshot.DescribeCounts(2, 1).Should().Be("2 errors, 1 warning");
        _ = ScriptAnalysisSnapshot.DescribeCounts(0, 3).Should().Be("3 warnings");
    }
}
