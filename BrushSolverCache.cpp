#include "BrushSolverCache.h"

#include <maya/MFnMesh.h>
#include <maya/MFnSkinCluster.h>
#include <maya/MFnSingleIndexedComponent.h>
#include <maya/MFnDependencyNode.h>
#include <maya/MItMeshVertex.h>
#include <maya/MDagPathArray.h>

#include <queue>
#include <algorithm>
#include <chrono>

#if defined(SIW_USE_TBB)
#include <tbb/parallel_for.h>
#include <tbb/blocked_range.h>
#endif

namespace
{
    double clamp01_cache(double v)
    {
        if (v < 0.0) return 0.0;
        if (v > 1.0) return 1.0;
        return v;
    }
}

BrushSolverCache::BrushSolverCache()
    : m_valid(false)
    , m_keepOnes(false)
    , m_strength(0.2)
    , m_iterations(1)
    , m_bleedDistance(2)
    , m_bleedFalloff(1.0)
    , m_vertexCount(0)
    , m_influenceCount(0)
    , m_weightsDirty(true)
{
}

BrushSolverCache::~BrushSolverCache()
{
}

void BrushSolverCache::invalidate()
{
    std::lock_guard<std::mutex> lock(m_mutex);
    m_valid = false;
    m_weightsDirty = true;
    m_meshPath = MDagPath();
    m_skinObj = MObject::kNullObj;
    m_allVertexComponent = MObject::kNullObj;
    m_allMeshNeighbors.clear();
    m_vertexToLocal.clear();
    m_allVerts.clear();
    m_currentWeights.clear();
    m_solvedWeights.clear();
    m_keepOneLockedInfluencePerVertex.clear();
    m_selectedInfluenceIndices.clear();
    m_allInfluenceIndices.clear();
    m_vertexCount = 0;
    m_influenceCount = 0;
}

void BrushSolverCache::invalidateTopology()
{
    std::lock_guard<std::mutex> lock(m_mutex);
    m_valid = false;
    m_weightsDirty = true;
}

void BrushSolverCache::invalidateWeightsOnly()
{
    std::lock_guard<std::mutex> lock(m_mutex);
    m_weightsDirty = true;
}


void BrushSolverCache::configure(const MDagPath& meshPath,
                                 const MObject& skinObj,
                                 bool keepOnes,
                                 double strength,
                                 int iterations,
                                 int bleedDistance,
                                 double bleedFalloff,
                                 const MStringArray& influenceNames)
{
    std::lock_guard<std::mutex> lock(m_mutex);

    const bool meshChanged = (!m_valid) ||
                             (m_meshPath.fullPathName() != meshPath.fullPathName()) ||
                             (m_skinObj != skinObj);

    m_meshPath = meshPath;
    m_skinObj = skinObj;
    m_keepOnes = keepOnes;
    m_strength = clamp01_cache(strength);
    m_iterations = std::max(1, iterations);
    m_bleedDistance = std::max(1, bleedDistance);
    m_bleedFalloff = std::max(0.0, bleedFalloff);
    m_influenceNames = influenceNames;

    if (meshChanged)
    {
        m_valid = false;
        m_weightsDirty = true;
    }

    m_meshPathKey = m_meshPath.fullPathName().asChar();
}

MStatus BrushSolverCache::buildAllVertexComponent()
{
    MStatus status;
    MFnMesh meshFn(m_meshPath, &status);
    if (status != MS::kSuccess)
        return status;

    m_vertexCount = meshFn.numVertices(&status);
    if (status != MS::kSuccess)
        return status;

    m_allVerts.setLength(m_vertexCount);
    m_vertexToLocal.assign(m_vertexCount, -1);

    for (unsigned int i = 0; i < m_vertexCount; ++i)
    {
        m_allVerts[i] = static_cast<int>(i);
        m_vertexToLocal[i] = static_cast<int>(i);
    }

    MFnSingleIndexedComponent compFn;
    m_allVertexComponent = compFn.create(MFn::kMeshVertComponent, &status);
    if (status != MS::kSuccess)
        return status;

    return compFn.addElements(m_allVerts);
}

