#include "smoothInfluenceWeightsCmd.h"

#include <maya/MArgDatabase.h>
#include <maya/MArgList.h>
#include <maya/MFnDependencyNode.h>
#include <maya/MFnGeometryFilter.h>
#include <maya/MFnMesh.h>
#include <maya/MFnSingleIndexedComponent.h>
#include <maya/MFnSkinCluster.h>
#include <maya/MGlobal.h>
#include <maya/MItDependencyNodes.h>
#include <maya/MItMeshVertex.h>
#include <maya/MItSelectionList.h>
#include <maya/MObjectArray.h>
#include <maya/MSelectionList.h>
#include <maya/MMatrix.h>
#include <maya/MPoint.h>
#include <maya/MDagPathArray.h>

#include <algorithm>
#include <cctype>
#include <queue>
#include <set>
#include <string>
#include <vector>

const char* SmoothInfluenceWeightsCmd::kCommandName = "smoothInfluenceWeights";

namespace
{
    const char* kSkinFlagShort = "-sc";
    const char* kSkinFlagLong  = "-skinCluster";
    const char* kGeoFlagShort = "-g";
    const char* kGeoFlagLong  = "-geometry";
    const char* kInfFlagShort = "-inf";
    const char* kInfFlagLong  = "-influence";
    const char* kKeepOnesFlagShort = "-ko";
    const char* kKeepOnesFlagLong  = "-keepOnes";
    const char* kSelOnlyFlagShort = "-so";
    const char* kSelOnlyFlagLong  = "-selectionOnly";
    const char* kIterFlagShort = "-it";
    const char* kIterFlagLong  = "-iterations";
    const char* kStrengthFlagShort = "-str";
    const char* kStrengthFlagLong  = "-strength";
    const char* kBorderPinFlagShort = "-bp";
    const char* kBorderPinFlagLong  = "-borderPin";
    const char* kBleedDistanceFlagShort = "-bd";
    const char* kBleedDistanceFlagLong  = "-bleedDistance";
    const char* kBleedFalloffFlagShort = "-bf";
    const char* kBleedFalloffFlagLong  = "-bleedFalloff";

    double clamp01(double v)
    {
        if (v < 0.0) return 0.0;
        if (v > 1.0) return 1.0;
        return v;
    }

    MString trimMString(const MString& input)
    {
        std::string s(input.asChar());
        size_t start = 0;
        while (start < s.size() && std::isspace(static_cast<unsigned char>(s[start])) != 0)
        {
            ++start;
        }
        size_t end = s.size();
        while (end > start && std::isspace(static_cast<unsigned char>(s[end - 1])) != 0)
        {
            --end;
        }
        return MString(s.substr(start, end - start).c_str());
    }

    MStatus getShapeFromDag(const MDagPath& inPath, MDagPath& outShapePath)
    {
        if (inPath.node().hasFn(MFn::kMesh))
        {
            outShapePath = inPath;
            return MS::kSuccess;
        }
        if (!inPath.node().hasFn(MFn::kTransform))
        {
            return MS::kFailure;
        }
        for (unsigned int i = 0; i < inPath.childCount(); ++i)
        {
            MObject child = inPath.child(i);
            if (child.hasFn(MFn::kMesh))
            {
                MDagPath shapePath = inPath;
                MStatus status = shapePath.extendToShapeDirectlyBelow(i);
                if (status == MS::kSuccess)
                {
                    outShapePath = shapePath;
                    return MS::kSuccess;
                }
            }
        }
        return MS::kFailure;
    }
}

void* SmoothInfluenceWeightsCmd::creator()
{
    return new SmoothInfluenceWeightsCmd();
}

MSyntax SmoothInfluenceWeightsCmd::newSyntax()
{
    MSyntax syntax;
    syntax.addFlag(kSkinFlagShort, kSkinFlagLong, MSyntax::kString);
    syntax.addFlag(kGeoFlagShort, kGeoFlagLong, MSyntax::kString);
    syntax.addFlag(kInfFlagShort, kInfFlagLong, MSyntax::kString);
    syntax.makeFlagMultiUse(kInfFlagShort);
    syntax.addFlag(kKeepOnesFlagShort, kKeepOnesFlagLong, MSyntax::kBoolean);
    syntax.addFlag(kSelOnlyFlagShort, kSelOnlyFlagLong, MSyntax::kBoolean);
    syntax.addFlag(kIterFlagShort, kIterFlagLong, MSyntax::kLong);
    syntax.addFlag(kStrengthFlagShort, kStrengthFlagLong, MSyntax::kDouble);
    syntax.addFlag(kBorderPinFlagShort, kBorderPinFlagLong, MSyntax::kBoolean);
    syntax.addFlag(kBleedDistanceFlagShort, kBleedDistanceFlagLong, MSyntax::kLong);
    syntax.addFlag(kBleedFalloffFlagShort, kBleedFalloffFlagLong, MSyntax::kDouble);
    syntax.enableEdit(false);
    syntax.enableQuery(false);
    return syntax;
}

