// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using AwesomeAssertions;
using Oxygen.Editor.ContentBrowser.Scripts;

namespace Oxygen.Editor.ContentBrowser.Tests;

[TestClass]
public sealed class ScriptAuthoringTests
{
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
}