MStatus BrushSolverCache::cacheAdjacency()
{
    MStatus status;
    m_allMeshNeighbors.clear();
    m_allMeshNeighbors.resize(m_vertexCount);

    for (unsigned int v = 0; v < m_vertexCount; ++v)
    {
        MFnSingleIndexedComponent compFn;
        MObject comp = compFn.create(MFn::kMeshVertComponent, &status);
        if (status != MS::kSuccess)
            return status;

        status = compFn.addElement(static_cast<int>(v));
        if (status != MS::kSuccess)
            return status;

        MItMeshVertex it(m_meshPath, comp, &status);
        if (status != MS::kSuccess || it.isDone())
            return MS::kFailure;

        status = it.getConnectedVertices(m_allMeshNeighbors[v]);
        if (status != MS::kSuccess)
            return status;
    }

    return MS::kSuccess;
}

MStatus BrushSolverCache::cacheInfluenceMap()
{
    MStatus status;
    MFnSkinCluster skinFn(m_skinObj, &status);
    if (status != MS::kSuccess)
        return status;

    MDagPathArray infPaths;
    const unsigned int infCount = skinFn.influenceObjects(infPaths, &status);
    if (status != MS::kSuccess)
        return status;

    m_influenceCount = infCount;
    m_allInfluenceIndices.setLength(m_influenceCount);
    for (unsigned int i = 0; i < m_influenceCount; ++i)
        m_allInfluenceIndices[i] = static_cast<int>(i);

    m_selectedInfluenceIndices.clear();
    if (m_influenceNames.length() == 0)
    {
        m_selectedInfluenceIndices = m_allInfluenceIndices;
        return MS::kSuccess;
    }

    for (unsigned int i = 0; i < m_influenceNames.length(); ++i)
    {
        const MString wanted = m_influenceNames[i];
        for (unsigned int j = 0; j < infPaths.length(); ++j)
        {
            const MString fullName = infPaths[j].fullPathName();
            const MString partialName = infPaths[j].partialPathName();
            const MString shortName = MFnDependencyNode(infPaths[j].node()).name();
            if (wanted == fullName || wanted == partialName || wanted == shortName)
            {
                m_selectedInfluenceIndices.append(static_cast<int>(j));
                break;
            }
        }
    }

    if (m_selectedInfluenceIndices.length() == 0)
        m_selectedInfluenceIndices = m_allInfluenceIndices;

    return MS::kSuccess;
}

MStatus BrushSolverCache::readCurrentWeightsFromSkin()
{
    MStatus status;
    MFnSkinCluster skinFn(m_skinObj, &status);
    if (status != MS::kSuccess)
        return status;

    status = skinFn.getWeights(m_meshPath, m_allVertexComponent, m_currentWeights, m_influenceCount);
    return status;
}

MStatus BrushSolverCache::initializeIfNeeded()
{
    const auto t0 = std::chrono::high_resolution_clock::now();

    if (!m_valid)
    {
        MStatus status = buildAllVertexComponent();
        if (status != MS::kSuccess)
            return status;

        status = cacheAdjacency();
        if (status != MS::kSuccess)
            return status;

        status = cacheInfluenceMap();
        if (status != MS::kSuccess)
            return status;

        m_solvedWeights.clear();
        m_keepOneLockedInfluencePerVertex.assign(m_vertexCount, -1);
        m_valid = true;
    }

    const auto t1 = std::chrono::high_resolution_clock::now();
    m_profile.lastInitMs = std::chrono::duration<double, std::milli>(t1 - t0).count();

    if (m_weightsDirty || m_currentWeights.length() == 0)
    {
        const auto tr0 = std::chrono::high_resolution_clock::now();
        MStatus status = readCurrentWeightsFromSkin();
        if (status != MS::kSuccess)
            return status;
        const auto tr1 = std::chrono::high_resolution_clock::now();
        m_profile.lastReadWeightsMs = std::chrono::duration<double, std::milli>(tr1 - tr0).count();

        m_solvedWeights = m_currentWeights;
        m_weightsDirty = false;
    }
    else
    {
        m_profile.lastReadWeightsMs = 0.0;
    }

    return MS::kSuccess;
}

