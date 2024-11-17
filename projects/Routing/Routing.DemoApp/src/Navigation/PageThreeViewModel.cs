// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

namespace DroidNet.Routing.Demo.Navigation;

/// <summary>
/// ViewModel for Page Three, implements IRoutingAware interface.
/// </summary>
[System.Diagnostics.CodeAnalysis.SuppressMessage(
    "Maintainability",
    "CA1515:Consider making public types internal",
    Justification = "ViewModel classes must be public because the ViewModel property in the generated code for the view is public")]
public class PageThreeViewModel : IRoutingAware
{
    /// <inheritdoc/>
    public IActiveRoute? ActiveRoute { get; set; }
}
