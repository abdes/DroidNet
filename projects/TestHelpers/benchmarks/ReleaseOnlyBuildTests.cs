// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

namespace DroidNet.TestHelpers.Benchmarks;

/// <summary>
///     Compiled into every benchmark project (see <c>Directory.build.targets</c>), so a Debug run, where every
///     <see cref="ReleaseOnlyAttribute"/> test is skipped, still runs one test and the runner does not fail with
///     "zero tests ran".
/// </summary>
[TestClass]
[System.Diagnostics.CodeAnalysis.SuppressMessage("Maintainability", "CA1515:Consider making public types internal", Justification = "MSTest discovers public test classes with the repository discovery configuration.")]
public sealed class ReleaseOnlyBuildTests
{
    [TestMethod]
    public void ReleaseOnlyTestsRunOnlyInReleaseBuilds()
    {
#if DEBUG
        Assert.IsFalse(new ReleaseOnlyAttribute().IsConditionMet);
#else
        Assert.IsTrue(new ReleaseOnlyAttribute().IsConditionMet);
#endif
    }
}
