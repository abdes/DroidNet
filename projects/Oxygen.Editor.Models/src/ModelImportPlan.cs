// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Collections.Immutable;

namespace Oxygen.Editor.Models;

/// <summary>What a person chose in the import dialog: the models, in the list's order, and the run's settings.</summary>
/// <param name="Models">The models to import.</param>
/// <param name="Run">The ImportTool's execution settings of the run.</param>
public sealed record ModelImportPlan(ImmutableArray<ModelImportChoice> Models, ImportRunSettings Run);
