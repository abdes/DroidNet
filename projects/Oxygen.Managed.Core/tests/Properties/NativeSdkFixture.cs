// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Reflection;

#if DEBUG
[assembly: AssemblyMetadata("Oxygen.NativeSdk", """{"version":1,"configuration":"Debug","artifacts":[{"id":"engine/bin/Oxygen.Engine-d.dll","size":4,"sha256":"E12E115ACF4552B2568B55E93CBD39394C4EF81C82447FAFC997882A02D23677","schemaId":null},{"id":"engine/bin/Oxygen.Engine.EditorInterface-d.dll","size":4,"sha256":"E12E115ACF4552B2568B55E93CBD39394C4EF81C82447FAFC997882A02D23677","schemaId":null}]}""")]
#else
[assembly: AssemblyMetadata("Oxygen.NativeSdk", """{"version":1,"configuration":"Release","artifacts":[{"id":"engine/bin/Oxygen.Engine.dll","size":4,"sha256":"E12E115ACF4552B2568B55E93CBD39394C4EF81C82447FAFC997882A02D23677","schemaId":null},{"id":"engine/bin/Oxygen.Engine.EditorInterface.dll","size":4,"sha256":"E12E115ACF4552B2568B55E93CBD39394C4EF81C82447FAFC997882A02D23677","schemaId":null}]}""")]
#endif
