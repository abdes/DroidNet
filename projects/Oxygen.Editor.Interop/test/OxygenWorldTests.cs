// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Diagnostics;
using System.Diagnostics.CodeAnalysis;
using System.Numerics;
using System.Security.Cryptography;
using FluentAssertions;
using Oxygen.Interop.World;

namespace DroidNet.Oxygen.Editor.Interop.Tests;

[TestClass]
[ExcludeFromCodeCoverage]
[TestCategory(nameof(OxygenWorld))]
public sealed class OxygenWorldTests
{
    [TestMethod]
    public void TestCreateEntityCreatesTransform()
    {
        var descriptor = CreateTestEntityDescriptor(0);
        using var entity = OxygenWorld.CreateSceneNode(descriptor);

        var transform = entity.GetTransform();
        transform.Dispose();

        _ = transform.Should().NotBeNull("Transform should be created with the entity");

        AssertVector3ApproximatelyEqual(transform.Position, descriptor.Transform.Position, OxygenWorld.PrecisionLow);
        AssertVector3ApproximatelyEqual(transform.Rotation, descriptor.Transform.Rotation, OxygenWorld.PrecisionLow);
        AssertVector3ApproximatelyEqual(transform.Scale, descriptor.Transform.Scale, OxygenWorld.PrecisionLow);
    }

    [TestMethod]
    public void TestConcurrentCreateAndRemoveEntities()
    {
        _ = new OxygenWorld();

        var createThread = new Thread(CreateEntities);
        var removeThread = new Thread(RemoveEntities);

        createThread.Start();
        removeThread.Start();

        createThread.Join();
        removeThread.Join();
    }

    [TestMethod]
    public void TestSetAndGetPosition()
    {
        var descriptor = CreateTestEntityDescriptor(0);
        using var entity = OxygenWorld.CreateSceneNode(descriptor);
        var transform = entity.GetTransform();

        var newPosition = new Vector3(10, 20, 30);
        transform.Position = newPosition;

        AssertVector3ApproximatelyEqual(transform.Position, newPosition, OxygenWorld.PrecisionLow);
    }

    [TestMethod]
    public void TestSetAndGetRotation()
    {
        var descriptor = CreateTestEntityDescriptor(0);
        using var entity = OxygenWorld.CreateSceneNode(descriptor);
        var transform = entity.GetTransform();

        var newRotation = new Vector3(5f, 20f, 15f);
        transform.Rotation = newRotation;

        AssertVector3ApproximatelyEqual(transform.Rotation, newRotation, OxygenWorld.PrecisionLow);
    }

    [TestMethod]
    public void TestSetAndGetScale()
    {
        var descriptor = CreateTestEntityDescriptor(0);
        using var entity = OxygenWorld.CreateSceneNode(descriptor);
        var transform = entity.GetTransform();

        var newScale = new Vector3(3, 3, 3);
        transform.Scale = newScale;

        AssertVector3ApproximatelyEqual(transform.Scale, newScale, OxygenWorld.PrecisionLow);
    }

    [TestMethod]
    public void TestRemoveEntity()
    {
        var descriptor = CreateTestEntityDescriptor(0);
        using var entity = OxygenWorld.CreateSceneNode(descriptor);

        var removedCount = OxygenWorld.RemoveSceneNode(entity);

        _ = removedCount.Should().Be(1);
    }

    [TestMethod]
    public void TestCreateMultipleEntities()
    {
        _ = new OxygenWorld();

        var descriptor1 = CreateTestEntityDescriptor(1);
        using var entity1 = OxygenWorld.CreateSceneNode(descriptor1);

        var descriptor2 = CreateTestEntityDescriptor(2);
        using var entity2 = OxygenWorld.CreateSceneNode(descriptor2);

        var transform1 = entity1.GetTransform();
        var transform2 = entity2.GetTransform();

        AssertVector3ApproximatelyEqual(transform1.Position, descriptor1.Transform.Position, OxygenWorld.PrecisionLow);
        AssertVector3ApproximatelyEqual(transform2.Position, descriptor2.Transform.Position, OxygenWorld.PrecisionLow);
    }

