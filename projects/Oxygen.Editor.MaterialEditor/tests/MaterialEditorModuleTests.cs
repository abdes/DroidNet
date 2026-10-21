// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using AwesomeAssertions;
using DroidNet.TestHelpers;
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
    public void MaterialEditorModule_RegistersTheEditorsView()
    {
        using var window = HostContainer.Create();

        new MaterialEditorModule().Register(window);

        var registrations = window.GetServiceRegistrations().ToArray();
        _ = registrations.Should().Contain(registration => registration.ServiceType == typeof(MaterialEditorView));
        _ = registrations.Should().NotContain(registration => registration.ServiceType == typeof(IDocumentEditorProvider));
    }

    [TestMethod]
    public void MaterialEditorSessionModule_RegistersTheEditorProvider()
    {
        using var session = HostContainer.Create();

        new MaterialEditorSessionModule().Register(session);

        var registrations = session.GetServiceRegistrations().ToArray();
        _ = registrations.Should().Contain(registration => registration.ServiceType == typeof(IDocumentEditorProvider) && registration.ImplementationType == typeof(MaterialEditorProvider));
    }
}
