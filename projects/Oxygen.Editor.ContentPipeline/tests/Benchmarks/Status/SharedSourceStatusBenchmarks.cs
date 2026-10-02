// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using Oxygen.Editor.ContentPipeline.TestSupport;

namespace Oxygen.Editor.ContentPipeline.Benchmarks.Tests.Status;

[TestClass]
[System.Diagnostics.CodeAnalysis.SuppressMessage("Maintainability", "CA1515:Consider making public types internal", Justification = "MSTest discovers public test classes with the repository discovery configuration.")]
[System.Diagnostics.CodeAnalysis.SuppressMessage("Naming", "CA1707:Identifiers should not contain underscores", Justification = "Scenario-based MSTest method names separate the operation and expected behavior.")]
public sealed class SharedSourceStatusBenchmarks
{
    public TestContext TestContext { get; set; } = null!;

    [TestMethod]
    public async Task ImportedModelBulkStatusPreservesIndividualOutputAvailability()
    {
        var elapsed = await SharedSourceStatusScenario.RunAsync(this.TestContext, 256).ConfigureAwait(false);
        this.TestContext.WriteLine($"Shared-source status: 256 meshes; {elapsed.TotalMilliseconds:F2} ms");
    }
}
