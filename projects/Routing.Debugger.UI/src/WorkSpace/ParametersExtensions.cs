// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

namespace DroidNet.Routing.Debugger.UI.WorkSpace;

internal static class ParametersExtensions
{
    public static bool FlagIsSet(this IParameters parameters, string name)
    {
        if (!parameters.TryGetValue(name, out var value))
        {
            return false;
        }

        return value is null || bool.Parse(value);
    }

    public static void SetFlag(this Parameters parameters, string name, bool value)
    {
        if (value)
        {
            parameters.AddOrUpdate(name, null);
        }
        else
        {
            _ = parameters.Remove(name);
        }
    }

    public static bool ParameterHasValue(this IParameters parameters, string name, string? value)
        => parameters.TryGetValue(name, out var parameterValue) &&
           parameterValue == value;
}
