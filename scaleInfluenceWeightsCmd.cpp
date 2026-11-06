#include "scaleInfluenceWeightsCmd.h"

#include <maya/MArgDatabase.h>
#include <maya/MArgList.h>
#include <maya/MFnDependencyNode.h>
#include <maya/MFnGeometryFilter.h>
#include <maya/MFnMesh.h>
#include <maya/MFnSingleIndexedComponent.h>
#include <maya/MFnSkinCluster.h>
#include <maya/MGlobal.h>
#include <maya/MItDependencyNodes.h>
#include <maya/MItSelectionList.h>
#include <maya/MObjectArray.h>
#include <maya/MSelectionList.h>
#include <maya/MDagPathArray.h>

#include <algorithm>
#include <cctype>
#include <set>
#include <string>
#include <vector>

const char* ScaleInfluenceWeightsCmd::kCommandName = "scaleInfluenceWeights";

namespace
{
    const char* kSkinFlagShort = "-sc";
    const char* kSkinFlagLong  = "-skinCluster";
    const char* kGeoFlagShort = "-g";
    const char* kGeoFlagLong  = "-geometry";
    const char* kInfFlagShort = "-inf";
    const char* kInfFlagLong  = "-influence";
    const char* kLockInfFlagShort = "-linf";
    const char* kLockInfFlagLong  = "-lockInfluence";
    const char* kKeepOnesFlagShort = "-ko";
    const char* kKeepOnesFlagLong  = "-keepOnes";
    const char* kSelOnlyFlagShort = "-so";
    const char* kSelOnlyFlagLong  = "-selectionOnly";
    const char* kIterFlagShort = "-it";
    const char* kIterFlagLong  = "-iterations";
    const char* kBleedDistanceFlagShort = "-bd";
    const char* kBleedDistanceFlagLong  = "-bleedDistance";
    const char* kBleedFalloffFlagShort = "-bf";
    const char* kBleedFalloffFlagLong  = "-bleedFalloff";
    const char* kStrengthFlagShort = "-str";
    const char* kStrengthFlagLong  = "-strength";
    const char* kScaleFactorFlagShort = "-sf";
    const char* kScaleFactorFlagLong  = "-scaleFactor";

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
            ++start;
        size_t end = s.size();
        while (end > start && std::isspace(static_cast<unsigned char>(s[end - 1])) != 0)
            --end;
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
            return MS::kFailure;

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

void* ScaleInfluenceWeightsCmd::creator()
{
    return new ScaleInfluenceWeightsCmd();
}

MSyntax ScaleInfluenceWeightsCmd::newSyntax()
{
    MSyntax syntax;
    syntax.addFlag(kSkinFlagShort, kSkinFlagLong, MSyntax::kString);
    syntax.addFlag(kGeoFlagShort, kGeoFlagLong, MSyntax::kString);
    syntax.addFlag(kInfFlagShort, kInfFlagLong, MSyntax::kString);
    syntax.makeFlagMultiUse(kInfFlagShort);
    syntax.addFlag(kLockInfFlagShort, kLockInfFlagLong, MSyntax::kString);
    syntax.makeFlagMultiUse(kLockInfFlagShort);
    syntax.addFlag(kKeepOnesFlagShort, kKeepOnesFlagLong, MSyntax::kBoolean);
    syntax.addFlag(kSelOnlyFlagShort, kSelOnlyFlagLong, MSyntax::kBoolean);
    syntax.addFlag(kIterFlagShort, kIterFlagLong, MSyntax::kLong);
    syntax.addFlag(kBleedDistanceFlagShort, kBleedDistanceFlagLong, MSyntax::kLong);
    syntax.addFlag(kBleedFalloffFlagShort, kBleedFalloffFlagLong, MSyntax::kDouble);
    syntax.addFlag(kStrengthFlagShort, kStrengthFlagLong, MSyntax::kDouble);
    syntax.addFlag(kScaleFactorFlagShort, kScaleFactorFlagLong, MSyntax::kDouble);
    syntax.enableEdit(false);
    syntax.enableQuery(false);
    return syntax;
}

MStatus ScaleInfluenceWeightsCmd::parseArgs(const MArgList& args)
{
    MStatus status;
    MArgDatabase argData(newSyntax(), args, &status);
    if (status != MS::kSuccess)
        return status;

    m_skinClusterName.clear();
    m_geometryName.clear();
    m_influenceNames.clear();
    m_lockedInfluenceNames.clear();
    m_keepOnes = false;
    m_selectionOnly = false;
    m_iterations = 1;
    m_strength = 0.2;
    m_scaleFactor = 1.25;

    if (argData.isFlagSet(kSkinFlagShort)) argData.getFlagArgument(kSkinFlagShort, 0, m_skinClusterName);
    if (argData.isFlagSet(kGeoFlagShort)) argData.getFlagArgument(kGeoFlagShort, 0, m_geometryName);
    if (argData.isFlagSet(kKeepOnesFlagShort)) argData.getFlagArgument(kKeepOnesFlagShort, 0, m_keepOnes);
    if (argData.isFlagSet(kSelOnlyFlagShort)) argData.getFlagArgument(kSelOnlyFlagShort, 0, m_selectionOnly);
    if (argData.isFlagSet(kIterFlagShort)) argData.getFlagArgument(kIterFlagShort, 0, m_iterations);
    if (argData.isFlagSet(kStrengthFlagShort)) argData.getFlagArgument(kStrengthFlagShort, 0, m_strength);
    if (argData.isFlagSet(kScaleFactorFlagShort)) argData.getFlagArgument(kScaleFactorFlagShort, 0, m_scaleFactor);

    if (m_iterations < 1) m_iterations = 1;
    m_strength = clamp01(m_strength);
    if (std::abs(m_scaleFactor) < 1.0e-8) m_scaleFactor = 1.0e-5;
    if (m_scaleFactor < -10.0) m_scaleFactor = -10.0;
    else if (m_scaleFactor > 10.0) m_scaleFactor = 10.0;

    auto readMulti = [&](const char* flagShort, MStringArray& outArray)
    {
        const unsigned int uses = argData.numberOfFlagUses(flagShort);
        for (unsigned int i = 0; i < uses; ++i)
        {
            MArgList oneUse;
            argData.getFlagArgumentList(flagShort, i, oneUse);
            for (unsigned int a = 0; a < oneUse.length(); ++a)
            {
                MString value = oneUse.asString(a, &status);
                if (status != MS::kSuccess)
                    continue;
                if (value.index(',') >= 0)
                {
                    MStringArray splitVals;
                    value.split(',', splitVals);
                    for (unsigned int s = 0; s < splitVals.length(); ++s)
                    {
                        MString name = trimMString(splitVals[s]);
                        if (name.length() > 0) outArray.append(name);
                    }
                }
                else
                {
                    MString name = trimMString(value);
                    if (name.length() > 0) outArray.append(name);
                }
            }
        }
    };

    readMulti(kInfFlagShort, m_influenceNames);
    readMulti(kLockInfFlagShort, m_lockedInfluenceNames);

    return MS::kSuccess;
}

MStatus ScaleInfluenceWeightsCmd::findSkinClusterFromMesh(const MDagPath& meshPath, MObject& outSkinClusterObj)
{
    MStatus status;
    MDagPath targetShapePath;
    status = getShapeFromDag(meshPath, targetShapePath);
    if (status != MS::kSuccess)
        targetShapePath = meshPath;

    MItDependencyNodes it(MFn::kSkinClusterFilter, &status);
    if (status != MS::kSuccess)
        return status;

    for (; !it.isDone(); it.next())
    {
        MObject skinObj = it.thisNode();
        if (skinObj.isNull())
            continue;

        MFnGeometryFilter geoFilter(skinObj, &status);
        if (status != MS::kSuccess)
            continue;

        MObjectArray outputGeos;
        status = geoFilter.getOutputGeometry(outputGeos);
        if (status != MS::kSuccess)
            continue;

        for (unsigned int i = 0; i < outputGeos.length(); ++i)
        {
            MDagPath outPath;
            if (MDagPath::getAPathTo(outputGeos[i], outPath) != MS::kSuccess)
                continue;

            MDagPath outShapePath;
            if (getShapeFromDag(outPath, outShapePath) != MS::kSuccess)
                outShapePath = outPath;

            if (outShapePath.fullPathName() == targetShapePath.fullPathName())
            {
                outSkinClusterObj = skinObj;
                return MS::kSuccess;
            }
        }
    }

    return MS::kFailure;
}

MStatus ScaleInfluenceWeightsCmd::findMeshFromSkinCluster(const MObject& skinClusterObj, MDagPath& outMeshPath)
{
    MStatus status;
    MFnGeometryFilter geoFilter(skinClusterObj, &status);
    if (status != MS::kSuccess)
        return status;

    MObjectArray outputGeos;
    status = geoFilter.getOutputGeometry(outputGeos);
    if (status != MS::kSuccess || outputGeos.length() == 0)
        return MS::kFailure;

    return MDagPath::getAPathTo(outputGeos[0], outMeshPath);
}

MStatus ScaleInfluenceWeightsCmd::resolveSkinAndMesh(MObject& outSkinClusterObj, MDagPath& outMeshPath)
{
    MStatus status;

    if (m_skinClusterName.length() > 0)
    {
        MSelectionList sl;
        status = sl.add(m_skinClusterName);
        if (status != MS::kSuccess)
            return status;
        sl.getDependNode(0, outSkinClusterObj);
        if (!outSkinClusterObj.hasFn(MFn::kSkinClusterFilter))
            return MS::kFailure;

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
            return status;
        MDagPath geoPath;
        sl.getDagPath(0, geoPath);
        status = getShapeFromDag(geoPath, outMeshPath);
        if (status != MS::kSuccess)
            return status;
        return findSkinClusterFromMesh(outMeshPath, outSkinClusterObj);
    }

    MSelectionList activeSel;
    MGlobal::getActiveSelectionList(activeSel);
    if (activeSel.length() == 0)
        return MS::kFailure;

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
            return status;
        return findSkinClusterFromMesh(outMeshPath, outSkinClusterObj);
    }

