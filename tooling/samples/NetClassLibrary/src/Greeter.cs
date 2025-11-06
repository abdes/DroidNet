// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

namespace DroidNet.Samples.NetClassLibrary;

/// <summary>
/// Makes a nice greeting message, including the assembly version.
/// </summary>
/// <param name="message">The greeting message.</param>
public class Greeter(string message)
{
    /// <summary>
    ///     Gets a greeting message including the assembly version.
    /// </summary>
    public string Greeting
        => $"({ThisAssembly.AssemblyVersion}/{ThisAssembly.AssemblyInformationalVersion}) says: {message}";
}
