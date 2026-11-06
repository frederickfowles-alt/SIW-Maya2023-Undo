#include "smoothInfluenceWeightsSettingsNode.h"

#include <maya/MGlobal.h>
#include <maya/MFnNumericAttribute.h>
#include <maya/MFnMessageAttribute.h>

const MTypeId SmoothInfluenceWeightsSettingsNode::kTypeId(0x0013B701);
const MString SmoothInfluenceWeightsSettingsNode::kTypeName("smoothInfluenceWeightsSettings");

MObject SmoothInfluenceWeightsSettingsNode::aSkinCluster;
MObject SmoothInfluenceWeightsSettingsNode::aKeepOnes;
MObject SmoothInfluenceWeightsSettingsNode::aStrength;
MObject SmoothInfluenceWeightsSettingsNode::aIterations;
MObject SmoothInfluenceWeightsSettingsNode::aBleedDistance;
MObject SmoothInfluenceWeightsSettingsNode::aBleedFalloff;

void* SmoothInfluenceWeightsSettingsNode::creator()
{
    return new SmoothInfluenceWeightsSettingsNode();
}

MStatus SmoothInfluenceWeightsSettingsNode::initialize()
{
    MStatus status;

    MFnMessageAttribute mAttr;
    MFnNumericAttribute nAttr;

    aSkinCluster = mAttr.create("skinCluster", "skc", &status);
    if (status != MS::kSuccess) return status;
    mAttr.setReadable(true);
    mAttr.setWritable(true);
    mAttr.setStorable(true);
    mAttr.setKeyable(false);
    mAttr.setHidden(false);
    status = addAttribute(aSkinCluster);
    if (status != MS::kSuccess) return status;

    aKeepOnes = nAttr.create("keepOnes", "ko", MFnNumericData::kBoolean, false, &status);
    if (status != MS::kSuccess) return status;
    nAttr.setReadable(true);
    nAttr.setWritable(true);
    nAttr.setStorable(true);
    nAttr.setKeyable(true);
    nAttr.setHidden(false);
    status = addAttribute(aKeepOnes);
    if (status != MS::kSuccess) return status;

    aStrength = nAttr.create("strength", "str", MFnNumericData::kDouble, 0.2, &status);
    if (status != MS::kSuccess) return status;
    nAttr.setMin(0.0);
    nAttr.setMax(1.0);
    nAttr.setReadable(true);
    nAttr.setWritable(true);
    nAttr.setStorable(true);
    nAttr.setKeyable(true);
    nAttr.setHidden(false);
    status = addAttribute(aStrength);
    if (status != MS::kSuccess) return status;

    aIterations = nAttr.create("iterations", "it", MFnNumericData::kInt, 5, &status);
    if (status != MS::kSuccess) return status;
    nAttr.setMin(1);
    nAttr.setReadable(true);
    nAttr.setWritable(true);
    nAttr.setStorable(true);
    nAttr.setKeyable(true);
    nAttr.setHidden(false);
    status = addAttribute(aIterations);
    if (status != MS::kSuccess) return status;

    aBleedDistance = nAttr.create("bleedDistance", "bd", MFnNumericData::kInt, 1, &status);
    if (status != MS::kSuccess) return status;
    nAttr.setMin(1);
    nAttr.setReadable(true);
    nAttr.setWritable(true);
    nAttr.setStorable(true);
    nAttr.setKeyable(true);
    nAttr.setHidden(false);
    status = addAttribute(aBleedDistance);
    if (status != MS::kSuccess) return status;

    aBleedFalloff = nAttr.create("bleedFalloff", "bf", MFnNumericData::kDouble, 1.0, &status);
    if (status != MS::kSuccess) return status;
    nAttr.setMin(0.0);
    nAttr.setReadable(true);
    nAttr.setWritable(true);
    nAttr.setStorable(true);
    nAttr.setKeyable(true);
    nAttr.setHidden(false);
    status = addAttribute(aBleedFalloff);
    if (status != MS::kSuccess) return status;

    return MS::kSuccess;
}
