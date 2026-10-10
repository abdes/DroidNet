// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using Oxygen.Editor.Runtime.Engine;

namespace Oxygen.Editor.World.Workspace;

/// <summary>The ranges the engine accepts for each ground grid setting.</summary>
internal static class GroundGridRanges
{
    /// <summary>The smallest grid spacing, in world units.</summary>
    public const float MinSpacing = 1e-4f;

    /// <summary>The shortest smooth motion time, in seconds.</summary>
    public const float MinSmoothTime = 1e-3f;

    private static readonly GroundGridSettings Defaults = new();

    /// <summary>Clamps an edit into range; a value that is not a number keeps the current one.</summary>
    /// <param name="requested">The edited settings.</param>
    /// <param name="current">The settings in effect.</param>
    /// <returns>The settings to apply.</returns>
    public static GroundGridSettings Clamp(GroundGridSettings requested, GroundGridSettings current) => requested with
    {
        Spacing = Clamp(requested.Spacing, MinSpacing, current.Spacing),
        MajorEvery = Math.Max(requested.MajorEvery, 1),
        LineThickness = Clamp(requested.LineThickness, 0f, current.LineThickness),
        MajorThickness = Clamp(requested.MajorThickness, 0f, current.MajorThickness),
        AxisThickness = Clamp(requested.AxisThickness, 0f, current.AxisThickness),
        FadeStart = Clamp(requested.FadeStart, 0f, current.FadeStart),
        FadePower = Clamp(requested.FadePower, 0f, current.FadePower),
        HorizonBoost = Clamp(requested.HorizonBoost, 0f, current.HorizonBoost),
        OriginX = float.IsFinite(requested.OriginX) ? requested.OriginX : current.OriginX,
        OriginY = float.IsFinite(requested.OriginY) ? requested.OriginY : current.OriginY,
        SmoothTime = Clamp(requested.SmoothTime, MinSmoothTime, current.SmoothTime),
        MinorColor = Clamp(requested.MinorColor, current.MinorColor),
        MajorColor = Clamp(requested.MajorColor, current.MajorColor),
        AxisColorX = Clamp(requested.AxisColorX, current.AxisColorX),
        AxisColorY = Clamp(requested.AxisColorY, current.AxisColorY),
        OriginColor = Clamp(requested.OriginColor, current.OriginColor),
    };

    /// <summary>Restores stored settings; a stored value outside its range restores its default.</summary>
    /// <param name="stored">The settings read from the user's preferences.</param>
    /// <returns>The settings to apply.</returns>
    public static GroundGridSettings Restore(GroundGridSettings stored) => stored with
    {
        Spacing = Restore(stored.Spacing, MinSpacing, Defaults.Spacing),
        MajorEvery = stored.MajorEvery >= 1 ? stored.MajorEvery : Defaults.MajorEvery,
        LineThickness = Restore(stored.LineThickness, 0f, Defaults.LineThickness),
        MajorThickness = Restore(stored.MajorThickness, 0f, Defaults.MajorThickness),
        AxisThickness = Restore(stored.AxisThickness, 0f, Defaults.AxisThickness),
        FadeStart = Restore(stored.FadeStart, 0f, Defaults.FadeStart),
        FadePower = Restore(stored.FadePower, 0f, Defaults.FadePower),
        HorizonBoost = Restore(stored.HorizonBoost, 0f, Defaults.HorizonBoost),
        OriginX = float.IsFinite(stored.OriginX) ? stored.OriginX : Defaults.OriginX,
        OriginY = float.IsFinite(stored.OriginY) ? stored.OriginY : Defaults.OriginY,
        SmoothTime = Restore(stored.SmoothTime, MinSmoothTime, Defaults.SmoothTime),
        MinorColor = IsValid(stored.MinorColor) ? stored.MinorColor : Defaults.MinorColor,
        MajorColor = IsValid(stored.MajorColor) ? stored.MajorColor : Defaults.MajorColor,
        AxisColorX = IsValid(stored.AxisColorX) ? stored.AxisColorX : Defaults.AxisColorX,
        AxisColorY = IsValid(stored.AxisColorY) ? stored.AxisColorY : Defaults.AxisColorY,
        OriginColor = IsValid(stored.OriginColor) ? stored.OriginColor : Defaults.OriginColor,
    };

    private static float Clamp(float value, float min, float fallback)
        => float.IsFinite(value) ? Math.Max(value, min) : fallback;

    private static float Restore(float value, float min, float fallback)
        => float.IsFinite(value) && value >= min ? value : fallback;

    private static RuntimeColor Clamp(RuntimeColor value, RuntimeColor fallback)
        => IsValid(value) ? value : fallback;

    private static bool IsValid(RuntimeColor color)
        => IsChannel(color.R) && IsChannel(color.G) && IsChannel(color.B) && IsChannel(color.A);

    private static bool IsChannel(float value) => float.IsFinite(value) && value is >= 0f and <= 1f;
}
