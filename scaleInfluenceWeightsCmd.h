#pragma once

#include <maya/MPxCommand.h>
#include <maya/MSyntax.h>
#include <maya/MString.h>
#include <maya/MStringArray.h>
#include <maya/MObject.h>
#include <maya/MDagPath.h>
#include <maya/MIntArray.h>
#include <maya/MDoubleArray.h>

#include <set>
#include <vector>

class ScaleInfluenceWeightsCmd : public MPxCommand
{
public:
    ScaleInfluenceWeightsCmd() = default;
    ~ScaleInfluenceWeightsCmd() override = default;

    static void* creator();
    static MSyntax newSyntax();
    static const char* kCommandName;

    MStatus doIt(const MArgList& args) override;
    MStatus redoIt() override;
    MStatus undoIt() override;
    bool isUndoable() const override { return true; }

private:
    MStatus parseArgs(const MArgList& args);
    MStatus resolveSkinAndMesh(MObject& outSkinClusterObj, MDagPath& outMeshPath);
    MStatus findSkinClusterFromMesh(const MDagPath& meshPath, MObject& outSkinClusterObj);
    MStatus findMeshFromSkinCluster(const MObject& skinClusterObj, MDagPath& outMeshPath);
    MStatus buildEditableComponent(const MDagPath& meshPath, MObject& outComponent, MIntArray& outVertexIds);
    MStatus collectInfluenceIndices(const MObject& skinClusterObj,
                                    const MStringArray& names,
                                    MIntArray& outPhysicalIndices);
    MStatus applyWeights(const MDoubleArray& weights);

    bool isLockedInfluence(unsigned int inf) const;
    bool isSelectedInfluence(unsigned int inf) const;

private:
    MString m_skinClusterName;
    MString m_geometryName;
    MStringArray m_influenceNames;
    MStringArray m_lockedInfluenceNames;

    bool m_keepOnes = false; // accepted for UI parity, unused in scale solver
    bool m_selectionOnly = false;
    int m_iterations = 1;
    double m_strength = 0.2;
    double m_scaleFactor = 1.25;

    MObject m_skinObj;
    MDagPath m_meshPath;
    MObject m_editableComponent;

    unsigned int m_influenceCount = 0;
    MIntArray m_selectedInfluenceIndices;
    MIntArray m_lockedInfluenceIndices;
    MIntArray m_editableVerts;

    MDoubleArray m_originalEditableWeights;
    MDoubleArray m_newEditableWeights;
};
