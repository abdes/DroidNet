// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using AwesomeAssertions;
using Oxygen.Editor.Scenes.Authoring;
using Oxygen.Editor.Scenes.Changes;
using Oxygen.Editor.Scenes.Edits.Properties;
using Oxygen.Editor.Scenes.Properties;
using Oxygen.Editor.Scenes.Tests.TestSupport;
using Oxygen.Editor.Schemas;
using Oxygen.Editor.World;

namespace Oxygen.Editor.Scenes.Tests.Edits.Properties;

/// <summary>
/// What is specific to <see cref="SetSceneProperties"/> (scene-edits.md 3.10). The value and cross-field rules of the
/// environment are <c>SceneEnvironmentRulesTests</c>.
/// </summary>
[TestClass]
public sealed class SetScenePropertiesTests
{
    [TestMethod]
    public async Task ExposureCompensation_RaisesSceneChange()
    {
        using var fixture = await AuthoringFixture.CreateAsync(SampleScene.Build()).ConfigureAwait(false);
        var edit = new SetSceneProperties(Exposure(1.5f), "Exposure");

        var result = await fixture.Authoring.ExecuteAsync(edit).ConfigureAwait(false);

        SceneAssert.Applied(result);
        _ = fixture.Scene.Environment.PostProcess.ExposureCompensationEv.Should().Be(1.5f);
        SceneAssert.Changes(fixture.Changes.Only(), new SceneChange.PropertiesChanged(Node: null, [SceneProperties.SceneEnvironment.ExposureCompensation.Id], EditPhase.Commit));
    }

    [TestMethod]
    public async Task NodeComponentProperty_IsInvalid()
    {
        using var fixture = await AuthoringFixture.CreateAsync(SampleScene.Build()).ConfigureAwait(false);
        var edit = new SetSceneProperties(new PropertyEdit.Builder().Set(SceneSchema.Transform.PositionX, 3f).Build(), "Position");
        var before = fixture.Snapshot();

        var result = await fixture.Authoring.ExecuteAsync(edit).ConfigureAwait(false);

        SceneAssert.Invalid(result, SceneEditCodes.PropertyUnknown);
        SceneAssert.Unchanged(fixture, before);
    }

    [TestMethod]
    public async Task Undo_RestoresTheEnvironment()
    {
        using var fixture = await AuthoringFixture.CreateAsync(SampleScene.Build()).ConfigureAwait(false);
        var original = fixture.Scene.Environment.PostProcess.ExposureCompensationEv;

        _ = await SceneAssert.RoundTripAsync(fixture, new SetSceneProperties(Exposure(1.5f), "Exposure")).ConfigureAwait(false);

        _ = fixture.Scene.Environment.PostProcess.ExposureCompensationEv.Should().Be(original);
    }

    private static PropertyEdit Exposure(float value)
        => new PropertyEdit.Builder().Set(SceneProperties.SceneEnvironment.ExposureCompensation, value).Build();
}
