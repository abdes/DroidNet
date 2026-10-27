// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using Oxygen.Editor.Scenes.Authoring;
using Oxygen.Editor.Scenes.Properties;
using Oxygen.Editor.World;
using Oxygen.Editor.World.Components;

namespace Oxygen.Editor.SceneInspector.Components.Lights;

/// <summary>Point light inspector editing.</summary>
/// <param name="authoringProvider">The authoring of the scene being inspected.</param>
public sealed partial class PointLightViewModel(
    Func<SceneAuthoring?>? authoringProvider = null)
    : LocalLightViewModel(SceneProperties.PointLight, "Edit Point Light", authoringProvider)
{
    /// <inheritdoc />
    public override string Header => "Point Light";

    /// <inheritdoc />
    public override string Description => "Omnidirectional emission, range and shadow data.";

    /// <inheritdoc />
    private protected override LightComponent? FindLight(SceneNode node)
        => node.Components.OfType<PointLightComponent>().FirstOrDefault();
}
