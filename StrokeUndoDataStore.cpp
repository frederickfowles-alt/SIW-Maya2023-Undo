#include "StrokeUndoDataStore.h"

// The synchronous handoff is header-only; undo snapshots live in each
// SmoothInfluenceWeightsStrokeUndoCmd instance owned by Maya's undo queue.
