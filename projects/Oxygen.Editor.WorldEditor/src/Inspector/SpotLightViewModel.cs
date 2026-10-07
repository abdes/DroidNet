// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using Oxygen.Editor.Schemas.Bindings;
using Oxygen.Editor.WorldEditor.Documents.Commands;

namespace Oxygen.Editor.World.Inspector;

/// <summary>Spot light inspector editing; cone angles are authored in radians and shown in degrees.</summary>
public sealed partial class SpotLightViewModel : LocalLightViewModel
{
    private const float RadToDeg = 180f / MathF.PI;
    private const float DegToRad = MathF.PI / 180f;

    /// <summary>Initializes a new instance of the <see cref="SpotLightViewModel"/> class.</summary>
    /// <param name="commandService">Optional command service used to apply light edits.</param>
    /// <param name="commandContextProvider">Optional provider for the active scene command context.</param>
    public SpotLightViewModel(
        ISceneDocumentCommandService? commandService = null,
        Func<SceneDocumentCommandContext?>? commandContextProvider = null)
        : base(SceneDocumentCommandService.SpotLight, "Edit Spot Light", commandService, commandContextProvider)
    {
        this.InnerConeAngleRadians = this.Register(new PropertyBinding<float>(SceneDocumentCommandService.SpotLight.InnerConeAngleRadiansDescriptor!), nameof(this.InnerConeAngleDegrees));
        this.OuterConeAngleRadians = this.Register(new PropertyBinding<float>(SceneDocumentCommandService.SpotLight.OuterConeAngleRadiansDescriptor!), nameof(this.OuterConeAngleDegrees));
    }

    /// <summary>Gets the inner cone half-angle binding, in radians.</summary>
    public PropertyBinding<float> InnerConeAngleRadians { get; }

    /// <summary>Gets the outer cone half-angle binding, in radians.</summary>
    public PropertyBinding<float> OuterConeAngleRadians { get; }

    /// <summary>Gets or sets the inner cone half-angle in display degrees.</summary>
    public float InnerConeAngleDegrees
    {
        get => this.InnerConeAngleRadians.Value * RadToDeg;
        set => RequestDegrees(this.InnerConeAngleRadians, value);
    }

    /// <summary>Gets or sets the outer cone half-angle in display degrees.</summary>
    public float OuterConeAngleDegrees
    {
        get => this.OuterConeAngleRadians.Value * RadToDeg;
        set => RequestDegrees(this.OuterConeAngleRadians, value);
    }

    /// <summary>Gets inner cone feedback.</summary>
    public InspectorFieldDiagnostic InnerConeAngleDiagnostic => this.Diagnostic(this.InnerConeAngleRadians.Id.Id);

    /// <summary>Gets outer cone feedback.</summary>
    public InspectorFieldDiagnostic OuterConeAngleDiagnostic => this.Diagnostic(this.OuterConeAngleRadians.Id.Id);

    /// <inheritdoc />
    public override string Header => "Spot Light";

    /// <inheritdoc />
    public override string Description => "Cone emission, range and shadow data.";

    /// <inheritdoc />
    private protected override LightComponent? FindLight(SceneNode node)
        => node.Components.OfType<SpotLightComponent>().FirstOrDefault();

    /// <summary>Requests a display-degree angle, ignoring the echo of its own radian round trip.</summary>
    private static void RequestDegrees(PropertyBinding<float> binding, float degrees)
    {
        var radians = degrees * DegToRad;
        if (binding.IsMixed || MathF.Abs(binding.Value - radians) > 1e-6f)
        {
            binding.Value = radians;
        }
    }
}
