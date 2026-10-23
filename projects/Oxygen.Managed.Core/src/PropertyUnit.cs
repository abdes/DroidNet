// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

namespace Oxygen.Managed.Core;

/// <summary>A unit that UIs show next to a property value, as the scene property schema declares it.</summary>
/// <param name="Id">The schema id of the unit (e.g. <c>metre</c>).</param>
/// <param name="Abbreviation">The short form shown next to a value (e.g. <c>m</c>).</param>
/// <param name="Name">The full plural name, for tooltips and automation (e.g. <c>metres</c>).</param>
public sealed record PropertyUnit(string Id, string Abbreviation, string Name);
