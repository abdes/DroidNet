// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Diagnostics.CodeAnalysis;
using AwesomeAssertions;
using DroidNet.TimeMachine.Changes;

namespace DroidNet.TimeMachine.Tests.Changes;

[TestClass]
[ExcludeFromCodeCoverage]
[TestCategory("Changes.Async")]
public class AsyncChangeTypesTests
{
    public TestContext TestContext { get; set; }

    [TestMethod]
    public async Task AsyncSimpleAction_ApplyAsync_ShouldInvokeAction()
    {
        // Arrange
        var invoked = false;
        var change = new AsyncSimpleAction(() =>
        {
            invoked = true;
            return ValueTask.CompletedTask;
        })
        {
            Key = "k",
        };

        // Act
        await change.ApplyAsync(this.TestContext.CancellationToken).ConfigureAwait(false);

        // Assert
        _ = invoked.Should().BeTrue();
    }

    [TestMethod]
    public void AsyncSimpleAction_Apply_ShouldThrow()
    {
        // Arrange
        var change = new AsyncSimpleAction(() => ValueTask.CompletedTask) { Key = "k" };

        // Act
        var act = change.Apply;

        // Assert
        _ = act.Should().Throw<InvalidOperationException>();
    }
}
