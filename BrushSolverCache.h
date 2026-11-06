#pragma once

#include <maya/MStatus.h>
#include <maya/MObject.h>
#include <maya/MDagPath.h>
#include <maya/MIntArray.h>
#include <maya/MDoubleArray.h>
#include <maya/MStringArray.h>

#include <vector>
#include <set>
#include <mutex>
#include <thread>
#include <string>
#include <cstdint>

class BrushSolverCache
{
public:
    BrushSolverCache();
    ~BrushSolverCache();

    void invalidate();
    void invalidateTopology();
    void invalidateWeightsOnly();

    void configure(const MDagPath& meshPath,
                   const MObject& skinObj,
                   bool keepOnes,
                   double strength,
                   int iterations,
                   int bleedDistance,
                   double bleedFalloff,
                   const MStringArray& influenceNames);

    MStatus solveAndApplyToBrush(const MIntArray& brushVerts, const MDoubleArray& brushOpacities);

public:
    struct ProfileStats
    {
        uint64_t stampCount = 0;
        double lastInitMs = 0.0;
        double lastReadWeightsMs = 0.0;
        double lastSolveMs = 0.0;
        double lastWriteMs = 0.0;
        double lastTotalMs = 0.0;
    };

    const ProfileStats& profileStats() const { return m_profile; }

private:
    MStatus initializeIfNeeded();
    MStatus buildAllVertexComponent();
    MStatus cacheAdjacency();
    MStatus cacheInfluenceMap();
    MStatus readCurrentWeightsFromSkin();
    MStatus writeSubsetWeights(const MIntArray& brushVerts);

    bool influenceIsInSmoothSet(unsigned int influenceIndex) const;
    void buildKeepOneSeeds(const MDoubleArray& sourceWeights);
    void enforceKeepOneSeeds(MDoubleArray& weights) const;
    void promoteNewKeepOneSeeds(MDoubleArray& weights);
    void applyRegularSmoothIteration(const MDoubleArray& currentWeights, MDoubleArray& nextWeights, bool smoothAll) const;
    void applyRegularSmoothIterationParallel(const MDoubleArray& currentWeights, MDoubleArray& nextWeights, bool smoothAll) const;
    void applyRegularSmoothIterationTBB(const MDoubleArray& currentWeights, MDoubleArray& nextWeights, bool smoothAll) const;
    void solveIterationAuto(const MDoubleArray& currentWeights, MDoubleArray& nextWeights, bool smoothAll) const;
    void gatherSeedCandidates(int editableLocal, std::vector<int>& outInfluences, std::vector<double>& outAmounts) const;
    double distanceWeight(int distance) const;
    void applyTransferToRow(MDoubleArray& weights, unsigned int rowBase, unsigned int bleedInf, double amount) const;

private:
    mutable std::mutex m_mutex;

    MDagPath m_meshPath;
    MObject m_skinObj;
    MObject m_allVertexComponent;

    bool m_valid;
    bool m_keepOnes;
    double m_strength;
    int m_iterations;
    int m_bleedDistance;
    double m_bleedFalloff;
    MStringArray m_influenceNames;

    unsigned int m_vertexCount;
    unsigned int m_influenceCount;

    MIntArray m_allInfluenceIndices;
    MIntArray m_selectedInfluenceIndices;

    std::vector<MIntArray> m_allMeshNeighbors;
    std::vector<int> m_vertexToLocal;
    MIntArray m_allVerts;

    MDoubleArray m_currentWeights;
    MDoubleArray m_solvedWeights;

    std::vector<int> m_keepOneLockedInfluencePerVertex;

    bool m_weightsDirty;
    std::string m_meshPathKey;
    ProfileStats m_profile;
};
