// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Diagnostics.CodeAnalysis;
using AwesomeAssertions;
using DryIoc;
using Microsoft.Extensions.DependencyInjection;
using Microsoft.Extensions.Logging;
using Microsoft.Extensions.Options;

namespace DroidNet.TestHelpers.Tests;

[TestClass]
[ExcludeFromCodeCoverage]
[TestCategory("Host Container")]
public class HostContainerTests
{
    [TestMethod]
    public void Create_ResolvesTheHostOpenGenerics()
    {
        // Arrange
        using var container = HostContainer.Create();

        // Act
        var logger = container.Resolve<ILogger<HostContainerTests>>();
        var options = container.Resolve<IOptions<Settings>>();

        // Assert
        _ = logger.Should().NotBeNull();
        _ = options.Value.Should().NotBeNull();
    }

    [TestMethod]
    public void Create_AddsTheConfiguredServices()
    {
        // Arrange
        var settings = new Settings();

        // Act
        using var container = HostContainer.Create(services => services.AddSingleton(settings));

        // Assert
        _ = container.Resolve<Settings>().Should().BeSameAs(settings);
    }

    [TestMethod]
    public void Dispose_TracksDisposableTransients()
    {
        // Arrange
        var container = HostContainer.Create();
        container.Register<Disposable>(Reuse.Transient);
        var transient = container.Resolve<Disposable>();

        // Act
        container.Dispose();

        // Assert
        _ = transient.IsDisposed.Should().BeTrue();
    }

    /// <summary>Options and a plain service of the tests.</summary>
    public sealed class Settings
    {
    }

    /// <summary>A disposable transient.</summary>
    public sealed class Disposable : IDisposable
    {
        /// <summary>Gets a value indicating whether the transient has been disposed.</summary>
        public bool IsDisposed { get; private set; }

        /// <inheritdoc />
        public void Dispose() => this.IsDisposed = true;
    }
}
