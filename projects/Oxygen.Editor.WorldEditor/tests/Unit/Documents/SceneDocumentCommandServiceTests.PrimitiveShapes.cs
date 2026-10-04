// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using AwesomeAssertions;
using Moq;
using Oxygen.Editor.World.Components;
using Oxygen.Editor.World;
using Oxygen.Managed.Core.Diagnostics;

namespace Oxygen.Editor.WorldEditor.Unit.Tests.Documents;

/// <summary>Verifies primitive authoring covers the ten engine built-in shape generators.</summary>
public sealed partial class SceneDocumentCommandServiceTests
{
    /// <summary>Every canonical built-in kind produces a node referencing its engine-generated geometry asset.</summary>
    /// <param name="kind">The canonical built-in generator name.</param>
    /// <returns>The test task.</returns>
    [TestMethod]
    [DataRow("Cube")]
    [DataRow("Sphere")]
    [DataRow("Cylinder")]
    [DataRow("Cone")]
    [DataRow("Plane")]
    [DataRow("Capsule")]
    [DataRow("IcoSphere")]
    [DataRow("Torus")]
    [DataRow("Quad")]
    [DataRow("SubdividedCube")]
    public async Task CreatePrimitiveAsync_WhenKindIsCanonicalBuiltIn_CreatesNodeWithGeneratedGeometryUri(string kind)
    {
        var fixture = CreateFixture();
        _ = fixture.Sync
            .Setup(value => value.CreateNodeAsync(It.IsAny<SceneNode>(), parentGuid: null))
            .Returns(Task.CompletedTask);
        var scene = CreateScene();
        var context = CreateContext(scene);

        var result = await fixture.Sut.CreatePrimitiveAsync(context, kind).ConfigureAwait(false);

        _ = result.Succeeded.Should().BeTrue();
        _ = result.Value.Should().NotBeNull();
        var node = result.Value!;
        _ = node.Name.Should().Be(kind);
        _ = scene.RootNodes.Should().ContainSingle().Which.Should().BeSameAs(node);
        var geometry = node.Components.OfType<GeometryComponent>().Should().ContainSingle().Which;
        _ = geometry.Geometry.Should().NotBeNull();
        _ = geometry.Geometry!.Uri
            .Should().Be(new Uri($"asset:///Engine/Generated/BasicShapes/{kind}"));
        _ = context.History.UndoStack.Should().ContainSingle();
    }

    /// <summary>An unknown primitive kind fails visibly and leaves the scene untouched.</summary>
    /// <returns>The test task.</returns>
    [TestMethod]
    public async Task CreatePrimitiveAsync_WhenKindIsUnknown_FailsVisiblyAndCreatesNoNode()
    {
        var fixture = CreateFixture();
        var scene = CreateScene();
        var context = CreateContext(scene);

        var result = await fixture.Sut.CreatePrimitiveAsync(context, "Tetrahedron").ConfigureAwait(false);

        _ = result.Succeeded.Should().BeFalse();
        _ = result.OperationResultId.Should().NotBeNull("an unsupported primitive kind must publish a visible operation result");
        _ = scene.RootNodes.Should().BeEmpty();
        _ = context.History.UndoStack.Should().BeEmpty();
        var published = fixture.Results.Published.Should().ContainSingle().Which;
        _ = published.OperationId.Should().Be(result.OperationResultId!.Value);
        _ = published.OperationKind.Should().Be(SceneOperationKinds.NodeCreatePrimitive);
        _ = published.Diagnostics.Should().ContainSingle()
            .Which.Code.Should().Be(DiagnosticCodes.ScenePrefix + "CREATE_PRIMITIVE_FAILED");
    }
}
