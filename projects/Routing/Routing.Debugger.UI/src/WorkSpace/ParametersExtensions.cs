// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using DroidNet.Docking;

namespace DroidNet.Routing.Debugger.UI.WorkSpace;

/// <summary>
/// Extension methods for <see cref="IParameters"/>.
/// </summary>
internal static class ParametersExtensions
{
    /// <summary>
    /// Checks if a flag is set in the parameters.
    /// </summary>
    /// <param name="parameters">The parameters to check.</param>
    /// <param name="name">The name of the flag.</param>
    /// <returns><see langword="true"/> if the flag is set; otherwise, <see langword="false"/>.</returns>
    internal static bool FlagIsSet(this IParameters parameters, string name) => parameters.TryGetValue(name, out var value) && (value is null || bool.Parse(value));

    /// <summary>
    /// Sets or unsets a flag in the parameters.
    /// </summary>
    /// <param name="parameters">The parameters to modify.</param>
    /// <param name="name">The name of the flag.</param>
    /// <param name="value">The value to set. If <see langword="true"/>, the flag is set; otherwise, it is removed.</param>
    internal static void SetFlag(this Parameters parameters, string name, bool value)
    {
        if (value)
        {
            parameters.AddOrUpdate(name, value: null);
        }
        else
        {
            _ = parameters.Remove(name);
        }
    }

    /// <summary>
    /// Checks if a parameter has a specific value.
    /// </summary>
    /// <param name="parameters">The parameters to check.</param>
    /// <param name="name">The name of the parameter.</param>
    /// <param name="value">The value to check against.</param>
    /// <returns><see langword="true"/> if the parameter has the specified value; otherwise, <see langword="false"/>.</returns>
    internal static bool ParameterHasValue(this IParameters parameters, string name, string? value)
        => parameters.TryGetValue(name, out var parameterValue) &&
           string.Equals(parameterValue, value, StringComparison.Ordinal);

    /// <summary>
    /// Gets the width from the parameters or returns a default width.
    /// </summary>
    /// <param name="parameters">The parameters to check.</param>
    /// <returns>A <see cref="Width"/> instance representing the width.</returns>
    internal static Width WidthOrDefault(this IParameters parameters)
        => parameters.TryGetValue("w", out var value) ? new Width(value) : new Width();

    /// <summary>
    /// Gets the height from the parameters or returns a default height.
    /// </summary>
    /// <param name="parameters">The parameters to check.</param>
    /// <returns>A <see cref="Height"/> instance representing the height.</returns>
    internal static Height HeightOrDefault(this IParameters parameters)
        => parameters.TryGetValue("h", out var value) ? new Height(value) : new Height();

    /// <summary>
    /// Gets the anchor information from the parameters or returns default values.
    /// </summary>
    /// <param name="parameters">The parameters to check.</param>
    /// <returns>A tuple containing the <see cref="AnchorPosition"/> and the relative dockable ID.</returns>
    /// <exception cref="InvalidOperationException">Thrown if multiple anchor positions are specified.</exception>
    /// <exception cref="ArgumentException">Thrown if the 'with' anchor position is used without a relative dockable ID.</exception>
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
            foreach (var other in Enum.GetNames<AnchorPosition>()
                         .Where(n => !string.Equals(n, anchor, StringComparison.OrdinalIgnoreCase)))
            {
                if (parameters.Contains(other))
                {
                    throw new InvalidOperationException(
                        $"you can only specify an anchor position for a dockable once. We first found `{anchor}`, then `{other}`");
                }
            }

            var anchorPosition = Enum.Parse<AnchorPosition>(anchor);
            _ = parameters.TryGetValue(anchor, out var relativeDockableId);

            return anchorPosition == AnchorPosition.With && relativeDockableId is null
                ? throw new ArgumentException(
                    "you must specify the relative dockable ID when you use 'with'",
                    nameof(parameters))
                : (anchorPosition, relativeDockableId);
        }

        // return default: left
        return (AnchorPosition.Left, null);
    }
}
