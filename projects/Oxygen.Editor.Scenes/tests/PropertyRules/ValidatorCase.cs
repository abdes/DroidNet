// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using Oxygen.Editor.Schemas;

namespace Oxygen.Editor.Scenes.Tests.PropertyRules;

/// <summary>One property value a validator test checks, and the code it expects.</summary>
/// <param name="Descriptor">The property's descriptor.</param>
/// <param name="Value">The value.</param>
/// <param name="Code">The expected diagnostic code.</param>
public sealed record ValidatorCase(PropertyDescriptor Descriptor, object? Value, string Code)
{
    /// <inheritdoc/>
    public override string ToString() => this.Descriptor.Id.Qualified();
}
