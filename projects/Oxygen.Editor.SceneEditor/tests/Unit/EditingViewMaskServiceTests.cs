// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using AwesomeAssertions;
using Moq;
using Oxygen.Editor.Data.Services;
using Oxygen.Editor.Projects.Testing;
using Oxygen.Editor.Scenes.Interaction;
using Oxygen.Managed.Core;
using Oxygen.Managed.Core.Diagnostics;
using Testably.Abstractions.Testing;

namespace Oxygen.Editor.SceneEditor.Unit.Tests;

/// <summary>The pending-suppression warning cadence of the editing view mask.</summary>
[TestClass]
[TestCategory("Workspace")]
internal sealed partial class EditingViewMaskServiceTests
{
    [TestMethod]
    public async Task EditingViewMaskService_WarnsOncePerSceneActivation()
    {
        var project = TestProject.Create(new MockFileSystem()).Context;
        var settings = new Mock<IEditorSettingsManager>(MockBehavior.Loose);
        var interaction = new SceneNodeInteraction(settings.Object, new CapturingOperationResultPublisher(), new OperationStatusReducer());
        var results = new CapturingOperationResultPublisher();

        using var mask = new EditingViewMaskService(interaction, results, new OperationStatusReducer());
        _ = mask.Start();

        var sceneA = SceneId.New();
        await interaction.RestoreAsync(project, sceneA).ConfigureAwait(false);

        // First change on the active scene reports the pending native suppression once (Q2), even
        // though several hidden entries follow within the same activation.
        interaction.SetHidden(NodeId.New(), isHidden: true);
        interaction.SetHidden(NodeId.New(), isHidden: true);
        _ = MaskWarnings(results).Should().Be(1, "one mask per scene activation (Q3)");

        // A different scene activation is allowed to warn again.
        var sceneB = SceneId.New();
        await interaction.RestoreAsync(project, sceneB).ConfigureAwait(false);
        interaction.SetHidden(NodeId.New(), isHidden: true);
        _ = MaskWarnings(results).Should().Be(2, "a new activation re-reports");
    }

    private static int MaskWarnings(CapturingOperationResultPublisher results)
        => results.Published
            .SelectMany(p => p.Diagnostics)
            .Count(d => string.Equals(d.Code, DiagnosticCodes.SettingsPrefix + "EDITING_VIEW_MASK_UNAVAILABLE", StringComparison.OrdinalIgnoreCase));

    private sealed class CapturingOperationResultPublisher : IOperationResultPublisher
    {
        public List<OperationResult> Published { get; } = [];

        public void Publish(OperationResult result) => this.Published.Add(result);

        public IDisposable Subscribe(IObserver<OperationResult> observer) => new NoopDisposable();
    }

    private sealed class NoopDisposable : IDisposable
    {
        public void Dispose()
        {
        }
    }
}
