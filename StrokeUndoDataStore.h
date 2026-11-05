#pragma once

#include <maya/MObject.h>
#include <maya/MDagPath.h>
#include <maya/MIntArray.h>
#include <maya/MDoubleArray.h>

// Short-lived, synchronous handoff only. The Maya MPxCommand owns its own
// before/after snapshots after doIt() consumes this payload. No history is
// stored here and no older Maya undo command depends on this class.
class StrokeUndoDataStore
{
public:
    struct StrokeData
    {
        bool valid = false;
        MObject skinObj;
        MDagPath meshPath;
        MIntArray verts;
        MIntArray influenceIndices;
        MDoubleArray beforeWeights;
        MDoubleArray afterWeights;
        unsigned int influenceCount = 0;
    };

    static unsigned int setPending(const StrokeData& data)
    {
        PendingEntry& entry = pending();
        entry.id = ++counter();
        if (entry.id == 0u)
            entry.id = ++counter();
        entry.data = data;
        entry.valid = true;
        return entry.id;
    }

    static bool consumePending(unsigned int id, StrokeData& outData)
    {
        PendingEntry& entry = pending();
        if (!entry.valid || id == 0u || entry.id != id)
            return false;

        outData = entry.data;
        entry.data = StrokeData();
        entry.id = 0u;
        entry.valid = false;
        return true;
    }

    static void discardPending(unsigned int id)
    {
        PendingEntry& entry = pending();
        if (entry.valid && entry.id == id)
            clear();
    }

    static void clear()
    {
        PendingEntry& entry = pending();
        entry.data = StrokeData();
        entry.id = 0u;
        entry.valid = false;
    }

private:
    StrokeUndoDataStore() = delete;

    struct PendingEntry
    {
        bool valid = false;
        unsigned int id = 0u;
        StrokeData data;
    };

    // Function-local statics in inline definitions are shared by all translation
    // units in the same plugin binary (C++11 and newer). No new .cpp linker
    // dependency is required.
    static PendingEntry& pending()
    {
        static PendingEntry entry;
        return entry;
    }

    static unsigned int& counter()
    {
        static unsigned int nextId = 0u;
        return nextId;
    }
};
