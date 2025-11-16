// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Reflection;
using Oxygen.Editor.Data.Settings;

namespace Oxygen.Editor.Data.Models;

/// <summary>
/// Extension methods for <see cref="ModuleSettings"/> providing persistence operations.
/// </summary>
public static class ModuleSettingsExtensions
{
    /// <summary>
    /// Saves all modified properties of the module settings to the settings manager.
    /// </summary>
    /// <param name="settings">The module settings instance.</param>
    /// <param name="manager">The settings manager to persist to.</param>
    /// <param name="ct">Cancellation token.</param>
    /// <returns>A task that completes when all modified settings are saved.</returns>
    /// <exception cref="InvalidOperationException">Thrown when a property cannot be saved.</exception>
    public static async Task SaveAsync(
        this ModuleSettings settings,
        IEditorSettingsManager manager,
        CancellationToken ct = default)
    {
        settings.OnSavingInternal();

        var modifiedProperties = settings.GetModifiedPropertiesInternal();
        foreach (var propertyName in modifiedProperties)
        {
            var propertyInfo = settings.GetType().GetProperty(propertyName);
            if (propertyInfo is null)
            {
                continue;
            }

            // Only persist properties that have a descriptor registered for them
            var descriptorsByCategory = await manager.GetDescriptorsByCategoryAsync(ct).ConfigureAwait(false);
            var descriptor = descriptorsByCategory.Values
                .SelectMany(v => v)
                .FirstOrDefault(d => string.Equals(d.Name, propertyName, StringComparison.Ordinal));

            if (descriptor == null)
            {
                // No descriptor => do not persist
                continue;
            }

            try
            {
                await SavePropertyAsync(settings, manager, propertyInfo, descriptor, ct).ConfigureAwait(false);
            }
            catch (Exception ex) when (ex is ArgumentException or TargetException or MethodAccessException or TargetInvocationException)
            {
                throw new InvalidOperationException($"Failed to save property '{propertyName}'", ex);
            }
        }

        settings.ClearModifiedPropertiesInternal();
        settings.SetDirtyInternal(false);
    }

    /// <summary>
    /// Loads all properties with registered descriptors from the settings manager.
    /// </summary>
    /// <param name="settings">The module settings instance.</param>
    /// <param name="manager">The settings manager to load from.</param>
    /// <param name="ct">Cancellation token.</param>
    /// <returns>A task that completes when all settings are loaded.</returns>
    /// <exception cref="InvalidOperationException">Thrown when a property cannot be loaded.</exception>
    public static async Task LoadAsync(
        this ModuleSettings settings,
        IEditorSettingsManager manager,
        CancellationToken ct = default)
    {
        settings.SetIsLoadedInternal(false);

        foreach (var property in settings.GetType().GetProperties())
        {
            // Descriptor-first: check if a descriptor exists for this property by name
            var descriptorsByCategory = await manager.GetDescriptorsByCategoryAsync(ct).ConfigureAwait(false);
            var descriptor = descriptorsByCategory.Values
                .SelectMany(v => v)
                .FirstOrDefault(d => string.Equals(d.Name, property.Name, StringComparison.Ordinal));

            if (descriptor == null)
            {
                // No descriptor, do not load
                continue;
            }

            try
            {
                var defaultValue = property.GetValue(settings);

                // Find the LoadSettingAsync<T>(SettingKey<T>, T defaultValue, SettingContext?, CancellationToken) method
                var matching = typeof(IEditorSettingsManager)
                    .GetMethods()
                    .FirstOrDefault(m =>
                        string.Equals(m.Name, nameof(IEditorSettingsManager.LoadSettingAsync), StringComparison.Ordinal) &&
                        m.IsGenericMethod &&
                        m.GetParameters().Length == 4);

                if (matching != null)
                {
                    // Use the overload that accepts a typed SettingKey and a default value
                    var genericMethod = matching.MakeGenericMethod(property.PropertyType);
                    var keyType = typeof(SettingKey<>).MakeGenericType(property.PropertyType);
                    var key = Activator.CreateInstance(keyType, settings.ModuleName, property.Name) ??
                              throw new InvalidOperationException("Failed to construct SettingKey");

                    // Call LoadSettingAsync<T>(SettingKey<T>, T defaultValue, SettingContext?, CancellationToken)
                    var task = (Task)genericMethod.Invoke(manager, [key, defaultValue, null, ct])!;
                    await task.ConfigureAwait(false);

                    var resultProperty = task.GetType().GetProperty("Result");
                    if (resultProperty != null)
                    {
                        var valueLoaded = resultProperty.GetValue(task);
                        property.SetValue(settings, valueLoaded);
                    }
                }
            }
            catch (Exception ex) when (ex is ArgumentException or TargetException or MethodAccessException or TargetInvocationException)
            {
                throw new InvalidOperationException($"Failed to load property '{property.Name}'", ex);
            }
        }

        settings.OnLoadedInternal();

        settings.SetDirtyInternal(false);
        settings.SetIsLoadedInternal(true);
    }

    private static async Task SavePropertyAsync(
        ModuleSettings settings,
        IEditorSettingsManager manager,
        PropertyInfo propertyInfo,
        ISettingDescriptor descriptor,
        CancellationToken ct)
    {
        var value = propertyInfo.GetValue(settings);
        if (value == null)
        {
            return;
        }

        // Build typed SettingKey<T> using instance ModuleName and property name for persistence
        var keyType = typeof(SettingKey<>).MakeGenericType(propertyInfo.PropertyType);
        var key = Activator.CreateInstance(keyType, settings.ModuleName, propertyInfo.Name) ??
                  throw new InvalidOperationException("Failed to construct SettingKey");

        // Validate using descriptor validators
        var validatorsObj = descriptor.GetType().GetProperty(nameof(SettingDescriptor<object>.Validators))?.GetValue(descriptor);
        var validationResults = new List<System.ComponentModel.DataAnnotations.ValidationResult>();
        if (validatorsObj is System.Collections.IEnumerable validators)
        {
            foreach (var v in validators)
            {
                if (v is System.ComponentModel.DataAnnotations.ValidationAttribute va)
                {
                    var validationContext = new System.ComponentModel.DataAnnotations.ValidationContext(value)
                    {
                        DisplayName = descriptor.DisplayName ?? propertyInfo.Name,
                    };
                    var result = va.GetValidationResult(value, validationContext);
                    if (result != System.ComponentModel.DataAnnotations.ValidationResult.Success && result != null)
                    {
                        validationResults.Add(result);
                    }
                }
            }
        }

        if (validationResults.Count > 0)
        {
            throw new SettingsValidationException(propertyInfo.Name, validationResults);
        }

        // Now call typed SaveSettingAsync with the constructed key
        await InvokeSaveMethod(manager, propertyInfo.PropertyType, key, value, ct).ConfigureAwait(false);
    }

    private static Task InvokeSaveMethod(
        IEditorSettingsManager manager,
        Type propertyType,
        object key,
        object? value,
        CancellationToken ct)
    {
        var method = typeof(IEditorSettingsManager).GetMethods()
            .Where(m => string.Equals(m.Name, nameof(IEditorSettingsManager.SaveSettingAsync), StringComparison.Ordinal) && m.IsGenericMethod)
            .First(m => m.GetParameters().Length == 4);

        var generic = method.MakeGenericMethod(propertyType);
        var task = (Task)generic.Invoke(manager, [key, value, null, ct])!;
        return task;
    }
}
