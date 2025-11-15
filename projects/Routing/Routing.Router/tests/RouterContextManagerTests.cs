// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Diagnostics.CodeAnalysis;
using AwesomeAssertions;
using DroidNet.Routing.Events;
using Moq;

namespace DroidNet.Routing.Tests;

/// <summary>
/// Contains unit test cases for the <see cref="RouterContextManager" /> class.
/// </summary>
[TestClass]
[ExcludeFromCodeCoverage]
[TestCategory(nameof(RouterContextManager))]
public class RouterContextManagerTests : IDisposable
{
    private const string TestTarget = "test-target";
    private readonly Mock<IContextProvider<NavigationContext>> contextProviderMock;
    private readonly RouterContextManager contextManager;
    private readonly NavigationContext mainContext = new(Target.Main, TestTarget) { RouteActivationObserver = null };
    private bool isDisposed;

    /// <summary>
    /// Initializes a new instance of the <see cref="RouterContextManagerTests" />
    /// class.
    /// </summary>
    /// <remarks>
    /// A new instance of the test class is created for each test case.
    /// </remarks>
    public RouterContextManagerTests()
    {
        this.contextProviderMock = new Mock<IContextProvider<NavigationContext>>();
        _ = this.contextProviderMock.Setup(a => a.ContextForTarget(Target.Main, It.IsAny<NavigationContext>()))
            .Returns(this.mainContext);

        this.contextManager = new RouterContextManager(this.contextProviderMock.Object);
    }

    /// <summary>
    /// Tests that calling <see cref="RouterContextManager.GetContextForTarget" /> with
    /// <see langword="null" /> or <see cref="Target.Self" />, while the current
    /// context is <c>not null</c>, should return the current context.
    /// </summary>
    /// <param name="target">
    /// The target name for which to get a context.
    /// </param>
    [TestMethod]
    [DataRow(null)]
    [DataRow("_self")]
    public void GetContextForTarget_NullTargetOrSelf_CurrentContextNotNull_ReturnsCurrentContext(string target)
    {
        var currentContext
            = this.SetCurrentContext(new NavigationContext("current", TestTarget) { RouteActivationObserver = null });

        var actualContext = this.contextManager.GetContextForTarget(target);

        _ = actualContext.Should().BeSameAs(currentContext);
    }

    /// <summary>
    /// Tests that calling <see cref="RouterContextManager.GetContextForTarget" /> with
    /// <see langword="null" /> or <see cref="Target.Self" />, while  the current
    /// context is <see langword="null" />, should return the main context.
    /// </summary>
    /// <param name="target">
    /// The target name for which to get a context.
    /// </param>
    [TestMethod]
    [DataRow(null)]
    [DataRow("_self")]
    public void GetContextForTarget_NullTargetOrSelf_CurrentContextNull_ReturnsMainContext(string target)
    {
        _ = this.SetCurrentContext(context: null);

        var actualContext = this.contextManager.GetContextForTarget(target);

        _ = actualContext.Should().BeSameAs(this.mainContext);
    }

    /// <summary>
    /// Tests that calling <see cref="RouterContextManager.GetContextForTarget" /> with
    /// <see cref="Target.Main" />, should return the main context.
    /// </summary>
    [TestMethod]
    public void GetContextForTarget_Main_ReturnsMainContext()
    {
        _ = this.SetCurrentContext(context: null);

        var actualContext = this.contextManager.GetContextForTarget(Target.Main);

        _ = actualContext.Should().BeSameAs(this.mainContext);
    }

    /// <summary>
    /// Tests that calling <see cref="RouterContextManager.GetContextForTarget" /> with
    /// <see cref="Target.Main" />, should return the main context.
    /// </summary>
    [TestMethod]
    public void GetContextForTarget_CustomTarget_ReturnsValidContext()
    {
        const string target = "custom";
        NavigationContext expectedContext = new(target, TestTarget) { RouteActivationObserver = null };
        _ = this.contextProviderMock.Setup(a => a.ContextForTarget(target, It.IsAny<NavigationContext>()))
            .Returns(expectedContext);

        var actualContext = this.contextManager.GetContextForTarget(target);

        _ = actualContext.Should().BeSameAs(expectedContext);
    }

    /// <inheritdoc />
    public void Dispose()
    {
        this.Dispose(disposing: true);
        GC.SuppressFinalize(this);
    }

    protected virtual void Dispose(bool disposing)
    {
        if (this.isDisposed)
        {
            return;
        }

        if (disposing)
        {
            this.contextManager.Dispose();
        }

        this.isDisposed = true;
    }

    private NavigationContext? SetCurrentContext(NavigationContext? context)
    {
        // Trigger ContextChanged event to set currentContext
        this.contextProviderMock.Raise(
            a => a.ContextChanged += null,
            this.contextProviderMock.Object,
            new ContextEventArgs(context));
        return context;
    }
}
