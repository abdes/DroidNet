// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using Oxygen.Managed.Core;

namespace Oxygen.Editor.World.Components;

/// <summary>A light rule the values of one light break together.</summary>
/// <param name="Rule">The broken rule.</param>
/// <param name="Properties">The properties the rule relates, of the light's kind.</param>
public sealed record LightRuleViolation(LightRule Rule, IReadOnlyList<PropertyId> Properties)
{
    /// <summary>Gets the reason, the one description of the rule.</summary>
    public string Message => this.Rule switch
    {
        LightRule.CascadeDistancesNotIncreasing => "Active cascade distances must be increasing.",
        LightRule.ExtentNotRepresentable => "Local range and radius are not representable together.",
        LightRule.InverseRangeNotRepresentable => "Inverse light range is not representable.",
        LightRule.SpotConeInvalid => "The inner cone must not be wider than the outer cone, the outer cone must be open, and both cannot be a right angle.",
        LightRule.SpotConeNotRepresentable => "Spot cone is not representable by the renderer.",
        LightRule.IntensityNotRepresentable => "Resolved light intensity is not representable.",
        LightRule.DiskRadianceNotRepresentable => "Resolved disk radiance is not representable.",
        _ => throw new InvalidOperationException($"{this.Rule} is not a light rule."),
    };
}
