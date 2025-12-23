// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

namespace Oxygen.Assets.Validation.LooseCooked.V1;

/// <summary>
/// Represents a validation issue found while validating a loose cooked container.
/// </summary>
public sealed record LooseCookedValidationIssue(string Code, string Message);
