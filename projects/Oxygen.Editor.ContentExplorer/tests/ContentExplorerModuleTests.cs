// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using AwesomeAssertions;
using DryIoc;
using Moq;
using Oxygen.Editor.Scripting;
using Oxygen.Editor.Shell.Windows;

namespace Oxygen.Editor.ContentExplorer.Tests;

/// <summary>The Content Explorer module contributes its panel and the ports its explorer implements.</summary>
[TestClass]
public sealed class ContentExplorerModuleTests
{
    /// <summary>The module contributes the Content Explorer panel with the title its tab shows.</summary>
    [TestMethod]
    public void ContributesTheContentExplorerPanel()
    {
        var module = new ContentExplorerModule();

        var panel = module.Panels.Should().ContainSingle().Subject;
        _ = panel.Id.Should().Be("content");
        _ = panel.Title.Should().Be("Content Explorer");
        _ = panel.ViewModelType.Should().Be<ContentExplorerViewModel>();
        _ = module.Commands.Should().BeEmpty();
    }

    /// <summary>The window's explorer reveals assets and creates scripts for the window's other parts.</summary>
    [TestMethod]
    public void RegistersTheExplorerAndThePortsItImplements()
    {
        using var window = new Container();
        window.RegisterInstance(Mock.Of<IPlacementService>());

        new ContentExplorerModule().Register(window);

        var registrations = window.GetServiceRegistrations().ToArray();
        _ = registrations.Should().Contain(registration => registration.ServiceType == typeof(ContentExplorerViewModel));
        _ = registrations.Should().Contain(registration => registration.ServiceType == typeof(ContentExplorerView));
        _ = window.Resolve<IAssetReveal>().Should().BeSameAs(window.Resolve<ContentExplorerAssetReveal>());
        _ = window.Resolve<IScriptCreation>().Should().BeSameAs(window.Resolve<ContentExplorerScriptCreation>());
    }
}
