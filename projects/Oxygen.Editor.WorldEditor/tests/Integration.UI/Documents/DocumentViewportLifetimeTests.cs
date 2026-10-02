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

    [TestMethod]
    [DataRow(60u)]
    [DataRow(10u)]
    public Task MainInspectionMainReleasesEachNativeView(uint targetFps)
        => new DocumentTransitionScenario(this.TestContext).RunAsync(targetFps, 3);
}
