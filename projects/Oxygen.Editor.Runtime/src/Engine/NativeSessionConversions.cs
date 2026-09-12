// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using Oxygen.Interop;

namespace Oxygen.Editor.Runtime.Engine;

/// <summary>Converts managed runtime contracts after the native session boundary has been entered.</summary>
internal static class NativeSessionConversions
{
    /// <summary>Maps supported options by their named engine meaning.</summary>
    /// <typeparam name="TNative">The native public enum.</typeparam>
    /// <param name="value">The managed runtime option.</param>
    /// <returns>The corresponding native option.</returns>
    public static TNative ToNative<TNative>(Enum value)
        where TNative : struct, Enum
        => Enum.IsDefined(value.GetType(), value) && Enum.TryParse<TNative>(value.ToString(), ignoreCase: false, out var native) && Enum.IsDefined(native)
            ? native : throw new ArgumentOutOfRangeException(nameof(value), value, "The runtime option is not supported by the native contract.");

    /// <summary>Creates the native view config while preserving omitted native defaults.</summary>
    /// <param name="config">The editor's managed view request.</param>
    /// <returns>The native configuration.</returns>
    public static ViewConfigManaged ToNative(RuntimeViewConfig config)
    {
        var native = new ViewConfigManaged { Name = config.Name, Purpose = config.Purpose, CompositingTarget = config.CompositingTarget };
        if (config.Width is { } width)
        {
            native.Width = width;
        }

        if (config.Height is { } height)
        {
            native.Height = height;
        }

        if (config.ClearColor is { } color)
        {
            native.ClearColor = new(color.R, color.G, color.B, color.A);
        }

        return native;
    }
}
