// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.ComponentModel.DataAnnotations;
using Oxygen.Editor.Data.Settings;

namespace Oxygen.Editor.Data;

/// <summary>
/// Validation helper methods for <see cref="EditorSettingsManager"/>.
/// </summary>
public partial class EditorSettingsManager
{
    /// <summary>
    /// Validates a value using the validators from a descriptor.
    /// </summary>
    /// <typeparam name="T">The type of the value.</typeparam>
    /// <param name="descriptor">The setting descriptor containing validators.</param>
    /// <param name="value">The value to validate.</param>
    /// <exception cref="SettingsValidationException">Thrown when validation fails.</exception>
    private static void ValidateSetting<T>(SettingDescriptor<T> descriptor, T value)
    {
        if (descriptor.Validators is not { Count: > 0 })
        {
            return;
        }

        var validationResults = new List<ValidationResult>();
        var validationContext = new ValidationContext(descriptor.Key) { MemberName = descriptor.Key.Name };

        foreach (var validator in descriptor.Validators)
        {
            var result = validator.GetValidationResult(value, validationContext);
            if (result != ValidationResult.Success)
            {
                validationResults.Add(result!);
            }
        }

        if (validationResults.Count > 0)
        {
            throw new SettingsValidationException("Validation failed for setting.", validationResults);
        }
    }

    /// <summary>
    /// Validates all items in a batch that have descriptors.
    /// </summary>
    /// <param name="items">The batch items to validate.</param>
    /// <exception cref="SettingsValidationException">Thrown when any item fails validation.</exception>
    private static void ValidateBatchItems(IReadOnlyList<BatchItem> items)
    {
        var allErrors = new List<ValidationResult>();

        foreach (var item in items)
        {
            if (item.Descriptor is null || item.Value is null)
            {
                continue;
            }

            var errors = ValidateWithDescriptor(item.Descriptor, item.Value);
            allErrors.AddRange(errors);
        }

        if (allErrors.Count > 0)
        {
            throw new SettingsValidationException("Batch validation failed.", allErrors);
        }
    }

    /// <summary>
    /// Validates a value against a descriptor's validators.
    /// </summary>
    /// <param name="descriptor">The descriptor with validation rules.</param>
    /// <param name="value">The value to validate.</param>
    /// <returns>A list of validation results (empty if valid).</returns>
    private static List<ValidationResult> ValidateWithDescriptor(ISettingDescriptor descriptor, object value)
    {
        var type = descriptor.GetType();
        if (!type.IsGenericType || type.GetGenericTypeDefinition() != typeof(SettingDescriptor<>))
        {
            return [];
        }

        if (type.GetProperty(nameof(SettingDescriptor<>.Validators))?.GetValue(descriptor) is not System.Collections.IEnumerable validators)
        {
            return [];
        }

        var results = new List<ValidationResult>();
        var key = type.GetProperty(nameof(SettingDescriptor<object>.Key))?.GetValue(descriptor);
        var validationContext = new ValidationContext(key ?? descriptor) { MemberName = descriptor.Name };

        foreach (ValidationAttribute validator in validators)
        {
            var result = validator.GetValidationResult(value, validationContext);
            if (result != ValidationResult.Success)
            {
                results.Add(result!);
            }
        }

        return results;
    }
}
