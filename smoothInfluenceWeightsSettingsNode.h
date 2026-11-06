#pragma once

#include <maya/MPxNode.h>
#include <maya/MTypeId.h>
#include <maya/MString.h>
#include <maya/MObject.h>

class SmoothInfluenceWeightsSettingsNode : public MPxNode
{
public:
    SmoothInfluenceWeightsSettingsNode() = default;
    ~SmoothInfluenceWeightsSettingsNode() override = default;

    static void* creator();
    static MStatus initialize();

    static const MTypeId kTypeId;
    static const MString kTypeName;

    static MObject aSkinCluster;
    static MObject aKeepOnes;
    static MObject aStrength;
    static MObject aIterations;
    static MObject aBleedDistance;
    static MObject aBleedFalloff;
};