MStatus SmoothInfluenceWeightsCmd::parseArgs(const MArgList& args)
{
    MStatus status;
    MArgDatabase argData(newSyntax(), args, &status);
    if (status != MS::kSuccess)
    {
        return status;
    }

    m_skinClusterName.clear();
    m_geometryName.clear();
    m_influenceNames.clear();
    m_keepOnes = false;
    m_selectionOnly = false;
    m_borderPin = false;
    m_iterations = 1;
    m_bleedDistance = 2;
    m_bleedFalloff = 1.0;
    m_strength = 0.2;
    m_keepOneThreshold = 0.999;

    if (argData.isFlagSet(kSkinFlagShort)) argData.getFlagArgument(kSkinFlagShort, 0, m_skinClusterName);
    if (argData.isFlagSet(kGeoFlagShort)) argData.getFlagArgument(kGeoFlagShort, 0, m_geometryName);
    if (argData.isFlagSet(kKeepOnesFlagShort)) argData.getFlagArgument(kKeepOnesFlagShort, 0, m_keepOnes);
    if (argData.isFlagSet(kSelOnlyFlagShort)) argData.getFlagArgument(kSelOnlyFlagShort, 0, m_selectionOnly);
    if (argData.isFlagSet(kIterFlagShort)) argData.getFlagArgument(kIterFlagShort, 0, m_iterations);
    if (argData.isFlagSet(kStrengthFlagShort)) argData.getFlagArgument(kStrengthFlagShort, 0, m_strength);
    if (argData.isFlagSet(kBorderPinFlagShort)) argData.getFlagArgument(kBorderPinFlagShort, 0, m_borderPin);
    if (argData.isFlagSet(kBleedDistanceFlagShort)) argData.getFlagArgument(kBleedDistanceFlagShort, 0, m_bleedDistance);
    if (argData.isFlagSet(kBleedFalloffFlagShort)) argData.getFlagArgument(kBleedFalloffFlagShort, 0, m_bleedFalloff);

    if (m_iterations < 1) m_iterations = 1;
    if (m_bleedDistance < 1) m_bleedDistance = 1;
    if (m_bleedFalloff < 0.0) m_bleedFalloff = 0.0;
    m_strength = clamp01(m_strength);

    const unsigned int infUses = argData.numberOfFlagUses(kInfFlagShort);
    for (unsigned int i = 0; i < infUses; ++i)
    {
        MArgList oneUse;
        argData.getFlagArgumentList(kInfFlagShort, i, oneUse);
        for (unsigned int a = 0; a < oneUse.length(); ++a)
        {
            MString value = oneUse.asString(a, &status);
            if (status != MS::kSuccess)
            {
                continue;
            }
            if (value.index(',') >= 0)
            {
                MStringArray splitVals;
                value.split(',', splitVals);
                for (unsigned int s = 0; s < splitVals.length(); ++s)
                {
                    MString name = trimMString(splitVals[s]);
                    if (name.length() > 0)
                    {
                        m_influenceNames.append(name);
                    }
                }
            }
            else
            {
                MString name = trimMString(value);
                if (name.length() > 0)
                {
                    m_influenceNames.append(name);
                }
            }
        }
    }

    MStringArray uniqueNames;
    for (unsigned int i = 0; i < m_influenceNames.length(); ++i)
    {
        bool found = false;
        for (unsigned int j = 0; j < uniqueNames.length(); ++j)
        {
            if (m_influenceNames[i] == uniqueNames[j])
            {
                found = true;
                break;
            }
        }
        if (!found)
        {
            uniqueNames.append(m_influenceNames[i]);
        }
    }
    m_influenceNames = uniqueNames;

    return MS::kSuccess;
}