    return MS::kFailure;
}

MStatus ScaleInfluenceWeightsCmd::buildEditableComponent(const MDagPath& meshPath, MObject& outComponent, MIntArray& outVertexIds)
{
    MStatus status;
    MFnSingleIndexedComponent compFn;
    outComponent = compFn.create(MFn::kMeshVertComponent, &status);
    if (status != MS::kSuccess)
        return status;

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
                selShapePath = selPath;
            if (selShapePath.fullPathName() != meshPath.fullPathName())
                continue;

            MFnSingleIndexedComponent selCompFn(selComp, &status);
            if (status != MS::kSuccess)
                continue;
            MIntArray elems;
            selCompFn.getElements(elems);
            for (unsigned int i = 0; i < elems.length(); ++i)
                vertIds.insert(elems[i]);
        }
        if (vertIds.empty())
            return MS::kFailure;
    }
    else
    {
        MFnMesh meshFn(meshPath, &status);
        if (status != MS::kSuccess)
            return status;
        const int vtxCount = static_cast<int>(meshFn.numVertices(&status));
        if (status != MS::kSuccess)
            return status;
        for (int i = 0; i < vtxCount; ++i)
            vertIds.insert(i);
    }

    outVertexIds.setLength(static_cast<unsigned int>(vertIds.size()));
    unsigned int idx = 0;
    for (std::set<int>::const_iterator it = vertIds.begin(); it != vertIds.end(); ++it)
        outVertexIds[idx++] = *it;

    return compFn.addElements(outVertexIds);
}

