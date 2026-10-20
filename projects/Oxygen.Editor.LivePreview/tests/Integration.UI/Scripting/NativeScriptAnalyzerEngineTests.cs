// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Text;
using AwesomeAssertions;
using Oxygen.Editor.LivePreview.Scripting;
using Oxygen.Editor.LivePreview.Tests.TestSupport.Native;
using Oxygen.Editor.Scripting;
using Oxygen.Editor.World.Components;
using Oxygen.Managed.Core.Diagnostics;

namespace Oxygen.Editor.LivePreview.Integration.UI.Tests.Scripting;

/// <summary>
/// Checks the engine's analyzer through <see cref="NativeScriptAnalyzer"/> on the shared engine: a script's declared
/// parameters, and a syntax error, in the Scripting module's vocabulary.
/// </summary>
[TestClass]
[TestCategory("Integration")]
public sealed partial class NativeScriptAnalyzerEngineTests : DroidNet.Tests.VisualUserInterfaceTests
{
    private const string Move = """
        local Script = oxygen.script {
          params = {
            speed = oxygen.param.float { default = 1, min = 0 },
            target = oxygen.param.node { description = "Node to follow" },
          },
        }

        function Script:on_start()
        end

        return Script
        """;

    public TestContext TestContext { get; set; } = null!;

    [TestMethod]
    public Task AScriptsParametersAndASyntaxErrorAreRead() => EnqueueAsync(async () =>
    {
        using var timeout = CancellationTokenSource.CreateLinkedTokenSource(this.TestContext.CancellationToken);
        timeout.CancelAfter(TimeSpan.FromMinutes(2));
        var engine = await SharedEngine.EnsureRunningAsync(timeout.Token).ConfigureAwait(true);
        using var analyzer = new NativeScriptAnalyzer(engine);
        await analyzer.WaitUntilAvailableAsync(timeout.Token).ConfigureAwait(true);

        var move = analyzer.Analyze("Content/move.lua", Encoding.UTF8.GetBytes(Move), definitions: null);
        var broken = analyzer.Analyze("Content/broken.lua", Encoding.UTF8.GetBytes("local x = ("), definitions: null);

        _ = move.IsUnavailable.Should().BeFalse();
        _ = move.HasDeclaration.Should().BeTrue();
        _ = move.Parameters.Select(static parameter => (parameter.Name, parameter.Kind)).Should().Equal(("speed", ScriptParameterKind.FloatParameter), ("target", ScriptParameterKind.NodeParameter));
        _ = move.Parameters[0].Default.Should().Be(ScriptValue.FromFloat(1f));
        _ = move.Parameters[0].Min.Should().Be(0);
        _ = move.Parameters[1].Description.Should().Be("Node to follow");
        _ = broken.Parameters.Should().BeEmpty();
        _ = broken.Problems.Should().Contain(static problem => problem.Code == "S0101" && problem.Severity == DiagnosticSeverity.Error);
    });
}
