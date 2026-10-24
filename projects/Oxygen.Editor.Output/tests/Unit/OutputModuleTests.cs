// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using AwesomeAssertions;
using DroidNet.Composition;
using DroidNet.Controls.OutputConsole.Model;
using DroidNet.TestHelpers;
using DryIoc;
using Microsoft.Extensions.Logging;
using Moq;
using Oxygen.Editor.LivePreview.Play;

namespace Oxygen.Editor.Output.Unit.Tests;

/// <summary>The Output module contributes its panel and follows the player's output from the window's start.</summary>
[TestClass]
public sealed class OutputModuleTests
{
    /// <summary>The module contributes the Output panel over its view model.</summary>
    [TestMethod]
    public void ContributesTheOutputPanel()
    {
        var panel = new OutputModule().Panels.Should().ContainSingle().Subject;

        _ = panel.Id.Should().Be(OutputModule.PanelId);
        _ = panel.ViewModelType.Should().Be<OutputViewModel>();
    }

    /// <summary>An activated window shows the player's output before the panel is ever shown.</summary>
    [TestMethod]
    public async Task ActivatedWindowFollowsThePlayerBeforeThePanelShows()
    {
        var log = new Mock<IPlayerLog>();
        var buffer = new OutputLogBuffer(new SubsystemMap([new(OutputModule.PlayerSource, "Player")]));
        using var host = HostContainer.Create();
        await using var root = ScopeFactory.CreateRoot(host);

        _ = new ScopeFactory().Create<IWindowLayer>(
            root,
            [new OutputModule()],
            window =>
            {
                window.RegisterInstance(log.Object);
                window.RegisterInstance(buffer);
            });
        log.Raise(player => player.LogReceived += null, new PlayerLogEventArgs(DateTimeOffset.Now, LogLevel.Information, channel: null, "started"));

        _ = buffer.Should().ContainSingle().Which.Source.Should().Be(OutputModule.PlayerSource);
    }
}
