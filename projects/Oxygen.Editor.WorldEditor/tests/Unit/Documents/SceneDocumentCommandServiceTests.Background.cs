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

namespace Oxygen.Editor.WorldEditor.Unit.Tests.Documents;

public sealed partial class SceneDocumentCommandServiceTests
{
    [TestMethod]
    public async Task BackgroundNativeRejection_PreservesAuthoringAndPublishesFieldDiagnostic()
    {
        var fixture = CreateFixture();
        var scene = CreateScene();
        var context = CreateContext(scene);
        var failure = new SyncOutcome(SyncStatus.Rejected, SceneOperationKinds.EditEnvironment, AffectedScope.Empty, LiveSyncDiagnosticCodes.EnvironmentRejected, "Native background was rejected");
        _ = fixture.Sync.Setup(value => value.UpdateEnvironmentAsync(scene, It.IsAny<SceneEnvironmentData>(), It.IsAny<SceneSyncRevision>(), It.IsAny<CancellationToken>()))
            .ReturnsAsync(new EnvironmentSyncResult(SyncStatus.Rejected, new Dictionary<string, SyncOutcome>(StringComparer.Ordinal)
            {
                [nameof(SceneEnvironmentData.Background)] = failure,
            }));
        var color = new Vector3(0.25f, 0.5f, 0.75f);

        var result = await fixture.Sut.EditSceneEnvironmentPropertiesAsync(
            context,
            PropertyEdit.SingleEdit(SceneSkyFields.SolidColor, color),
            "Edit Background",
            EditSessionToken.OneShot).ConfigureAwait(false);

        _ = result.Succeeded.Should().BeTrue();
        _ = SceneSkyFields.SolidColorOf(scene.Environment).Should().Be(color);
        _ = context.History.UndoStack.Should().ContainSingle();
        _ = fixture.Results.Published.SelectMany(value => value.Diagnostics).Should().Contain(value => value.Code == LiveSyncDiagnosticCodes.EnvironmentRejected && value.Message == failure.Message);
    }
}
