// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using DroidNet.Tests;
using Oxygen.Editor.WorldEditor.TestSupport;

namespace Oxygen.Editor.WorldEditor.Integration.UI.Tests.Documents;

[TestClass]
public sealed class DocumentViewportLifetimeTests : VisualUserInterfaceTests
{
    public TestContext TestContext { get; set; } = null!;

    /// <summary>One Main/Inspect/Main transition at a slow native cadence releases the old view and recreates it.</summary>
    /// <returns>The asynchronous document-transition regression.</returns>
    [TestMethod]
    public Task MainInspectionMainReleasesTheNativeView()
        => new DocumentTransitionScenario(this.TestContext).RunAsync(targetFps: 10, cycles: 1);
}