MStatus SmoothInfluenceWeightsCmd::findSkinClusterFromMesh(const MDagPath& meshPath, MObject& outSkinClusterObj)
{
    MStatus status;
    MDagPath targetShapePath;
    status = getShapeFromDag(meshPath, targetShapePath);
    if (status != MS::kSuccess)
    {
        targetShapePath = meshPath;
    }

    MItDependencyNodes it(MFn::kSkinClusterFilter, &status);
    if (status != MS::kSuccess)
    {
        return status;
    }

    for (; !it.isDone(); it.next())
    {
        MObject skinObj = it.thisNode();
        if (skinObj.isNull())
        {
            continue;
        }

        MFnGeometryFilter geoFilter(skinObj, &status);
        if (status != MS::kSuccess)
        {
            continue;
        }

        MObjectArray outputGeos;
        status = geoFilter.getOutputGeometry(outputGeos);
        if (status != MS::kSuccess)
        {
            continue;
        }

        for (unsigned int i = 0; i < outputGeos.length(); ++i)
        {
            MDagPath outPath;
            if (MDagPath::getAPathTo(outputGeos[i], outPath) != MS::kSuccess)
            {
                continue;
            }
            MDagPath outShapePath;
            if (getShapeFromDag(outPath, outShapePath) != MS::kSuccess)
            {
                outShapePath = outPath;
            }
            if (outShapePath.fullPathName() == targetShapePath.fullPathName())
            {
                outSkinClusterObj = skinObj;
                return MS::kSuccess;
            }
        }
    }

    return MS::kFailure;
}

MStatus SmoothInfluenceWeightsCmd::findMeshFromSkinCluster(const MObject& skinClusterObj, MDagPath& outMeshPath)
{
    MStatus status;
    MFnGeometryFilter geoFilter(skinClusterObj, &status);
    if (status != MS::kSuccess)
    {
        return status;
    }
    MObjectArray outputGeos;
    status = geoFilter.getOutputGeometry(outputGeos);
    if (status != MS::kSuccess || outputGeos.length() == 0)
    {
        return MS::kFailure;
    }
    return MDagPath::getAPathTo(outputGeos[0], outMeshPath);
}

MStatus SmoothInfluenceWeightsCmd::resolveSkinAndMesh(MObject& outSkinClusterObj, MDagPath& outMeshPath)
{
    MStatus status;

    if (m_skinClusterName.length() > 0)
    {
        MSelectionList sl;
        status = sl.add(m_skinClusterName);
        if (status != MS::kSuccess)
        {
            return status;
        }
        sl.getDependNode(0, outSkinClusterObj);
        if (!outSkinClusterObj.hasFn(MFn::kSkinClusterFilter))
        {
            return MS::kFailure;
        }
        if (m_geometryName.length() > 0)
        {
            MSelectionList geoSl;
            geoSl.add(m_geometryName);
            MDagPath geoPath;
            geoSl.getDagPath(0, geoPath);
            return getShapeFromDag(geoPath, outMeshPath);
        }
        return findMeshFromSkinCluster(outSkinClusterObj, outMeshPath);
    }

    if (m_geometryName.length() > 0)
    {
        MSelectionList sl;
        status = sl.add(m_geometryName);
        if (status != MS::kSuccess)
        {
            return status;
        }
        MDagPath geoPath;
        sl.getDagPath(0, geoPath);
        status = getShapeFromDag(geoPath, outMeshPath);
        if (status != MS::kSuccess)
        {
            return status;
        }
        return findSkinClusterFromMesh(outMeshPath, outSkinClusterObj);
    }

    MSelectionList activeSel;
    MGlobal::getActiveSelectionList(activeSel);
    if (activeSel.length() == 0)
    {
        return MS::kFailure;
    }

    MItSelectionList itVerts(activeSel, MFn::kMeshVertComponent);
    if (!itVerts.isDone())
    {
        MObject comp;
        itVerts.getDagPath(outMeshPath, comp);
        getShapeFromDag(outMeshPath, outMeshPath);
        return findSkinClusterFromMesh(outMeshPath, outSkinClusterObj);
    }

    MItSelectionList itMesh(activeSel, MFn::kMesh);
    if (!itMesh.isDone())
    {
        MObject comp;
        itMesh.getDagPath(outMeshPath, comp);
        getShapeFromDag(outMeshPath, outMeshPath);
        return findSkinClusterFromMesh(outMeshPath, outSkinClusterObj);
    }

    MItSelectionList itXform(activeSel, MFn::kTransform);
    if (!itXform.isDone())
    {
        MObject comp;
        itXform.getDagPath(outMeshPath, comp);
        status = getShapeFromDag(outMeshPath, outMeshPath);
        if (status != MS::kSuccess)
        {
            return status;
        }
        return findSkinClusterFromMesh(outMeshPath, outSkinClusterObj);
    }

    return MS::kFailure;
}

