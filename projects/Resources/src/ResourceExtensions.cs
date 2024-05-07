// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

namespace DroidNet.Resources;

using System.Diagnostics;
using System.Reflection;
using Microsoft.Windows.ApplicationModel.Resources;

public static class ResourceExtensions
{
    private static readonly ResourceMap Strings = new ResourceManager().MainResourceMap;

    public static string GetLocalized(this string value)
    {
        var localized = Strings.TryGetValue(value);
        return localized != null ? localized.ValueAsString : value;
    }

    public static string GetLocalizedMine(this string value)
    {
        var assemblyName = Assembly.GetCallingAssembly()
            .GetName()
            .Name;
        var subMap = Strings.GetSubtree($"{Assembly.GetCallingAssembly().GetName().Name}/Strings");
        if (subMap == null)
        {
            Debug.WriteLine($"Resource map for assembly `{assemblyName}` does not exist.");
            return value;
        }

        var localized = subMap.TryGetValue(value);
        return localized != null ? localized.ValueAsString : value;
    }
}
