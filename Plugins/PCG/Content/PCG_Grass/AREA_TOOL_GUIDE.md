# Garden Grass - Draw Spline Surface

Area graph: `/PCG/EdMode/DrawSplineSurface/PCG_GardenGrass_AreaTool`.

## Use

1. In PCG mode choose Draw Spline Surface and the Garden Grass preset.
2. Draw a closed spline around the area and accept.
3. In the PCG component parameter overrides, enable the override for Excluded Actors, then add actors with the world picker. Actor tags are no longer used by the area graph.
4. Optionally enable Enable Included Surfaces (disabled by default). Add world actors to Included Surfaces. Grass is generated on their visible top surfaces inside the drawn spline; an enabled empty list generates no grass.
5. Generate the PCG component after changing actor lists or moving/resizing included surfaces.

## Surface placement and cutting

Included surfaces normally use rendered depth to place grass at the selected mesh height. Enable **Wrap Entire Surface** under Surface Selection to sample the actual mesh triangles, including vertical faces and undersides. Enable Snap to Surface and Align to Surface for outward-facing grass across a sphere. Grass Density is measured per square metre of actual surface area in this mode, avoiding sparse sides. The spline still limits the XY planting footprint; draw it around the entire mesh silhouette. Wrapped sampling uses the mesh's LOD0 render geometry (Nanite uses its fallback geometry). For generation in a packaged game, enable Allow CPU Access on included meshes. Existing planar areas keep Wrap Entire Surface disabled by default.

Without inclusion, points project onto WorldStatic ground using complex collision, ignoring Excluded Actors when exclusions are enabled. The ground must support collision queries. The Projection group exposes Snap to Surface, Align to Surface, and Snap to Surface Max Distance (centimetres). Snapping and alignment are independent. The default search extends 100 metres above and below the spline, preserving the existing placement range. A missed or out-of-range projection leaves the sample transform unchanged. Included Surfaces still limits the planting footprint when snapping and alignment are disabled.

Parameters are grouped under Sampling, Surface Selection, Exclusion, Spawning, Random Transform, and Projection in both the Garden Grass tool and component overrides. Each Grass Meshes entry retains its Weight and nested Material controls.

Under **Surface Selection**, turn on **Wrap Entire Surface**, then choose **Wrap Coverage**: **Entire Surface** includes the underside; **Above Spline** places roots only above the spline surface evaluated at each XY position. **Wrap Height Offset** moves that planting boundary up or down in centimetres. Enable each parameter's override checkbox when editing an individual area. Wrapped contact exclusion also hides entire clumps rooted inside an excluded actor, preventing their tips from emerging above the actor.

The material clips individual blade pixels at the rendered included/excluded silhouettes, without a fixed border gap. Full Footprint excludes the entire overhead silhouette, including elevated parts. In wrapped mode, contact exclusion clips only blade pixels between the obstacle's bottom and top depths, so a low obstacle does not cut an infinite column through grass above it. Depth masks have finite resolution and represent the vertical interval of selected obstacles; disconnected geometry stacked along the same vertical line can merge into one interval.

MaskResolution on the Surface Selection and Pixel Mask node controls raster precision (2048 by default, up to 4096). Very large areas or tiny details may need smaller separate areas or higher resolution. This is finite-resolution clipping, not an exact geometric Boolean. With inclusion disabled, only grass roots are constrained to the spline; the spline itself is not a pixel mask.

## Other controls

- Grass Meshes: expandable array of weighted mesh entries. Each entry contains Static Mesh, Weight, Color Map, Dry Amount, Dry Map 1/2, Tint Color, Tint Dry Color, Normal Map, and Roughness. These material settings are independent for each entry, including repeated entries using the same mesh.
- Grass Density: clumps per square metre.
- Weight is relative: weights 3 and 1 produce approximately 75% and 25% of grass points. Weight 0 disables an entry. Missing meshes and nonpositive/invalid weights are ignored; an empty array or all-zero weights generates no grass. Each entry uses its own M_GardenGrass instance, sharing only the area clipping mask. Use textures compatible with that entry's mesh UVs.
- Enable Random Transform and min/max Offset, Rotation, Scale: applied after surface projection. Rotation is relative to the surface so random yaw preserves alignment. Nonzero offsets or pitch/roll intentionally move grass away from that surface alignment.
- Enable Exclusions: enables the Excluded Actors array.
- Exclude Full Footprint: switches between footprint and contact modes.
- For a cut through the complete overhead overlap, enable **Exclude Full Footprint** on the area's parameter overrides. Contact mode intentionally preserves grass above or below the obstacle. Full-footprint clipping also tests the root-to-pixel segment so clipped blades cannot reappear beyond the far edge. The segment test uses eight samples and the masks have finite resolution.
- Contact Probe Half Size: Z is the contact tolerance in centimetres; X/Y do not expand the silhouette.

## Baking finished fields

Select the area's **GardenGrassMask** component and use **Grass Baking > Bake Grass** after generation finishes. This copies the generated instances into ordinary instanced components on the same actor, saves persistent material instances and full-precision depth masks under `/Game/GardenGrassBakes`, releases the PCG-managed instances, disables PCG generation, and stops mask ticking/captures. **Save the level** after baking. The status field reports success or why baking could not run. Non-partitioned areas are supported; partitioned areas must be handled separately.

Use **Unbake Grass** before changing the spline, surfaces, exclusions, mesh choices, or materials. It removes the baked copies, restores the previous PCG generation settings, and regenerates the live grass. Old bake assets are retained to avoid deleting assets used by other saved levels. Baked grass is fixed in world space relative to its saved mask: do not move the actor or blockers without unbaking and rebaking.

Baking removes procedural update work, not grass rendering cost. Density, shadows, overdraw, and the exclusion shader still affect GPU performance. The live mask now skips material rebuilding for unchanged masks, and wrapped sampling avoids the inclusion-depth CPU readback. Actual production hitches still require profiling if they persist after baking.

When moving a baked map to another project, migrate its `/Game/GardenGrassBakes` dependencies together with the map and plugins. These are map-specific bake outputs, not dependencies of the reusable grass graph.

## Portability

Grass node classes live in the PCG runtime module. Copy the updated PCG plugin, including source, config, and content, and rebuild for the target engine version. The PCGToolset plugin provides editing/automation tools; it is not required to load the grass node classes. Select actors again in each destination world; graph defaults deliberately contain no project-specific actor references.

The older `/PCG/EdMode/Volume/PCG_GardenGrass` graph is a legacy workflow. These instructions and explicit actor arrays apply to the Area Tool.


The mask material is applied on PCG generation completion; it does not wait for the periodic editor tick. After migrating an older graph, existing mesh choices and their former shared material settings are copied into the new entries.

