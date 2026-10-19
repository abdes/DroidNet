// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Text;
using AwesomeAssertions;
using Oxygen.Editor.ContentPipeline.Authoring;
using Oxygen.Editor.Projects;
using Oxygen.Editor.World.Scripting;
using Oxygen.Managed.Assets.Authoring.Input;
using Oxygen.Managed.Core;

namespace Oxygen.Editor.WorldEditor.Unit.Tests.Scripting;

/// <summary>
/// Saving an input source reports what its cook would reject, against the engine's own input schema and the project's
/// input sources.
/// </summary>
[TestClass]
public sealed class InputFileCheckTests : IDisposable
{
    private static readonly AssetPath Source = AssetPath.Parse("/Content/Input/Player.input");

    private readonly string root = Path.Combine(Path.GetTempPath(), "oxygen-input-check-tests", Guid.NewGuid().ToString("N"));
    private readonly ProjectContext project;

    public InputFileCheckTests()
    {
        _ = Directory.CreateDirectory(Path.Combine(this.root, "Content", "Input"));
        File.WriteAllText(Path.Combine(this.root, "Content", "Input", "Common.input.json"), """{ "actions": [ { "name": "Fire", "type": "bool" } ] }""");
        this.project = ProjectContext.FromProject(new Project(new ProjectInfo("Test", Category.Games, this.root)
        {
            AuthoringMounts = [new ProjectMountPoint("Content", "Content")],
        }));
    }

    private string SourcePath => Path.Combine(this.root, "Content", "Input", "Player.input.json");

    public void Dispose()
    {
        if (Directory.Exists(this.root))
        {
            Directory.Delete(this.root, recursive: true);
        }
    }

    [TestMethod]
    public void TheNewInputFileTemplateChecksClean()
        => _ = this.Check(AssetTemplates.InputFile("Player")).Should().BeEmpty();

    [TestMethod]
    public void AnUndeclaredActionIsReportedAtItsUse()
    {
        var text = AssetTemplates.InputFile("Player").Replace("\"action\": \"Jump\"", "\"action\": \"Boost\"", StringComparison.Ordinal);

        var problem = this.Check(text).Should().ContainSingle().Subject;

        _ = problem.Code.Should().Be(InputSourceCodes.ActionUndeclared);
        _ = problem.Line.Should().Be(13);
    }

    [TestMethod]
    public void AnActionOfAnotherSourceIsNamedByItsPath()
    {
        var uses = AssetTemplates.InputFile("Player").Replace("\"action\": \"Jump\"", "\"action\": \"/Content/Input/Common.input/Fire.oiact\"", StringComparison.Ordinal);
        var missing = uses.Replace("Fire.oiact", "Boost.oiact", StringComparison.Ordinal);

        _ = this.Check(uses).Should().BeEmpty();
        var problem = this.Check(missing).Should().ContainSingle().Subject;
        _ = problem.Code.Should().Be(InputSourceCodes.ActionMissing);
        _ = problem.Line.Should().Be(13);
    }

    [TestMethod]
    public void InvalidJsonIsReportedAtItsPosition()
    {
        var problem = this.Check("{\n  \"actions\": [\n").Should().ContainSingle().Subject;

        _ = problem.Code.Should().Be(InputSourceCodes.NotJson);
        _ = problem.Line.Should().NotBeNull();
    }

    [TestMethod]
    public void AValueTheSchemaRejectsIsReportedWhereItIs()
    {
        var text = AssetTemplates.InputFile("Player").Replace("\"slot\": \"Space\"", "\"slot\": \"NotAKey\"", StringComparison.Ordinal);

        _ = this.Check(text).Should().Contain(static problem => problem.Code == InputSourceCodes.SchemaMismatch && problem.Line == 14);
    }

    private IReadOnlyList<Editor.Scripting.ScriptProblem> Check(string text)
        => InputFileCheck.Check(this.project, this.SourcePath, Source, Encoding.UTF8.GetBytes(text));
}
