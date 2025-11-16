// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.ComponentModel.DataAnnotations;
using System.Drawing;
using System.Globalization;

namespace Oxygen.Editor.Data.Validation;

/// <summary>
/// Validation attribute for <see cref="System.Drawing.Point"/> and <see cref="System.Drawing.Size"/>
/// that validates each coordinate (X/Y or Width/Height) against the provided ranges.
/// </summary>
[AttributeUsage(AttributeTargets.Property | AttributeTargets.Field | AttributeTargets.Parameter | AttributeTargets.Method, AllowMultiple = false, Inherited = true)]
public sealed class PointBoundsAttribute : ValidationAttribute
{
    /// <summary>
    /// Initializes a new instance of the <see cref="PointBoundsAttribute"/> class with the specified coordinate bounds.
    /// </summary>
    /// <param name="xMin">Minimum allowed value for the X coordinate.</param>
    /// <param name="xMax">Maximum allowed value for the X coordinate.</param>
    /// <param name="yMin">Minimum allowed value for the Y coordinate.</param>
    /// <param name="yMax">Maximum allowed value for the Y coordinate.</param>
    public PointBoundsAttribute(int xMin = int.MinValue, int xMax = int.MaxValue, int yMin = int.MinValue, int yMax = int.MaxValue)
    {
        this.XMin = xMin;
        this.XMax = xMax;
        this.YMin = yMin;
        this.YMax = yMax;
    }

    /// <summary>Gets minimum allowed X coordinate.</summary>
    public int XMin { get; }

    /// <summary>Gets maximum allowed X coordinate.</summary>
    public int XMax { get; }

    /// <summary>Gets minimum allowed Y coordinate.</summary>
    public int YMin { get; }

    /// <summary>Gets maximum allowed Y coordinate.</summary>
    public int YMax { get; }

    /// <inheritdoc/>
    /// <summary>
    /// Only valid when applied to a <see cref="System.Drawing.Point"/>.
    /// </summary>
    /// <param name="value">The value to evaluate.</param>
    /// <returns>True when the value is null (valid) or when <see cref="System.Drawing.Point"/> coordinates lie within bounds.</returns>
    public override bool IsValid(object? value) => this.IsValid(value, validationContext: null) == ValidationResult.Success;

    /// <summary>
    /// Validates the provided value and returns a <see cref="ValidationResult"/>. Returns failure for null values (enforces presence) and for values not within the configured bounds.
    /// </summary>
    /// <param name="value">Value being validated.</param>
    /// <param name="validationContext">Context for the validation, from which a MemberName may be returned.</param>
    /// <returns>A ValidationResult describing success or failure.</returns>
    protected override ValidationResult? IsValid(object? value, ValidationContext? validationContext)
    {
        var member = validationContext?.MemberName ?? string.Empty;
        var displayName = validationContext?.DisplayName ?? member;

        if (value is null)
        {
            // Allow null; consumers should use [Required] if presence is required
            return ValidationResult.Success;
        }

        if (value is not Point p)
        {
            var invalidMsg = this.ErrorMessage ?? $"{displayName} is not a valid Point.";
            return new ValidationResult(invalidMsg, [member]);
        }

        if (p.X < this.XMin || p.X > this.XMax || p.Y < this.YMin || p.Y > this.YMax)
        {
            var minX = this.XMin.ToString(CultureInfo.InvariantCulture);
            var maxX = this.XMax.ToString(CultureInfo.InvariantCulture);
            var minY = this.YMin.ToString(CultureInfo.InvariantCulture);
            var maxY = this.YMax.ToString(CultureInfo.InvariantCulture);
            var rangeMsg = this.ErrorMessage ?? $"{displayName} coordinates must be in range X:[{minX}..{maxX}], Y:[{minY}..{maxY}].";
            return new ValidationResult(rangeMsg, [member]);
        }

        return ValidationResult.Success;
    }
}
