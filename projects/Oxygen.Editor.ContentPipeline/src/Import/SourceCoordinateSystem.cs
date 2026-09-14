// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

namespace Oxygen.Editor.ContentPipeline.Import;

/// <summary>Native source coordinates before conversion to Oxygen's meter-based, right-handed Z-up space.</summary>
/// <param name="UnitMeters">Meters per source unit, or null when source metadata is missing.</param>
/// <param name="RightAxis">The authored right axis.</param>
/// <param name="UpAxis">The authored up axis.</param>
/// <param name="FrontAxis">The authored front axis.</param>
/// <param name="IsLeftHanded">Source handedness, or null when it cannot be established.</param>
/// <param name="ReversesWinding">Whether native coordinate conversion reverses triangle winding.</param>
public sealed record SourceCoordinateSystem(double? UnitMeters, string RightAxis, string UpAxis, string FrontAxis, bool? IsLeftHanded, bool ReversesWinding);
