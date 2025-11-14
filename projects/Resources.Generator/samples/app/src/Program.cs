// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using DroidNet.Resources;
using DroidNet.Resources.Generator.Localized_b6ad7bc3;
using DroidNet.Resources.Generator.Sample.Greeter;
using DryIoc;

// Setup DI container with localization
using var container = new Container();
container.WithLocalization();

// Application resources, Localized and Special resource files
Console.WriteLine("MSG_Start".L());
Console.WriteLine($"I have a special message: {"Special/MSG_SecretMessage".R()}");

// Resource from Greeter assembly, using the generated extension in the assembly
new NiceGreeter("World").SayHello();
new NiceGreeter().SayHello();

// Resources from assembly, using the generic R<T> extension
Console.WriteLine($"Greeting: {"MSG_Hello".L<NiceGreeter>()}");
Console.WriteLine($"Greeting: {"Special/MSG_GreeterSpecial".R<NiceGreeter>()}");

return 0;
