// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using AwesomeAssertions;
using Oxygen.Editor.Scenes.Properties;
using Oxygen.Editor.WorldEditor.TestSupport;
using static Oxygen.Editor.WorldEditor.TestSupport.NativeFieldCases;
using static Oxygen.Editor.WorldEditor.TestSupport.NativeSceneAssertions;

namespace Oxygen.Editor.WorldEditor.Integration.UI.Tests.Authoring;

[TestClass]
public sealed partial class EnvironmentDefaultsTests : DroidNet.Tests.VisualUserInterfaceTests
{
    public TestContext TestContext { get; set; } = null!;

    /// <summary>A scene with the default environment, or with none saved, shows the atmosphere and reaches the engine with the defaults, which survive saved reload.</summary>
    /// <param name="missing">Whether the loaded scene omits its environment.</param>
    /// <returns>The test task.</returns>
    [TestMethod]
    [DataRow(false)]
    [DataRow(true)]
    public Task DefaultEnvironmentReachesNativeState(bool missing) => EnqueueAsync(async () =>
    {
        var fixture = new NativeSceneFixture(automatic: false, scene =>
        {
            if (missing)
            {
                scene.Hydrate(scene.Dehydrate() with { Environment = null! });
            }
        });
        await using var lifetime = fixture.ConfigureAwait(true);
        using var timeout = CancellationTokenSource.CreateLinkedTokenSource(this.TestContext.CancellationToken);
        timeout.CancelAfter(TimeSpan.FromSeconds(30));
        await fixture.InitializeAsync(timeout.Token).ConfigureAwait(true);
        _ = SceneSkyFields.ResolveBackdrop(fixture.Source.Environment).Should().Be(EnvironmentBackdrop.Atmosphere);
        var expected = NativeEnvironmentFields.ToDictionary(field => field.Field, field => field.ReadSource(fixture.Source.Environment), StringComparer.Ordinal);
        await AssertEnvironmentFieldValuesAsync(fixture, expected, timeout.Token).ConfigureAwait(true);
        await fixture.SaveAndReopenAsync(timeout.Token).ConfigureAwait(true);
        await AssertEnvironmentFieldValuesAsync(fixture, expected, timeout.Token).ConfigureAwait(true);
        _ = fixture.Document.History.UndoStack.Should().BeEmpty();
    });
}
