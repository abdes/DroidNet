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
    /// Manual exposure using the authored EV100 value.
    /// </summary>
    Manual = 0,

    /// <summary>
    /// Manual exposure using the active camera EV when available.
    /// </summary>
    ManualCamera = 1,

    /// <summary>
    /// Automatic exposure.
    /// </summary>
    Auto = 2,
}

/// <summary>
/// Scene tone mapping mode for environment authoring.
/// </summary>
public enum ToneMappingMode
{
    /// <summary>
    /// No tone mapping.
    /// </summary>
    None = 0,

    /// <summary>
    /// ACES fitted tone mapping.
    /// </summary>
    AcesFitted = 1,

    /// <summary>
    /// Filmic tone mapping.
    /// </summary>
    Filmic = 2,

    /// <summary>
    /// Reinhard tone mapping.
    /// </summary>
    Reinhard = 3,
}

/// <summary>
/// Scene auto-exposure metering mode.
/// </summary>
public enum MeteringMode
{
    /// <summary>
    /// Average luminance metering.
    /// </summary>
    Average = 0,

    /// <summary>
    /// Center-weighted luminance metering.
    /// </summary>
    CenterWeighted = 1,

    /// <summary>
    /// Spot metering.
    /// </summary>
    Spot = 2,
}