MStatus ScaleInfluenceWeightsCmd::collectInfluenceIndices(const MObject& skinClusterObj,
                                                          const MStringArray& names,
                                                          MIntArray& outPhysicalIndices)
{
    MStatus status;
    outPhysicalIndices.clear();

    MFnSkinCluster skinFn(skinClusterObj, &status);
    if (status != MS::kSuccess)
        return status;

    MDagPathArray infPaths;
    const unsigned int infCount = skinFn.influenceObjects(infPaths, &status);
    if (status != MS::kSuccess)
        return status;

    if (names.length() == 0)
        return MS::kSuccess;

    for (unsigned int i = 0; i < names.length(); ++i)
    {
        const MString wanted = names[i];
        for (unsigned int j = 0; j < infCount; ++j)
        {
            const MString fullName = infPaths[j].fullPathName();
            const MString partialName = infPaths[j].partialPathName();
            const MString shortName = MFnDependencyNode(infPaths[j].node()).name();
            if (wanted == fullName || wanted == partialName || wanted == shortName)
            {
                outPhysicalIndices.append(static_cast<int>(j));
                break;
            }
        }
    }

    return MS::kSuccess;
}

bool ScaleInfluenceWeightsCmd::isLockedInfluence(unsigned int inf) const
{
    for (unsigned int i = 0; i < m_lockedInfluenceIndices.length(); ++i)
    {
        if (static_cast<unsigned int>(m_lockedInfluenceIndices[i]) == inf)
            return true;
    }
    return false;
}

bool ScaleInfluenceWeightsCmd::isSelectedInfluence(unsigned int inf) const
{
    for (unsigned int i = 0; i < m_selectedInfluenceIndices.length(); ++i)
    {
        if (static_cast<unsigned int>(m_selectedInfluenceIndices[i]) == inf)
            return true;
    }
    return false;
}