MStatus SmoothInfluenceWeightsCmd::buildEditableComponent(const MDagPath& meshPath, MObject& outComponent, MIntArray& outVertexIds)
{
    MStatus status;
    MFnSingleIndexedComponent compFn;
    outComponent = compFn.create(MFn::kMeshVertComponent, &status);
    if (status != MS::kSuccess)
    {
        return status;
    }

    std::set<int> vertIds;
    if (m_selectionOnly)
    {
        MSelectionList activeSel;
        MGlobal::getActiveSelectionList(activeSel);
        MItSelectionList it(activeSel, MFn::kMeshVertComponent);
        for (; !it.isDone(); it.next())
        {
            MDagPath selPath;
            MObject selComp;
            it.getDagPath(selPath, selComp);
            MDagPath selShapePath;
            if (getShapeFromDag(selPath, selShapePath) != MS::kSuccess)
            {
                selShapePath = selPath;
            }
            if (selShapePath.fullPathName() != meshPath.fullPathName())
            {
                continue;
            }
            MFnSingleIndexedComponent selCompFn(selComp, &status);
            if (status != MS::kSuccess)
            {
                continue;
            }
            MIntArray elems;
            selCompFn.getElements(elems);
            for (unsigned int i = 0; i < elems.length(); ++i)
            {
                vertIds.insert(elems[i]);
            }
        }
        if (vertIds.empty())
        {
            return MS::kFailure;
        }
    }
    else
    {
        MFnMesh meshFn(meshPath, &status);
        if (status != MS::kSuccess)
        {
            return status;
        }
        const int vtxCount = static_cast<int>(meshFn.numVertices(&status));
        if (status != MS::kSuccess)
        {
            return status;
        }
        for (int i = 0; i < vtxCount; ++i)
        {
            vertIds.insert(i);
        }
    }

    outVertexIds.setLength(static_cast<unsigned int>(vertIds.size()));
    unsigned int idx = 0;
    for (std::set<int>::const_iterator it = vertIds.begin(); it != vertIds.end(); ++it)
    {
        outVertexIds[idx++] = *it;
    }
    return compFn.addElements(outVertexIds);
}

MStatus SmoothInfluenceWeightsCmd::collectInfluenceIndices(const MObject& skinClusterObj, MIntArray& outPhysicalIndices)
{
    MStatus status;
    outPhysicalIndices.clear();

    MFnSkinCluster skinFn(skinClusterObj, &status);
    if (status != MS::kSuccess)
    {
        return status;
    }

    MDagPathArray infPaths;
    skinFn.influenceObjects(infPaths, &status);
    if (status != MS::kSuccess)
    {
        return status;
    }

    if (m_influenceNames.length() == 0)
    {
        outPhysicalIndices.setLength(infPaths.length());
        for (unsigned int i = 0; i < infPaths.length(); ++i)
        {
            outPhysicalIndices[i] = static_cast<int>(i);
        }
        return MS::kSuccess;
    }

    for (unsigned int i = 0; i < m_influenceNames.length(); ++i)
    {
        const MString wanted = m_influenceNames[i];
        bool found = false;
        for (unsigned int j = 0; j < infPaths.length(); ++j)
        {
            const MString fullName = infPaths[j].fullPathName();
            const MString partialName = infPaths[j].partialPathName();
            const MString shortName = MFnDependencyNode(infPaths[j].node()).name();
            if (wanted == fullName || wanted == partialName || wanted == shortName)
            {
                outPhysicalIndices.append(static_cast<int>(j));
                found = true;
                break;
            }
        }
        if (!found)
        {
            MGlobal::displayWarning("smoothInfluenceWeights: influence not found on skinCluster: " + wanted);
        }
    }

    return (outPhysicalIndices.length() > 0) ? MS::kSuccess : MS::kFailure;
}

MStatus SmoothInfluenceWeightsCmd::cacheAllMeshNeighbors(const MDagPath& meshPath)
{
    MStatus status;
    MFnMesh meshFn(meshPath, &status);
    if (status != MS::kSuccess)
    {
        return status;
    }
    const unsigned int vtxCount = meshFn.numVertices(&status);
    if (status != MS::kSuccess)
    {
        return status;
    }

    m_allMeshNeighbors.clear();
    m_allMeshNeighbors.resize(vtxCount);

    for (unsigned int v = 0; v < vtxCount; ++v)
    {
        MFnSingleIndexedComponent compFn;
        MObject comp = compFn.create(MFn::kMeshVertComponent, &status);
        if (status != MS::kSuccess)
        {
            return status;
        }
        compFn.addElement(static_cast<int>(v));
        MItMeshVertex it(meshPath, comp, &status);
        if (status != MS::kSuccess || it.isDone())
        {
            return MS::kFailure;
        }
        MIntArray nbrs;
        status = it.getConnectedVertices(nbrs);
        if (status != MS::kSuccess)
        {
            return status;
        }
        m_allMeshNeighbors[v] = nbrs;
    }
    return MS::kSuccess;
}

bool SmoothInfluenceWeightsCmd::isEditableVertex(int vtxId) const
{
    return m_editableVertSet.find(vtxId) != m_editableVertSet.end();
}

bool SmoothInfluenceWeightsCmd::isBorderVertex(int vtxId) const
{
    return m_borderVertSet.find(vtxId) != m_borderVertSet.end();
}

