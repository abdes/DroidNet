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
public class DependencyInjectionTests : VisualUserInterfaceTests
{
    [TestMethod]
    public Task SpatialMapperFactory_Creates_Mapper_With_Context_Async() => EnqueueAsync(async () =>
    {
        // Arrange
        var container = new Container();
        container.RegisterDelegate<SpatialMapperFactory>(r =>
            (element, window) => new SpatialMapper(element, window));

        var factory = container.Resolve<SpatialMapperFactory>();
        var element = new Button();
        var window = VisualUserInterfaceTestsApp.MainWindow;

        await LoadTestContentAsync(element).ConfigureAwait(true);

        // Act
        var mapper = factory(element, window);

        // Assert
        _ = mapper.Should().NotBeNull();
        _ = mapper.Should().BeOfType<SpatialMapper>();
    });

    [TestMethod]
    public Task SpatialMapperFactory_Returns_Distinct_Instances_Async() => EnqueueAsync(async () =>
    {
        // Arrange
        var container = new Container();
        container.RegisterDelegate<SpatialMapperFactory>(r =>
            (element, window) => new SpatialMapper(element, window));

        var factory = container.Resolve<SpatialMapperFactory>();
        var window = VisualUserInterfaceTestsApp.MainWindow;
        var element1 = new Button();
        var element2 = new Button();
        var stackPanel = new StackPanel();
        stackPanel.Children.Add(element1);
        stackPanel.Children.Add(element2);

        await LoadTestContentAsync(stackPanel).ConfigureAwait(true);

        // Act
        var mapper1 = factory(element1, window);
        var mapper2 = factory(element2, window);

        // Assert
        _ = mapper1.Should().NotBeSameAs(mapper2);
    });

    [TestMethod]
    public Task SpatialContextService_Resolves_From_Factory_Async() => EnqueueAsync(async () =>
    {
        // Arrange
        var container = new Container();
        container.RegisterDelegate<SpatialMapperFactory>(r =>
            (element, window) => new SpatialMapper(element, window));
        container.Register<SpatialContextService>();

        var service = container.Resolve<SpatialContextService>();
        var element = new Button();
        var window = VisualUserInterfaceTestsApp.MainWindow;

        await LoadTestContentAsync(element).ConfigureAwait(true);

        // Act
        var mapper = service.GetMapper(element, window);

        // Assert
        _ = mapper.Should().NotBeNull();
        _ = mapper.Should().BeOfType<SpatialMapper>();
    });

    [TestMethod]
    public Task SpatialContextService_LazyMapper_Resolution_Async() => EnqueueAsync(async () =>
    {
        // Arrange
        var container = new Container();
        container.RegisterDelegate<SpatialMapperFactory>(r =>
            (element, window) => new SpatialMapper(element, window));
        container.Register<SpatialContextService>();

        var service = container.Resolve<SpatialContextService>();
        var element = new Button();
        var window = VisualUserInterfaceTestsApp.MainWindow;

        await LoadTestContentAsync(element).ConfigureAwait(true);

        // Act
        var lazyMapper = service.GetLazyMapper(element, window);
        var mapper = lazyMapper.Value;

        // Assert
        _ = mapper.Should().NotBeNull();
        _ = mapper.Should().BeOfType<SpatialMapper>();
    });

    [TestMethod]
    public Task SpatialContextService_LazyMapper_NotEvaluatedUntilAccessed_Async() => EnqueueAsync(async () =>
    {
        // Arrange
        var container = new Container();
        var callCount = 0;
        container.RegisterDelegate<SpatialMapperFactory>(r =>
            (element, window) =>
            {
                callCount++;
                return new SpatialMapper(element, window);
            });
        container.Register<SpatialContextService>();

        var service = container.Resolve<SpatialContextService>();
        var element = new Button();
        var window = VisualUserInterfaceTestsApp.MainWindow;

        await LoadTestContentAsync(element).ConfigureAwait(true);

        // Act
        var lazyMapper = service.GetLazyMapper(element, window);
        _ = callCount.Should().Be(0); // Not yet evaluated
        var mapper = lazyMapper.Value;
        _ = callCount.Should().Be(1); // Now evaluated

        // Assert
        _ = mapper.Should().NotBeNull();
    });

    [TestMethod]
    public Task SpatialMapperFactory_With_NullElement_Throws_Async() => EnqueueAsync(async () =>
    {
        // Arrange
        var container = new Container();
        container.RegisterDelegate<SpatialMapperFactory>(r =>
            (element, window) => new SpatialMapper(element, window));

        var factory = container.Resolve<SpatialMapperFactory>();
        var window = VisualUserInterfaceTestsApp.MainWindow;

        // Act & Assert
        Action act = () => factory(null!, window);
        _ = act.Should().Throw<ArgumentNullException>();
    });
}
