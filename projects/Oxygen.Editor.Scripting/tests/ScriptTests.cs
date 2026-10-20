// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using AwesomeAssertions;
using Oxygen.Managed.Core;
using Oxygen.Managed.Core.Diagnostics;

namespace Oxygen.Editor.Scripting.Tests;

/// <summary>
/// A script counts its errors and warnings, notes aside, and finds a declared parameter by its exact name.
/// </summary>
[TestClass]
public sealed class ScriptTests
{
    [TestMethod]
    public void AScriptCountsItsErrorsAndWarnings()
    {
        var script = new Script
        {
            Identity = AssetPath.Parse("/Content/move.oscript"),
            Origin = ScriptOrigin.Source,
            Problems = [Problem(DiagnosticSeverity.Error), Problem(DiagnosticSeverity.Warning), Problem(DiagnosticSeverity.Info)],
            Parameters = [new ScriptParameter { Name = "speed", Kind = ScriptParameterKind.FloatParameter }],
        };

        _ = (script.ErrorCount, script.WarningCount).Should().Be((1, 1));
        _ = script.ProblemSummary.Should().Be("1 error, 1 warning");
        _ = Script.DescribeCounts(0, 3).Should().Be("3 warnings");
        _ = script.FindParameter("speed").Should().NotBeNull();
        _ = script.FindParameter("Speed").Should().BeNull();
    }

    private static ScriptProblem Problem(DiagnosticSeverity severity) => new(severity, "S0001", "a problem", "rendered");
}
