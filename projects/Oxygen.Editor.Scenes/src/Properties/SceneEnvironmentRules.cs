// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Numerics;
using Oxygen.Editor.ContentPipeline;
using Oxygen.Editor.Scenes.Authoring;
using Oxygen.Editor.Schemas;
using Oxygen.Editor.World;
using Oxygen.Editor.World.Serialization;
using Oxygen.Managed.Core;

namespace Oxygen.Editor.Scenes.Properties;

/// <summary>The rules of the scene's environment: the sky, the backdrop and the post process stay valid together.</summary>
internal sealed class SceneEnvironmentRules : IComponentRules
{
    /// <inheritdoc/>
    public string Kind => SceneProperties.SceneEnvironmentKind;

    /// <inheritdoc/>
    public IReadOnlyDictionary<PropertyId, PropertyDescriptor> Descriptors => SceneProperties.SceneEnvironment.ById;

    /// <inheritdoc/>
    public bool IsSceneKind => true;

    /// <inheritdoc/>
    public object? TargetOf(Scene scene, SceneNode? node) => new SceneEnvironmentTarget(scene.Environment);

    /// <inheritdoc/>
    public void Commit(Scene scene, object target) => scene.SetEnvironment(((SceneEnvironmentTarget)target).Value);

    /// <inheritdoc/>
    /// <remarks>
    /// One environment property can rewrite several records, as the backdrop choice rewrites the atmosphere, the sky
    /// sphere and the background, so every environment property is coupled to every other.
    /// </remarks>
    public IReadOnlySet<PropertyId> Coupled(IEnumerable<PropertyId> edited) => this.Descriptors.Keys.ToHashSet();

    /// <inheritdoc/>
    public SceneDiagnostic? Validate(Scene scene, IReadOnlyList<PropertyTargetEdit> edits)
    {
        var target = new SceneEnvironmentTarget(scene.Environment);
        foreach (var edit in edits)
        {
            PropertyApply.ApplyToTarget(target, edit.Edit, this.Descriptors);
        }

        var candidate = target.Value;
        return ProblemOf(candidate) is { } problem ? SceneDiagnostic.Error(SceneEditCodes.PropertiesInconsistent, problem) : null;
    }

    /// <summary>Checks the rules a complete environment must follow across its fields.</summary>
    /// <param name="candidate">The environment.</param>
    /// <returns>Why it breaks a rule, or <see langword="null"/> when it follows them all.</returns>
    internal static string? ProblemOf(SceneEnvironmentData candidate)
        => candidate.Background is { Enabled: true } background && !IsDisplayColor(background.ColorRgb)
            ? "The background color must be between 0 and 1."
            : (candidate.SkySphere is { } sphere ? SceneSkyFields.ValidateSkySphere(sphere) : null)
                ?? (candidate.SkyLight is { } light ? SceneSkyFields.ValidateSkyLight(light) : null)
                ?? (candidate.SkyAtmosphere is { } sky && !IsFinite(sky) ? "Sky atmosphere values must be finite." : null)
                ?? (candidate.PostProcess is { } post ? SceneDescriptorGenerator.ValidatePostProcess(post) : null);

    private static bool IsDisplayColor(Vector3 value) => IsFinite(value) && value == Vector3.Clamp(value, Vector3.Zero, Vector3.One);

    private static bool IsFinite(Vector3 value) => float.IsFinite(value.X) && float.IsFinite(value.Y) && float.IsFinite(value.Z);

    private static bool IsFinite(SkyAtmosphereEnvironmentData value)
        => float.IsFinite(value.PlanetRadiusMeters)
            && float.IsFinite(value.AtmosphereHeightMeters)
            && IsFinite(value.GroundAlbedoRgb)
            && float.IsFinite(value.RayleighScaleHeightMeters)
            && float.IsFinite(value.MieScaleHeightMeters)
            && float.IsFinite(value.MieAnisotropy)
            && IsFinite(value.SkyLuminanceFactorRgb)
            && float.IsFinite(value.AerialPerspectiveDistanceScale)
            && float.IsFinite(value.AerialScatteringStrength)
            && float.IsFinite(value.AerialPerspectiveStartDepthMeters)
            && float.IsFinite(value.HeightFogContribution);
}
