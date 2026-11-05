#pragma once

#include <maya/MPxCommand.h>
#include <maya/MStatus.h>
#include <maya/MArgList.h>
#include <maya/MSyntax.h>
#include <maya/MDoubleArray.h>

#include "StrokeUndoDataStore.h"

// One command instance per completed paint stroke. Maya retains each instance
// on its own undo queue; no external fixed-size history is consulted.
class SmoothInfluenceWeightsStrokeUndoCmd : public MPxCommand
{
public:
    SmoothInfluenceWeightsStrokeUndoCmd() = default;
    ~SmoothInfluenceWeightsStrokeUndoCmd() override = default;

    static void* creator();
    static MSyntax newSyntax();
    static const char* kCommandName;
    static const char* kIdFlag;
    static const char* kIdFlagLong;

    MStatus doIt(const MArgList& args) override;
    MStatus redoIt() override;
    MStatus undoIt() override;
    bool isUndoable() const override { return true; }

private:
    MStatus applyWeights(const MDoubleArray& weights);

    StrokeUndoDataStore::StrokeData m_data;
    bool m_hasData = false;
};
