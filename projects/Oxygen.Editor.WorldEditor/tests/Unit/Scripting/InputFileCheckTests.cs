// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using AwesomeAssertions;
using Json.Schema;
using Oxygen.Editor.ContentPipeline.Authoring;
using Oxygen.Editor.World.Scripting;
using Oxygen.Managed.Core.Diagnostics;

namespace Oxygen.Editor.WorldEditor.Unit.Tests.Scripting;

/// <summary>
/// Saving an input file reports what its cook would reject, against the engine's own input schema.
/// </summary>
[TestClass]
public sealed class InputFileCheckTests
{
    private const string Path = @"C:\Project\Content\Input\Player.input.json";

    private static readonly JsonSchema Schema = JsonSchema.FromText(File.ReadAllText(System.IO.Path.Combine(AppContext.BaseDirectory, "Schemas", "oxygen.input.schema.json")));

    [TestMethod]
    public void TheNewInputFileTemplateChecksClean()
        => _ = InputFileCheck.Check(Path, ScriptTemplates.InputFile("Player"), Schema).Should().BeEmpty();

    [TestMethod]
    public void AnUndeclaredActionIsReportedAtItsUse()
    {
        var text = ScriptTemplates.InputFile("Player").Replace("\"action\": \"Jump\"", "\"action\": \"Boost\"", StringComparison.Ordinal);

        var problem = InputFileCheck.Check(Path, text, Schema).Should().ContainSingle().Subject;

        _ = problem.Code.Should().Be(ScriptDiagnosticCodes.InputReferenceUnsupported);
        _ = problem.Message.Should().Contain("'Boost'");
        _ = problem.Line.Should().Be(13);
    }

    [TestMethod]
    public void InvalidJsonIsReportedAtItsPosition()
    {
        var problem = InputFileCheck.Check(Path, "{\n  \"actions\": [\n", Schema).Should().ContainSingle().Subject;

        _ = problem.Code.Should().Be(ScriptDiagnosticCodes.InputInvalid);
        _ = problem.Line.Should().NotBeNull();
    }

    [TestMethod]
    public void AValueTheSchemaRejectsIsReported()
    {
        var text = ScriptTemplates.InputFile("Player").Replace("\"slot\": \"Space\"", "\"slot\": \"NotAKey\"", StringComparison.Ordinal);

        _ = InputFileCheck.Check(Path, text, Schema).Should().Contain(static problem => problem.Code == ScriptDiagnosticCodes.InputInvalid);
    }
}
