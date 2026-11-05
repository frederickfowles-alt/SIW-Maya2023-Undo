#include "SmoothInfluenceWeightsStrokeUndoCmd.h"

#include <maya/MArgDatabase.h>
#include <maya/MArgList.h>
#include <maya/MFn.h>
#include <maya/MFnSkinCluster.h>
#include <maya/MFnSingleIndexedComponent.h>

const char* SmoothInfluenceWeightsStrokeUndoCmd::kCommandName = "smoothInfluenceWeightsStrokeUndo";
const char* SmoothInfluenceWeightsStrokeUndoCmd::kIdFlag = "-id";
const char* SmoothInfluenceWeightsStrokeUndoCmd::kIdFlagLong = "-entryId";

void* SmoothInfluenceWeightsStrokeUndoCmd::creator()
{
    return new SmoothInfluenceWeightsStrokeUndoCmd();
}

MSyntax SmoothInfluenceWeightsStrokeUndoCmd::newSyntax()
{
    MSyntax syn;
    syn.addFlag(kIdFlag, kIdFlagLong, MSyntax::kUnsigned);
    return syn;
}

MStatus SmoothInfluenceWeightsStrokeUndoCmd::doIt(const MArgList& args)
{
    MStatus status;
    MArgDatabase argData(newSyntax(), args, &status);
    if (status != MS::kSuccess)
        return status;
    if (!argData.isFlagSet(kIdFlag))
        return MS::kFailure;

    unsigned int entryId = 0u;
    status = argData.getFlagArgument(kIdFlag, 0, entryId);
    if (status != MS::kSuccess)
        return status;

    // Consume the handoff ONCE. From here on, Maya's undo command instance
    // retains the complete snapshot until Maya evicts this command itself.
    if (!StrokeUndoDataStore::consumePending(entryId, m_data))
        return MS::kFailure;

    const unsigned int influenceCount = m_data.influenceIndices.length();
    const unsigned int expectedCount = m_data.verts.length() * influenceCount;
    if (!m_data.valid || m_data.verts.length() == 0u ||
        influenceCount == 0u || m_data.influenceCount != influenceCount ||
        m_data.beforeWeights.length() != expectedCount ||
        m_data.afterWeights.length() != expectedCount)
    {
        m_data = StrokeUndoDataStore::StrokeData();
        return MS::kFailure;
    }

    m_hasData = true;

    // IMPORTANT: painting already applied the after-weights before this
    // bookkeeping command was issued. Do not write weights again here.
    // Maya calls redoIt() only when the user redoes an undone stroke.
    return MS::kSuccess;
}

MStatus SmoothInfluenceWeightsStrokeUndoCmd::applyWeights(const MDoubleArray& weights)
{
    if (!m_hasData || !m_data.valid || m_data.skinObj.isNull())
        return MS::kFailure;

    MStatus status;
    MFnSingleIndexedComponent compFn;
    MObject comp = compFn.create(MFn::kMeshVertComponent, &status);
    if (status != MS::kSuccess)
        return status;

    status = compFn.addElements(m_data.verts);
    if (status != MS::kSuccess)
        return status;

    MFnSkinCluster skinFn(m_data.skinObj, &status);
    if (status != MS::kSuccess)
        return status;

    // Keep mutable copies for Maya 2023 API compatibility. The 5-argument
    // overload uses the default oldValues pointer, avoiding nullptr overload
    // ambiguities from previous revisions.
    MIntArray indices(m_data.influenceIndices);
    MDoubleArray values(weights);
    return skinFn.setWeights(m_data.meshPath, comp, indices, values, false);
}

MStatus SmoothInfluenceWeightsStrokeUndoCmd::redoIt()
{
    return applyWeights(m_data.afterWeights);
}

MStatus SmoothInfluenceWeightsStrokeUndoCmd::undoIt()
{
    return applyWeights(m_data.beforeWeights);
}
