# Draw Spline Surface: append output on one actor

The project PCG override changes only the PCG Mode **Draw Spline Surface** acceptance behavior. Repeated tool sessions append output instead of replacing previously accepted grass, while the PCG Component Details panel remains Unreal Engine's standard interface.

## Authoring workflow

1. Select the target surface actor.
2. Run **Draw Spline Surface**, choose a grass/static mesh, draw the closed surface, and press **Accept**.
3. Select the same actor and run **Draw Spline Surface** again.
4. Choose another mesh, draw another surface, and press **Accept**.

After the second Accept, the actor contains one authoring `PCGToolComponent`, multiple closed `SplineComponent` instances, and separate persistent `ISM_*` components. Starting another draw does not clear an accepted spline or delete its baked ISM.

## Ownership model

When **Accept** is pressed, the current generated component output is baked in place immediately, or immediately after an in-flight interactive generation finishes:

- The ISM remains on the same actor with the same transforms and instances.
- PCG ownership and pending-cleanup tags are removed.
- The generated component becomes persistent and transactional.
- The next tool session creates a new closed spline and new managed output.
- The same visible `PCGToolComponent` continues to author the current stroke only.

Accepted output is deliberately detached from PCG management. Therefore normal PCG Generate, Cleanup, and Parameter Overrides operate on the current component generation and do not retroactively edit older baked strokes.

## Standard PCG editing

No custom Graph Layers, Selected Pattern array, selected-index control, or custom Regenerate/Remove buttons are installed. Selecting `PCGToolComponent` shows Unreal's default:

- Graph picker
- Parameter Overrides
- Generate/Cleanup actions
- Standard component settings

To change an older accepted stroke, remove its baked spline/ISM components and redraw it. This avoids ambiguous selected-index behavior and prevents a component-wide parameter refresh from unexpectedly modifying multiple accepted outputs.

## Cancel behavior

Canceling a newly started append session destroys only the new transient spline. The previous accepted spline and baked ISM output remain intact.

## Validation

Automation coverage:

- `Plugins.PCG.Component.AppendOutput.BakeGeneratedComponentsInPlace`
- `Plugins.PCG.Component.AppendOutput.SplineSurfaceSession`

The tests verify persistent detached output, multiple coexisting splines and ISMs, and restoration of the prior accepted spline after Cancel.
