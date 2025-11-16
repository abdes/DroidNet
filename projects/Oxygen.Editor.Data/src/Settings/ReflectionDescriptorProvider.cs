// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Reflection;

namespace Oxygen.Editor.Data.Settings;

/// <summary>
/// Provides setting descriptors by scanning assemblies for <see cref="SettingsDescriptorSet"/> subclasses via reflection.
/// This is an opt-in fallback provider for scenarios without source-generated registration.
/// </summary>
/// <remarks>
/// This provider is expensive and should only be used during development or for dynamic plugin scenarios.
/// </remarks>
public sealed class ReflectionDescriptorProvider : IDescriptorProvider
{
    /// <inheritdoc/>
    public IEnumerable<ISettingDescriptor> EnumerateDescriptors()
    {
        var assemblies = AppDomain.CurrentDomain.GetAssemblies();
        foreach (var assembly in assemblies)
        {
            Type[] types;
            try
            {
                types = assembly.GetTypes();
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

                var properties = type.GetProperties(BindingFlags.Public | BindingFlags.Static);
                foreach (var property in properties)
                {
                    if (!property.PropertyType.IsGenericType)
                    {
                        continue;
                    }

                    var genericDef = property.PropertyType.GetGenericTypeDefinition();
                    if (genericDef != typeof(SettingDescriptor<>))
                    {
                        continue;
                    }

                    var value = property.GetValue(null);
                    if (value is ISettingDescriptor descriptor)
                    {
                        yield return descriptor;
                    }
                }
            }
        }
    }
}
