// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Collections.Immutable;
using Oxygen.Interop.Input;
using Oxygen.Interop.World;

namespace Oxygen.Editor.Runtime.Engine;

/// <summary>Pure conversions at the mixed-mode transport boundary.</summary>
internal static class RuntimeTransportConversion
{
    /// <summary>Converts a managed payload without changing its identity or units.</summary>
    /// <param name="entries">The managed payload.</param>
    /// <returns>The native transport value.</returns>
    public static PropertyValueEntry[] ToNative(ImmutableArray<RuntimePropertyValue> entries)
        => [.. entries.Select(value => new PropertyValueEntry { ComponentId = value.ComponentId, FieldId = value.FieldId, Value = value.Value })];

    /// <summary>Converts a managed payload without changing its identity or units.</summary>
    /// <param name="value">The managed payload.</param>
    /// <returns>The native transport value.</returns>
    public static EditorKeyEventManaged ToNative(RuntimeKeyEvent value)
        => new() { key = ToNative(value.Key), pressed = value.Pressed, repeat = value.Repeat, position = value.Position, timestamp = value.Timestamp };

    /// <summary>Converts a managed payload without changing its identity or units.</summary>
    /// <param name="value">The managed payload.</param>
    /// <returns>The native transport value.</returns>
    public static EditorButtonEventManaged ToNative(RuntimeButtonEvent value)
        => new() { button = ToNative(value.Button), pressed = value.Pressed, position = value.Position, timestamp = value.Timestamp };

    /// <summary>Converts a managed payload without changing its identity or units.</summary>
    /// <param name="value">The managed payload.</param>
    /// <returns>The native transport value.</returns>
    public static EditorMouseMotionEventManaged ToNative(RuntimeMouseMotionEvent value)
        => new() { motion = value.Motion, position = value.Position, timestamp = value.Timestamp };

    /// <summary>Converts a managed payload without changing its identity or units.</summary>
    /// <param name="value">The managed payload.</param>
    /// <returns>The native transport value.</returns>
    public static EditorMouseWheelEventManaged ToNative(RuntimeMouseWheelEvent value)
        => new() { scroll = value.Scroll, position = value.Position, timestamp = value.Timestamp };

    /// <summary>Converts a managed payload without changing its identity or units.</summary>
    /// <param name="value">The managed payload.</param>
    /// <returns>The native transport value.</returns>
    public static PlatformKey ToNative(RuntimeKey value)
    {
        if (!Enum.IsDefined(value))
        {
            throw new ArgumentOutOfRangeException(nameof(value));
        }

        // RuntimeKey deliberately fixes the native key ordinals. Boundary tests
        // compare every named value, including modifiers, to detect contract drift.
        return (PlatformKey)value;
    }

    /// <summary>Converts a managed payload without changing its identity or units.</summary>
    /// <param name="value">The managed payload.</param>
    /// <returns>The native transport value.</returns>
    public static PlatformMouseButton ToNative(RuntimeMouseButton value) => value switch
    {
        RuntimeMouseButton.None => PlatformMouseButton.None,
        RuntimeMouseButton.Left => PlatformMouseButton.Left,
        RuntimeMouseButton.Right => PlatformMouseButton.Right,
        RuntimeMouseButton.Middle => PlatformMouseButton.Middle,
        RuntimeMouseButton.ExtButton1 => PlatformMouseButton.ExtButton1,
        RuntimeMouseButton.ExtButton2 => PlatformMouseButton.ExtButton2,
        _ => throw new ArgumentOutOfRangeException(nameof(value)),
    };
}
