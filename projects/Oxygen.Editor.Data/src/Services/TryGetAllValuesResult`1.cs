// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using Oxygen.Editor.Data.Settings;

namespace Oxygen.Editor.Data.Services;

/// <summary>
/// Result for TryGetAllValuesAsync containing typed scope values and optional errors.
/// </summary>
/// <typeparam name="T">The type of the deserialized values.</typeparam>
public sealed record TryGetAllValuesResult<T>(bool Success, IReadOnlyList<ScopedValue<T>> Values, IReadOnlyList<string> Errors);
