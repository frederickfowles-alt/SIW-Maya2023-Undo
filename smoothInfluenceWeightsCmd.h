#pragma once

#include <maya/MPxCommand.h>
#include <maya/MSyntax.h>
#include <maya/MString.h>
#include <maya/MStringArray.h>
#include <maya/MObject.h>
#include <maya/MDagPath.h>
#include <maya/MIntArray.h>
#include <maya/MDoubleArray.h>
#include <maya/MPointArray.h>

#include <set>
#include <vector>

class SmoothInfluenceWeightsCmd : public MPxCommand
{
public:
    SmoothInfluenceWeightsCmd() = default;
    ~SmoothInfluenceWeightsCmd() override = default;

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
    MStatus collectInfluenceIndices(const MObject& skinClusterObj, MIntArray& outPhysicalIndices);
    MStatus cacheAllMeshNeighbors(const MDagPath& meshPath);
    MStatus applyWeights(const MDoubleArray& weights);

    bool isEditableVertex(int vtxId) const;
    bool isBorderVertex(int vtxId) const;
    bool influenceIsInSmoothSet(unsigned int influenceIndex) const;

    struct BleedCandidate
    {
        int influenceIndex;
        int minDistance;
        double totalWeight;
    };

    void buildKeepOneSeeds();
    void enforceKeepOneSeeds(MDoubleArray& weights) const;
    void promoteNewKeepOneSeeds(MDoubleArray& weights);
    double distanceWeight(int distance) const;
    void gatherSeedCandidates(int editableLocal, std::vector<BleedCandidate>& outCandidates) const;
    void applyTransferToRow(MDoubleArray& weights, unsigned int rowBase, unsigned int bleedInf, double amount) const;

    MStatus redoRegularSmooth();
    MStatus redoBleedSmooth();
    void applyRegularSmoothIteration(const MDoubleArray& currentWeights, MDoubleArray& nextWeights, bool smoothAll) const;

private:
    MString m_skinClusterName;
    MString m_geometryName;
    MStringArray m_influenceNames;

    bool m_keepOnes = false;
    bool m_selectionOnly = false;
    bool m_borderPin = false;

    int m_iterations = 1;
    int m_bleedDistance = 2;
    double m_bleedFalloff = 1.0;
    double m_strength = 0.2;
    double m_keepOneThreshold = 0.999;

    MObject m_skinObj;
    MDagPath m_meshPath;
    MObject m_editableComponent;

    MIntArray m_selectedInfluenceIndices;
    unsigned int m_influenceCount = 0;

    MIntArray m_editableVerts;
    std::set<int> m_editableVertSet;
    std::set<int> m_borderVertSet;
    std::vector<int> m_vertexToEditableLocal;
    std::vector<MIntArray> m_allMeshNeighbors;

    MDoubleArray m_originalEditableWeights;
    MDoubleArray m_newEditableWeights;

    std::vector<int> m_keepOneLockedInfluencePerVertex;
};
