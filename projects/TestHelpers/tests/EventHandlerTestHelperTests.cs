// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Diagnostics.CodeAnalysis;
using FluentAssertions;

namespace DroidNet.TestHelpers.Tests;

[TestClass]
[ExcludeFromCodeCoverage]
[TestCategory("Event Handler Tests")]
public class EventHandlerTestHelperTests
{
    [TestMethod]
    public void FindAllRegisteredDelegates_ShouldReturnAllDelegates()
    {
        // Arrange
        var emitter = new TestEventEmitter();

        emitter.TestEvent += Handler1;
        emitter.TestEvent += Handler2;

        // Act
        var delegates = EventHandlerTestHelper.FindAllRegisteredDelegates(emitter, "TestEvent");

        // Assert
        _ = delegates.Should().Contain([(EventHandler)Handler1, (EventHandler)Handler2]);
        return;

        static void Handler1(object? sender, EventArgs args)
        {
        }

        static void Handler2(object? sender, EventArgs args)
        {
        }
    }

    [TestMethod]
    public void FindAllRegisteredDelegates_ShouldReturnEmptyList_WhenNoDelegatesRegistered()
    {
        // Arrange
        var emitter = new TestEventEmitter();

        // Act
        var delegates = EventHandlerTestHelper.FindAllRegisteredDelegates(emitter, "TestEvent");

        // Assert
        _ = delegates.Should().BeEmpty();
    }

    [TestMethod]
    public void FindRegisteredDelegates_ShouldReturnDelegatesForSpecificTarget()
    {
        // Arrange
        var emitter = new TestEventEmitter();
        var target = new TestTarget();

        emitter.TestEvent += target.Handler1;
        emitter.TestEvent += target.Handler2;
        emitter.TestEvent += target.Handler3;

        // Act
        var delegates = EventHandlerTestHelper.FindRegisteredDelegates(emitter, "TestEvent", target);

        // Assert
        _ = delegates.Should().Contain([(EventHandler)target.Handler1, (EventHandler)target.Handler2, (EventHandler)target.Handler3]);
    }

    [TestMethod]
    public void FindRegisteredDelegates_ShouldReturnEmptyList_WhenNoDelegatesForSpecificTarget()
    {
        // Arrange
        var emitter = new TestEventEmitter();
        var target = new object();

        emitter.TestEvent += Handler1;
        emitter.TestEvent += Handler2;

        // Act
        var delegates = EventHandlerTestHelper.FindRegisteredDelegates(emitter, "TestEvent", target);

        // Assert
        _ = delegates.Should().BeEmpty();
        return;

        static void Handler1(object? sender, EventArgs args)
        {
        }

        static void Handler2(object? sender, EventArgs args)
        {
        }
    }

    private sealed class TestEventEmitter
    {
#pragma warning disable CS0067 // Event is never used
        public event EventHandler? TestEvent;
#pragma warning restore CS0067 // Event is never used
    }

    [SuppressMessage("ReSharper", "MemberCanBeMadeStatic.Local", Justification = "instance methods required for the test case")]
    private sealed class TestTarget
    {
        public void Handler1(object? sender, EventArgs args)
        {
        }

        public void Handler2(object? sender, EventArgs args)
        {
        }

        public void Handler3(object? sender, EventArgs args)
        {
        }
    }
}
