// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Diagnostics;
using System.Diagnostics.CodeAnalysis;
using FluentAssertions;

namespace DroidNet.TestHelpers.Tests;

[TestClass]
[ExcludeFromCodeCoverage]
[TestCategory("Test Assertions")]
public partial class DebugAssertionTests : TestSuiteWithAssertions
{
    [TestMethod]
    public void AssertionFailures_AreRecorded_WhenTheyHappen()
    {
        MethodWithAssert(truth: false);

#if DEBUG
        _ = this.TraceListener.RecordedMessages.Should().Contain(message => message.StartsWith("Fail: "));
#else
        _ = this.TraceListener.RecordedMessages.Should().BeEmpty();
#endif
    }

    [TestMethod]
    public void NoRecords_WhenNoAssertionFailures()
    {
        MethodWithAssert(truth: true);

        _ = this.TraceListener.RecordedMessages.Should().BeEmpty();
    }

    private static void MethodWithAssert(bool truth) => Debug.Assert(truth, "assert failure was requested");
}
