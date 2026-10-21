// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using AwesomeAssertions;

namespace Oxygen.Editor.Cooking.Unit.Tests;

/// <summary>The Cooking module contributes its panel over its view model.</summary>
[TestClass]
public sealed class CookingModuleTests
{
    /// <summary>The module contributes the Cooking panel with the identity and title its tab starts with.</summary>
    [TestMethod]
    public void ContributesTheCookingPanel()
    {
        var panel = new CookingModule().Panels.Should().ContainSingle().Subject;

        _ = panel.Id.Should().Be("cook");
        _ = panel.Title.Should().Be("Cooking");
        _ = panel.ViewModelType.Should().Be<CookingPanelViewModel>();
    }
}
