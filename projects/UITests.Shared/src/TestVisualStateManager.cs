// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Diagnostics.CodeAnalysis;
using Microsoft.UI.Xaml;
using Microsoft.UI.Xaml.Controls;

namespace DroidNet.Tests;

/// <summary>
/// A custom <see cref="VisualStateManager"/> used for testing purposes. This class intercepts state changes and
/// allows for verification in unit tests.
/// </summary>
/// <remarks>
/// <para>
/// This class is designed to help developers test visual state transitions in their controls. By
/// intercepting state changes, it allows for easy verification of whether a control has
/// transitioned to the expected state.
/// </para>
/// <para>
/// <strong>Important:</strong> This class should only be used in a testing context. It is not
/// intended for use in production code.
/// </para>
/// </remarks>
/// <example>
/// <para><strong>Example Usage:</strong></para>
/// <![CDATA[
/// [TestMethod]
/// public async Task UpdatesVisualStateToExpanded_Async()
/// {
///     // Setup
///     var expander = new Controls.Expander();
///     await LoadTestContentAsync(expander).ConfigureAwait(true);
///
///     // Set up the custom VisualStateManager on the element that the control under test uses for visual states being tested
///     var activeElement = expander.FindDescendant<Grid>(e => string.Equals(e.Name, "PART_ActiveElement", StringComparison.Ordinal));
///     var testVisualStateManager = new TestVisualStateManager();
///     VisualStateManager.SetCustomVisualStateManager(activeElement, testVisualStateManager);
///
///     // Act
///     expander.IsExpanded = true;
///
///     // Assert
///     // Here again, the control under state may have used a parent FrameWorkElement to update the visual state.
///     // It is that element that needs to be used to get the current state.
///     _ = vsm.GetCurrentState(expander).Should().Be("Expanded");
/// }
/// ]]>
/// </example>
[SuppressMessage("Usage", "CA1812:Avoid uninstantiated internal classes", Justification = "Used by tests / XAML reflection")]
internal sealed partial class TestVisualStateManager : VisualStateManager
{
    private readonly Dictionary<FrameworkElement, List<string>> controlStates = [];

    /// <summary>
    /// Clears the cached state of all controls, preparing for a new test.
    /// </summary>
    public void Reset() => this.controlStates.Clear();

    /// <summary>
    /// Gets the current state of the specified control.
    /// </summary>
    /// <param name="control">The control to get the current state for.</param>
    /// <returns>The name of the current state of the control.</returns>
    public List<string>? GetCurrentStates(FrameworkElement? control)
        => control is null ? null : this.controlStates.TryGetValue(control, out var state) ? state : [];

    /// <summary>
    /// Overrides the GoToStateCore method to intercept state changes and signal the stateChangedCompletionSource.
    /// </summary>
    /// <param name="control">The control that is transitioning to a new state.</param>
    /// <param name="templateRoot">The root element of the control's template.</param>
    /// <param name="stateName">The name of the state to transition to.</param>
    /// <param name="group">The VisualStateGroup that the state belongs to.</param>
    /// <param name="state">The VisualState to transition to.</param>
    /// <param name="useTransitions">A value indicating whether to use transitions when transitioning to the new state.</param>
    /// <returns>True if the state transition was successful; otherwise, false.</returns>
    protected override bool GoToStateCore(Control control, FrameworkElement templateRoot, string stateName, VisualStateGroup group, VisualState state, bool useTransitions)
    {
        if (!this.controlStates.TryGetValue(control, out var states))
        {
            states = [];
            this.controlStates.Add(control, states);
        }

        states.Add(stateName);
        return base.GoToStateCore(control, templateRoot, stateName, group, state, useTransitions);
    }
}
