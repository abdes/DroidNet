// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using AwesomeAssertions;
using Oxygen.Editor.LivePreview.Scripting;
using Oxygen.Editor.LivePreview.Tests.TestSupport;
using Oxygen.Editor.Runtime.Engine;

namespace Oxygen.Editor.LivePreview.Tests.Scripting;

/// <summary>
/// The native analyzer is available once the engine service has verified the SDK it loads from, and not before.
/// </summary>
[TestClass]
public sealed class NativeScriptAnalyzerTests
{
    public TestContext TestContext { get; set; } = null!;

    [TestMethod]
    public async Task TheAnalyzerWaitsUntilTheEngineHasVerifiedTheSdk()
    {
        var engine = new FakeEngine(EngineServiceState.Initializing);
        await using (engine.ConfigureAwait(false))
        {
            using var analyzer = new NativeScriptAnalyzer(engine);

            var available = analyzer.WaitUntilAvailableAsync(this.TestContext.CancellationToken);
            _ = available.IsCompleted.Should().BeFalse("the SDK is not verified yet");

            engine.ChangeState(EngineServiceState.Ready);

            await available.WaitAsync(TimeSpan.FromSeconds(10), this.TestContext.CancellationToken).ConfigureAwait(false);
        }
    }

    [TestMethod]
    public async Task AnEngineAlreadyRunningMakesTheAnalyzerAvailable()
    {
        var engine = new FakeEngine(EngineServiceState.Running);
        await using (engine.ConfigureAwait(false))
        {
            using var analyzer = new NativeScriptAnalyzer(engine);

            _ = analyzer.WaitUntilAvailableAsync(this.TestContext.CancellationToken).IsCompletedSuccessfully.Should().BeTrue();
        }
    }
}
