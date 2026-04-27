// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using AwesomeAssertions;
using Oxygen.Core.Diagnostics;

namespace DroidNet.TestHelpers.Tests;

[TestClass]
[TestCategory("Operation Results")]
public sealed class DiagnosticSceneVocabularyTests
{
    [TestMethod]
    public void SceneDiagnosticPrefixes_AreAllocated()
    {
        _ = DiagnosticCodes.ScenePrefix.Should().Be("OXE.SCENE.");
        _ = DiagnosticCodes.DocumentPrefix.Should().Be("OXE.DOCUMENT.");
        _ = DiagnosticCodes.LiveSyncPrefix.Should().Be("OXE.LIVESYNC.");
    }

    [TestMethod]
    public void SceneOperationKinds_AreStableStrings()
    {
        _ = SceneOperationKinds.NodeCreatePrimitive.Should().Be("Scene.Node.CreatePrimitive");
        _ = SceneOperationKinds.NodeCreateLight.Should().Be("Scene.Node.CreateLight");
        _ = SceneOperationKinds.NodeRename.Should().Be("Scene.Node.Rename");
        _ = SceneOperationKinds.NodeDelete.Should().Be("Scene.Node.Delete");
        _ = SceneOperationKinds.NodeReparent.Should().Be("Scene.Node.Reparent");
        _ = SceneOperationKinds.EditTransform.Should().Be("Scene.Component.EditTransform");
        _ = SceneOperationKinds.EditGeometry.Should().Be("Scene.Component.EditGeometry");
        _ = SceneOperationKinds.EditMaterialSlot.Should().Be("Scene.Component.EditMaterialSlot");
        _ = SceneOperationKinds.EditPerspectiveCamera.Should().Be("Scene.Component.EditCamera");
        _ = SceneOperationKinds.EditDirectionalLight.Should().Be("Scene.Component.EditLight");
        _ = SceneOperationKinds.AddComponent.Should().Be("Scene.Component.Add");
        _ = SceneOperationKinds.RemoveComponent.Should().Be("Scene.Component.Remove");
        _ = SceneOperationKinds.EditEnvironment.Should().Be("Scene.Environment.Edit");
        _ = SceneOperationKinds.ExplorerFolderCreate.Should().Be("Scene.ExplorerFolder.Create");
        _ = SceneOperationKinds.ExplorerFolderRename.Should().Be("Scene.ExplorerFolder.Rename");
        _ = SceneOperationKinds.ExplorerFolderDelete.Should().Be("Scene.ExplorerFolder.Delete");
        _ = SceneOperationKinds.ExplorerLayoutMoveNode.Should().Be("Scene.ExplorerLayout.MoveNode");
        _ = SceneOperationKinds.Save.Should().Be("Scene.Save");
    }

