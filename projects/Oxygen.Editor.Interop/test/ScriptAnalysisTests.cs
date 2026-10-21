// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Diagnostics.CodeAnalysis;
using System.Text;
using AwesomeAssertions;
using Oxygen.Editor.Interop.Testing;
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

    [TestMethod]
    public void Render_RuntimeDiagnosticJson_RendersItsSourceLineSlotAndCatalogEntry()
    {
        const string json = """
            {
              "version": 1, "severity": "error", "code": "S0701", "family": "runtime",
              "message": "attempt to index nil with 'position'",
              "labels": [ {
                "span": { "source": "Content/move.lua", "begin": { "line": 10, "column": 9 }, "end": { "line": 10, "column": 13 } },
                "message": "this is nil", "primary": true } ],
              "notes": [ "the node was destroyed" ],
              "slot": { "node": "Player", "slot_index": 1, "hook": "on_gameplay" }
            }
            """;
        var requested = new List<string>();

        var info = ScriptAnalysis.Render(json, source =>
        {
            requested.Add(source);
            return MoveScript;
        });

        _ = info.Should().NotBeNull();
        _ = requested.Should().Equal("Content/move.lua");
        _ = info!.Code.Should().Be("S0701");
        _ = info.Severity.Should().Be(ScriptDiagnosticSeverity.Error);
        _ = info.PrimarySpan!.Value.Line.Should().Be(10);
        _ = info.Rendered.Should().Contain("error[S0701]").And.Contain("fnd_one").And.Contain("the node was destroyed");
        _ = info.Explanation.Should().NotBeEmpty();
        _ = (info.SlotNode, info.SlotIndex, info.SlotHook).Should().Be(("Player", 1, "on_gameplay"));
    }

    [TestMethod]
    public void Render_UnknownSourceOrMalformedJson_RendersWithoutSourceOrReturnsNull()
    {
        const string withoutSource = """
            { "version": 1, "severity": "warning", "code": "S0701", "family": "runtime", "message": "slow hook",
              "labels": [ { "span": { "source": "gone.lua", "begin": { "line": 1, "column": 1 }, "end": { "line": 1, "column": 2 } },
                "message": "here", "primary": true } ], "notes": [] }
            """;

        var rendered = ScriptAnalysis.Render(withoutSource, static _ => null);

        _ = rendered!.Rendered.Should().StartWith("warning[S0701]: slow hook");
        _ = rendered.SlotNode.Should().BeNull();
        _ = ScriptAnalysis.Render("{ not json", static _ => null).Should().BeNull();
        _ = ScriptAnalysis.Render("""{ "severity": "error", "code": "S9999", "message": "m", "labels": [], "notes": [] }""", static _ => null).Should().BeNull();
    }
}
