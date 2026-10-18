// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Diagnostics.CodeAnalysis;
using AwesomeAssertions;
using Oxygen.Editor.World.Components;
using Oxygen.Editor.World.Serialization;

namespace Oxygen.Editor.World.Tests.Components;

/// <summary>
/// Verifies component factory lookup, registration independence, and failure modes.
/// </summary>
[TestClass]
[ExcludeFromCodeCoverage]
[TestCategory(nameof(GameComponent))]
public sealed class GameComponentTests
{
    [TestMethod]
    public void CreateAndHydrate_WithUnregisteredRecordType_ThrowsInvalidOperationException()
    {
        var unregistered = new UnregisteredComponentData { Name = "Unknown" };

        Action act = () => GameComponent.CreateAndHydrate(unregistered);

        _ = act.Should().Throw<InvalidOperationException>();
    }

    [TestMethod]
    public void CreateAndHydrate_DirectCallWithoutSceneSerializer_SucceedsForBuiltInTypes()
    {
        var transformData = new TransformData { Name = "DirectTransform" };

        var component = GameComponent.CreateAndHydrate(transformData);

        _ = component.Should().NotBeNull();
        _ = component.Should().BeOfType<TransformComponent>();
        _ = component.Name.Should().Be("DirectTransform");
    }

    private sealed record UnregisteredComponentData : ComponentData;
}
