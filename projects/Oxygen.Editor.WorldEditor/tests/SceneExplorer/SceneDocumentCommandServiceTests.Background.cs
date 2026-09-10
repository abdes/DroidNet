// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Numerics;
using AwesomeAssertions;
using Moq;
using Oxygen.Editor.Schemas;
using Oxygen.Editor.World.Serialization;
using Oxygen.Editor.World.Services;
using Oxygen.Editor.WorldEditor.Documents.Commands;
using Oxygen.Managed.Core.Diagnostics;

namespace Oxygen.Editor.World.SceneExplorer.Tests;

public sealed partial class SceneDocumentCommandServiceTests
{
    [TestMethod]
    public async Task BackgroundNativeRejection_PreservesAuthoringAndPublishesFieldDiagnostic()
    {
        var fixture = CreateFixture();
        var scene = CreateScene();
        var context = CreateContext(scene);
        var failure = new SyncOutcome(SyncStatus.Rejected, SceneOperationKinds.EditEnvironment, AffectedScope.Empty, LiveSyncDiagnosticCodes.EnvironmentBackgroundRejected, "Native background was rejected");
        _ = fixture.Sync.Setup(value => value.UpdateEnvironmentAsync(scene, It.IsAny<SceneEnvironmentData>(), It.IsAny<CancellationToken>()))
            .ReturnsAsync(new EnvironmentSyncResult(SyncStatus.Rejected, new Dictionary<string, SyncOutcome>(StringComparer.Ordinal)
            {
                [nameof(SceneEnvironmentData.BackgroundColor)] = failure,
            }));
        var color = new Vector3(0.25f, 0.5f, 0.75f);

        var result = await fixture.Sut.EditSceneEnvironmentPropertiesAsync(
            context,
            PropertyEdit.Single(SceneDocumentCommandService.SceneEnvironment.BackgroundColor, color),
            "Edit Background",
            EditSessionToken.OneShot).ConfigureAwait(false);

        _ = result.Succeeded.Should().BeTrue();
        _ = scene.Environment.BackgroundColor.Should().Be(color);
        _ = context.History.UndoStack.Should().ContainSingle();
        _ = fixture.Results.Published.SelectMany(value => value.Diagnostics).Should().Contain(value => value.Code == LiveSyncDiagnosticCodes.EnvironmentBackgroundRejected && value.Message == failure.Message);
    }
}
