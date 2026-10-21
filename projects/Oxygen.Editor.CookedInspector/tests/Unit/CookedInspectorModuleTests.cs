// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using AwesomeAssertions;

namespace Oxygen.Editor.CookedInspector.Unit.Tests;

/// <summary>The Cooked Inspector module contributes its panel over its view model.</summary>
[TestClass]
public sealed class CookedInspectorModuleTests
{
    /// <summary>The module contributes the Cooked Inspector panel with the title its tab shows.</summary>
    [TestMethod]
    public void ContributesTheCookedInspectorPanel()
    {
        var panel = new CookedInspectorModule().Panels.Should().ContainSingle().Subject;

        _ = panel.Id.Should().Be("inspect");
        _ = panel.Title.Should().Be("Cooked Inspector");
        _ = panel.ViewModelType.Should().Be<CookedInspectionViewModel>();
    }
}
