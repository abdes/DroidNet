// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using AwesomeAssertions;
using Oxygen.Editor.World.Scripting;

namespace Oxygen.Editor.WorldEditor.Unit.Tests.Scripting;

/// <summary>
/// Two script sources with one name in one folder cook to the same asset; each reports the other.
/// </summary>
[TestClass]
public sealed partial class ScriptCollisionTests : IDisposable
{
    private readonly string folder = Path.Combine(Path.GetTempPath(), "oxygen-script-collision-tests", Guid.NewGuid().ToString("N"));

    public ScriptCollisionTests() => Directory.CreateDirectory(this.folder);

    public void Dispose()
    {
        if (Directory.Exists(this.folder))
        {
            Directory.Delete(this.folder, recursive: true);
        }
    }

    [TestMethod]
    public void FindCollisionsNamesTheOtherSourceWithTheSameName()
    {
        var lua = this.Write("move.lua");
        var luau = this.Write("move.luau");
        _ = this.Write("jump.lua");

        _ = ScriptAnalysisService.FindCollisions(lua).Should().Equal("move.luau");
        _ = ScriptAnalysisService.FindCollisions(luau).Should().Equal("move.lua");
    }

    [TestMethod]
    public void FindCollisionsIsEmptyForAUniqueName()
        => _ = ScriptAnalysisService.FindCollisions(this.Write("jump.lua")).Should().BeEmpty();

    private string Write(string name)
    {
        var path = Path.Combine(this.folder, name);
        File.WriteAllText(path, "return {}");
        return path;
    }
}
