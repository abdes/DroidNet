// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

namespace Oxygen.Editor.Models;

/// <summary>A model import record that could not be read.</summary>
/// <param name="RecordPath">The record's full path.</param>
/// <param name="Reason">Why it could not be read.</param>
public sealed record ModelImportRecordProblem(string RecordPath, string Reason);
