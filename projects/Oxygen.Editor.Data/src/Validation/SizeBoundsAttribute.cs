// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.ComponentModel.DataAnnotations;
using System.Globalization;

namespace Oxygen.Editor.Data.Validation;

/// <summary>
/// Validation attribute for <see cref="System.Drawing.Size"/>.
/// Validates width and height against provided ranges.
/// </summary>
[AttributeUsage(AttributeTargets.Property | AttributeTargets.Field | AttributeTargets.Parameter | AttributeTargets.Method, AllowMultiple = false, Inherited = true)]
public sealed class SizeBoundsAttribute : ValidationAttribute
{
    /// <summary>
    /// Initializes a new instance of the <see cref="SizeBoundsAttribute"/> class.
    /// </summary>
    /// <param name="widthMin">Minimum allowed width.</param>
    /// <param name="widthMax">Maximum allowed width.</param>
    /// <param name="heightMin">Minimum allowed height.</param>
    /// <param name="heightMax">Maximum allowed height.</param>
    public SizeBoundsAttribute(int widthMin = int.MinValue, int widthMax = int.MaxValue, int heightMin = int.MinValue, int heightMax = int.MaxValue)
    {
        this.WidthMin = widthMin;
        this.WidthMax = widthMax;
        this.HeightMin = heightMin;
        this.HeightMax = heightMax;
    }

    /// <summary>Gets minimum allowed width.</summary>
    public int WidthMin { get; }

    /// <summary>Gets maximum allowed width.</summary>
    public int WidthMax { get; }

    /// <summary>Gets minimum allowed height.</summary>
    public int HeightMin { get; }

    /// <summary>Gets maximum allowed height.</summary>
    public int HeightMax { get; }

    /// <inheritdoc/>
    public override bool IsValid(object? value) => this.IsValid(value, validationContext: null) == ValidationResult.Success;

    /// <inheritdoc/>
    protected override ValidationResult? IsValid(object? value, ValidationContext? validationContext)
    {
        var member = validationContext?.MemberName ?? string.Empty;
        var displayName = validationContext?.DisplayName ?? member;

        if (value is null)
        {
            // Allow null; consumers should use [Required] if presence is required
            return ValidationResult.Success;
        }

        if (value is not System.Drawing.Size s)
        {
            var invalidMsg = this.ErrorMessage ?? $"{displayName} is not a valid Size.";
            return new ValidationResult(invalidMsg, [member]);
        }

        if (s.Width < this.WidthMin || s.Width > this.WidthMax || s.Height < this.HeightMin || s.Height > this.HeightMax)
        {
            var wMin = this.WidthMin.ToString(CultureInfo.InvariantCulture);
            var wMax = this.WidthMax.ToString(CultureInfo.InvariantCulture);
            var hMin = this.HeightMin.ToString(CultureInfo.InvariantCulture);
            var hMax = this.HeightMax.ToString(CultureInfo.InvariantCulture);
            var rangeMsg = this.ErrorMessage ?? $"{displayName} width/height must be in range W:[{wMin}..{wMax}], H:[{hMin}..{hMax}]";
            return new ValidationResult(rangeMsg, [member]);
        }

        return ValidationResult.Success;
    }
}
