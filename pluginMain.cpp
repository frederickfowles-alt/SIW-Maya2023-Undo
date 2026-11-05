#include <maya/MFnPlugin.h>

#include "smoothInfluenceWeightsCmd.h"
#include "smoothInfluenceWeightsSettingsNode.h"
#include "smoothInfluenceWeightsPaintContext.h"
#include "SmoothInfluenceWeightsStrokeUndoCmd.h"
#include "scaleInfluenceWeightsCmd.h"
#include "scaleInfluenceWeightsPaintContext.h"

MStatus initializePlugin(MObject obj)
{
    MFnPlugin plugin(obj, "Frederick Fowles", "1.0.0", "Any");
    MStatus status;

    status = plugin.registerCommand(
        SmoothInfluenceWeightsCmd::kCommandName,
        SmoothInfluenceWeightsCmd::creator,
        SmoothInfluenceWeightsCmd::newSyntax);
    if (!status)
    {
        status.perror("registerCommand smoothInfluenceWeights");
        return status;
    }

    status = plugin.registerCommand(
        SmoothInfluenceWeightsStrokeUndoCmd::kCommandName,
        SmoothInfluenceWeightsStrokeUndoCmd::creator,
        SmoothInfluenceWeightsStrokeUndoCmd::newSyntax);
    if (!status)
    {
        status.perror("registerCommand smoothInfluenceWeightsStrokeUndo");
        return status;
    }

    status = plugin.registerNode(
        SmoothInfluenceWeightsSettingsNode::kTypeName,
        SmoothInfluenceWeightsSettingsNode::kTypeId,
        SmoothInfluenceWeightsSettingsNode::creator,
        SmoothInfluenceWeightsSettingsNode::initialize);
    if (!status)
    {
        status.perror("registerNode smoothInfluenceWeightsSettings");
        return status;
    }

    status = plugin.registerContextCommand(
        "smoothInfluenceWeightsPaintContext",
        &SmoothInfluenceWeightsPaintContextCmd::creator);
    if (!status)
    {
        status.perror("registerContextCommand smoothInfluenceWeightsPaintContext");
        return status;
    }

    status = plugin.registerCommand(
        ScaleInfluenceWeightsCmd::kCommandName,
        ScaleInfluenceWeightsCmd::creator,
        ScaleInfluenceWeightsCmd::newSyntax);
    if (!status)
    {
        status.perror("registerCommand scaleInfluenceWeights");
        return status;
    }

    status = plugin.registerContextCommand(
        "scaleInfluenceWeightsPaintContext",
        &ScaleInfluenceWeightsPaintContextCmd::creator);
    if (!status)
    {
        status.perror("registerContextCommand scaleInfluenceWeightsPaintContext");
        return status;
    }

    return MS::kSuccess;
}

MStatus uninitializePlugin(MObject obj)
{
    MFnPlugin plugin(obj);
    MStatus status;
    MStatus finalStatus = MS::kSuccess;

    status = plugin.deregisterContextCommand("scaleInfluenceWeightsPaintContext");
    if (!status)
    {
        status.perror("deregisterContextCommand scaleInfluenceWeightsPaintContext");
        finalStatus = status;
    }

    status = plugin.deregisterCommand(ScaleInfluenceWeightsCmd::kCommandName);
    if (!status)
    {
        status.perror("deregisterCommand scaleInfluenceWeights");
        finalStatus = status;
    }

    status = plugin.deregisterContextCommand("smoothInfluenceWeightsPaintContext");
    if (!status)
    {
        status.perror("deregisterContextCommand smoothInfluenceWeightsPaintContext");
        finalStatus = status;
    }

    status = plugin.deregisterNode(SmoothInfluenceWeightsSettingsNode::kTypeId);
    if (!status)
    {
        status.perror("deregisterNode smoothInfluenceWeightsSettings");
        finalStatus = status;
    }

    status = plugin.deregisterCommand(SmoothInfluenceWeightsStrokeUndoCmd::kCommandName);
    if (!status)
    {
        status.perror("deregisterCommand smoothInfluenceWeightsStrokeUndo");
        finalStatus = status;
    }

    status = plugin.deregisterCommand(SmoothInfluenceWeightsCmd::kCommandName);
    if (!status)
    {
        status.perror("deregisterCommand smoothInfluenceWeights");
        finalStatus = status;
    }

    return finalStatus;
}