bool SmoothInfluenceWeightsCmd::influenceIsInSmoothSet(unsigned int influenceIndex) const
{
    if (m_selectedInfluenceIndices.length() == 0)
    {
        return true;
    }
    for (unsigned int i = 0; i < m_selectedInfluenceIndices.length(); ++i)
    {
        if (static_cast<unsigned int>(m_selectedInfluenceIndices[i]) == influenceIndex)
        {
            return true;
        }
    }
    return false;
}

void SmoothInfluenceWeightsCmd::buildKeepOneSeeds()
{
    m_keepOneLockedInfluencePerVertex.assign(m_editableVerts.length(), -1);
    if (!m_keepOnes || m_influenceCount == 0)
    {
        return;
    }

    for (unsigned int editableLocal = 0; editableLocal < m_editableVerts.length(); ++editableLocal)
    {
        const unsigned int rowBase = editableLocal * m_influenceCount;
        double rowSum = 0.0;
        double maxVal = -1.0;
        int maxInf = -1;

        for (unsigned int inf = 0; inf < m_influenceCount; ++inf)
        {
            const double w = std::max(0.0, m_originalEditableWeights[rowBase + inf]);
            rowSum += w;
            if (w > maxVal)
            {
                maxVal = w;
                maxInf = static_cast<int>(inf);
            }
        }

        if (rowSum <= 1e-8 || maxInf < 0)
        {
            continue;
        }

        const double normalizedDominance = maxVal / rowSum;
        if (normalizedDominance >= m_keepOneThreshold && influenceIsInSmoothSet(static_cast<unsigned int>(maxInf)))
        {
            m_keepOneLockedInfluencePerVertex[editableLocal] = maxInf;
        }
    }
}

void SmoothInfluenceWeightsCmd::enforceKeepOneSeeds(MDoubleArray& weights) const
{
    if (!m_keepOnes)
    {
        return;
    }

    for (unsigned int editableLocal = 0; editableLocal < m_editableVerts.length(); ++editableLocal)
    {
        if (editableLocal >= m_keepOneLockedInfluencePerVertex.size())
        {
            continue;
        }
        const int lockedInf = m_keepOneLockedInfluencePerVertex[editableLocal];
        if (lockedInf < 0)
        {
            continue;
        }
        const unsigned int rowBase = editableLocal * m_influenceCount;
        for (unsigned int inf = 0; inf < m_influenceCount; ++inf)
        {
            weights[rowBase + inf] = (static_cast<int>(inf) == lockedInf) ? 1.0 : 0.0;
        }
    }
}

void SmoothInfluenceWeightsCmd::promoteNewKeepOneSeeds(MDoubleArray& weights)
{
    if (!m_keepOnes)
    {
        return;
    }

    for (unsigned int editableLocal = 0; editableLocal < m_editableVerts.length(); ++editableLocal)
    {
        if (editableLocal >= m_keepOneLockedInfluencePerVertex.size())
        {
            continue;
        }
        if (m_keepOneLockedInfluencePerVertex[editableLocal] >= 0)
        {
            continue;
        }

        const unsigned int rowBase = editableLocal * m_influenceCount;
        double rowSum = 0.0;
        double maxVal = -1.0;
        int maxInf = -1;

        for (unsigned int inf = 0; inf < m_influenceCount; ++inf)
        {
            const double w = std::max(0.0, weights[rowBase + inf]);
            rowSum += w;
            if (w > maxVal)
            {
                maxVal = w;
                maxInf = static_cast<int>(inf);
            }
        }

        if (rowSum <= 1e-8 || maxInf < 0)
        {
            continue;
        }

        if ((maxVal / rowSum) >= m_keepOneThreshold && influenceIsInSmoothSet(static_cast<unsigned int>(maxInf)))
        {
            m_keepOneLockedInfluencePerVertex[editableLocal] = maxInf;
            for (unsigned int inf = 0; inf < m_influenceCount; ++inf)
            {
                weights[rowBase + inf] = (static_cast<int>(inf) == maxInf) ? 1.0 : 0.0;
            }
        }
    }
}

double SmoothInfluenceWeightsCmd::distanceWeight(int distance) const
{
    if (distance < 1 || distance > m_bleedDistance || m_bleedDistance < 1)
    {
        return 0.0;
    }

    if (m_bleedFalloff <= 0.0)
    {
        return 1.0;
    }

    const double t = static_cast<double>(distance - 1) / static_cast<double>(m_bleedDistance);
    const double w = 1.0 - (t * m_bleedFalloff);
    return std::max(0.0, w);
}