bool BrushSolverCache::influenceIsInSmoothSet(unsigned int influenceIndex) const
{
    if (m_selectedInfluenceIndices.length() == 0)
        return true;

    for (unsigned int i = 0; i < m_selectedInfluenceIndices.length(); ++i)
    {
        if (static_cast<unsigned int>(m_selectedInfluenceIndices[i]) == influenceIndex)
            return true;
    }
    return false;
}

void BrushSolverCache::buildKeepOneSeeds(const MDoubleArray& sourceWeights)
{
    m_keepOneLockedInfluencePerVertex.assign(m_vertexCount, -1);
    if (!m_keepOnes || m_influenceCount == 0)
        return;

    for (unsigned int v = 0; v < m_vertexCount; ++v)
    {
        const unsigned int base = v * m_influenceCount;
        double rowSum = 0.0;
        double maxVal = -1.0;
        int maxInf = -1;

        for (unsigned int inf = 0; inf < m_influenceCount; ++inf)
        {
            const double w = std::max(0.0, sourceWeights[base + inf]);
            rowSum += w;
            if (w > maxVal)
            {
                maxVal = w;
                maxInf = static_cast<int>(inf);
            }
        }

        if (rowSum <= 1.0e-8 || maxInf < 0)
            continue;

        const double normalizedDominance = maxVal / rowSum;
        if (normalizedDominance >= 0.999 && influenceIsInSmoothSet(static_cast<unsigned int>(maxInf)))
            m_keepOneLockedInfluencePerVertex[v] = maxInf;
    }
}

void BrushSolverCache::enforceKeepOneSeeds(MDoubleArray& weights) const
{
    if (!m_keepOnes)
        return;

    for (unsigned int v = 0; v < m_vertexCount; ++v)
    {
        const int lockedInf = m_keepOneLockedInfluencePerVertex[v];
        if (lockedInf < 0)
            continue;

        const unsigned int base = v * m_influenceCount;
        for (unsigned int inf = 0; inf < m_influenceCount; ++inf)
            weights[base + inf] = (static_cast<int>(inf) == lockedInf) ? 1.0 : 0.0;
    }
}

void BrushSolverCache::promoteNewKeepOneSeeds(MDoubleArray& weights)
{
    if (!m_keepOnes)
        return;

    for (unsigned int v = 0; v < m_vertexCount; ++v)
    {
        if (m_keepOneLockedInfluencePerVertex[v] >= 0)
            continue;

        const unsigned int base = v * m_influenceCount;
        double rowSum = 0.0;
        double maxVal = -1.0;
        int maxInf = -1;

        for (unsigned int inf = 0; inf < m_influenceCount; ++inf)
        {
            const double w = std::max(0.0, weights[base + inf]);
            rowSum += w;
            if (w > maxVal)
            {
                maxVal = w;
                maxInf = static_cast<int>(inf);
            }
        }

        if (rowSum <= 1.0e-8 || maxInf < 0)
            continue;

        if ((maxVal / rowSum) >= 0.999 && influenceIsInSmoothSet(static_cast<unsigned int>(maxInf)))
        {
            m_keepOneLockedInfluencePerVertex[v] = maxInf;
            for (unsigned int inf = 0; inf < m_influenceCount; ++inf)
                weights[base + inf] = (static_cast<int>(inf) == maxInf) ? 1.0 : 0.0;
        }
    }
}

double BrushSolverCache::distanceWeight(int distance) const
{
    if (distance < 1 || distance > m_bleedDistance || m_bleedDistance < 1)
        return 0.0;

    if (m_bleedFalloff <= 0.0)
        return 1.0;

    const double t = static_cast<double>(distance - 1) / static_cast<double>(m_bleedDistance);
    return std::max(0.0, 1.0 - (t * m_bleedFalloff));
}

