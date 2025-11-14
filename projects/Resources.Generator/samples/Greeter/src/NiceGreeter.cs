// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Globalization;
using DroidNet.Resources.Generator.Localized_b36e2313;

namespace DroidNet.Resources.Generator.Sample.Greeter;

/// <summary>
/// Makes a nice greeting message, including the assembly version.
/// </summary>
public class NiceGreeter(string? who = null)
{
    /// <summary>
    ///     Prints a greeting message.
    /// </summary>
    public void SayHello()
    {
        var assembly = typeof(NiceGreeter).Assembly.GetName().Name;
        var version = $"({ThisAssembly.AssemblyVersion})";
        var helloMessage = "MSG_Hello".L();
        var greeting = string.Format(CultureInfo.CurrentCulture, helloMessage, who ?? "MSG_Stranger".L());
        Console.WriteLine($"{assembly} {version} says: {greeting}");
    }
}
