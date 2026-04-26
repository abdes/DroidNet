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
        _ = SceneOperationKinds.ExplorerFolderCreate.Should().Be("Scene.ExplorerFolder.Create");
        _ = SceneOperationKinds.ExplorerFolderRename.Should().Be("Scene.ExplorerFolder.Rename");
        _ = SceneOperationKinds.ExplorerFolderDelete.Should().Be("Scene.ExplorerFolder.Delete");
        _ = SceneOperationKinds.ExplorerLayoutMoveNode.Should().Be("Scene.ExplorerLayout.MoveNode");
        _ = SceneOperationKinds.Save.Should().Be("Scene.Save");
    }
}