void BrushSolverCache::gatherSeedCandidates(int editableLocal, std::vector<int>& outInfluences, std::vector<double>& outAmounts) const
{
    outInfluences.clear();
    outAmounts.clear();
    if (editableLocal < 0 || editableLocal >= static_cast<int>(m_vertexCount))
        return;

    std::queue<int> frontier;
    std::vector<int> distances(m_vertexCount, -1);
    frontier.push(editableLocal);
    distances[editableLocal] = 0;

    std::vector<double> totalWeightPerInfluence(static_cast<size_t>(m_influenceCount), 0.0);

    while (!frontier.empty())
    {
        const int vtx = frontier.front();
        frontier.pop();
        const int dist = distances[vtx];
        if (dist >= m_bleedDistance)
            continue;

        const MIntArray& nbrs = m_allMeshNeighbors[static_cast<size_t>(vtx)];
        for (unsigned int i = 0; i < nbrs.length(); ++i)
        {
            const int nbr = nbrs[i];
            if (nbr < 0 || nbr >= static_cast<int>(m_vertexCount))
                continue;
            if (distances[nbr] >= 0)
                continue;

            const int nextDist = dist + 1;
            distances[nbr] = nextDist;
            frontier.push(nbr);

            if (nextDist > m_bleedDistance)
                continue;

            const int seedInf = m_keepOneLockedInfluencePerVertex[static_cast<size_t>(nbr)];
            if (seedInf < 0)
                continue;
            if (!influenceIsInSmoothSet(static_cast<unsigned int>(seedInf)))
                continue;

            const double w = distanceWeight(nextDist);
            if (w <= 0.0)
                continue;

            totalWeightPerInfluence[static_cast<size_t>(seedInf)] += w;
        }
    }

    for (unsigned int inf = 0; inf < m_influenceCount; ++inf)
    {
        if (totalWeightPerInfluence[inf] > 0.0)
        {
            outInfluences.push_back(static_cast<int>(inf));
            outAmounts.push_back(totalWeightPerInfluence[inf]);
        }
    }
}

void BrushSolverCache::applyTransferToRow(MDoubleArray& weights, unsigned int rowBase, unsigned int bleedInf, double amount) const
{
    amount = clamp01_cache(amount);
    const double oldBleed = clamp01_cache(weights[rowBase + bleedInf]);
    const double newBleed = std::min(1.0, oldBleed + amount);
    const double oldOthersTotal = std::max(0.0, 1.0 - oldBleed);
    const double newOthersTotal = std::max(0.0, 1.0 - newBleed);

    weights[rowBase + bleedInf] = newBleed;

    if (oldOthersTotal > 1.0e-8)
    {
        const double scale = newOthersTotal / oldOthersTotal;
        for (unsigned int inf = 0; inf < m_influenceCount; ++inf)
        {
            if (inf == bleedInf)
                continue;
            weights[rowBase + inf] = std::max(0.0, weights[rowBase + inf] * scale);
        }
    }
    else
    {
        for (unsigned int inf = 0; inf < m_influenceCount; ++inf)
        {
            if (inf == bleedInf)
                continue;
            weights[rowBase + inf] = 0.0;
        }
    }
}

