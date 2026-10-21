// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using AwesomeAssertions;
using Oxygen.Editor.Shell.Commands;
using Oxygen.Editor.Shell.Contributions;

namespace Oxygen.Editor.Shell.Unit.Tests.Contributions;

/// <summary>The contributions are checked once: an identity names one panel and one command.</summary>
[TestClass]
public sealed class ContributionRegistryTests
{
    /// <summary>A contributed panel is found by its identity, beside the Shell's own commands.</summary>
    [TestMethod]
    public void PanelsAreFoundByIdentityBesideTheShellsCommands()
    {
        var registry = new ContributionRegistry([new PanelDescriptor("cook", "Cooking", typeof(object))], []);

        _ = registry.FindPanel("cook")!.Title.Should().Be("Cooking");
        _ = registry.FindPanel("log").Should().BeNull();
        _ = registry.Commands.Should().Contain([ShellCommands.Undo, ShellCommands.Redo]);
    }

    /// <summary>A panel's feature can retitle its tab; the descriptor's title stands until it does.</summary>
    [TestMethod]
    public void PanelTitleFollowsTheFeatureThatSetsIt()
    {
        var registry = new ContributionRegistry([new PanelDescriptor("problems", "Problems", typeof(object))], []);

        _ = registry.TitleOf("problems").Should().Be("Problems");

        registry.SetTitle("problems", "Problems  2");

        _ = registry.TitleOf("problems").Should().Be("Problems  2");
        _ = registry.TitleOf("log").Should().BeNull();
        _ = ((Action)(() => registry.SetTitle("log", "Output"))).Should().Throw<InvalidOperationException>();
    }

    /// <summary>Two panels, or two commands, with one identity fail.</summary>
    [TestMethod]
    public void DuplicateIdentitiesFail()
    {
        var panels = () => new ContributionRegistry([new PanelDescriptor("cook", "Cooking", typeof(object)), new PanelDescriptor("cook", "Jobs", typeof(object))], []);
        var commands = () => new ContributionRegistry([], [new CommandDescriptor(ShellCommands.Undo.Id, "Undo again")]);

        _ = panels.Should().Throw<InvalidOperationException>().WithMessage("*panels*'cook'*");
        _ = commands.Should().Throw<InvalidOperationException>().WithMessage("*commands*'Edit.Undo'*");
    }
}
