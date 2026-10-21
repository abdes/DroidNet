// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using AwesomeAssertions;
using DryIoc;
using Oxygen.Editor.Shell.Contributions;

namespace Oxygen.Editor.MaterialEditor.Tests;

[TestClass]
public sealed class MaterialEditorModuleTests
{
    [TestMethod]
    public void MaterialEditorModule_ContributesNeitherPanelNorCommand()
    {
        var module = new MaterialEditorModule();

        _ = module.Panels.Should().BeEmpty();
        _ = module.Commands.Should().BeEmpty();
    }

    [TestMethod]
    public void MaterialEditorModule_RegistersTheEditorProviderAndItsView()
    {
        using var window = new Container();

        new MaterialEditorModule().Register(window);

        var registrations = window.GetServiceRegistrations().ToArray();
        _ = registrations.Should().Contain(registration => registration.ServiceType == typeof(IDocumentEditorProvider) && registration.ImplementationType == typeof(MaterialEditorProvider));
        _ = registrations.Should().Contain(registration => registration.ServiceType == typeof(MaterialEditorView));
    }
}