void BrushSolverCache::applyRegularSmoothIteration(const MDoubleArray& currentWeights, MDoubleArray& nextWeights, bool smoothAll) const
{
    nextWeights = currentWeights;

    for (unsigned int v = 0; v < m_vertexCount; ++v)
    {
        const MIntArray& nbrs = m_allMeshNeighbors[static_cast<size_t>(v)];
        if (nbrs.length() == 0)
            continue;

        const unsigned int rowBase = v * m_influenceCount;
        std::vector<double> average(static_cast<size_t>(m_influenceCount), 0.0);
        unsigned int validCount = 0;

        for (unsigned int n = 0; n < nbrs.length(); ++n)
        {
            const int nbr = nbrs[n];
            if (nbr < 0 || nbr >= static_cast<int>(m_vertexCount))
                continue;

            const unsigned int nbrBase = static_cast<unsigned int>(nbr) * m_influenceCount;
            for (unsigned int inf = 0; inf < m_influenceCount; ++inf)
                average[inf] += currentWeights[nbrBase + inf];
            ++validCount;
        }

        if (validCount == 0)
            continue;

        for (unsigned int inf = 0; inf < m_influenceCount; ++inf)
            average[inf] /= static_cast<double>(validCount);

        if (smoothAll)
        {
            for (unsigned int inf = 0; inf < m_influenceCount; ++inf)
                nextWeights[rowBase + inf] = ((1.0 - m_strength) * currentWeights[rowBase + inf]) + (m_strength * average[inf]);
        }
        else
        {
            double targetSelected = 0.0;
            double currentSelected = 0.0;
            for (unsigned int i = 0; i < m_selectedInfluenceIndices.length(); ++i)
            {
                const unsigned int inf = static_cast<unsigned int>(m_selectedInfluenceIndices[i]);
                targetSelected += average[inf];
                currentSelected += currentWeights[rowBase + inf];
            }

            const double newSelected = ((1.0 - m_strength) * currentSelected) + (m_strength * targetSelected);

            if (currentSelected > 1.0e-8)
            {
                const double scaleSelected = newSelected / currentSelected;
                for (unsigned int i = 0; i < m_selectedInfluenceIndices.length(); ++i)
                {
                    const unsigned int inf = static_cast<unsigned int>(m_selectedInfluenceIndices[i]);
                    nextWeights[rowBase + inf] = currentWeights[rowBase + inf] * scaleSelected;
                }
            }

            const double currentOther = std::max(0.0, 1.0 - currentSelected);
            const double newOther = std::max(0.0, 1.0 - newSelected);
            if (currentOther > 1.0e-8)
            {
                const double scaleOther = newOther / currentOther;
                for (unsigned int inf = 0; inf < m_influenceCount; ++inf)
                {
                    if (influenceIsInSmoothSet(inf))
                        continue;
                    nextWeights[rowBase + inf] = currentWeights[rowBase + inf] * scaleOther;
                }
            }
        }

        double rowSum = 0.0;
        for (unsigned int inf = 0; inf < m_influenceCount; ++inf)
        {
            nextWeights[rowBase + inf] = std::max(0.0, nextWeights[rowBase + inf]);
            rowSum += nextWeights[rowBase + inf];
        }
        if (rowSum > 1.0e-8)
        {
            for (unsigned int inf = 0; inf < m_influenceCount; ++inf)
                nextWeights[rowBase + inf] /= rowSum;
        }
    }
}

