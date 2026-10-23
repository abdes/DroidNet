// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

namespace Oxygen.Editor.Runtime.Engine;

/// <summary>One override of a geometry property: the schema's override id and the value of its value type.</summary>
/// <param name="Id">The property's override id.</param>
/// <param name="Value">The value; its kind matches the property's value type.</param>
public sealed record RuntimeOverride(RuntimeOverrideId Id, RuntimeOverrideValue Value);
