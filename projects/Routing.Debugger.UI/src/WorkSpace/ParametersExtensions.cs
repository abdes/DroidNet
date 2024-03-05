// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

namespace DroidNet.Routing.Debugger.UI.WorkSpace;

using DroidNet.Docking;

internal static class ParametersExtensions
{
    internal static bool FlagIsSet(this IParameters parameters, string name)
    {
        if (!parameters.TryGetValue(name, out var value))
        {
            return false;
        }

        return value is null || bool.Parse(value);
    }

    internal static void SetFlag(this Parameters parameters, string name, bool value)
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

    internal static bool ParameterHasValue(this IParameters parameters, string name, string? value)
        => parameters.TryGetValue(name, out var parameterValue) &&
           parameterValue == value;

    internal static Width WidthOrDefault(this IParameters parameters)
        => parameters.TryGetValue("w", out var value) ? new Width(value) : new Width();

    internal static Height HeightOrDefault(this IParameters parameters)
        => parameters.TryGetValue("h", out var value) ? new Height(value!) : new Height();

    internal static (AnchorPosition anchorPosition, string? relativeDockableId) AnchorInfoOrDefault(
        this IParameters parameters)
    {
        foreach (var anchor in Enum.GetNames<AnchorPosition>())
        {
            if (!parameters.Contains(anchor))
            {
                continue;
            }

            // Check that no other anchor position is specified in the parameters
            foreach (var other in Enum.GetNames<AnchorPosition>().Where(n => n != anchor))
            {
                if (parameters.Contains(other))
                {
                    throw new InvalidOperationException(
                        $"you can only specify an anchor position for a dockable once. We first found `{anchor}`, then `{other}`");
                }
            }

            var anchorPosition = Enum.Parse<AnchorPosition>(anchor);
            _ = parameters.TryGetValue(anchor, out var relativeDockableId);

            if (anchorPosition == AnchorPosition.With && relativeDockableId is null)
            {
                throw new ArgumentException(
                    "you must specify the relative dockable ID when you use 'with'",
                    nameof(parameters));
            }

            return (anchorPosition, relativeDockableId);
        }

        // return default: left
        return (AnchorPosition.Left, null);
    }
}