void BrushSolverCache::applyRegularSmoothIterationParallel(const MDoubleArray& currentWeights, MDoubleArray& nextWeights, bool smoothAll) const
{
    nextWeights = currentWeights;

    const unsigned int workerCountRaw = std::thread::hardware_concurrency();
    const unsigned int workerCount = std::max(1u, std::min(workerCountRaw == 0 ? 4u : workerCountRaw, 16u));
    const unsigned int vertexCount = m_vertexCount;

    auto worker = [&](unsigned int begin, unsigned int end)
    {
        for (unsigned int v = begin; v < end; ++v)
        {
            const MIntArray& nbrs = m_allMeshNeighbors[static_cast<size_t>(v)];
            if (nbrs.length() == 0)
                continue;

            const unsigned int rowBase = v * m_influenceCount;
            std::vector<double> average(static_cast<size_t>(m_influenceCount), 0.0);
            unsigned int validCount = 0;

            for (unsigned int n = 0; n < nbrs.length(); ++n)
            {
                const int nbr = nbrs[n];
                if (nbr < 0 || nbr >= static_cast<int>(m_vertexCount))
                    continue;

                const unsigned int nbrBase = static_cast<unsigned int>(nbr) * m_influenceCount;
                for (unsigned int inf = 0; inf < m_influenceCount; ++inf)
                    average[inf] += currentWeights[nbrBase + inf];
                ++validCount;
            }

            if (validCount == 0)
                continue;

            for (unsigned int inf = 0; inf < m_influenceCount; ++inf)
                average[inf] /= static_cast<double>(validCount);

            if (smoothAll)
            {
                for (unsigned int inf = 0; inf < m_influenceCount; ++inf)
                    nextWeights[rowBase + inf] = ((1.0 - m_strength) * currentWeights[rowBase + inf]) + (m_strength * average[inf]);
            }
            else
            {
                double targetSelected = 0.0;
                double currentSelected = 0.0;
                for (unsigned int i = 0; i < m_selectedInfluenceIndices.length(); ++i)
                {
                    const unsigned int inf = static_cast<unsigned int>(m_selectedInfluenceIndices[i]);
                    targetSelected += average[inf];
                    currentSelected += currentWeights[rowBase + inf];
                }

                const double newSelected = ((1.0 - m_strength) * currentSelected) + (m_strength * targetSelected);

                if (currentSelected > 1.0e-8)
                {
                    const double scaleSelected = newSelected / currentSelected;
                    for (unsigned int i = 0; i < m_selectedInfluenceIndices.length(); ++i)
                    {
                        const unsigned int inf = static_cast<unsigned int>(m_selectedInfluenceIndices[i]);
                        nextWeights[rowBase + inf] = currentWeights[rowBase + inf] * scaleSelected;
                    }
                }

                const double currentOther = std::max(0.0, 1.0 - currentSelected);
                const double newOther = std::max(0.0, 1.0 - newSelected);
                if (currentOther > 1.0e-8)
                {
                    const double scaleOther = newOther / currentOther;
                    for (unsigned int inf = 0; inf < m_influenceCount; ++inf)
                    {
                        if (influenceIsInSmoothSet(inf))
                            continue;
                        nextWeights[rowBase + inf] = currentWeights[rowBase + inf] * scaleOther;
                    }
                }
            }

            double rowSum = 0.0;
            for (unsigned int inf = 0; inf < m_influenceCount; ++inf)
            {
                nextWeights[rowBase + inf] = std::max(0.0, nextWeights[rowBase + inf]);
                rowSum += nextWeights[rowBase + inf];
            }
            if (rowSum > 1.0e-8)
            {
                for (unsigned int inf = 0; inf < m_influenceCount; ++inf)
                    nextWeights[rowBase + inf] /= rowSum;
            }
        }
    };

    if (workerCount <= 1 || vertexCount < 512)
    {
        worker(0, vertexCount);
        return;
    }

    std::vector<std::thread> threads;
    threads.reserve(workerCount);

    const unsigned int chunk = (vertexCount + workerCount - 1) / workerCount;
    for (unsigned int t = 0; t < workerCount; ++t)
    {
        const unsigned int begin = t * chunk;
        const unsigned int end = std::min(vertexCount, begin + chunk);
        if (begin >= end)
            break;
        threads.emplace_back(worker, begin, end);
    }

    for (size_t i = 0; i < threads.size(); ++i)
        threads[i].join();
}


