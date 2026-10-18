// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using AwesomeAssertions;
using Oxygen.Editor.Scenes.Authoring;
using Oxygen.Editor.Scenes.Changes;
using Oxygen.Editor.Scenes.Edits.Settings;
using Oxygen.Editor.Scenes.Tests.TestSupport;
using Oxygen.Editor.World.Serialization;
using Oxygen.Managed.Core;

namespace Oxygen.Editor.Scenes.Tests.Edits.Settings;

/// <summary>
/// What is specific to <see cref="SetSceneReferences"/> (scene-edits.md 3.11 and 4): the scene's asset references are
/// replaced as a whole and each must be a canonical asset of its type. The edit has no target node. Undo and the
/// change stream of every edit are <c>EditContractTests</c>.
/// </summary>
[TestClass]
public sealed class SetSceneReferencesTests
{
    public static IEnumerable<TestDataRow<ReferencesCase>> InvalidReferences =>
    [
        new(new ReferencesCase(new SceneReferencesData { Scripts = [SampleScene.CubeGeometry] })) { DisplayName = "ScriptOfWrongType" },
        new(new ReferencesCase(new SceneReferencesData { ExtraAssets = ["/Content/../Extra.bin"] })) { DisplayName = "ExtraPathNotCanonical" },
    ];

    [TestMethod]
    public async Task References_SetAndRaiseSceneReferencesChanged()
    {
        using var fixture = await AuthoringFixture.CreateAsync(SampleScene.Build()).ConfigureAwait(false);
        var references = Complete();

        var result = await fixture.Authoring.ExecuteAsync(new SetSceneReferences(references)).ConfigureAwait(false);

        SceneAssert.Applied(result);
        _ = fixture.Scene.References.Scripts.Should().Equal(references.Scripts);
        _ = fixture.Scene.References.InputActions.Should().Equal(references.InputActions);
        _ = fixture.Scene.References.PhysicsSidecars.Should().Equal(references.PhysicsSidecars);
        _ = fixture.Scene.References.ExtraAssets.Should().Equal(references.ExtraAssets);
        SceneAssert.Changes(fixture.Changes.Only(), new SceneChange.SceneReferencesChanged());
    }

    [TestMethod]
    [DynamicData(nameof(InvalidReferences))]
    public async Task InvalidReference_IsInvalid(ReferencesCase row)
    {
        ArgumentNullException.ThrowIfNull(row);
        using var fixture = await AuthoringFixture.CreateAsync(SampleScene.Build()).ConfigureAwait(false);
        var before = fixture.Snapshot();

        var result = await fixture.Authoring.ExecuteAsync(new SetSceneReferences(row.References)).ConfigureAwait(false);

        SceneAssert.Invalid(result, SceneEditCodes.AssetReferenceInvalid);
        SceneAssert.Unchanged(fixture, before);
    }

    [TestMethod]
    public async Task EqualReferences_AppliesWithoutAnEntry()
    {
        using var fixture = await AuthoringFixture.CreateAsync(SampleScene.Builder().References(Complete()).Build()).ConfigureAwait(false);
        var before = fixture.Snapshot();

        var result = await fixture.Authoring.ExecuteAsync(new SetSceneReferences(Complete())).ConfigureAwait(false);

        SceneAssert.Applied(result);
        SceneAssert.Unchanged(fixture, before);
    }

    private static SceneReferencesData Complete()
        => new()
        {
            Scripts = [SampleScene.MoveScript],
            InputActions = [AssetPath.Parse("/Content/Jump.oiact")],
            PhysicsSidecars = [AssetPath.Parse("/Content/World.opscene")],
            ExtraAssets = ["/Content/Extra.bin"],
        };
}
