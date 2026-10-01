// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using DroidNet.Tests;
using Oxygen.Editor.WorldEditor.TestSupport;

namespace Oxygen.Editor.WorldEditor.Benchmarks.UI.Tests.SceneEditor;

[TestClass]
public sealed class DocumentViewportStressTests : VisualUserInterfaceTests
{
    public TestContext TestContext { get; set; } = null!;

    [TestMethod]
    [DataRow(60u)]
    [DataRow(10u)]
    public Task RepeatedMainInspectionTransitionsKeepResourcesBalanced(uint targetFps)
        => new DocumentTransitionScenario(this.TestContext).RunAsync(targetFps, 30);
}
