// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Diagnostics.CodeAnalysis;
using DroidNet.Tests;
using DryIoc;
using FluentAssertions;
using Microsoft.UI.Xaml.Controls;

namespace DroidNet.Coordinates.Tests;

[TestClass]
[ExcludeFromCodeCoverage]
[TestCategory("DependencyInjectionTests")]
[TestCategory("UITest")]
public partial class DependencyInjectionTests : VisualUserInterfaceTests, IDisposable
{
    private readonly IContainer container = new Container().WithSpatialMapping();
    private bool isDisposed;

    [TestMethod]
    public Task SpatialMapperFactory_Creates_Mapper_With_Context_Async() => EnqueueAsync(async () =>
    {
        // Arrange
        var factory = this.container.Resolve<SpatialMapperFactory>();
        var element = new Button();
        var window = VisualUserInterfaceTestsApp.MainWindow;

        await LoadTestContentAsync(element).ConfigureAwait(true);

        // Act
        var mapper = factory(window, element);

        // Assert
        _ = mapper.Should().NotBeNull();
        _ = mapper.Should().BeOfType<SpatialMapper>();
    });

    [TestMethod]
    public Task SpatialMapperFactory_Returns_Distinct_Instances_Async() => EnqueueAsync(async () =>
    {
        // Arrange
        var factory = this.container.Resolve<SpatialMapperFactory>();
        var window = VisualUserInterfaceTestsApp.MainWindow;
        var element1 = new Button();
        var element2 = new Button();
        var stackPanel = new StackPanel();
        stackPanel.Children.Add(element1);
        stackPanel.Children.Add(element2);

        await LoadTestContentAsync(stackPanel).ConfigureAwait(true);

        // Act
        var mapper1 = factory(window, element1);
        var mapper2 = factory(window, element2);

        // Assert
        _ = mapper1.Should().NotBeSameAs(mapper2);
    });

    [TestMethod]
    public Task SpatialMapperFactory_With_NullElement_Works_Async() => EnqueueAsync(() =>
    {
        // Arrange
        var factory = this.container.Resolve<SpatialMapperFactory>();
        var window = VisualUserInterfaceTestsApp.MainWindow;

        // Act - null element is now valid
        var mapper = factory(window, element: null);

        // Assert
        _ = mapper.Should().NotBeNull();
        _ = mapper.Should().BeOfType<SpatialMapper>();
    });

    [TestMethod]
    public void SpatialMapperFactory_With_NullElementAndWindow_Works()
    {
        // Arrange
        var factory = this.container.Resolve<SpatialMapperFactory>();

        // Act - both null is now valid (for Physical↔Screen conversions only)
        var mapper = factory(window: null, element: null);

        // Assert
        _ = mapper.Should().NotBeNull();
        _ = mapper.Should().BeOfType<SpatialMapper>();
    }

    [TestMethod]
    public void WithSpatialMapping_Resolves_Consumer_With_Factory()
    {
        // Arrange
        this.container.Register<FactoryConsumer>(Reuse.Transient);

        // Act
        var consumer = this.container.Resolve<FactoryConsumer>();

        // Assert
        _ = consumer.Factory.Should().NotBeNull();
    }

    public void Dispose()
    {
        // Do not change this code. Put cleanup code in 'Dispose(bool disposing)' method
        this.Dispose(disposing: true);
        GC.SuppressFinalize(this);
    }

    protected virtual void Dispose(bool disposing)
    {
        if (!this.isDisposed)
        {
            if (disposing)
            {
            }

            this.container.Dispose();
            this.isDisposed = true;
        }
    }

    [SuppressMessage("Performance", "CA1812:Avoid uninstantiated internal classes", Justification = "Instantiated indirectly by dependency injection in tests")]
    private sealed class FactoryConsumer(SpatialMapperFactory factory)
    {
        public SpatialMapperFactory Factory { get; } = factory;
    }
}
