// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

namespace Oxygen.Editor.World.Serialization;

/// <summary>
/// Scene exposure mode for environment authoring.
/// </summary>
public enum ExposureMode
{
    /// <summary>
    /// Automatic exposure.
    /// </summary>
    Auto,

    /// <summary>
    /// Manual exposure.
    /// </summary>
    Manual,
}

/// <summary>
/// Scene tone mapping mode for environment authoring.
/// </summary>
public enum ToneMappingMode
{
    /// <summary>
    /// No tone mapping.
    /// </summary>
    None,

    /// <summary>
    /// ACES tone mapping.
    /// </summary>
    Aces,

    /// <summary>
    /// Reinhard tone mapping.
    /// </summary>
    Reinhard,

    /// <summary>
    /// Filmic tone mapping.
    /// </summary>
    Filmic,
}
