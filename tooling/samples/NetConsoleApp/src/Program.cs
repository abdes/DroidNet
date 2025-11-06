// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using DroidNet.Samples.NetClassLibrary;
using DroidNet.Samples.NetConsoleApp.Resources;

namespace DroidNet.Samples.NetConsoleApp;

/// <summary>
///     Application entry point.
/// </summary>
internal static class Program
{
    /// <summary>
    ///     The main entry point of the application.
    /// </summary>
    /// <param name="args">command line arguments.</param>
    [STAThread]
    public static void Main(string[] args)
    {
        _ = args;

        Console.WriteLine(Strings.AssemblyFileVersionText, ThisAssembly.AssemblyFileVersion);
        Console.WriteLine(Strings.AssemblyInformationalVersionText, ThisAssembly.AssemblyInformationalVersion);

        Console.WriteLine();

        Console.WriteLine(new Greeter(Strings.HelloWorldText).Greeting);
    }
}
