// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using AwesomeAssertions;
using DroidNet.Documents;
using Moq;
using Oxygen.Editor.LivePreview.Scenes;

namespace Oxygen.Editor.WorldEditor.Unit.Tests.Documents;

/// <summary>Checks that a scene editor builds its panes however it learns that its scene is in the engine.</summary>
public sealed partial class SceneEditorConflictTests
{
    /// <summary>An editor created after its scene finished synchronizing builds its panes at once.</summary>
    /// <returns>The test task.</returns>
    [TestMethod]
    public async Task EditorCreatedAfterSynchronizationBuildsItsPanes()
    {
        using var fixture = await Fixture.CreateAsync(this.TestContext, active: true, synchronized: true).ConfigureAwait(false);

        _ = fixture.Editor.Viewports.Should().ContainSingle();
    }

    /// <summary>The preview's readiness builds the panes, even while another tab is active.</summary>
    /// <returns>The test task.</returns>
    [TestMethod]
    public async Task ReadinessBuildsThePanesOfAnInactiveEditor()
    {
        using var fixture = await Fixture.CreateAsync(this.TestContext, active: false).ConfigureAwait(false);
        _ = fixture.Editor.Viewports.Should().BeEmpty();

        RaiseReady(fixture, fixture.Tab.DocumentId);

        _ = fixture.Editor.Viewports.Should().ContainSingle();
    }

    /// <summary>A notification for another document instance leaves the editor waiting.</summary>
    /// <returns>The test task.</returns>
    [TestMethod]
    public async Task ReadinessOfAnotherDocumentIsIgnored()
    {
        using var fixture = await Fixture.CreateAsync(this.TestContext, active: true).ConfigureAwait(false);

        RaiseReady(fixture, DocumentId.New());

        _ = fixture.Editor.Viewports.Should().BeEmpty();
    }

    // The preview raises readiness on the document's owner thread, which is the test's thread here.
    private static void RaiseReady(Fixture fixture, DocumentId document)
    {
        _ = fixture.Preview.Setup(value => value.IsReady(document)).Returns(value: true);
        fixture.Preview.Raise(value => value.ReadyChanged += null, fixture.Preview.Object, new ScenePreviewEventArgs(document));
    }
}