    [TestMethod]
    public void TestUpdateTransformProperties()
    {
        var descriptor = CreateTestEntityDescriptor(0);
        using var entity = OxygenWorld.CreateSceneNode(descriptor);
        var transform = entity.GetTransform();

        var newPosition = new Vector3(10, 20, 30);
        var newRotation = new Vector3(0.5f, 0.5f, 0.5f);
        var newScale = new Vector3(3, 3, 3);

        transform.Position = newPosition;
        transform.Rotation = newRotation;
        transform.Scale = newScale;

        AssertVector3ApproximatelyEqual(transform.Position, newPosition, OxygenWorld.PrecisionLow);
        AssertVector3ApproximatelyEqual(transform.Rotation, newRotation, OxygenWorld.PrecisionLow);
        AssertVector3ApproximatelyEqual(transform.Scale, newScale, OxygenWorld.PrecisionLow);
    }

    private static void CreateEntities()
    {
        try
        {
            for (var i = 0; i < 10; i++)
            {
                var descriptor = CreateTestEntityDescriptor(i);
                using var entity = OxygenWorld.CreateSceneNode(descriptor);
                Debug.WriteLine($"T1 Created entity {entity}");

                var transform = entity.GetTransform();
                AssertVector3ApproximatelyEqual(transform.Position, descriptor.Transform.Position, OxygenWorld.PrecisionLow);
                AssertVector3ApproximatelyEqual(transform.Rotation, descriptor.Transform.Rotation, OxygenWorld.PrecisionLow);
                AssertVector3ApproximatelyEqual(transform.Scale, descriptor.Transform.Scale, OxygenWorld.PrecisionLow);
            }
        }
        catch (Exception ex)
        {
            Debug.WriteLine($"Exception in createThread: {ex}");
            Debugger.Break();
            throw;
        }
    }

    private static void RemoveEntities()
    {
        try
        {
            for (var i = 0; i < 10; i++)
            {
                var descriptor = CreateTestEntityDescriptor(i);
                using var entity = OxygenWorld.CreateSceneNode(descriptor);
                Debug.WriteLine($"T2 Created entity {entity}");
                var removedCount = OxygenWorld.RemoveSceneNode(entity);
                Debug.WriteLine($"T2 Removed entity {entity}");
                _ = removedCount.Should().Be(1);
            }
        }
        catch (Exception ex)
        {
            Debug.WriteLine($"Exception in removeThread: {ex}");
            Debugger.Break();
            throw;
        }
    }

    private static SceneNodeDescriptor CreateTestEntityDescriptor(int i)
    {
        var rotation = new Vector3(
            GetSecureRandomFloat(0, 90),
            GetSecureRandomFloat(0, 90),
            GetSecureRandomFloat(0, 90));

        return new SceneNodeDescriptor
        {
            Transform = new TransformDescriptor
            {
                Position = new Vector3(i, i, i),
                Rotation = rotation,
                Scale = new Vector3(1, 1, 1),
            },
        };
    }

    private static float GetSecureRandomFloat(float minValue, float maxValue)
    {
        using var rng = RandomNumberGenerator.Create();
        var bytes = new byte[4];
        rng.GetBytes(bytes);
        var randomValue = BitConverter.ToUInt32(bytes, 0) / (float)uint.MaxValue;
        return minValue + (randomValue * (maxValue - minValue));
    }

    private static void AssertVector3ApproximatelyEqual(Vector3 actual, Vector3 expected, float epsilon)
    {
        _ = actual.X.Should().BeApproximately(expected.X, epsilon);
        _ = actual.Y.Should().BeApproximately(expected.Y, epsilon);
        _ = actual.Z.Should().BeApproximately(expected.Z, epsilon);
    }
}
