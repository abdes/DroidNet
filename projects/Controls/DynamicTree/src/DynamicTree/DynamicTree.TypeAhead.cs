// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using Microsoft.UI.Xaml;
using Windows.System;
using static DroidNet.Controls.DynamicTreeViewModel;

namespace DroidNet.Controls;

/// <summary>
///     Type-ahead navigation of the <see cref="DynamicTree" /> control: typed characters move focus to the next item
///     whose label starts with them.
/// </summary>
public partial class DynamicTree
{
    private static readonly TimeSpan TypeAheadResetDelay = TimeSpan.FromMilliseconds(1000);

    private DispatcherTimer? typeAheadTimer;
    private string typeAheadText = string.Empty;

    // Only the letter and digit keys map to a character; other keys are not type-ahead input.
    private static bool TryGetTypeAheadCharacter(VirtualKey key, out char character)
    {
        character = default;

        if (key is >= VirtualKey.A and <= VirtualKey.Z)
        {
            character = (char)('A' + ((int)key - (int)VirtualKey.A));
            return true;
        }

        if (key is >= VirtualKey.Number0 and <= VirtualKey.Number9)
        {
            character = (char)('0' + ((int)key - (int)VirtualKey.Number0));
            return true;
        }

        if (key is >= VirtualKey.NumberPad0 and <= VirtualKey.NumberPad9)
        {
            character = (char)('0' + ((int)key - (int)VirtualKey.NumberPad0));
            return true;
        }

        return false;
    }

    // Forgets the typed prefix and stops the timer that would forget it.
    private void ResetTypeAhead()
    {
        this.typeAheadTimer?.Stop();
        this.typeAheadText = string.Empty;
    }

    private bool TryHandleTypeAhead(VirtualKey key)
    {
        if (this.ViewModel is null || !this.IsTypeAheadEnabled)
        {
            return false;
        }

        if (key == VirtualKey.Escape)
        {
            if (!string.IsNullOrEmpty(this.typeAheadText))
            {
                this.ResetTypeAhead();
                return true;
            }

            return false;
        }

        if (key == VirtualKey.Back)
        {
            if (this.typeAheadText.Length == 0)
            {
                return false;
            }

            this.typeAheadText = this.typeAheadText[..^1];
            this.RestartTypeAheadTimer();
            return this.typeAheadText.Length == 0
                || this.ViewModel.FocusNextByPrefix(this.typeAheadText, RequestOrigin.KeyboardInput);
        }

        if (!TryGetTypeAheadCharacter(key, out var character))
        {
            return false;
        }

        this.typeAheadText += character;
        this.RestartTypeAheadTimer();

        return this.ViewModel.FocusNextByPrefix(this.typeAheadText, RequestOrigin.KeyboardInput);
    }

    private void RestartTypeAheadTimer()
    {
        if (this.typeAheadTimer is null)
        {
            this.typeAheadTimer = new DispatcherTimer
            {
                Interval = TypeAheadResetDelay,
            };

            this.typeAheadTimer.Tick += this.OnTypeAheadTimerTick;
        }

        this.typeAheadTimer.Stop();
        this.typeAheadTimer.Start();
    }

    private void OnTypeAheadTimerTick(object? sender, object args)
    {
        _ = sender; // unused
        _ = args; // unused

        this.ResetTypeAhead();
    }
}
