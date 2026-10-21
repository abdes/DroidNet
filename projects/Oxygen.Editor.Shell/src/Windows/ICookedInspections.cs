// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

namespace Oxygen.Editor.Shell.Windows;

/// <summary>Opens the cooked inspection of the window's project.</summary>
/// <remarks>The inspection feature implements it, so a part that wants an inspection needs no reference to it.</remarks>
public interface ICookedInspections
{
    /// <summary>Sets what the cooked inspection panel inspects and brings it forward; a request for the scope it shows inspects it again.</summary>
    /// <param name="request">What to inspect.</param>
    /// <returns><see langword="true"/> when the panel took the request.</returns>
    public Task<bool> InspectAsync(CookedInspectionRequest request);
}
