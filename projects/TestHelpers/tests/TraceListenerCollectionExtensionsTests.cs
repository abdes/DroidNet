// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Diagnostics;
using System.Diagnostics.CodeAnalysis;
using FluentAssertions;

namespace DroidNet.TestHelpers.Tests;

[TestClass]
[ExcludeFromCodeCoverage]
[TestCategory("Suspend Assertions")]
public class TraceListenerCollectionExtensionsTests
{
    [TestMethod]
    public void AssertSuspend_ShouldSuspendAndResumeTraceListeners()
    {
        // Arrange
        var traceListenerCollection = Trace.Listeners;
        var listener = new DefaultTraceListener();
        _ = traceListenerCollection.Add(listener);

        // Act
        using (traceListenerCollection.AssertSuspend())
        {
            // Assert
            _ = traceListenerCollection.Count.Should().Be(0);
        }

        // Assert
        _ = traceListenerCollection.Contains(listener).Should().BeTrue();
        traceListenerCollection.Remove(listener);
    }

    [TestMethod]
    public void AssertSuspend_ShouldNotThrow_WhenNoListeners()
    {
        // Arrange
        var traceListenerCollection = Trace.Listeners;

        // Act
        Action act = () =>
        {
            using (traceListenerCollection.AssertSuspend())
            {
                // Assert
                _ = traceListenerCollection.Count.Should().Be(0);
            }
        };

        // Assert
        _ = act.Should().NotThrow();
    }
}
