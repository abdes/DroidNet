// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using AwesomeAssertions;

namespace Oxygen.Editor.Problems.Unit.Tests;

/// <summary>The Problems module contributes its panel over its view model.</summary>
[TestClass]
internal sealed class ProblemsModuleTests
{
    /// <summary>The module contributes the Problems panel with the title its tab starts with.</summary>
    [TestMethod]
    public void ContributesTheProblemsPanel()
    {
        var panel = new ProblemsModule().Panels.Should().ContainSingle().Subject;

        _ = panel.Id.Should().Be(ProblemsModule.PanelId);
        _ = panel.Title.Should().Be(ProblemsViewModel.BaseTitle);
        _ = panel.ViewModelType.Should().Be<ProblemsViewModel>();
    }
}
