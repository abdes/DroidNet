// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Diagnostics.CodeAnalysis;
using System.Text;
using AwesomeAssertions;
using Oxygen.Interop.Scripting;

namespace Oxygen.Editor.Interop.Tests;

[TestClass]
[ExcludeFromCodeCoverage]
[TestCategory(nameof(ScriptAnalysis))]
public sealed class ScriptAnalysisTests
{
    private const string MoveScript = """
        local Script = oxygen.script {
          params = {
            speed = oxygen.param.float { default = 2, min = 0, unit = "m/s",
              description = "How fast the node moves" },
            tint = oxygen.param.vec3 { color = true },
          },
        }

        function Script:on_start()
          local node = oxygen.scene.fnd_one("Player")
        end

        return Script
        """;

    [TestMethod]
    public void Analyze_Declarations_ReturnsParametersWithHints()
    {
        var result = ScriptAnalysis.Analyze("Content/move.lua", Encoding.UTF8.GetBytes(MoveScript), ScriptAnalysisLevel.Declarations, typeDefinitions: null);

        _ = result.HasDeclaration.Should().BeTrue();
        _ = result.Parameters.Select(p => p.Name).Should().Equal("speed", "tint");
        var speed = result.Parameters[0];
        _ = speed.Type.Should().Be(ScriptParameterType.Float);
        _ = speed.DefaultValue.Should().Be(2.0f);
        _ = speed.Min.Should().Be(0.0);
        _ = speed.Unit.Should().Be("m/s");
        _ = speed.Description.Should().Be("How fast the node moves");
        _ = speed.Line.Should().Be(3);
        _ = result.Parameters[1].IsColor.Should().BeTrue();
        _ = result.Diagnostics.Should().BeEmpty();
    }

    [TestMethod]
    public void Analyze_TypesWithDefinitions_ReportsUnknownMemberWithSpanAndRendering()
    {
        var definitions = File.ReadAllText(Path.Combine(NativeDllSearchPath.ResolveEngineInstallDirectory(), "share", "oxygen", "scripting", "oxygen.d.luau"));

        var result = ScriptAnalysis.Analyze("Content/move.lua", Encoding.UTF8.GetBytes(MoveScript), ScriptAnalysisLevel.Types, definitions);

        var unknown = result.Diagnostics.Should().ContainSingle(d => d.Code == "S0503").Subject;
        _ = unknown.Severity.Should().Be(ScriptDiagnosticSeverity.Error);
        _ = unknown.PrimarySpan.Should().NotBeNull();
        _ = unknown.PrimarySpan!.Value.SourceName.Should().Be("Content/move.lua");
        _ = unknown.PrimarySpan.Value.Line.Should().Be(10);
        _ = unknown.Summary.Should().StartWith("Content/move.lua:10:");
        _ = unknown.Rendered.Should().Contain("fnd_one").And.Contain("error[S0503]");
    }

    [TestMethod]
    public void Analyze_SyntaxError_ReportsErrorWithoutParameters()
    {
        var result = ScriptAnalysis.Analyze("broken.lua", Encoding.UTF8.GetBytes("local x = ("), ScriptAnalysisLevel.Lint, typeDefinitions: null);

        _ = result.Parameters.Should().BeEmpty();
        _ = result.Diagnostics.Should().Contain(d => d.Code == "S0101" && d.Severity == ScriptDiagnosticSeverity.Error);
    }
}
