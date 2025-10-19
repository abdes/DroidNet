// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

namespace DroidNet.Config;

/// <summary>
/// Event arguments for initialization state changes.
/// </summary>
public sealed class InitializationStateChangedEventArgs : EventArgs
{
    /// <summary>
    /// Initializes a new instance of the <see cref="InitializationStateChangedEventArgs"/> class.
    /// </summary>
    /// <param name="isInitialized">A value indicating whether the service is initialized.</param>
    public InitializationStateChangedEventArgs(bool isInitialized)
    {
        this.IsInitialized = isInitialized;
    }

    /// <summary>
    /// Gets a value indicating whether the service is initialized.
    /// </summary>
    public bool IsInitialized { get; }
}
