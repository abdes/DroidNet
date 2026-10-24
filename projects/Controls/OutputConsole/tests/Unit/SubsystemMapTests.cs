// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using AwesomeAssertions;
using DroidNet.Controls.OutputConsole.Model;

namespace Controls.OutputConsole.Tests;

[TestClass]
public class SubsystemMapTests
{
    private static readonly SubsystemMap Map = new(
    [
        new("Oxygen", "Editor"),
        new("Oxygen.Editor.ContentPipeline", "Content"),
        new("Oxygen.Editor.ContentPipeline.Cooking", "Cooker"),
        new("Player", "Player"),
    ]);

    [TestMethod]
    [DataRow("Oxygen.Editor.ContentPipeline.Cooking.AutomaticCookService", "Cooker")]
    [DataRow("Oxygen.Editor.ContentPipeline.Mounting.Mounts", "Content")]
    [DataRow("Oxygen.Editor.World.SceneService", "Editor")]
    [DataRow("Player", "Player")]
    public void Resolve_UsesTheLongestMatchingPrefix(string source, string expected)
        => _ = Map.Resolve(source).Should().Be(expected);

    [TestMethod]
    [DataRow("OxygenTools.Something")]
    [DataRow("PlayerTwo")]
    [DataRow("oxygen.editor.World")]
    [DataRow("ThirdParty.Lib")]
    public void Resolve_WithoutPrefixAtASegmentBoundary_ReturnsNull(string source)
        => _ = Map.Resolve(source).Should().BeNull();

    [TestMethod]
    [DataRow(null)]
    [DataRow("")]
    public void Resolve_WithoutSource_ReturnsNull(string? source)
        => _ = Map.Resolve(source).Should().BeNull();

    [TestMethod]
    public void Entries_KeepTheGivenOrder()
        => _ = Map.Entries.Select(e => e.Key).Should().Equal(
            "Oxygen",
            "Oxygen.Editor.ContentPipeline",
            "Oxygen.Editor.ContentPipeline.Cooking",
            "Player");

    [TestMethod]
    public void Names_ListEverySubsystemOnceSorted()
        => _ = Map.Names.Should().Equal("Content", "Cooker", "Editor", "Player");

    [TestMethod]
    [DataRow("", "Editor")]
    [DataRow("Oxygen", "")]
    public void Constructor_WithEmptyPrefixOrSubsystem_Throws(string prefix, string subsystem)
    {
        var act = () => new SubsystemMap([new(prefix, subsystem)]);

        _ = act.Should().Throw<ArgumentException>();
    }

    [TestMethod]
    public void Constructor_WithDuplicatePrefix_Throws()
    {
        var act = () => new SubsystemMap([new("Oxygen", "Editor"), new("Oxygen", "Engine")]);

        _ = act.Should().Throw<ArgumentException>();
    }
}
