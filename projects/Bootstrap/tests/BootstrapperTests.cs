// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.ComponentModel;
using System.IO.Abstractions;
using DroidNet.Config;
using DroidNet.Mvvm.Converters;
using DroidNet.Routing;
using DryIoc;
using FluentAssertions;
using Microsoft.Extensions.DependencyInjection;
using Microsoft.UI.Xaml.Data;

namespace DroidNet.Bootstrap.Tests;

/// <summary>
/// Contains unit test cases for the <see cref="Bootstrapper" /> class.
/// </summary>
[TestClass]
[TestCategory("Bootstrapper")]
[TestCategory("UITest")]
public sealed partial class BootstrapperTests : IDisposable
{
    private readonly Bootstrapper defaultConfiguredBuilder;

    /// <summary>
    /// Initializes a new instance of the <see cref="BootstrapperTests"/> class.
    /// </summary>
    public BootstrapperTests()
    {
        var args = Array.Empty<string>();
        var bootstrapper = new Bootstrapper(args);
        try
        {
            this.defaultConfiguredBuilder = bootstrapper.Configure();
            bootstrapper = null; // Dispose ownership transferred to defaultConfiguredBuilder
        }
        finally
        {
            bootstrapper?.Dispose();
        }

        _ = new MyService(); // Just to ensure the type is not marked as unused
    }

    private interface IMyService
    {
        public void DoSomething();
    }

    public void Dispose() => this.defaultConfiguredBuilder.Dispose();

    [TestMethod]
    [DisplayName("Configure should initialize early services and make them available")]
    public void Configure_ConfiguresEarlyServices()
    {
        // Arrange & Act
        _ = this.defaultConfiguredBuilder.Build();
        var pathFinder = this.defaultConfiguredBuilder.Container.GetService<IPathFinder>();
        var fs = this.defaultConfiguredBuilder.Container.GetService<IFileSystem>();

        // Assert
        _ = this.defaultConfiguredBuilder.Container.Should().NotBeNull();
        _ = this.defaultConfiguredBuilder.FileSystemService.Should().NotBeNull();
        _ = pathFinder.Should().Be(this.defaultConfiguredBuilder.PathFinderService);
        _ = fs.Should().Be(this.defaultConfiguredBuilder.FileSystemService);
    }

    [TestMethod]
    [DisplayName("Build should auto-configure if Configure not called")]
    public void Build_BeforeConfigure_ShouldAutoConfigure()
    {
        // Arrange
        var args = Array.Empty<string>();
        using var bootstrapper = new Bootstrapper(args);

        // Act - Call Build without Configure
        _ = bootstrapper.Build();
        var pathFinder = bootstrapper.PathFinderService;
        var fs = bootstrapper.FileSystemService;

        // Assert
        _ = bootstrapper.Container.Should().NotBeNull();
        _ = pathFinder.Should().NotBeNull();
        _ = fs.Should().NotBeNull();

        // Verify early services are configured correctly
        var containerPathFinder = bootstrapper.Container.GetService<IPathFinder>();
        var containerFs = bootstrapper.Container.GetService<IFileSystem>();

        _ = containerPathFinder.Should().BeSameAs(pathFinder);
        _ = containerFs.Should().BeSameAs(fs);
    }

    [TestMethod]
    [DisplayName("WithRouting should register and configure IRouter service")]
    public void WithRouting_ShouldConfigureRoutes()
    {
        // Arrange
        var routes = new Routes([]);

        // Act
        _ = this.defaultConfiguredBuilder.WithRouting(routes).Build();

        // Assert
        var router = this.defaultConfiguredBuilder.Container.GetService<IRouter>();
        _ = router.Should().NotBeNull();
    }

    [TestMethod]
    [DisplayName("WithMvvm should register and configure MVVM services")]
    public void WithMvvm_ShouldConfigureMvvmServices()
    {
        // Arrange & Act
        _ = this.defaultConfiguredBuilder.WithMvvm().Build();

        // Assert
        var vmToViewModel
            = this.defaultConfiguredBuilder.Container.Resolve<IValueConverter>(serviceKey: "VmToView");
        _ = vmToViewModel.Should().NotBeNull();
        vmToViewModel = this.defaultConfiguredBuilder.Container.GetService<ViewModelToView>();
        _ = vmToViewModel.Should().NotBeNull();
    }

    /*
     * NOTE: We cannot test WithWinUI() here because it requires an Application instance to be
     * created and initialized. This is not possible in a unit test environment.
     */

    [TestMethod]
    [DisplayName("Dispose should reset bootstrapper state and dispose container")]
    public void Dispose_ResetsTheBootstrapper()
    {
        // Arrange
        _ = this.defaultConfiguredBuilder.Build();
        var container = this.defaultConfiguredBuilder.Container;

        // Act
        this.defaultConfiguredBuilder.Dispose();

        // Assert
        _ = this.defaultConfiguredBuilder.FileSystemService.Should().BeNull();
        _ = this.defaultConfiguredBuilder.PathFinderService.Should().BeNull();

        _ = container.Invoking(c => c.GetService<IFileSystem>())
            .Should()
            .Throw<ContainerException>()
            .WithMessage("*disposed*");
    }