void SmoothInfluenceWeightsCmd::gatherSeedCandidates(int editableLocal, std::vector<BleedCandidate>& outCandidates) const
{
    outCandidates.clear();
    if (editableLocal < 0 || editableLocal >= static_cast<int>(m_editableVerts.length()))
    {
        return;
    }

    const int startVtx = m_editableVerts[static_cast<unsigned int>(editableLocal)];
    if (startVtx < 0 || startVtx >= static_cast<int>(m_allMeshNeighbors.size()))
    {
        return;
    }

    std::queue<int> frontier;
    std::vector<int> distances(m_allMeshNeighbors.size(), -1);
    frontier.push(startVtx);
    distances[startVtx] = 0;

    std::vector<int> bestDistancePerInfluence(static_cast<size_t>(m_influenceCount), -1);
    std::vector<double> totalWeightPerInfluence(static_cast<size_t>(m_influenceCount), 0.0);

    while (!frontier.empty())
    {
        const int vtx = frontier.front();
        frontier.pop();
        const int dist = distances[vtx];
        if (dist >= m_bleedDistance)
        {
            continue;
        }

        const MIntArray& nbrs = m_allMeshNeighbors[static_cast<size_t>(vtx)];
        for (unsigned int i = 0; i < nbrs.length(); ++i)
        {
            const int nbr = nbrs[i];
            if (nbr < 0 || nbr >= static_cast<int>(distances.size()))
            {
                continue;
            }
            if (distances[nbr] >= 0)
            {
                continue;
            }

            const int nextDist = dist + 1;
            distances[nbr] = nextDist;
            frontier.push(nbr);

            if (nextDist > m_bleedDistance)
            {
                continue;
            }
            if (!isEditableVertex(nbr))
            {
                continue;
            }

            const int nbrLocal = m_vertexToEditableLocal[static_cast<size_t>(nbr)];
            if (nbrLocal < 0 || nbrLocal >= static_cast<int>(m_keepOneLockedInfluencePerVertex.size()))
            {
                continue;
            }

            const int seedInf = m_keepOneLockedInfluencePerVertex[static_cast<size_t>(nbrLocal)];
            if (seedInf < 0)
            {
                continue;
            }
            if (!influenceIsInSmoothSet(static_cast<unsigned int>(seedInf)))
            {
                continue;
            }

            const double w = distanceWeight(nextDist);
            if (w <= 0.0)
            {
                continue;
            }

            int& bestDist = bestDistancePerInfluence[static_cast<size_t>(seedInf)];
            if (bestDist < 0 || nextDist < bestDist)
            {
                bestDist = nextDist;
            }
            totalWeightPerInfluence[static_cast<size_t>(seedInf)] += w;
        }
    }

    for (unsigned int inf = 0; inf < m_influenceCount; ++inf)
    {
        if (bestDistancePerInfluence[inf] >= 0 && totalWeightPerInfluence[inf] > 0.0)
        {
            BleedCandidate c;
            c.influenceIndex = static_cast<int>(inf);
            c.minDistance = bestDistancePerInfluence[inf];
            c.totalWeight = totalWeightPerInfluence[inf];
            outCandidates.push_back(c);
        }
    }

    std::sort(outCandidates.begin(), outCandidates.end(),
        [](const BleedCandidate& a, const BleedCandidate& b)
        {
            if (a.minDistance != b.minDistance)
            {
                return a.minDistance > b.minDistance; // farthest first, nearest last
            }
            if (a.totalWeight != b.totalWeight)
            {
                return a.totalWeight < b.totalWeight;
            }
            return a.influenceIndex < b.influenceIndex;
        });
}

void SmoothInfluenceWeightsCmd::applyTransferToRow(MDoubleArray& weights, unsigned int rowBase, unsigned int bleedInf, double amount) const
{
    amount = clamp01(amount);
    const double oldBleed = clamp01(weights[rowBase + bleedInf]);
    const double newBleed = std::min(1.0, oldBleed + amount);
    const double oldOthersTotal = std::max(0.0, 1.0 - oldBleed);
    const double newOthersTotal = std::max(0.0, 1.0 - newBleed);

    weights[rowBase + bleedInf] = newBleed;

    if (oldOthersTotal > 1e-8)
    {
        const double scale = newOthersTotal / oldOthersTotal;
        for (unsigned int inf = 0; inf < m_influenceCount; ++inf)
        {
            if (inf == bleedInf)
            {
                continue;
            }
            weights[rowBase + inf] = std::max(0.0, weights[rowBase + inf] * scale);
        }
    }
    else
    {
        for (unsigned int inf = 0; inf < m_influenceCount; ++inf)
        {
            if (inf == bleedInf)
            {
                continue;
            }
            weights[rowBase + inf] = 0.0;
        }
    }
}

