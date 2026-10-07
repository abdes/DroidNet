// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using Oxygen.Editor.WorldEditor.Documents.Commands;

namespace Oxygen.Editor.World.Inspector;

/// <summary>Point light inspector editing.</summary>
public sealed partial class PointLightViewModel : LocalLightViewModel
{
    /// <summary>Initializes a new instance of the <see cref="PointLightViewModel"/> class.</summary>
    /// <param name="commandService">Optional command service used to apply light edits.</param>
    /// <param name="commandContextProvider">Optional provider for the active scene command context.</param>
    public PointLightViewModel(
        ISceneDocumentCommandService? commandService = null,
        Func<SceneDocumentCommandContext?>? commandContextProvider = null)
        : base(SceneDocumentCommandService.PointLight, "Edit Point Light", commandService, commandContextProvider)
    {
    }

    /// <inheritdoc />
    public override string Header => "Point Light";

    /// <inheritdoc />
    public override string Description => "Omnidirectional emission, range and shadow data.";

    /// <inheritdoc />
    private protected override LightComponent? FindLight(SceneNode node)
        => node.Components.OfType<PointLightComponent>().FirstOrDefault();
}
