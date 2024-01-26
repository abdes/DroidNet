// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

namespace DroidNet.Samples.NetConsoleApp;

using DroidNet.Samples.Samples.NetClassLibrary;

internal static class Program
{
    public static void Main(string[] args)
    {
        _ = args;

        Console.WriteLine(new HelloWorld().Greeting);
    }
}
