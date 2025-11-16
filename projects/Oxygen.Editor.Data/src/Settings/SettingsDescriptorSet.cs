// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.ComponentModel;
using System.ComponentModel.DataAnnotations;
using System.Reflection;

namespace Oxygen.Editor.Data.Settings;

/// <summary>
/// Base class for settings descriptor collections. Derived classes can declare static properties
/// using <see cref="CreateDescriptor{T}"/> to expose setting descriptors for UI and validation.
/// </summary>
public abstract class SettingsDescriptorSet
{
    /// <summary>
    /// Helper to create a typed <see cref="SettingDescriptor{T}"/> for a module/property.
    /// </summary>
    /// <typeparam name="T">The setting value type.</typeparam>
    /// <param name="module">The module name the setting belongs to.</param>
    /// <param name="name">The setting key name.</param>
    /// <param name="propertyName">The caller member name used to locate the declaring property (optional).</param>
    /// <returns>A new <see cref="SettingDescriptor{T}"/> instance describing the setting.</returns>
    protected static SettingDescriptor<T> CreateDescriptor<T>(string module, string name, [System.Runtime.CompilerServices.CallerMemberName] string? propertyName = null)
    {
        string? displayName = null;
        string? description = null;
        string? category = null;
        var validators = new List<ValidationAttribute>();

        if (!string.IsNullOrEmpty(propertyName) && TryFindAttributes(propertyName, out var d, out var c, out var v))
        {
            displayName = d?.Name;
            description = d?.Description;
            category = c?.Category;
            validators = v;
        }

        // completed lookup
        return new SettingDescriptor<T>
        {
            Key = new SettingKey<T>(module, name),
            DisplayName = displayName,
            Description = description,
            Category = category,
            Validators = validators,
        };

        // Search types that derive from SettingsDescriptorSet to find the declaring property
        static bool TryFindAttributes(string propName, out DisplayAttribute? displayAttr, out CategoryAttribute? catAttr, out List<ValidationAttribute> foundValidators)
        {
            displayAttr = null;
            catAttr = null;
            foundValidators = [];

            var assemblies = AppDomain.CurrentDomain.GetAssemblies();
            foreach (var asm in assemblies)
            {
                Type[] types;
                try
                {
                    types = asm.GetTypes();
                }
                catch (ReflectionTypeLoadException ex)
                {
                    types = ex.Types.Where(t => t is not null).ToArray()!;
                }

                foreach (var type in types)
                {
                    if (type is null || !type.IsSubclassOf(typeof(SettingsDescriptorSet)))
                    {
                        continue;
                    }

                    var prop = type.GetProperty(propName, BindingFlags.Public | BindingFlags.Static);
                    if (prop is null)
                    {
                        continue;
                    }

                    var genericDef = prop.PropertyType.IsGenericType ? prop.PropertyType.GetGenericTypeDefinition() : null;
                    if (genericDef != typeof(SettingDescriptor<>))
                    {
                        continue;
                    }

                    displayAttr = prop.GetCustomAttribute<DisplayAttribute>();
                    catAttr = prop.GetCustomAttribute<CategoryAttribute>();
                    foundValidators = prop.GetCustomAttributes<ValidationAttribute>().ToList();
                    return true;
                }
            }

            return false;
        }
    }
}
