// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

namespace DroidNet.Samples.NetConsoleApp;

using DroidNet.Samples.NetClassLibrary;
using DroidNet.Samples.NetConsoleApp.Resources;

internal static class Program
{
    public static void Main(string[] args)
    {
        _ = args;

        Console.WriteLine(Strings.AssemblyFileVersionText, ThisAssembly.AssemblyFileVersion);
        Console.WriteLine(Strings.AssemblyInformationalVersionText, ThisAssembly.AssemblyInformationalVersion);

        Console.WriteLine();

        Console.WriteLine(new Greeter(Strings.HelloWorldText).Greeting);
    }
}