void SmoothInfluenceWeightsCmd::applyRegularSmoothIteration(const MDoubleArray& currentWeights, MDoubleArray& nextWeights, bool smoothAll) const
{
    nextWeights = currentWeights;

    for (unsigned int editableLocal = 0; editableLocal < m_editableVerts.length(); ++editableLocal)
    {
        const int vtxId = m_editableVerts[editableLocal];
        if (m_borderPin && isBorderVertex(vtxId))
        {
            continue;
        }

        const MIntArray& nbrs = m_allMeshNeighbors[static_cast<size_t>(vtxId)];
        if (nbrs.length() == 0)
        {
            continue;
        }

        const unsigned int rowBase = editableLocal * m_influenceCount;
        std::vector<double> average(static_cast<size_t>(m_influenceCount), 0.0);
        unsigned int validCount = 0;

        for (unsigned int n = 0; n < nbrs.length(); ++n)
        {
            const int nbr = nbrs[n];
            if (!isEditableVertex(nbr))
            {
                continue;
            }
            const int nbrLocal = m_vertexToEditableLocal[static_cast<size_t>(nbr)];
            if (nbrLocal < 0)
            {
                continue;
            }
            const unsigned int nbrBase = static_cast<unsigned int>(nbrLocal) * m_influenceCount;
            for (unsigned int inf = 0; inf < m_influenceCount; ++inf)
            {
                average[inf] += currentWeights[nbrBase + inf];
            }
            ++validCount;
        }

        if (validCount == 0)
        {
            continue;
        }

        for (unsigned int inf = 0; inf < m_influenceCount; ++inf)
        {
            average[inf] /= static_cast<double>(validCount);
        }

        if (smoothAll)
        {
            for (unsigned int inf = 0; inf < m_influenceCount; ++inf)
            {
                nextWeights[rowBase + inf] = ((1.0 - m_strength) * currentWeights[rowBase + inf]) + (m_strength * average[inf]);
            }
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

            if (currentSelected > 1e-8)
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
            if (currentOther > 1e-8)
            {
                const double scaleOther = newOther / currentOther;
                for (unsigned int inf = 0; inf < m_influenceCount; ++inf)
                {
                    if (influenceIsInSmoothSet(inf))
                    {
                        continue;
                    }
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
        if (rowSum > 1e-8)
        {
            for (unsigned int inf = 0; inf < m_influenceCount; ++inf)
            {
                nextWeights[rowBase + inf] /= rowSum;
            }
        }
    }
}

MStatus SmoothInfluenceWeightsCmd::applyWeights(const MDoubleArray& weights)
{
    MStatus status;
    MFnSkinCluster skinFn(m_skinObj, &status);
    if (status != MS::kSuccess)
    {
        return status;
    }

    MIntArray allIndices;
    allIndices.setLength(m_influenceCount);
    for (unsigned int i = 0; i < m_influenceCount; ++i)
    {
        allIndices[i] = static_cast<int>(i);
    }

    MDoubleArray mutableWeights(weights);
    return skinFn.setWeights(m_meshPath, m_editableComponent, allIndices, mutableWeights, false, nullptr);
}

MStatus SmoothInfluenceWeightsCmd::doIt(const MArgList& args)
{
    MStatus status = parseArgs(args);
    if (status != MS::kSuccess)
    {
        return status;
    }

    status = resolveSkinAndMesh(m_skinObj, m_meshPath);
    if (status != MS::kSuccess)
    {
        return status;
    }

    status = cacheAllMeshNeighbors(m_meshPath);
    if (status != MS::kSuccess)
    {
        return status;
    }

    status = buildEditableComponent(m_meshPath, m_editableComponent, m_editableVerts);
    if (status != MS::kSuccess)
    {
        return status;
    }

    m_editableVertSet.clear();
    for (unsigned int i = 0; i < m_editableVerts.length(); ++i)
    {
        m_editableVertSet.insert(m_editableVerts[i]);
    }

    m_borderVertSet.clear();
    if (m_selectionOnly)
    {
        for (std::set<int>::const_iterator it = m_editableVertSet.begin(); it != m_editableVertSet.end(); ++it)
        {
            const int vtxId = *it;
            const MIntArray& nbrs = m_allMeshNeighbors[static_cast<size_t>(vtxId)];
            for (unsigned int n = 0; n < nbrs.length(); ++n)
            {
                if (!isEditableVertex(nbrs[n]))
                {
                    m_borderVertSet.insert(vtxId);
                    break;
                }
            }
        }
    }

    status = collectInfluenceIndices(m_skinObj, m_selectedInfluenceIndices);
    if (status != MS::kSuccess)
    {
        return status;
    }

    MFnSkinCluster skinFn(m_skinObj, &status);
    if (status != MS::kSuccess)
    {
        return status;
    }
    status = skinFn.getWeights(m_meshPath, m_editableComponent, m_originalEditableWeights, m_influenceCount);
    if (status != MS::kSuccess)
    {
        return status;
    }

    m_newEditableWeights = m_originalEditableWeights;

    MFnMesh meshFn(m_meshPath, &status);
    if (status != MS::kSuccess)
    {
        return status;
    }
    const unsigned int totalVerts = meshFn.numVertices(&status);
    if (status != MS::kSuccess)
    {
        return status;
    }
    m_vertexToEditableLocal.assign(totalVerts, -1);
    for (unsigned int i = 0; i < m_editableVerts.length(); ++i)
    {
        m_vertexToEditableLocal[static_cast<size_t>(m_editableVerts[i])] = static_cast<int>(i);
    }

    return redoIt();
}

MStatus SmoothInfluenceWeightsCmd::redoRegularSmooth()
{
    m_newEditableWeights = m_originalEditableWeights;
    const bool smoothAll = (m_selectedInfluenceIndices.length() == 0 || m_selectedInfluenceIndices.length() == m_influenceCount);

    for (int iter = 0; iter < m_iterations; ++iter)
    {
        MDoubleArray nextWeights;
        applyRegularSmoothIteration(m_newEditableWeights, nextWeights, smoothAll);
        m_newEditableWeights = nextWeights;
    }

    return applyWeights(m_newEditableWeights);
}


MStatus SmoothInfluenceWeightsCmd::redoBleedSmooth()
{
    m_newEditableWeights = m_originalEditableWeights;
    buildKeepOneSeeds();
    enforceKeepOneSeeds(m_newEditableWeights);

    const bool smoothAll = (m_selectedInfluenceIndices.length() == 0 || m_selectedInfluenceIndices.length() == m_influenceCount);

    for (int iter = 0; iter < m_iterations; ++iter)
    {
        // First pass: smooth all non-locked editable verts normally under the brush.
        MDoubleArray nextWeights;
        applyRegularSmoothIteration(m_newEditableWeights, nextWeights, smoothAll);

        // Re-assert locked 1.0 seeds so protected verts do not drift during smoothing.
        enforceKeepOneSeeds(nextWeights);

        // Second pass: bleed the protected influences outward to nearby editable verts.
        for (unsigned int editableLocal = 0; editableLocal < m_editableVerts.length(); ++editableLocal)
        {
            const int vtxId = m_editableVerts[editableLocal];
            if (m_borderPin && isBorderVertex(vtxId))
            {
                continue;
            }
            if (m_keepOneLockedInfluencePerVertex[editableLocal] >= 0)
            {
                // Protected 1.0 verts stay untouched.
                continue;
            }

            std::vector<BleedCandidate> candidates;
            gatherSeedCandidates(static_cast<int>(editableLocal), candidates);
            if (candidates.empty())
            {
                continue;
            }

            const unsigned int rowBase = editableLocal * m_influenceCount;
            std::vector<double> add(static_cast<size_t>(m_influenceCount), 0.0);
            double totalAdd = 0.0;

            for (size_t c = 0; c < candidates.size(); ++c)
            {
                const unsigned int inf = static_cast<unsigned int>(candidates[c].influenceIndex);
                if (!influenceIsInSmoothSet(inf))
                {
                    continue;
                }

                const double amount = std::max(0.0, m_strength * candidates[c].totalWeight);
                if (amount <= 0.0)
                {
                    continue;
                }

                add[inf] += amount;
                totalAdd += amount;
            }

            if (totalAdd <= 1e-8)
            {
                continue;
            }

            for (unsigned int inf = 0; inf < m_influenceCount; ++inf)
            {
                nextWeights[rowBase + inf] = std::max(0.0, nextWeights[rowBase + inf] + add[inf]);
            }

            double rowSum = 0.0;
            for (unsigned int inf = 0; inf < m_influenceCount; ++inf)
            {
                rowSum += nextWeights[rowBase + inf];
            }

            if (rowSum > 1e-8)
            {
                for (unsigned int inf = 0; inf < m_influenceCount; ++inf)
                {
                    nextWeights[rowBase + inf] /= rowSum;
                }
            }
        }

        // Final pass: any protected or newly promoted near-1.0 verts are restored.
        enforceKeepOneSeeds(nextWeights);
        promoteNewKeepOneSeeds(nextWeights);
        enforceKeepOneSeeds(nextWeights);

        m_newEditableWeights = nextWeights;
    }

    return applyWeights(m_newEditableWeights);
}

MStatus SmoothInfluenceWeightsCmd::redoIt()
{
    return m_keepOnes ? redoBleedSmooth() : redoRegularSmooth();
}

MStatus SmoothInfluenceWeightsCmd::undoIt()
{
    return applyWeights(m_originalEditableWeights);
}
