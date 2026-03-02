# Physics Descriptor Example Pack

This directory is the canonical descriptor-first physics authoring sample for
Oxygen.

It demonstrates all four physics import domains:

1. `physics-resource-descriptor` (`.opres`)
2. `physics-material-descriptor` (`.opmat`)
3. `collision-shape-descriptor` (`.ocshape`)
4. `physics-sidecar` (`.opscene`, emitted beside target `.oscene`)

## Files

1. `import-manifest.physics.json`: orchestrates the full graph.
2. `park.scene.json`: target scene descriptor.
3. `park_hinge_joint_a.physics-resource.json`: resource descriptor for
   `jolt_constraint_binary`.
4. `ground.physics-material.json`, `dynamic.physics-material.json`: physics
   material descriptors.
5. `floor.shape.json`, `actor.shape.json`: collision shape descriptors.
6. `park.physics-sidecar.json`: sidecar bindings across all seven binding
   families.
7. `data/park_hinge_joint_a.jphbin`: source payload for the resource descriptor.

## Run

From repository root:

```powershell
.\out\build-ninja\bin\Debug\Oxygen.Cooker.ImportTool.exe batch --manifest .\Examples\Content\physics\import-manifest.physics.json
```

Expected output root:

`Examples/Content/.cooked`

Key emitted artifacts:

1. `Scenes/physics_park.oscene`
2. `Scenes/physics_park.opscene`
3. `Physics/Materials/*.opmat`
4. `Physics/Shapes/*.ocshape`
5. `Physics/Resources/*.opres`
6. `Physics/Resources/physics.table`
7. `Physics/Resources/physics.data`
