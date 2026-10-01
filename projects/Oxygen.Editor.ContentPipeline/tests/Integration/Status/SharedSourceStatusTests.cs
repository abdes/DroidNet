// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using Oxygen.Editor.ContentPipeline.TestSupport;

namespace Oxygen.Editor.ContentPipeline.Integration.Tests.Status;

[TestClass]
[System.Diagnostics.CodeAnalysis.SuppressMessage("Maintainability", "CA1515:Consider making public types internal", Justification = "MSTest discovers public test classes with the repository discovery configuration.")]
public sealed class SharedSourceStatusTests
{
    public TestContext TestContext { get; set; } = null!;

    [TestMethod]
    public async Task SharedSourceStatusPreservesEachOutputWithoutStartingWorkers()
    {
        _ = await SharedSourceStatusScenario.RunAsync(this.TestContext, 3).ConfigureAwait(false);
    }
}
