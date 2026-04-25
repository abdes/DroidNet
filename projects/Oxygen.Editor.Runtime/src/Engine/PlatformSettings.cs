// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

namespace Oxygen.Editor.Runtime.Engine;

/// <summary>
///     Platform settings for native engine startup.
/// </summary>
public sealed class PlatformSettings
{
    /// <summary>
    ///     Gets or sets a value indicating whether the platform runs without native windows.
    /// </summary>
    public bool? Headless { get; set; }

    /// <summary>
    ///     Gets or sets the native platform thread pool size.
    /// </summary>
    public uint? ThreadPoolSize { get; set; }
}
