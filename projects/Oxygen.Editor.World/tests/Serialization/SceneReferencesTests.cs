// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Diagnostics.CodeAnalysis;
using System.Text;
using AwesomeAssertions;
using Oxygen.Editor.World.Serialization;
using Oxygen.Editor.World.Tests.TestSupport;
using Oxygen.Managed.Core;

namespace Oxygen.Editor.World.Tests.Serialization;

/// <summary>
/// Verifies immutability and serialization round-tripping of scene references collections.
/// </summary>
[TestClass]
[ExcludeFromCodeCoverage]
[TestCategory(nameof(SceneReferencesData))]
public sealed class SceneReferencesTests
{
    [TestMethod]
    [DataRow(false)]
    [DataRow(true)]
    public void EmptySceneReferences_CreatedOrHydrated_RejectDirectMutation(bool hydrate)
    {
        var scene = new Scene { Name = "Scene" };
        if (hydrate)
        {
            var data = scene.Dehydrate();
            _ = data.References.Should().BeNull();
            scene.Hydrate(data);
        }

        var reference = AssetPath.Parse("/Content/Scripts/Orbit.oscript");
        foreach (var collection in new[]
        {
            scene.References.Scripts,
            scene.References.InputActions,
            scene.References.PhysicsSidecars,
        })
        {
            var edit = () => collection.Add(reference);
            _ = edit.Should().Throw<NotSupportedException>();
            _ = collection.Should().BeEmpty();
        }

        var extraEdit = () => scene.References.ExtraAssets.Add(AssetPath.Parse("/Content/Extra.otex"));
        _ = extraEdit.Should().Throw<NotSupportedException>();
        _ = scene.References.ExtraAssets.Should().BeEmpty();
    }

    [TestMethod]
    public void HydratedSceneReferencesCannotBeMutatedOutsideSceneCommands()
    {
        var scene = new Scene { Name = "Scene" };
        var script = AssetPath.Parse("/Content/Scripts/Orbit.oscript");
        scene.Hydrate(new SceneData
        {
            Id = scene.Id,
            Name = scene.Name,
            RootNodes = [],
            References = new SceneReferencesData { Scripts = [script] },
        });

        var edit = () => scene.References.Scripts.Add(AssetPath.Parse("/Content/Scripts/Other.oscript"));

        _ = edit.Should().Throw<NotSupportedException>();
        _ = scene.References.Scripts.Should().ContainSingle().Which.Should().Be(script);
    }

    [TestMethod]
    public async Task SceneReferences_RoundTripAndRemainOmittedWhenEmpty()
    {
        var references = new SceneReferencesData
        {
            Scripts = [AssetPath.Parse("/Content/Scripts/Orbit.oscript.json")],
            InputActions = [AssetPath.Parse("/Content/Input/Jump.oiact")],
            PhysicsSidecars = [AssetPath.Parse("/Content/Physics/Main.opscene")],
            ExtraAssets = [AssetPath.Parse("/Content/Extras/lookup.bin")],
        };
        var scene = Scene.CreateAndHydrate(new SceneData
        {
            Id = SceneId.New(),
            Name = "Reference Round Trip",
            References = references,
        });

        var restored = await SceneTestSupport.RoundTripAsync(scene).ConfigureAwait(false);

        _ = restored.References.Should().BeEquivalentTo(references);
        var emptyStream = new MemoryStream();
        await using var emptyStreamLifetime = emptyStream.ConfigureAwait(false);
        await SceneSerializer.SerializeAsync(emptyStream, new Scene { Name = "Empty References" }).ConfigureAwait(false);
        var json = Encoding.UTF8.GetString(emptyStream.ToArray());
        _ = json.Should().NotContain("\"References\"");
    }
}
