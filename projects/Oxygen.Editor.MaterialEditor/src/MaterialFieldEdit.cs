// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

namespace Oxygen.Editor.MaterialEditor;

/// <summary>
/// One scalar material field edit.
/// </summary>
/// <param name="FieldKey">The stable material field key.</param>
/// <param name="NewValue">The scalar value.</param>
public readonly record struct MaterialFieldEdit(string FieldKey, object? NewValue);
