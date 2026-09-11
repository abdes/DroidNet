// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using AwesomeAssertions;
using Microsoft.UI.Xaml.Controls;
using Oxygen.Editor.World.Inspector;

namespace Oxygen.Editor.World.Tests;

/// <summary>Qualifies default and legacy missing environment data in the scene inspector.</summary>
public sealed partial class InspectorControlTests
{
    /// <summary>Empty selection opens scene settings and preserves defaults through saved reload.</summary>
    /// <param name="missing">Whether the loaded scene omits its environment.</param>
    /// <returns>The test task.</returns>
    [TestMethod]
    [DataRow(false)]
    [DataRow(true)]
    public Task DefaultEnvironmentAndEmptySelectionReachNativeState(bool missing) => EnqueueAsync(async () =>
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
        using var host = fixture.CreateInspectorHost([]);
        _ = host.HasInspectorContent.Should().BeTrue();
        var model = host.PropertyEditors.Should().ContainSingle().Which.Should().BeOfType<EnvironmentViewModel>().Subject;
        var view = new EnvironmentView { ViewModel = model };
        await LoadTestContentAsync(new ScrollViewer { Content = view }).ConfigureAwait(true);
        _ = model.AtmosphereEnabled.Should().BeTrue();
        _ = fixture.Source.Environment.SunNodeId.Should().BeNull();
        var expected = NativeEnvironmentFields.ToDictionary(field => field.Field, field => field.ReadSource(fixture.Source.Environment), StringComparer.Ordinal);
        await AssertEnvironmentFieldValuesAsync(fixture, expected, timeout.Token).ConfigureAwait(true);
        await fixture.SaveAndReopenAsync(timeout.Token).ConfigureAwait(true);
        await AssertEnvironmentFieldValuesAsync(fixture, expected, timeout.Token).ConfigureAwait(true);
        _ = fixture.Context.History.UndoStack.Should().BeEmpty();
    });
}