    [TestMethod]
    public void LiveSyncDiagnosticCodes_AreStableStrings()
    {
        _ = LiveSyncDiagnosticCodes.NotRunning.Should().Be("OXE.LIVESYNC.NotRunning");
        _ = LiveSyncDiagnosticCodes.RuntimeFaulted.Should().Be("OXE.LIVESYNC.RuntimeFaulted");
        _ = LiveSyncDiagnosticCodes.Cancelled.Should().Be("OXE.LIVESYNC.Cancelled");
        _ = LiveSyncDiagnosticCodes.TransformRejected.Should().Be("OXE.LIVESYNC.TRANSFORM.Rejected");
        _ = LiveSyncDiagnosticCodes.TransformFailed.Should().Be("OXE.LIVESYNC.TRANSFORM.Failed");
        _ = LiveSyncDiagnosticCodes.GeometryRejected.Should().Be("OXE.LIVESYNC.GEOMETRY.Rejected");
        _ = LiveSyncDiagnosticCodes.GeometryFailed.Should().Be("OXE.LIVESYNC.GEOMETRY.Failed");
        _ = LiveSyncDiagnosticCodes.GeometryUnresolvedAtRuntime.Should().Be("OXE.LIVESYNC.GEOMETRY.UnresolvedAtRuntime");
        _ = LiveSyncDiagnosticCodes.CameraRejected.Should().Be("OXE.LIVESYNC.CAMERA.Rejected");
        _ = LiveSyncDiagnosticCodes.CameraUnsupported.Should().Be("OXE.LIVESYNC.CAMERA.Unsupported");
        _ = LiveSyncDiagnosticCodes.CameraFailed.Should().Be("OXE.LIVESYNC.CAMERA.Failed");
        _ = LiveSyncDiagnosticCodes.LightRejected.Should().Be("OXE.LIVESYNC.LIGHT.Rejected");
        _ = LiveSyncDiagnosticCodes.LightFailed.Should().Be("OXE.LIVESYNC.LIGHT.Failed");
        _ = LiveSyncDiagnosticCodes.MaterialUnsupported.Should().Be("OXE.LIVESYNC.MATERIAL.Unsupported");
        _ = LiveSyncDiagnosticCodes.EnvironmentAtmosphereUnsupported.Should().Be("OXE.LIVESYNC.ENVIRONMENT.Atmosphere.Unsupported");
        _ = LiveSyncDiagnosticCodes.EnvironmentSunUnsupported.Should().Be("OXE.LIVESYNC.ENVIRONMENT.Sun.Unsupported");
        _ = LiveSyncDiagnosticCodes.EnvironmentExposureUnsupported.Should().Be("OXE.LIVESYNC.ENVIRONMENT.Exposure.Unsupported");
        _ = LiveSyncDiagnosticCodes.EnvironmentToneMappingUnsupported.Should().Be("OXE.LIVESYNC.ENVIRONMENT.ToneMapping.Unsupported");
        _ = LiveSyncDiagnosticCodes.EnvironmentBackgroundUnsupported.Should().Be("OXE.LIVESYNC.ENVIRONMENT.Background.Unsupported");
        _ = LiveSyncDiagnosticCodes.EnvironmentRejected.Should().Be("OXE.LIVESYNC.ENVIRONMENT.Rejected");
    }

    [TestMethod]
    public void SceneDiagnosticCodes_AreStableStrings()
    {
        _ = SceneDiagnosticCodes.TransformScaleZeroAxis.Should().Be("OXE.SCENE.TransformComponent.Scale.ZeroAxis");
        _ = SceneDiagnosticCodes.TransformFieldNotFinite.Should().Be("OXE.SCENE.TransformComponent.Field.NotFinite");
        _ = SceneDiagnosticCodes.GeometryReferenceRequired.Should().Be("OXE.SCENE.GeometryComponent.Geometry.Required");
        _ = SceneDiagnosticCodes.PerspectiveCameraNearFarInvalid.Should().Be("OXE.SCENE.PerspectiveCamera.NearFar.Invalid");
        _ = SceneDiagnosticCodes.PerspectiveCameraNearPlaneNonPositive.Should().Be("OXE.SCENE.PerspectiveCamera.NearPlane.NonPositive");
        _ = SceneDiagnosticCodes.PerspectiveCameraAspectRatioNonPositive.Should().Be("OXE.SCENE.PerspectiveCamera.AspectRatio.NonPositive");
        _ = SceneDiagnosticCodes.DirectionalLightSunExclusivity.Should().Be("OXE.SCENE.DirectionalLight.Sun.Exclusivity");
        _ = SceneDiagnosticCodes.DirectionalLightFieldNotFinite.Should().Be("OXE.SCENE.DirectionalLight.Field.NotFinite");
        _ = SceneDiagnosticCodes.EnvironmentSunRefStale.Should().Be("OXE.SCENE.ENVIRONMENT.SunRefStale");
        _ = SceneDiagnosticCodes.EnvironmentExposureModeInvalid.Should().Be("OXE.SCENE.ENVIRONMENT.ExposureMode.Invalid");
        _ = SceneDiagnosticCodes.EnvironmentToneMappingInvalid.Should().Be("OXE.SCENE.ENVIRONMENT.ToneMapping.Invalid");
        _ = SceneDiagnosticCodes.EnvironmentExposureCompensationInvalid.Should().Be("OXE.SCENE.ENVIRONMENT.ExposureCompensation.Invalid");
        _ = SceneDiagnosticCodes.EnvironmentBackgroundColorInvalid.Should().Be("OXE.SCENE.ENVIRONMENT.BackgroundColor.Invalid");
        _ = SceneDiagnosticCodes.ComponentAddDenied.Should().Be("OXE.SCENE.COMPONENT.AddDenied");
        _ = SceneDiagnosticCodes.ComponentRemoveDenied.Should().Be("OXE.SCENE.COMPONENT.RemoveDenied");
    }
}