void BrushSolverCache::applyRegularSmoothIterationTBB(const MDoubleArray& currentWeights, MDoubleArray& nextWeights, bool smoothAll) const
{
#if defined(SIW_USE_TBB)
    nextWeights = currentWeights;

    tbb::parallel_for(tbb::blocked_range<unsigned int>(0, m_vertexCount),
        [&](const tbb::blocked_range<unsigned int>& range)
        {
            for (unsigned int v = range.begin(); v < range.end(); ++v)
            {
                const MIntArray& nbrs = m_allMeshNeighbors[static_cast<size_t>(v)];
                if (nbrs.length() == 0)
                    continue;

                const unsigned int rowBase = v * m_influenceCount;
                std::vector<double> average(static_cast<size_t>(m_influenceCount), 0.0);
                unsigned int validCount = 0;

                for (unsigned int n = 0; n < nbrs.length(); ++n)
                {
                    const int nbr = nbrs[n];
                    if (nbr < 0 || nbr >= static_cast<int>(m_vertexCount))
                        continue;

                    const unsigned int nbrBase = static_cast<unsigned int>(nbr) * m_influenceCount;
                    for (unsigned int inf = 0; inf < m_influenceCount; ++inf)
                        average[inf] += currentWeights[nbrBase + inf];
                    ++validCount;
                }

                if (validCount == 0)
                    continue;

                for (unsigned int inf = 0; inf < m_influenceCount; ++inf)
                    average[inf] /= static_cast<double>(validCount);

                if (smoothAll)
                {
                    for (unsigned int inf = 0; inf < m_influenceCount; ++inf)
                        nextWeights[rowBase + inf] = ((1.0 - m_strength) * currentWeights[rowBase + inf]) + (m_strength * average[inf]);
                }
                else
                {
                    double targetSelected = 0.0;
                    double currentSelected = 0.0;
                    for (unsigned int i = 0; i < m_selectedInfluenceIndices.length(); ++i)
                    {
                        const unsigned int inf = static_cast<unsigned int>(m_selectedInfluenceIndices[i]);
                        targetSelected += average[inf];
                        currentSelected += currentWeights[rowBase + inf];
                    }

                    const double newSelected = ((1.0 - m_strength) * currentSelected) + (m_strength * targetSelected);

                    if (currentSelected > 1.0e-8)
                    {
                        const double scaleSelected = newSelected / currentSelected;
                        for (unsigned int i = 0; i < m_selectedInfluenceIndices.length(); ++i)
                        {
                            const unsigned int inf = static_cast<unsigned int>(m_selectedInfluenceIndices[i]);
                            nextWeights[rowBase + inf] = currentWeights[rowBase + inf] * scaleSelected;
                        }
                    }

                    const double currentOther = std::max(0.0, 1.0 - currentSelected);
                    const double newOther = std::max(0.0, 1.0 - newSelected);
                    if (currentOther > 1.0e-8)
                    {
                        const double scaleOther = newOther / currentOther;
                        for (unsigned int inf = 0; inf < m_influenceCount; ++inf)
                        {
                            if (influenceIsInSmoothSet(inf))
                                continue;
                            nextWeights[rowBase + inf] = currentWeights[rowBase + inf] * scaleOther;
                        }
                    }
                }

                double rowSum = 0.0;
                for (unsigned int inf = 0; inf < m_influenceCount; ++inf)
                {
                    nextWeights[rowBase + inf] = std::max(0.0, nextWeights[rowBase + inf]);
                    rowSum += nextWeights[rowBase + inf];
                }
                if (rowSum > 1.0e-8)
                {
                    for (unsigned int inf = 0; inf < m_influenceCount; ++inf)
                        nextWeights[rowBase + inf] /= rowSum;
                }
            }
        });
#else
    applyRegularSmoothIterationParallel(currentWeights, nextWeights, smoothAll);
#endif
}

void BrushSolverCache::solveIterationAuto(const MDoubleArray& currentWeights, MDoubleArray& nextWeights, bool smoothAll) const
{
#if defined(SIW_USE_TBB)
    applyRegularSmoothIterationTBB(currentWeights, nextWeights, smoothAll);
#else
    applyRegularSmoothIterationParallel(currentWeights, nextWeights, smoothAll);
#endif
}

MStatus BrushSolverCache::writeSubsetWeights(const MIntArray& brushVerts)
{
    MStatus status;
    MFnSingleIndexedComponent compFn;
    MObject comp = compFn.create(MFn::kMeshVertComponent, &status);
    if (status != MS::kSuccess)
        return status;

    MIntArray brushVertsCopy(brushVerts);
    status = compFn.addElements(brushVertsCopy);
    if (status != MS::kSuccess)
        return status;

    MDoubleArray subsetWeights;
    subsetWeights.setLength(static_cast<unsigned int>(brushVerts.length() * m_influenceCount));

    for (unsigned int i = 0; i < brushVerts.length(); ++i)
    {
        const int vtx = brushVerts[i];
        if (vtx < 0 || vtx >= static_cast<int>(m_vertexCount))
            continue;

        const unsigned int srcBase = static_cast<unsigned int>(vtx) * m_influenceCount;
        const unsigned int dstBase = i * m_influenceCount;
        for (unsigned int inf = 0; inf < m_influenceCount; ++inf)
            subsetWeights[dstBase + inf] = m_currentWeights[srcBase + inf];
    }

    MFnSkinCluster skinFn(m_skinObj, &status);
    if (status != MS::kSuccess)
        return status;

    return skinFn.setWeights(m_meshPath, comp, m_allInfluenceIndices, subsetWeights, false, nullptr);
}

