# SIW Maya 2023 — Native Undo Snapshots

SIW - Native Maya Undo Snapshots (Maya 2023)


SOURCE BASELINE
  undo_stack_15_retained_history_fix_v3(20261105-205452).zip

PURPOSE
  Each completed Smooth/Scale brush stroke gets its own undoable MPxCommand
  with private before/after weight snapshots. Maya, not a global 15-entry
  data store, retains the commands. No old undo entries are evicted by SIW.

CHANGED FULL FILES
  StrokeUndoDataStore.h
  StrokeUndoDataStore.cpp
  SmoothInfluenceWeightsStrokeUndoCmd.h
  SmoothInfluenceWeightsStrokeUndoCmd.cpp
  smoothInfluenceWeightsPaintContext.cpp
  scaleInfluenceWeightsPaintContext.cpp

INCLUDED UNCHANGED
  pluginMain.cpp  (already registers the undo command with newSyntax)

IMPORTANT BEHAVIOR
  - The solver, selected-vertex filtering, and preview coloring code in
    both paint contexts are unchanged; only finalizeStrokeUndo handoff changes.
  - doIt captures data and DOES NOT write the weights a second time.
  - undoIt restores the before snapshot; redoIt restores the after snapshot.
  - The transient handoff store has no history cap and is cleared as soon
    as each command consumes its data.
  - No new compiled translation unit is required. The store is header-only;
    its .cpp file is an include-only stub for existing build configurations.
  - Maya's native undo queue must be enabled and configured with at least
    15 available operations (or infinite). It is shared with other Maya edits.
  - The existing Undo/Redo UI scriptJobs should refresh preview colors.

NOT COVERED
  The supplied archive does NOT include smoothInfluenceWeightsCmd.cpp or
  scaleInfluenceWeightsCmd.cpp. Therefore this package does NOT implement
  Flood-specific undo. Flood commands need separate inspection and changes
  if their own MPxCommand undoIt/redoIt implementations do not work.

BUILD AND TEST
  1. Replace the included files in your existing Maya 2023 plugin project.
  2. Clean and rebuild the existing plugin (do not change context registration).
  3. In Maya, verify undo is enabled and queue length is at least 15.
  4. Make 16 distinct Smooth strokes on the same mesh; undo 15 times and
     redo 15 times, comparing actual skin weights (not only preview colors).
  5. Repeat for Scale with vertex-only selection and confirm no unselected
     vertices change. Test both with and without selected vertices.
  6. Test alternating Smooth and Scale strokes; undo/redo across the switch.
  7. Test Undo after a new stroke following several undos (redo invalidation).

LIMITATION
  No Maya SDK/Visual Studio compiler or live Maya session is available in
  this environment, so these changes have been statically checked but cannot
  be certified as compiling or working in Maya 2023 without your build/test.