    [TestMethod]
    [DisplayName("Container should be accessible only after Build is called")]
    public void Container_ShouldThrowBeforeBuild()
    {
        // Act & Assert
        var act = () => this.defaultConfiguredBuilder.Container;
        _ = act.Should()
            .Throw<InvalidOperationException>()
            .WithMessage("*Host not built yet*");
    }

    [TestMethod]
    [DisplayName("Build can only be called once")]
    public void Build_CalledTwice_ShouldThrow()
    {
        // Arrange
        _ = this.defaultConfiguredBuilder.Build();

        // Act & Assert
        var act = this.defaultConfiguredBuilder.Build;
        _ = act.Should()
            .Throw<InvalidOperationException>()
            .WithMessage("*Host already built*");
    }

    [TestMethod]
    [DisplayName("Early services should maintain singleton lifetime")]
    public void EarlyServices_ShouldMaintainSingletonLifetime()
    {
        // Arrange
        _ = this.defaultConfiguredBuilder.Build();

        // Act
        var pathFinder1 = this.defaultConfiguredBuilder.Container.GetService<IPathFinder>();
        var pathFinder2 = this.defaultConfiguredBuilder.Container.GetService<IPathFinder>();
        var fs1 = this.defaultConfiguredBuilder.Container.GetService<IFileSystem>();
        var fs2 = this.defaultConfiguredBuilder.Container.GetService<IFileSystem>();

        // Assert
        _ = pathFinder1.Should().BeSameAs(pathFinder2);
        _ = fs1.Should().BeSameAs(fs2);
    }

    [TestMethod]
    [DisplayName("PathFinder should use build-specific default mode with no args")]
    public void PathFinder_NoArgs_ShouldUseConfigurationDefaultMode()
    {
        // Arrange & Act
        _ = this.defaultConfiguredBuilder.Build();
        var pathFinder = this.defaultConfiguredBuilder.PathFinderService;

        // Assert
        _ = pathFinder.Should().NotBeNull();
#if DEBUG
        _ = pathFinder!.Mode.Should().Be(PathFinder.DevelopmentMode);
#else
        _ = pathFinder!.Mode.Should().Be(PathFinder.RealMode);
#endif
    }

    [TestMethod]
    [DisplayName("PathFinder should use dev mode when specified in args")]
    public void PathFinder_DevMode_ShouldUseDevPaths()
    {
        // Arrange
        var args = new[] { "--mode", "dev" };
        using var bootstrapper = new Bootstrapper(args);
        _ = bootstrapper.Configure();

        // Act
        _ = bootstrapper.Build();
        var pathFinder = bootstrapper.PathFinderService;

        // Assert
        _ = pathFinder.Should().NotBeNull();
        _ = pathFinder!.Mode.Should().Be(PathFinder.DevelopmentMode);
    }

    [TestMethod]
    [DisplayName("PathFinder should use real mode when specified in args")]
    public void PathFinder_RealMode_ShouldUseRealPaths()
    {
        // Arrange
        var args = new[] { "--mode", "real" };
        using var bootstrapper = new Bootstrapper(args);
        _ = bootstrapper.Configure();

        // Act
        _ = bootstrapper.Build();
        var pathFinder = bootstrapper.PathFinderService;

        // Assert
        _ = pathFinder.Should().NotBeNull();
        _ = pathFinder!.Mode.Should().Be(PathFinder.RealMode);
        _ = pathFinder.LocalAppData.Should().NotContain("Development");
    }

    [TestMethod]
    [DisplayName("PathFinder should respect environment variable over default")]
    public void PathFinder_EnvironmentVariable_ShouldOverrideDefault()
    {
        // Arrange
        Environment.SetEnvironmentVariable("MODE", "dev");
        try
        {
            using var bootstrapper = new Bootstrapper([]);
            _ = bootstrapper.Configure();

            // Act
            _ = bootstrapper.Build();
            var pathFinder = bootstrapper.PathFinderService;

            // Assert
            _ = pathFinder.Should().NotBeNull();
            _ = pathFinder!.Mode.Should().Be(PathFinder.DevelopmentMode);
        }
        finally
        {
            Environment.SetEnvironmentVariable("MODE", value: null);
        }
    }

    [TestMethod]
    [DisplayName("PathFinder args should override environment variable")]
    public void PathFinder_Args_ShouldOverrideEnvironmentVariable()
    {
        // Arrange
        Environment.SetEnvironmentVariable("MODE", "dev");
        try
        {
            var args = new[] { "--mode", "real" };
            using var bootstrapper = new Bootstrapper(args);
            _ = bootstrapper.Configure();

            // Act
            _ = bootstrapper.Build();
            var pathFinder = bootstrapper.PathFinderService;

            // Assert
            _ = pathFinder.Should().NotBeNull();
            _ = pathFinder!.Mode.Should().Be(PathFinder.RealMode);
        }
        finally
        {
            Environment.SetEnvironmentVariable("MODE", value: null);
        }
    }

    private sealed class MyService : IMyService
    {
        public void DoSomething()
        {
            // Implementation here
        }
    }
}