MStatus BrushSolverCache::solveAndApplyToBrush(const MIntArray& brushVerts, const MDoubleArray& brushOpacities)
{
    std::lock_guard<std::mutex> lock(m_mutex);

    const auto t0 = std::chrono::high_resolution_clock::now();

    MStatus status = initializeIfNeeded();
    if (status != MS::kSuccess)
        return status;

    const bool smoothAll = (m_selectedInfluenceIndices.length() == 0 || m_selectedInfluenceIndices.length() == m_influenceCount);

    MDoubleArray solved = m_currentWeights;

    const auto ts0 = std::chrono::high_resolution_clock::now();

    if (!m_keepOnes)
    {
        for (int iter = 0; iter < m_iterations; ++iter)
        {
            MDoubleArray nextWeights;
            solveIterationAuto(solved, nextWeights, smoothAll);
            solved = nextWeights;
        }
    }
    else
    {
        buildKeepOneSeeds(m_currentWeights);
        enforceKeepOneSeeds(solved);

        for (int iter = 0; iter < m_iterations; ++iter)
        {
            MDoubleArray nextWeights;
            solveIterationAuto(solved, nextWeights, smoothAll);
            enforceKeepOneSeeds(nextWeights);

            for (unsigned int v = 0; v < m_vertexCount; ++v)
            {
                if (m_keepOneLockedInfluencePerVertex[v] >= 0)
                    continue;

                std::vector<int> infs;
                std::vector<double> amounts;
                gatherSeedCandidates(static_cast<int>(v), infs, amounts);
                if (infs.empty())
                    continue;

                const unsigned int rowBase = v * m_influenceCount;

                std::vector<double> add(static_cast<size_t>(m_influenceCount), 0.0);
                double totalAdd = 0.0;

                for (size_t i = 0; i < infs.size(); ++i)
                {
                    const unsigned int inf = static_cast<unsigned int>(infs[i]);
                    if (!influenceIsInSmoothSet(inf))
                        continue;

                    const double amount = std::max(0.0, m_strength * amounts[i]);
                    if (amount <= 0.0)
                        continue;

                    add[inf] += amount;
                    totalAdd += amount;
                }

                if (totalAdd <= 1.0e-8)
                    continue;

                for (unsigned int inf = 0; inf < m_influenceCount; ++inf)
                {
                    nextWeights[rowBase + inf] =
                        std::max(0.0, nextWeights[rowBase + inf] + add[inf]);
                }

                double rowSum = 0.0;
                for (unsigned int inf = 0; inf < m_influenceCount; ++inf)
                {
                    rowSum += nextWeights[rowBase + inf];
                }

                if (rowSum > 1.0e-8)
                {
                    for (unsigned int inf = 0; inf < m_influenceCount; ++inf)
                    {
                        nextWeights[rowBase + inf] /= rowSum;
                    }
                }
            }

            enforceKeepOneSeeds(nextWeights);
            promoteNewKeepOneSeeds(nextWeights);
            enforceKeepOneSeeds(nextWeights);
            solved = nextWeights;
        }
    }

    const auto ts1 = std::chrono::high_resolution_clock::now();
    m_profile.lastSolveMs = std::chrono::duration<double, std::milli>(ts1 - ts0).count();

    for (unsigned int i = 0; i < brushVerts.length(); ++i)
    {
        const int vtx = brushVerts[i];
        if (vtx < 0 || vtx >= static_cast<int>(m_vertexCount))
            continue;

        double alpha = 1.0;
        if (i < brushOpacities.length())
            alpha = brushOpacities[i];

        alpha = std::max(0.0, std::min(1.0, alpha));
        if (alpha <= 1.0e-8)
            continue;

        const unsigned int base = static_cast<unsigned int>(vtx) * m_influenceCount;
        for (unsigned int inf = 0; inf < m_influenceCount; ++inf)
        {
            const double src = m_currentWeights[base + inf];
            const double dst = solved[base + inf];
            m_currentWeights[base + inf] = ((1.0 - alpha) * src) + (alpha * dst);
        }
    }

    const auto tw0 = std::chrono::high_resolution_clock::now();
    status = writeSubsetWeights(brushVerts);
    if (status != MS::kSuccess)
        return status;
    const auto tw1 = std::chrono::high_resolution_clock::now();

    m_profile.lastWriteMs = std::chrono::duration<double, std::milli>(tw1 - tw0).count();
    m_profile.stampCount += 1;
    m_profile.lastTotalMs = std::chrono::duration<double, std::milli>(tw1 - t0).count();

    m_weightsDirty = true;
    return MS::kSuccess;
}
