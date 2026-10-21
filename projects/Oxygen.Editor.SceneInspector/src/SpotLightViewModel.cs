// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using Oxygen.Editor.Scenes.Authoring;
using Oxygen.Editor.Scenes.Properties;
using Oxygen.Editor.Schemas.Bindings;
using Oxygen.Editor.World;
using Oxygen.Editor.World.Components;

namespace Oxygen.Editor.SceneInspector;

/// <summary>Spot light inspector editing; cone angles are authored in radians and shown in degrees.</summary>
public sealed partial class SpotLightViewModel : LocalLightViewModel
{
    private const float RadToDeg = 180f / MathF.PI;
    private const float DegToRad = MathF.PI / 180f;

    /// <summary>Initializes a new instance of the <see cref="SpotLightViewModel"/> class.</summary>
    /// <param name="authoringProvider">The authoring of the scene being inspected.</param>
    public SpotLightViewModel(
        Func<SceneAuthoring?>? authoringProvider = null)
        : base(SceneProperties.SpotLight, "Edit Spot Light", authoringProvider)
    {
        this.InnerConeAngleRadians = this.Register(new NumberBinding<float>(SceneProperties.SpotLight.InnerConeAngleRadiansDescriptor!), nameof(this.InnerConeAngleDegrees));
        this.OuterConeAngleRadians = this.Register(new NumberBinding<float>(SceneProperties.SpotLight.OuterConeAngleRadiansDescriptor!), nameof(this.OuterConeAngleDegrees));
    }

    /// <summary>Gets the inner cone half-angle binding, in radians.</summary>
    public NumberBinding<float> InnerConeAngleRadians { get; }

    /// <summary>Gets the outer cone half-angle binding, in radians.</summary>
    public NumberBinding<float> OuterConeAngleRadians { get; }

    /// <summary>Gets or sets the inner cone half-angle in display degrees.</summary>
    public double InnerConeAngleDegrees
    {
        get => this.InnerConeAngleRadians.Value * RadToDeg;
        set => RequestDegrees(this.InnerConeAngleRadians, value);
    }

    /// <summary>Gets or sets the outer cone half-angle in display degrees.</summary>
    public double OuterConeAngleDegrees
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
    private static void RequestDegrees(NumberBinding<float> binding, double degrees)
    {
        var radians = (float)(degrees * DegToRad);
        if (binding.IsMixed || MathF.Abs(binding.Value - radians) > 1e-6f)
        {
            binding.Value = radians;
        }
    }
}