MStatus ScaleInfluenceWeightsCmd::applyWeights(const MDoubleArray& weights)
{
    MStatus status;
    MFnSkinCluster skinFn(m_skinObj, &status);
    if (status != MS::kSuccess)
        return status;

    MIntArray allIndices;
    allIndices.setLength(m_influenceCount);
    for (unsigned int i = 0; i < m_influenceCount; ++i)
        allIndices[i] = static_cast<int>(i);

    MDoubleArray mutableWeights(weights);
    return skinFn.setWeights(m_meshPath, m_editableComponent, allIndices, mutableWeights, false, nullptr);
}

MStatus ScaleInfluenceWeightsCmd::doIt(const MArgList& args)
{
    MStatus status = parseArgs(args);
    if (status != MS::kSuccess)
        return status;

    status = resolveSkinAndMesh(m_skinObj, m_meshPath);
    if (status != MS::kSuccess)
        return status;

    status = buildEditableComponent(m_meshPath, m_editableComponent, m_editableVerts);
    if (status != MS::kSuccess)
        return status;

    status = collectInfluenceIndices(m_skinObj, m_influenceNames, m_selectedInfluenceIndices);
    if (status != MS::kSuccess)
        return status;

    status = collectInfluenceIndices(m_skinObj, m_lockedInfluenceNames, m_lockedInfluenceIndices);
    if (status != MS::kSuccess)
        return status;

    MFnSkinCluster skinFn(m_skinObj, &status);
    if (status != MS::kSuccess)
        return status;

    status = skinFn.getWeights(m_meshPath, m_editableComponent, m_originalEditableWeights, m_influenceCount);
    if (status != MS::kSuccess)
        return status;

    m_newEditableWeights = m_originalEditableWeights;
    return redoIt();
}

MStatus ScaleInfluenceWeightsCmd::redoIt()
{
    m_newEditableWeights = m_originalEditableWeights;
    const double effectiveScale = 1.0 + (m_strength * (m_scaleFactor - 1.0));

    for (int iter = 0; iter < m_iterations; ++iter)
    {
        MDoubleArray nextWeights = m_newEditableWeights;

        for (unsigned int v = 0; v < m_editableVerts.length(); ++v)
        {
            const unsigned int rowBase = v * m_influenceCount;

            double lockedSum = 0.0;
            for (unsigned int inf = 0; inf < m_influenceCount; ++inf)
            {
                if (isLockedInfluence(inf))
                    lockedSum += std::max(0.0, nextWeights[rowBase + inf]);
            }

            const double freeBudget = 1.0 - lockedSum;
            if (freeBudget <= 1.0e-8)
                continue;

            std::vector<unsigned int> freeIndices;
            for (unsigned int inf = 0; inf < m_influenceCount; ++inf)
            {
                if (!isLockedInfluence(inf))
                    freeIndices.push_back(inf);
            }
            if (freeIndices.empty())
                continue;

            std::vector<double> freeWeights(freeIndices.size(), 0.0);
            double freeSum = 0.0;
            for (size_t i = 0; i < freeIndices.size(); ++i)
            {
                freeWeights[i] = std::max(0.0, nextWeights[rowBase + freeIndices[i]]);
                freeSum += freeWeights[i];
            }

            if (freeSum <= 1.0e-8)
                continue;

            const bool anySelected = (m_selectedInfluenceIndices.length() > 0);
            int dominantLocal = -1;
            double dominantValue = -1.0;
            if (!anySelected)
            {
                for (size_t i = 0; i < freeWeights.size(); ++i)
                {
                    if (freeWeights[i] > dominantValue)
                    {
                        dominantValue = freeWeights[i];
                        dominantLocal = static_cast<int>(i);
                    }
                }
            }

            for (size_t i = 0; i < freeIndices.size(); ++i)
            {
                bool scaleThis = false;
                if (anySelected)
                    scaleThis = isSelectedInfluence(freeIndices[i]);
                else
                    scaleThis = (static_cast<int>(i) == dominantLocal);

                if (scaleThis)
                    freeWeights[i] *= effectiveScale;
            }

            double newFreeSum = 0.0;
            for (size_t i = 0; i < freeWeights.size(); ++i)
                newFreeSum += freeWeights[i];

            if (newFreeSum <= 1.0e-8)
                continue;

            const double normScale = freeBudget / newFreeSum;
            for (size_t i = 0; i < freeIndices.size(); ++i)
                nextWeights[rowBase + freeIndices[i]] = std::max(0.0, freeWeights[i] * normScale);
        }

        m_newEditableWeights = nextWeights;
    }

    return applyWeights(m_newEditableWeights);
}

MStatus ScaleInfluenceWeightsCmd::undoIt()
{
    return applyWeights(m_originalEditableWeights);
}
