// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using Oxygen.Editor.Schemas.Bindings;

namespace Oxygen.Editor.World.Inspector.Editing;

/// <summary>
/// Forwards control values to a binding only when they change the authored value.
/// </summary>
/// <remarks>
/// <see cref="PropertyBinding{T}.Value"/> always raises change and request events. Two-way
/// selection and toggle controls echo the value they were just given, so binding them straight to
/// the binding re-enters forever; routing them through this guard ends the echo.
/// </remarks>
internal static class InspectorBindingRequests
{
    /// <summary>Requests a value unless it already is the binding's unmixed value.</summary>
    /// <typeparam name="T">The authored value type.</typeparam>
    /// <param name="binding">The binding that owns the value.</param>
    /// <param name="value">The control's value.</param>
    public static void Request<T>(PropertyBinding<T> binding, T value)
    {
        if (binding.IsMixed || !EqualityComparer<T>.Default.Equals(binding.Value, value))
        {
            binding.Value = value;
        }
    }
}
