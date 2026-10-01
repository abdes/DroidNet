// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Globalization;
using System.Reactive.Disposables;
using AwesomeAssertions;
using CommunityToolkit.WinUI;
using DroidNet.Controls;
using DroidNet.TestHelpers;
using DroidNet.Tests;
using Microsoft.Extensions.Logging;
using Microsoft.UI.Xaml.Automation.Peers;
using Microsoft.UI.Xaml.Automation.Provider;
using Microsoft.UI.Xaml.Controls;
using Microsoft.UI.Xaml;
using Oxygen.Editor.Runtime.Engine;
using Oxygen.Editor.World.Inspector.Geometry;
using Oxygen.Editor.World.Inspector;
using Oxygen.Editor.World.Slots;
using Oxygen.Editor.World;
using Oxygen.Editor.WorldEditor.Documents.Commands;
using Oxygen.Editor.WorldEditor.TestSupport;
using Oxygen.Managed.Assets.Model;
using Oxygen.Managed.Core;
using static Oxygen.Editor.WorldEditor.TestSupport.InspectorFieldCases;
using NumberBox = DroidNet.Controls.NumberBox;

namespace Oxygen.Editor.WorldEditor.TestSupport;

internal static class NativeSceneAssertions
{
    internal static async Task AssertEnvironmentFieldValuesAsync(NativeSceneFixture fixture, Dictionary<string, object> expected, CancellationToken cancellationToken)
    {
        var native = await fixture.ReadNativeAsync(cancellationToken).ConfigureAwait(true);
        foreach (var field in NativeEnvironmentFields)
        {
            _ = field.ReadSource(fixture.Source.Environment).Should().Be(expected[field.Field], "source field {0}", field.Field);
            _ = field.ReadNative(native).Should().Be(expected[field.Field], "native field {0}", field.Field);
        }

        foreach (var property in typeof(EnvironmentViewModel).GetProperties().Where(property => property.PropertyType == typeof(InspectorFieldDiagnostic)))
        {
            _ = ((InspectorFieldDiagnostic)property.GetValue(fixture.Model)!).Message.Should().BeEmpty("valid field workflow must have no {0} error", property.Name);
        }
    }

    internal static async Task<RuntimeNodeState> AssertGeometryAsync(NativeSceneFixture fixture, Guid nodeId, string shape, CancellationToken cancellationToken)
    {
        var geometry = fixture.Source.RootNodes.Single(node => node.Id == nodeId).Components.OfType<GeometryComponent>().Single();
        _ = geometry.Geometry.Should().NotBeNull();
        _ = geometry.Geometry!.Uri.Should().Be(AssetUris.BuildGeneratedUri($"BasicShapes/{shape}"));
        var native = await WaitForNodeAsync(fixture, nodeId, state => string.Equals(state.GeometryName, shape, StringComparison.OrdinalIgnoreCase), cancellationToken).ConfigureAwait(true);
        _ = native.GeometryKey.Should().NotBeNullOrEmpty();
        _ = native.VertexCount.Should().BePositive();
        _ = native.IndexCount.Should().BePositive();
        return native;
    }

    internal static async Task<RuntimeNodeState> WaitForNodeAsync(NativeSceneFixture fixture, Guid nodeId, Func<RuntimeNodeState, bool> expected, CancellationToken cancellationToken)
    {
        RuntimeNodeState? state = null;
        for (var attempt = 0; attempt < 150; attempt++)
        {
            state = await fixture.ReadNodeAsync(nodeId, cancellationToken).ConfigureAwait(true);
            if (expected(state))
            {
                return state;
            }

            await Task.Delay(10, cancellationToken).ConfigureAwait(true);
        }

        _ = expected(state!).Should().BeTrue("native state must converge: {0}; materials: {1}; outcomes: {2}", state, string.Join(", ", state!.MaterialKeys), string.Join("; ", fixture.Results.Select(result => result.Message)));
        return state;
    }

    internal async static Task<RuntimeEnvironmentState> WaitForCurrentSkyAsync(TestContext testContext, string stage, NativeSceneFixture fixture, Func<RuntimeEnvironmentState, bool> accepts, CancellationToken cancellationToken)
    {
        // Observations run in SceneMutation. Require the later render's identity;
        // a matching desired/published key alone could describe the prior frame.
        var reported = false;
        while (true)
        {
            cancellationToken.ThrowIfCancellationRequested();
            var state = await fixture.ReadNativeAsync(cancellationToken).ConfigureAwait(true);
            if (!reported)
            {
                testContext.WriteLine($"IBL entering {stage}: observed={state.SkyLightObserved}, usable={state.SkyLightUsable}, scene={state.SkyLightSceneLifetime}, frame={state.SkyLightFrameSequence}, generation={state.SkyLightPublishedRevision}, source={state.SkyLightPublishedSourceRevision}, desired={state.SkyLightDesiredSourceRevision}, age={state.SkyLightSourceAgeFrames}");
                reported = true;
            }

            if (!state.SkyLightObserved || !state.SkyLightUsable || state.SkyLightPublishedRevision == 0 || state.SkyLightSourceAgeFrames != 0 || state.SkyLightPublishedSourceRevision != state.SkyLightDesiredSourceRevision || !accepts(state))
            {
                continue;
            }

            testContext.WriteLine($"IBL {stage}: scene={state.SkyLightSceneLifetime}, frame={state.SkyLightFrameSequence}, generation={state.SkyLightPublishedRevision}, source={state.SkyLightPublishedSourceRevision}, age={state.SkyLightSourceAgeFrames}");
            return state;
        }
    }

    internal static Uri? ReadMaterialUri(SceneNode node) => node.Components.OfType<GeometryComponent>().Single().OverrideSlots.OfType<MaterialsSlot>().FirstOrDefault()?.Material.Uri;

    internal static async Task AssertMaterialAsync(NativeSceneFixture fixture, Guid nodeId, string expected, CancellationToken cancellationToken)
    {
        var state = await WaitForNodeAsync(fixture, nodeId, value => value.MaterialKeys.Length == 1 && string.Equals(value.MaterialKeys[0], expected, StringComparison.OrdinalIgnoreCase), cancellationToken).ConfigureAwait(true);
        _ = state.MaterialKeys.Should().ContainSingle().Which.Should().BeEquivalentTo(expected);
    }

    internal static async Task ObserveRenderedFramesAsync(NativeSceneFixture fixture, CancellationToken cancellationToken)
    {
        var nodeId = fixture.Source.RootNodes[0].Id;
        // Each observation crosses SceneMutation on a later native frame. Continue
        // beyond GPU deferred-release latency after a viewport is destroyed.
        for (var frame = 0; frame < 8; ++frame)
        {
            _ = await fixture.ReadNodeAsync(nodeId, cancellationToken).ConfigureAwait(true);
        }
    }
}
