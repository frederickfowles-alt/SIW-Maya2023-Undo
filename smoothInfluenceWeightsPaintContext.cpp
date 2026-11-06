#include "smoothInfluenceWeightsPaintContext.h"
#include "BrushSolverCache.h"
#include "SmoothInfluenceWeightsStrokeUndoCmd.h"

#include <maya/MSelectionList.h>
#include <maya/MFnMesh.h>
#include <maya/MPointArray.h>
#include <maya/MFloatPoint.h>
#include <maya/MFloatVector.h>
#include <maya/MStringArray.h>
#include <maya/MFnCamera.h>
#include <maya/MArgParser.h>
#include <maya/MArgList.h>
#include <maya/MToolsInfo.h>
#include <maya/MFnSingleIndexedComponent.h>
#include <maya/MColorArray.h>
#include <maya/MFnSkinCluster.h>
#include <maya/MDagPathArray.h>
#include <maya/MItMeshVertex.h>
#include <maya/MRenderer.h>
#include <maya/M3dView.h>

#include <cmath>
#include <cstdio>
#include <algorithm>
#include <queue>
#include <vector>
#include <string>

using namespace MHWRender;

namespace
{
    const double kPi = 3.14159265358979323846;
    const char* kContextClassName = "smoothInfluenceWeightsPaintContext";
    const char* kPreviewColorSetName = "siwPreviewColorSet";

    MColor paletteColor(unsigned int idx)
    {
        static const MColor palette[] = {
            MColor(1.0f, 0.0f, 0.0f),
            MColor(0.0f, 1.0f, 0.0f),
            MColor(0.0f, 0.0f, 1.0f),
            MColor(1.0f, 1.0f, 0.0f),
            MColor(1.0f, 0.0f, 1.0f),
            MColor(0.0f, 1.0f, 1.0f),
            MColor(1.0f, 0.5f, 0.0f),
            MColor(0.5f, 0.0f, 1.0f)
        };
        return palette[idx % (sizeof(palette) / sizeof(palette[0]))];
    }

    bool containsInt(const MIntArray& arr, int value)
    {
        for (unsigned int i = 0; i < arr.length(); ++i)
        {
            if (arr[i] == value)
                return true;
        }
        return false;
    }
}

void* SmoothInfluenceWeightsPaintContext::creator()
{
    return new SmoothInfluenceWeightsPaintContext();
}

void* SmoothInfluenceWeightsPaintContextCmd::creator()
{
    return new SmoothInfluenceWeightsPaintContextCmd();
}

SmoothInfluenceWeightsPaintContextCmd::SmoothInfluenceWeightsPaintContextCmd()
    : m_ctx(nullptr)
{
}

SmoothInfluenceWeightsPaintContextCmd::~SmoothInfluenceWeightsPaintContextCmd()
{
    m_ctx = nullptr;
}

MPxContext* SmoothInfluenceWeightsPaintContextCmd::makeObj()
{
    m_ctx = new SmoothInfluenceWeightsPaintContext();
    return m_ctx;
}

MStatus SmoothInfluenceWeightsPaintContextCmd::appendSyntax()
{
    MSyntax syn = syntax();
    syn.addFlag("-ko", "-keepOnes", MSyntax::kBoolean);
    syn.addFlag("-str", "-strength", MSyntax::kDouble);
    syn.addFlag("-it", "-iterations", MSyntax::kLong);
    syn.addFlag("-bd", "-bleedDistance", MSyntax::kLong);
    syn.addFlag("-bf", "-bleedFalloff", MSyntax::kDouble);
    syn.addFlag("-br", "-brushRadius", MSyntax::kDouble);
    syn.addFlag("-fd", "-falloffDistance", MSyntax::kDouble);
    syn.addFlag("-op", "-opacity", MSyntax::kDouble);
    syn.addFlag("-dop", "-displayOpacity", MSyntax::kDouble);
    syn.addFlag("-ddk", "-displayDarken", MSyntax::kDouble);
    syn.addFlag("-inf", "-influence", MSyntax::kString);
    syn.makeFlagMultiUse("-inf");
    syn.addFlag("-rp", "-refreshPreview");
    return MS::kSuccess;
}

MStatus SmoothInfluenceWeightsPaintContextCmd::doEditFlags()
{
    MStatus status;
    MArgParser argData = parser(&status);
    if (status != MS::kSuccess)
        return status;

    if (!m_ctx)
        return MS::kFailure;

    if (argData.isFlagSet("-ko"))
    {
        bool value = false;
        argData.getFlagArgument("-ko", 0, value);
        m_ctx->setKeepOnes(value);
    }

    if (argData.isFlagSet("-str"))
    {
        double value = 0.2;
        argData.getFlagArgument("-str", 0, value);
        m_ctx->setStrength(value);
    }

    if (argData.isFlagSet("-it"))
    {
        int value = 1;
        argData.getFlagArgument("-it", 0, value);
        m_ctx->setIterations(value);
    }

    if (argData.isFlagSet("-bd"))
    {
        int value = 1;
        argData.getFlagArgument("-bd", 0, value);
        m_ctx->setBleedDistance(value);
    }

    if (argData.isFlagSet("-bf"))
    {
        double value = 1.0;
        argData.getFlagArgument("-bf", 0, value);
        m_ctx->setBleedFalloff(value);
    }

    if (argData.isFlagSet("-br"))
    {
        double value = 40.0;
        argData.getFlagArgument("-br", 0, value);
        m_ctx->setBrushRadius(value);
    }

    if (argData.isFlagSet("-fd"))
    {
        double value = 20.0;
        argData.getFlagArgument("-fd", 0, value);
        m_ctx->setFalloffDistance(value);
    }

    if (argData.isFlagSet("-op"))
    {
        double value = 1.0;
        argData.getFlagArgument("-op", 0, value);
        m_ctx->setOpacity(value);
    }

    if (argData.isFlagSet("-dop"))
    {
        double value = 1.0;
        argData.getFlagArgument("-dop", 0, value);
        m_ctx->setDisplayOpacity(value);
    }

    if (argData.isFlagSet("-ddk"))
    {
        double value = 0.0;
        argData.getFlagArgument("-ddk", 0, value);
        m_ctx->setDisplayDarken(value);
    }

    if (argData.isFlagSet("-inf"))
    {
        MStringArray influences;
        const unsigned int uses = argData.numberOfFlagUses("-inf");
        for (unsigned int i = 0; i < uses; ++i)
        {
            MArgList oneUse;
            argData.getFlagArgumentList("-inf", i, oneUse);
            if (oneUse.length() > 0)
                influences.append(oneUse.asString(0));
        }
        m_ctx->setInfluences(influences);
    }

    if (argData.isFlagSet("-rp"))
    {
        m_ctx->refreshPreview();
    }
    else if (argData.isFlagSet("-ko") || argData.isFlagSet("-str") || argData.isFlagSet("-it") ||
             argData.isFlagSet("-bd") || argData.isFlagSet("-bf") || argData.isFlagSet("-br") ||
             argData.isFlagSet("-fd") || argData.isFlagSet("-op") || argData.isFlagSet("-dop") || argData.isFlagSet("-ddk") ||
             argData.isFlagSet("-inf"))
    {
        m_ctx->refreshPreview();
    }

    return MS::kSuccess;
}

MStatus SmoothInfluenceWeightsPaintContextCmd::doQueryFlags()
{
    MStatus status;
    MArgParser argData = parser(&status);
    if (status != MS::kSuccess)
        return status;

    if (!m_ctx)
        return MS::kFailure;

    if (argData.isFlagSet("-ko")) return setResult(m_ctx->keepOnes());
    if (argData.isFlagSet("-str")) return setResult(m_ctx->strength());
    if (argData.isFlagSet("-it")) return setResult(m_ctx->iterations());
    if (argData.isFlagSet("-bd")) return setResult(m_ctx->bleedDistance());
    if (argData.isFlagSet("-bf")) return setResult(m_ctx->bleedFalloff());
    if (argData.isFlagSet("-br")) return setResult(m_ctx->brushRadius());
    if (argData.isFlagSet("-fd")) return setResult(m_ctx->falloffDistance());
    if (argData.isFlagSet("-op")) return setResult(m_ctx->opacity());
    if (argData.isFlagSet("-dop")) return setResult(m_ctx->displayOpacity());
    if (argData.isFlagSet("-ddk")) return setResult(m_ctx->displayDarken());

    return MS::kSuccess;
}

SmoothInfluenceWeightsPaintContext::SmoothInfluenceWeightsPaintContext()
    : m_keepOnes(false)
    , m_strength(0.2)
    , m_iterations(1)
    , m_bleedDistance(2)
    , m_bleedFalloff(1.0)
    , m_brushRadiusPx(40.0)
    , m_falloffDistancePx(20.0)
    , m_opacity(1.0)
    , m_displayOpacity(1.0)
    , m_displayDarken(0.0)
    , m_stampSpacing(0.35)
    , m_isDragging(false)
    , m_hasCursorData(false)
    , m_hasLastStamp(false)
    , m_hitPoint(MPoint::origin)
    , m_hitNormal(MVector(0.0, 1.0, 0.0))
    , m_lastStampPoint(MPoint::origin)
    , m_worldRadius(1.0)
    , m_outerWorldRadius(1.5)
    , m_dragMode(kDragNone)
    , m_dragStartX(0)
    , m_dragStartY(0)
    , m_dragStartBrushRadiusPx(40.0)
    , m_dragStartStrength(0.2)
    , m_dragStartIterations(1)
    , m_dragStartBleedDistance(2)
    , m_dragStartBleedFalloff(1.0)
    , m_dragStartFalloffDistancePx(20.0)
    , m_dragStartOpacity(1.0)
    , m_hasStrokeUndoData(false)
{
    setTitleString("Smooth Influence Weights Paint");
    setImage("paintSkinWeights.png", MPxContext::kImage1);
    setHelpString("MMB=size | Shift+MMB=strength | Ctrl+MMB=iterations | Ctrl+Shift+MMB=falloff");
}

SmoothInfluenceWeightsPaintContext::~SmoothInfluenceWeightsPaintContext()
{
}

void SmoothInfluenceWeightsPaintContext::getClassName(MString& name) const
{
    name = kContextClassName;
}

void SmoothInfluenceWeightsPaintContext::markToolSettingsDirty() const
{
    MToolsInfo::setDirtyFlag(*this);
}

void SmoothInfluenceWeightsPaintContext::setKeepOnes(bool value) { m_keepOnes = value; markToolSettingsDirty(); }
void SmoothInfluenceWeightsPaintContext::setStrength(double value)
{
    m_strength = value;
    if (m_strength < 0.001) m_strength = 0.001;
    if (m_strength > 1.0) m_strength = 1.0;
    m_solverCache.invalidateWeightsOnly();
    markToolSettingsDirty();
}
void SmoothInfluenceWeightsPaintContext::setIterations(int value) { m_iterations = value < 1 ? 1 : value; markToolSettingsDirty(); }
void SmoothInfluenceWeightsPaintContext::setBleedDistance(int value) { m_bleedDistance = value < 1 ? 1 : value; markToolSettingsDirty(); }
void SmoothInfluenceWeightsPaintContext::setBleedFalloff(double value) { m_bleedFalloff = value < 0.0 ? 0.0 : value; markToolSettingsDirty(); }
void SmoothInfluenceWeightsPaintContext::setBrushRadius(double value) { m_brushRadiusPx = value < 2.0 ? 2.0 : value; markToolSettingsDirty(); }
void SmoothInfluenceWeightsPaintContext::setInfluences(const MStringArray& influences) { m_influences = influences; markToolSettingsDirty(); }

void SmoothInfluenceWeightsPaintContext::refreshPreview()
{
    MDagPath meshPath;
    if (resolveTargetMesh(meshPath))
    {
        updatePreviewColors(meshPath);
        MHWRender::MRenderer::setGeometryDrawDirty(meshPath);
        M3dView::active3dView().refresh(false, true);
    }
}


void SmoothInfluenceWeightsPaintContext::setFalloffDistance(double value)
{
    m_falloffDistancePx = value < 0.0 ? 0.0 : value;
    markToolSettingsDirty();
}

void SmoothInfluenceWeightsPaintContext::setOpacity(double value)
{
    m_opacity = value;
    if (m_opacity < 0.0) m_opacity = 0.0;
    if (m_opacity > 1.0) m_opacity = 1.0;
    markToolSettingsDirty();
}

void SmoothInfluenceWeightsPaintContext::setDisplayOpacity(double value)
{
    m_displayOpacity = value;
    if (m_displayOpacity < 0.0) m_displayOpacity = 0.0;
    if (m_displayOpacity > 1.0) m_displayOpacity = 1.0;
    markToolSettingsDirty();
}

void SmoothInfluenceWeightsPaintContext::setDisplayDarken(double value)
{
    m_displayDarken = value;
    if (m_displayDarken < 0.0) m_displayDarken = 0.0;
    if (m_displayDarken > 1.0) m_displayDarken = 1.0;
    markToolSettingsDirty();
}


void SmoothInfluenceWeightsPaintContext::toolOnSetup(MEvent&)
{
    setHelpString("MMB=size | Shift+MMB=strength | Ctrl+MMB=iterations | Ctrl+Shift+MMB=falloff");
    m_hasLastStamp = false;
    m_dragMode = kDragNone;
    resetStrokeUndoState();
    m_solverCache.invalidate();
    refreshPreview();
}

void SmoothInfluenceWeightsPaintContext::toolOffCleanup()
{
    m_isDragging = false;
    m_hasCursorData = false;
    m_hasLastStamp = false;
    m_dragMode = kDragNone;
    resetStrokeUndoState();
    m_solverCache.invalidate();
}

MStatus SmoothInfluenceWeightsPaintContext::doEnterRegion(MEvent&)
{
    setHelpString("MMB=size | Shift+MMB=strength | Ctrl+MMB=iterations | Ctrl+Shift+MMB=falloff");
    return MS::kSuccess;
}

void SmoothInfluenceWeightsPaintContext::beginResizeMode(short x, short y, DragMode mode)
{
    m_dragMode = mode;
    m_dragStartX = x;
    m_dragStartY = y;
    m_dragStartBrushRadiusPx = m_brushRadiusPx;
    m_dragStartStrength = m_strength;
    m_dragStartIterations = m_iterations;
    m_dragStartBleedDistance = m_bleedDistance;
    m_dragStartBleedFalloff = m_bleedFalloff;
    m_dragStartFalloffDistance = m_falloffDistancePx;

    switch (mode)
    {
    case kDragResizeRadius:
        setHelpString("Adjust Brush Size: MMB drag left/right");
        break;
    case kDragAdjustStrength:
        setHelpString("Adjust Strength: Shift+MMB drag left/right");
        break;
    case kDragAdjustIterations:
        setHelpString("Adjust Iterations: Ctrl+MMB drag left/right");
        break;
    case kDragAdjustBleedCombined:
        setHelpString("Adjust Falloff Size: Ctrl+Shift+MMB drag left/right");
        break;
    case kDragAdjustFalloffDistance:
        setHelpString("Adjust Falloff Size: Ctrl+Shift+MMB drag left/right");
        break;
    default:
        break;
    }
}

void SmoothInfluenceWeightsPaintContext::updateResizeMode(short x, short y)
{
    const double deltaX = static_cast<double>(x - m_dragStartX);
    const double deltaY = static_cast<double>(m_dragStartY - y);

    if (m_dragMode == kDragResizeRadius)
    {
        setBrushRadius(m_dragStartBrushRadiusPx + deltaX);
        MString msg = "Brush Size: "; msg += m_brushRadiusPx; msg += " px"; setHelpString(msg.asChar());
    }
    else if (m_dragMode == kDragAdjustStrength)
    {
        setStrength(m_dragStartStrength + (deltaX * 0.005));
        MString msg = "Strength: "; msg += m_strength; setHelpString(msg.asChar());
    }
    else if (m_dragMode == kDragAdjustIterations)
    {
        const int step = static_cast<int>(deltaX / 14.0);
        setIterations(m_dragStartIterations + step);
        MString msg = "Iterations: "; msg += m_iterations; setHelpString(msg.asChar());
    }
    else if (m_dragMode == kDragAdjustBleedCombined)
    {
        const int distStep = static_cast<int>(deltaX / 14.0);
        setBleedDistance(m_dragStartBleedDistance + distStep);
        setBleedFalloff(m_dragStartBleedFalloff + (deltaY * 0.01));
        setFalloffDistance(m_dragStartFalloffDistancePx + deltaX);
        MString msg = "Falloff Size: "; msg += m_falloffDistancePx; msg += " px"; setHelpString(msg.asChar());
    }
    else if (m_dragMode == kDragAdjustFalloffDistance)
    {
        setFalloffDistance(m_dragStartFalloffDistance + deltaX);
        MString msg = "Falloff Size: "; msg += m_falloffDistancePx; msg += " px"; setHelpString(msg.asChar());
    }
}

void SmoothInfluenceWeightsPaintContext::endResizeMode()
{
    m_dragMode = kDragNone;
    setHelpString("MMB=size | Shift+MMB=strength | Ctrl+MMB=iterations | Ctrl+Shift+MMB=falloff");
}

MStatus SmoothInfluenceWeightsPaintContext::handlePress(MEvent& event, MHWRender::MUIDrawManager* drawMgr)
{
    short x = 0, y = 0;
    event.getPosition(x, y);

    m_isDragging = true;
    m_hasLastStamp = false;
    resetStrokeUndoState();

    if (event.mouseButton() == MEvent::kMiddleMouse)
    {
        const bool shift = event.isModifierShift();
        const bool ctrl = event.isModifierControl();

        if (ctrl && shift)
            beginResizeMode(x, y, kDragAdjustFalloffDistance);
        else if (ctrl)
            beginResizeMode(x, y, kDragAdjustIterations);
        else if (shift)
            beginResizeMode(x, y, kDragAdjustStrength);
        else
            beginResizeMode(x, y, kDragResizeRadius);

        updateHitFromMouse(x, y);
        if (drawMgr) drawBrush(*drawMgr);
        return MS::kSuccess;
    }

    m_dragMode = kDragPaint;
    updateHitFromMouse(x, y);
    if (drawMgr) drawBrush(*drawMgr);

    MDagPath meshPath;
    if (!resolveTargetMesh(meshPath))
        return MS::kSuccess;

    MIntArray verts;
    MDoubleArray opacities;
    if (m_hasCursorData && shouldStamp(m_hitPoint) &&
        collectVertsAndOpacities(meshPath, m_hitPoint, m_worldRadius, m_outerWorldRadius, m_opacity, verts, opacities))
    {
        appendStrokeUndoVerts(meshPath, verts);
        applyCommandToVerts(meshPath, verts, opacities);
        recordStamp(m_hitPoint);
        updatePreviewColorsSubset(meshPath, verts);
        MHWRender::MRenderer::setGeometryDrawDirty(meshPath);
        M3dView::active3dView().refresh(false, true);
    }

    return MS::kSuccess;
}

MStatus SmoothInfluenceWeightsPaintContext::handleDrag(MEvent& event, MHWRender::MUIDrawManager* drawMgr)
{
    short x = 0, y = 0;
    event.getPosition(x, y);
    updateHitFromMouse(x, y);

    if (m_dragMode == kDragResizeRadius || m_dragMode == kDragAdjustStrength ||
        m_dragMode == kDragAdjustIterations || m_dragMode == kDragAdjustBleedCombined ||
        m_dragMode == kDragAdjustFalloffDistance)
    {
        updateResizeMode(x, y);
        if (drawMgr) drawBrush(*drawMgr);
        return MS::kSuccess;
    }

    if (drawMgr) drawBrush(*drawMgr);

    MDagPath meshPath;
    if (!resolveTargetMesh(meshPath))
        return MS::kSuccess;

    MIntArray verts;
    MDoubleArray opacities;
    if (m_hasCursorData && shouldStamp(m_hitPoint) &&
        collectVertsAndOpacities(meshPath, m_hitPoint, m_worldRadius, m_outerWorldRadius, m_opacity, verts, opacities))
    {
        appendStrokeUndoVerts(meshPath, verts);
        applyCommandToVerts(meshPath, verts, opacities);
        recordStamp(m_hitPoint);
        updatePreviewColorsSubset(meshPath, verts);
        MHWRender::MRenderer::setGeometryDrawDirty(meshPath);
        M3dView::active3dView().refresh(false, true);
    }

    return MS::kSuccess;
}

MStatus SmoothInfluenceWeightsPaintContext::handleRelease(MEvent&, MHWRender::MUIDrawManager* drawMgr)
{
    finalizeStrokeUndo();
    m_isDragging = false;
    m_hasLastStamp = false;
    if (m_dragMode == kDragResizeRadius || m_dragMode == kDragAdjustStrength ||
        m_dragMode == kDragAdjustIterations || m_dragMode == kDragAdjustBleedCombined ||
        m_dragMode == kDragAdjustFalloffDistance)
        endResizeMode();
    m_dragMode = kDragNone;
    if (drawMgr) drawBrush(*drawMgr);
    return MS::kSuccess;
}

MStatus SmoothInfluenceWeightsPaintContext::doPress(MEvent& event) { return handlePress(event, nullptr); }
MStatus SmoothInfluenceWeightsPaintContext::doDrag(MEvent& event) { return handleDrag(event, nullptr); }
MStatus SmoothInfluenceWeightsPaintContext::doRelease(MEvent& event) { return handleRelease(event, nullptr); }
MStatus SmoothInfluenceWeightsPaintContext::doPress(MEvent& event, MUIDrawManager& drawMgr, const MFrameContext&) { return handlePress(event, &drawMgr); }
MStatus SmoothInfluenceWeightsPaintContext::doDrag(MEvent& event, MUIDrawManager& drawMgr, const MFrameContext&) { return handleDrag(event, &drawMgr); }
MStatus SmoothInfluenceWeightsPaintContext::doRelease(MEvent& event, MUIDrawManager& drawMgr, const MFrameContext&) { return handleRelease(event, &drawMgr); }

MStatus SmoothInfluenceWeightsPaintContext::doPtrMoved(MEvent& event, MUIDrawManager& drawMgr, const MFrameContext&)
{
    short x = 0, y = 0;
    event.getPosition(x, y);
    updateHitFromMouse(x, y);
    drawBrush(drawMgr);
    return MS::kSuccess;
}


void SmoothInfluenceWeightsPaintContext::resetStrokeUndoState()
{
    m_strokeUndoVertSet.clear();
    m_strokeUndoVerts.clear();
    m_strokeUndoBeforeWeights.clear();
    m_strokeUndoMeshPath = MDagPath();
    m_strokeUndoSkinObj = MObject::kNullObj;
    m_hasStrokeUndoData = false;
}

bool SmoothInfluenceWeightsPaintContext::readWeightsForVerts(const MObject& skinObj,
                                                             const MDagPath& meshPath,
                                                             const MIntArray& verts,
                                                             MDoubleArray& outWeights,
                                                             MIntArray* outInfluenceIndices,
                                                             unsigned int* outInfluenceCount) const
{
    outWeights.clear();
    if (verts.length() == 0)
        return true;

    MStatus status;
    MFnSingleIndexedComponent compFn;
    MObject comp = compFn.create(MFn::kMeshVertComponent, &status);
    if (status != MS::kSuccess)
        return false;

    MIntArray vertsCopy(verts);
    status = compFn.addElements(vertsCopy);
    if (status != MS::kSuccess)
        return false;

    MFnSkinCluster skinFn(skinObj, &status);
    if (status != MS::kSuccess)
        return false;

    unsigned int influenceCount = 0;
    status = skinFn.getWeights(meshPath, comp, outWeights, influenceCount);
    if (status != MS::kSuccess)
        return false;

    if (outInfluenceIndices)
    {
        outInfluenceIndices->setLength(influenceCount);
        for (unsigned int i = 0; i < influenceCount; ++i)
            (*outInfluenceIndices)[i] = static_cast<int>(i);
    }

    if (outInfluenceCount)
        *outInfluenceCount = influenceCount;

    return true;
}

bool SmoothInfluenceWeightsPaintContext::appendStrokeUndoVerts(const MDagPath& meshPath, const MIntArray& verts)
{
    if (verts.length() == 0)
        return true;

    MObject skinObj;
    if (!resolveSkinCluster(meshPath, skinObj))
        return false;

    if (!m_hasStrokeUndoData)
    {
        m_strokeUndoMeshPath = meshPath;
        m_strokeUndoSkinObj = skinObj;
        m_hasStrokeUndoData = true;
    }

    MIntArray newVerts;
    for (unsigned int i = 0; i < verts.length(); ++i)
    {
        const int v = verts[i];
        if (m_strokeUndoVertSet.insert(v).second)
            newVerts.append(v);
    }

    if (newVerts.length() == 0)
        return true;

    MDoubleArray newBeforeWeights;
    unsigned int influenceCount = 0;
    if (!readWeightsForVerts(m_strokeUndoSkinObj,
                             m_strokeUndoMeshPath,
                             newVerts,
                             newBeforeWeights,
                             nullptr,
                             &influenceCount))
    {
        return false;
    }

    struct RowData
    {
        int vtx;
        std::vector<double> weights;
    };

    std::vector<RowData> rows;
    rows.reserve(static_cast<size_t>(m_strokeUndoVerts.length() + newVerts.length()));

    if (m_strokeUndoVerts.length() > 0)
    {
        for (unsigned int i = 0; i < m_strokeUndoVerts.length(); ++i)
        {
            RowData row;
            row.vtx = m_strokeUndoVerts[i];
            row.weights.resize(influenceCount);

            const unsigned int base = i * influenceCount;
            for (unsigned int inf = 0; inf < influenceCount; ++inf)
                row.weights[inf] = m_strokeUndoBeforeWeights[base + inf];

            rows.push_back(row);
        }
    }

    for (unsigned int i = 0; i < newVerts.length(); ++i)
    {
        RowData row;
        row.vtx = newVerts[i];
        row.weights.resize(influenceCount);

        const unsigned int base = i * influenceCount;
        for (unsigned int inf = 0; inf < influenceCount; ++inf)
            row.weights[inf] = newBeforeWeights[base + inf];

        rows.push_back(row);
    }

    std::sort(rows.begin(), rows.end(),
        [](const RowData& a, const RowData& b)
        {
            return a.vtx < b.vtx;
        });

    m_strokeUndoVerts.setLength(static_cast<unsigned int>(rows.size()));
    m_strokeUndoBeforeWeights.setLength(static_cast<unsigned int>(rows.size() * influenceCount));

    for (unsigned int i = 0; i < static_cast<unsigned int>(rows.size()); ++i)
    {
        m_strokeUndoVerts[i] = rows[i].vtx;
        const unsigned int base = i * influenceCount;
        for (unsigned int inf = 0; inf < influenceCount; ++inf)
            m_strokeUndoBeforeWeights[base + inf] = rows[i].weights[inf];
    }

    return true;
}

void SmoothInfluenceWeightsPaintContext::finalizeStrokeUndo()
{
    if (!m_hasStrokeUndoData || m_strokeUndoVerts.length() == 0)
        return;

    struct RowData
    {
        int vtx;
        std::vector<double> weights;
    };

    MStatus status;
    MFnSkinCluster skinFn(m_strokeUndoSkinObj, &status);
    if (status != MS::kSuccess)
    {
        resetStrokeUndoState();
        return;
    }

    MDagPathArray infPaths;
    const unsigned int influenceCount = skinFn.influenceObjects(infPaths, &status);
    if (status != MS::kSuccess || influenceCount == 0)
    {
        resetStrokeUndoState();
        return;
    }

    std::vector<RowData> rows;
    rows.reserve(static_cast<size_t>(m_strokeUndoVerts.length()));

    for (unsigned int i = 0; i < m_strokeUndoVerts.length(); ++i)
    {
        RowData row;
        row.vtx = m_strokeUndoVerts[i];
        row.weights.resize(influenceCount);

        const unsigned int base = i * influenceCount;
        for (unsigned int inf = 0; inf < influenceCount; ++inf)
            row.weights[inf] = m_strokeUndoBeforeWeights[base + inf];

        rows.push_back(row);
    }

    std::sort(rows.begin(), rows.end(),
        [](const RowData& a, const RowData& b)
        {
            return a.vtx < b.vtx;
        });

    MIntArray sortedVerts;
    sortedVerts.setLength(static_cast<unsigned int>(rows.size()));
    MDoubleArray sortedBeforeWeights;
    sortedBeforeWeights.setLength(static_cast<unsigned int>(rows.size() * influenceCount));

    for (unsigned int i = 0; i < static_cast<unsigned int>(rows.size()); ++i)
    {
        sortedVerts[i] = rows[i].vtx;
        const unsigned int base = i * influenceCount;
        for (unsigned int inf = 0; inf < influenceCount; ++inf)
            sortedBeforeWeights[base + inf] = rows[i].weights[inf];
    }

    StrokeUndoDataStore::StrokeData data;
    data.valid = true;
    data.skinObj = m_strokeUndoSkinObj;
    data.meshPath = m_strokeUndoMeshPath;
    data.verts = sortedVerts;
    data.beforeWeights = sortedBeforeWeights;

    if (!readWeightsForVerts(m_strokeUndoSkinObj,
                             m_strokeUndoMeshPath,
                             sortedVerts,
                             data.afterWeights,
                             &data.influenceIndices,
                             &data.influenceCount))
    {
        resetStrokeUndoState();
        return;
    }

    const unsigned int undoId = StrokeUndoDataStore::setPending(data);
    MString undoCmd = MString(SmoothInfluenceWeightsStrokeUndoCmd::kCommandName)
                    + " "
                    + SmoothInfluenceWeightsStrokeUndoCmd::kIdFlag
                    + " "
                    + std::to_string(undoId).c_str();
    // Maya records one undoable command for the entire completed stroke.
    // The command consumes the snapshot into its own lifetime-owned data.
    const MStatus undoStatus = MGlobal::executeCommand(undoCmd, false, true);
    if (undoStatus != MS::kSuccess)
    {
        StrokeUndoDataStore::discardPending(undoId);
        MGlobal::displayWarning("SIW: failed to register stroke undo command.");
    }

    resetStrokeUndoState();
}


bool SmoothInfluenceWeightsPaintContext::resolveTargetMesh(MDagPath& outMeshPath) const
{
    MSelectionList sel;
    MGlobal::getActiveSelectionList(sel);
    if (sel.length() == 0) return false;

    for (unsigned int i = 0; i < sel.length(); ++i)
    {
        MDagPath path; MObject comp;
        if (sel.getDagPath(i, path, comp) != MS::kSuccess) continue;

        if (path.node().hasFn(MFn::kMesh))
        {
            outMeshPath = path;
            return true;
        }

        if (path.node().hasFn(MFn::kTransform))
        {
            const unsigned int childCount = path.childCount();
            for (unsigned int c = 0; c < childCount; ++c)
            {
                MObject child = path.child(c);
                if (child.hasFn(MFn::kMesh))
                {
                    MDagPath shapePath = path;
                    if (shapePath.extendToShapeDirectlyBelow(c) == MS::kSuccess)
                    {
                        outMeshPath = shapePath;
                        return true;
                    }
                }
            }
        }
    }
    return false;
}

bool SmoothInfluenceWeightsPaintContext::resolveSkinCluster(const MDagPath& meshPath, MObject& outSkinObj) const
{
    MString cmd = "findRelatedSkinCluster \"";
    cmd += meshPath.fullPathName();
    cmd += "\"";

    MString result;
    if (MGlobal::executeCommand(cmd, result, false, false) != MS::kSuccess) return false;
    if (result.length() == 0) return false;

    MSelectionList sel;
    if (sel.add(result) != MS::kSuccess) return false;
    return sel.getDependNode(0, outSkinObj) == MS::kSuccess;
}

double SmoothInfluenceWeightsPaintContext::computeWorldRadiusForPixels(const MPoint& hitPoint, double radiusPx) const
{
    M3dView view = M3dView::active3dView();
    MDagPath cameraPath;
    if (view.getCamera(cameraPath) != MS::kSuccess) return 1.0;

    MStatus status;
    MFnCamera camFn(cameraPath, &status);
    if (status != MS::kSuccess) return 1.0;

    const MPoint eye = camFn.eyePoint(MSpace::kWorld, &status);
    if (status != MS::kSuccess) return 1.0;

    const double distance = (hitPoint - eye).length();
    const double vfov = camFn.verticalFieldOfView(&status);
    if (status != MS::kSuccess) return 1.0;

    const unsigned int portH = static_cast<unsigned int>(view.portHeight());
    if (portH == 0) return 1.0;

    const double worldPerPixel = (2.0 * distance * std::tan(vfov * 0.5)) / static_cast<double>(portH);
    const double result = radiusPx * worldPerPixel;
    return (result > 1.0e-5) ? result : 1.0e-5;
}

double SmoothInfluenceWeightsPaintContext::computeWorldRadius(const MPoint& hitPoint) const
{
    return computeWorldRadiusForPixels(hitPoint, m_brushRadiusPx);
}

bool SmoothInfluenceWeightsPaintContext::computeAverageNormal(const MDagPath& meshPath, const MPoint& center, double radius, MVector& outNormal) const
{
    MStatus status;
    MFnMesh fnMesh(meshPath, &status);
    if (status != MS::kSuccess) return false;

    MPointArray points;
    fnMesh.getPoints(points, MSpace::kWorld);
    const unsigned int numVerts = points.length();
    if (numVerts == 0) return false;

    const double r2 = radius * radius;

    std::vector<char> candidate(numVerts, 0);
    int seedVert = -1;
    double bestSeedD2 = 1.0e100;

    for (unsigned int i = 0; i < numVerts; ++i)
    {
        MVector d = points[i] - center;
        const double d2 = (d.x * d.x) + (d.y * d.y) + (d.z * d.z);
        if (d2 > r2) continue;

        candidate[i] = 1;
        if (d2 < bestSeedD2)
        {
            bestSeedD2 = d2;
            seedVert = static_cast<int>(i);
        }
    }

    if (seedVert < 0) return false;

    std::vector<char> visited(numVerts, 0);
    std::queue<int> q;
    q.push(seedVert);
    visited[seedVert] = 1;

    while (!q.empty())
    {
        const int vtx = q.front();
        q.pop();

        MItMeshVertex itVert(meshPath);
        int prevIndex = 0;

        if (itVert.setIndex(vtx, prevIndex) != MS::kSuccess)
            continue;

        MIntArray connected;
        if (itVert.getConnectedVertices(connected) != MS::kSuccess)
            continue;

        for (unsigned int i = 0; i < connected.length(); ++i)
        {
            const int n = connected[i];
            if (n < 0 || static_cast<unsigned int>(n) >= numVerts)
                continue;
            if (!candidate[n] || visited[n])
                continue;

            visited[n] = 1;
            q.push(n);
        }
    }

    MVector sum(0.0, 0.0, 0.0);
    double totalWeight = 0.0;

    for (unsigned int i = 0; i < numVerts; ++i)
    {
        if (!visited[i]) continue;

        MVector d = points[i] - center;
        const double dist = d.length();
        double w = 1.0;
        if (radius > 1.0e-8)
        {
            w = 1.0 - std::min(1.0, dist / radius);
        }
        if (w <= 1.0e-8) continue;

        MVector n;
        status = fnMesh.getVertexNormal(static_cast<int>(i), true, n, MSpace::kWorld);
        if (status != MS::kSuccess) continue;
        if (n.length() < 1.0e-8) continue;

        sum += (n.normal() * w);
        totalWeight += w;
    }

    if (totalWeight <= 1.0e-8 || sum.length() < 1.0e-8) return false;
    sum.normalize();
    outNormal = sum;
    return true;
}

bool SmoothInfluenceWeightsPaintContext::updateHitFromMouse(short x, short y)
{
    M3dView view = M3dView::active3dView();
    MPoint worldPoint; MVector worldDir;
    if (view.viewToWorld(x, y, worldPoint, worldDir) != MS::kSuccess) { m_hasCursorData = false; return false; }

    MDagPath meshPath;
    if (!resolveTargetMesh(meshPath)) { m_hasCursorData = false; return false; }

    MStatus status;
    MFnMesh fnMesh(meshPath, &status);
    if (status != MS::kSuccess) { m_hasCursorData = false; return false; }

    MFloatPoint hitPoint; float hitRayParam = 0.0f; int hitFace = -1; int hitTri = -1;
    const bool hit = fnMesh.closestIntersection(
        MFloatPoint(worldPoint), MFloatVector(worldDir), nullptr, nullptr, false,
        MSpace::kWorld, 1.0e6f, false, nullptr, hitPoint, &hitRayParam, &hitFace, &hitTri,
        nullptr, nullptr, 1.0e-6f, &status);

    if (!hit || status != MS::kSuccess) { m_hasCursorData = false; return false; }

    m_hitPoint = MPoint(hitPoint);
    m_worldRadius = computeWorldRadiusForPixels(m_hitPoint, m_brushRadiusPx);
    m_outerWorldRadius = computeWorldRadiusForPixels(m_hitPoint, m_brushRadiusPx + m_falloffDistancePx);

    MVector avgNormal;
    if (computeAverageNormal(meshPath, m_hitPoint, m_worldRadius, avgNormal))
        m_hitNormal = avgNormal;
    else
        m_hitNormal = MVector(0.0, 1.0, 0.0);

    m_hasCursorData = true;
    return true;
}

bool SmoothInfluenceWeightsPaintContext::collectVertsAndOpacities(const MDagPath& meshPath,
                                  const MPoint& center,
                                  double innerRadius,
                                  double outerRadius,
                                  double opacity,
                                  MIntArray& outVerts,
                                  MDoubleArray& outOpacities) const
{
    outVerts.clear();
    outOpacities.clear();

    MStatus status;
    MFnMesh fnMesh(meshPath, &status);
    if (status != MS::kSuccess) return false;

    MPointArray points;
    fnMesh.getPoints(points, MSpace::kWorld);

    const double inner2 = innerRadius * innerRadius;
    const double outer2 = outerRadius * outerRadius;
    const double effectiveOpacity = std::max(0.0, std::min(1.0, opacity));
    const double falloffWidth = std::max(0.0, outerRadius - innerRadius);

    for (unsigned int i = 0; i < points.length(); ++i)
    {
        MVector d = points[i] - center;
        const double d2 = (d.x * d.x) + (d.y * d.y) + (d.z * d.z);
        if (d2 > outer2)
            continue;

        double alpha = effectiveOpacity;
        if (d2 > inner2 && falloffWidth > 1.0e-8)
        {
            const double dist = std::sqrt(d2);
            const double t = (dist - innerRadius) / falloffWidth;
            alpha = effectiveOpacity * std::max(0.0, 1.0 - t);
        }

        if (alpha <= 1.0e-8)
            continue;

        outVerts.append(static_cast<int>(i));
        outOpacities.append(alpha);
    }

    return outVerts.length() > 0;
}

bool SmoothInfluenceWeightsPaintContext::shouldStamp(const MPoint& point) const
{
    if (!m_hasLastStamp) return true;
    const double minDist = m_worldRadius * m_stampSpacing;
    return (point - m_lastStampPoint).length() >= minDist;
}

void SmoothInfluenceWeightsPaintContext::recordStamp(const MPoint& point)
{
    m_lastStampPoint = point;
    m_hasLastStamp = true;
}

bool SmoothInfluenceWeightsPaintContext::applyCommandToVerts(const MDagPath& meshPath, const MIntArray& verts, const MDoubleArray& opacities)
{
    if (verts.length() == 0)
        return false;

    MObject skinObj;
    if (!resolveSkinCluster(meshPath, skinObj))
        return false;

    m_solverCache.configure(
        meshPath,
        skinObj,
        m_keepOnes,
        m_strength,
        m_iterations,
        m_bleedDistance,
        m_bleedFalloff,
        m_influences);

    return m_solverCache.solveAndApplyToBrush(verts, opacities) == MS::kSuccess;
}

bool SmoothInfluenceWeightsPaintContext::updatePreviewColors(const MDagPath& meshPath) const
{
    MObject skinObj;
    if (!resolveSkinCluster(meshPath, skinObj))
        return false;

    MStatus status;
    MFnSkinCluster skinFn(skinObj, &status);
    if (status != MS::kSuccess)
        return false;

    MFnMesh meshFn(meshPath, &status);
    if (status != MS::kSuccess)
        return false;

    MPointArray points;
    meshFn.getPoints(points, MSpace::kObject);
    const unsigned int vertexCount = points.length();
    if (vertexCount == 0)
        return false;

    MIntArray allVerts;
    allVerts.setLength(vertexCount);
    for (unsigned int i = 0; i < vertexCount; ++i)
        allVerts[i] = static_cast<int>(i);

    MFnSingleIndexedComponent compFn;
    MObject compObj = compFn.create(MFn::kMeshVertComponent, &status);
    if (status != MS::kSuccess)
        return false;

    status = compFn.addElements(allVerts);
    if (status != MS::kSuccess)
        return false;

    MDoubleArray weights;
    unsigned int influenceCount = 0;
    status = skinFn.getWeights(meshPath, compObj, weights, influenceCount);
    if (status != MS::kSuccess || influenceCount == 0)
        return false;

    MDagPathArray infPaths;
    const unsigned int infCount = skinFn.influenceObjects(infPaths);
    if (infCount == 0 || infPaths.length() == 0)
        return false;

    MIntArray selectedIndices;
    if (m_influences.length() == 0)
    {
        selectedIndices.setLength(influenceCount);
        for (unsigned int i = 0; i < influenceCount; ++i)
            selectedIndices[i] = static_cast<int>(i);
    }
    else
    {
        for (unsigned int i = 0; i < infPaths.length(); ++i)
        {
            const MString fullName = infPaths[i].fullPathName();
            const MString partialName = infPaths[i].partialPathName();

            for (unsigned int j = 0; j < m_influences.length(); ++j)
            {
                if (m_influences[j] == fullName || m_influences[j] == partialName)
                {
                    if (!containsInt(selectedIndices, static_cast<int>(i)))
                        selectedIndices.append(static_cast<int>(i));
                }
            }
        }
    }

    if (selectedIndices.length() == 0)
    {
        selectedIndices.setLength(influenceCount);
        for (unsigned int i = 0; i < influenceCount; ++i)
            selectedIndices[i] = static_cast<int>(i);
    }

    const bool singleInfluence = (m_influences.length() == 1 && selectedIndices.length() == 1);

    MColorArray colors;
    colors.setLength(vertexCount);

    for (unsigned int v = 0; v < vertexCount; ++v)
    {
        if (singleInfluence)
        {
            const unsigned int idx = static_cast<unsigned int>(selectedIndices[0]);
            const float g = static_cast<float>(weights[(v * influenceCount) + idx]);
            float dg = g * static_cast<float>(1.0 - m_displayDarken);
            colors[v] = MColor(dg, dg, dg, static_cast<float>(m_displayOpacity));
        }
        else
        {
            MColor accum(0.0f, 0.0f, 0.0f, 1.0f);
            double sumW = 0.0;

            for (unsigned int s = 0; s < selectedIndices.length(); ++s)
            {
                const unsigned int idx = static_cast<unsigned int>(selectedIndices[s]);
                const double w = weights[(v * influenceCount) + idx];
                if (w <= 0.0)
                    continue;

                const MColor c = paletteColor(idx);
                accum.r += static_cast<float>(c.r * w);
                accum.g += static_cast<float>(c.g * w);
                accum.b += static_cast<float>(c.b * w);
                sumW += w;
            }

            if (m_influences.length() > 1 && sumW > 1.0e-8)
            {
                accum.r /= static_cast<float>(sumW);
                accum.g /= static_cast<float>(sumW);
                accum.b /= static_cast<float>(sumW);
            }

            accum.r *= static_cast<float>(1.0 - m_displayDarken);
            accum.g *= static_cast<float>(1.0 - m_displayDarken);
            accum.b *= static_cast<float>(1.0 - m_displayDarken);
            accum.a = static_cast<float>(m_displayOpacity);
            colors[v] = accum;
        }
    }

    MStringArray sets;
    meshFn.getColorSetNames(sets);

    bool hasSet = false;
    for (unsigned int i = 0; i < sets.length(); ++i)
    {
        if (sets[i] == kPreviewColorSetName)
        {
            hasSet = true;
            break;
        }
    }

    if (!hasSet)
    {
        meshFn.createColorSetWithName(kPreviewColorSetName);
    }

    status = meshFn.setCurrentColorSetName(kPreviewColorSetName);
    if (status != MS::kSuccess)
        return false;

    status = meshFn.setVertexColors(colors, allVerts);
    if (status != MS::kSuccess)
        return false;

    status = meshFn.setDisplayColors(true);
    if (status != MS::kSuccess)
        return false;
    MHWRender::MRenderer::setGeometryDrawDirty(meshPath);
    return true;
}


bool SmoothInfluenceWeightsPaintContext::updatePreviewColorsSubset(const MDagPath& meshPath, const MIntArray& verts) const
{
    if (verts.length() == 0)
        return true;

    MObject skinObj;
    if (!resolveSkinCluster(meshPath, skinObj))
        return false;

    MStatus status;
    MFnSkinCluster skinFn(skinObj, &status);
    if (status != MS::kSuccess)
        return false;

    MFnMesh meshFn(meshPath, &status);
    if (status != MS::kSuccess)
        return false;

    MFnSingleIndexedComponent compFn;
    MObject compObj = compFn.create(MFn::kMeshVertComponent, &status);
    if (status != MS::kSuccess)
        return false;

    MIntArray vertsCopy(verts);
    status = compFn.addElements(vertsCopy);
    if (status != MS::kSuccess)
        return false;

    MDoubleArray weights;
    unsigned int influenceCount = 0;
    status = skinFn.getWeights(meshPath, compObj, weights, influenceCount);
    if (status != MS::kSuccess || influenceCount == 0)
        return false;

    MDagPathArray infPaths;
    const unsigned int infCount = skinFn.influenceObjects(infPaths, &status);
    if (status != MS::kSuccess || infCount == 0 || infPaths.length() == 0)
        return false;

    MIntArray selectedIndices;
    if (m_influences.length() == 0)
    {
        selectedIndices.setLength(influenceCount);
        for (unsigned int i = 0; i < influenceCount; ++i)
            selectedIndices[i] = static_cast<int>(i);
    }
    else
    {
        for (unsigned int i = 0; i < infPaths.length(); ++i)
        {
            const MString fullName = infPaths[i].fullPathName();
            const MString partialName = infPaths[i].partialPathName();

            for (unsigned int j = 0; j < m_influences.length(); ++j)
            {
                if (m_influences[j] == fullName || m_influences[j] == partialName)
                {
                    bool exists = false;
                    for (unsigned int k = 0; k < selectedIndices.length(); ++k)
                    {
                        if (selectedIndices[k] == static_cast<int>(i))
                        {
                            exists = true;
                            break;
                        }
                    }
                    if (!exists)
                        selectedIndices.append(static_cast<int>(i));
                }
            }
        }
    }

    if (selectedIndices.length() == 0)
    {
        selectedIndices.setLength(influenceCount);
        for (unsigned int i = 0; i < influenceCount; ++i)
            selectedIndices[i] = static_cast<int>(i);
    }

    const bool singleInfluence = (m_influences.length() == 1 && selectedIndices.length() == 1);

    MColorArray colors;
    colors.setLength(verts.length());

    for (unsigned int v = 0; v < verts.length(); ++v)
    {
        if (singleInfluence)
        {
            const unsigned int idx = static_cast<unsigned int>(selectedIndices[0]);
            const float g = static_cast<float>(weights[(v * influenceCount) + idx]);
            float dg = g * static_cast<float>(1.0 - m_displayDarken);
            colors[v] = MColor(dg, dg, dg, static_cast<float>(m_displayOpacity));
        }
        else
        {
            MColor accum(0.0f, 0.0f, 0.0f, 1.0f);
            double sumW = 0.0;

            for (unsigned int s = 0; s < selectedIndices.length(); ++s)
            {
                const unsigned int idx = static_cast<unsigned int>(selectedIndices[s]);
                const double w = weights[(v * influenceCount) + idx];
                if (w <= 0.0)
                    continue;

                MColor c;
                switch (idx % 8)
                {
                case 0: c = MColor(1.0f, 0.0f, 0.0f); break;
                case 1: c = MColor(0.0f, 1.0f, 0.0f); break;
                case 2: c = MColor(0.0f, 0.0f, 1.0f); break;
                case 3: c = MColor(1.0f, 1.0f, 0.0f); break;
                case 4: c = MColor(1.0f, 0.0f, 1.0f); break;
                case 5: c = MColor(0.0f, 1.0f, 1.0f); break;
                case 6: c = MColor(1.0f, 0.5f, 0.0f); break;
                default: c = MColor(0.5f, 0.0f, 1.0f); break;
                }

                accum.r += static_cast<float>(c.r * w);
                accum.g += static_cast<float>(c.g * w);
                accum.b += static_cast<float>(c.b * w);
                sumW += w;
            }

            if (m_influences.length() > 1 && sumW > 1.0e-8)
            {
                accum.r /= static_cast<float>(sumW);
                accum.g /= static_cast<float>(sumW);
                accum.b /= static_cast<float>(sumW);
            }

            accum.r *= static_cast<float>(1.0 - m_displayDarken);
            accum.g *= static_cast<float>(1.0 - m_displayDarken);
            accum.b *= static_cast<float>(1.0 - m_displayDarken);
            accum.a = static_cast<float>(m_displayOpacity);
            colors[v] = accum;
        }
    }

    MString colorSetName(kPreviewColorSetName);
    MStringArray sets;
    meshFn.getColorSetNames(sets);

    bool hasSet = false;
    for (unsigned int i = 0; i < sets.length(); ++i)
    {
        if (sets[i] == colorSetName)
        {
            hasSet = true;
            break;
        }
    }

    if (!hasSet)
    {
        meshFn.createColorSetWithName(colorSetName);
    }

    status = meshFn.setCurrentColorSetName(colorSetName);
    if (status != MS::kSuccess)
        return false;

    status = meshFn.setVertexColors(colors, vertsCopy);
    if (status != MS::kSuccess)
        return false;

    status = meshFn.setDisplayColors(true);
    if (status != MS::kSuccess)
        return false;
    MHWRender::MRenderer::setGeometryDrawDirty(meshPath);
    return true;
}


void SmoothInfluenceWeightsPaintContext::drawBrush(MUIDrawManager& drawMgr) const
{
    if (!m_hasCursorData)
        return;

    M3dView view = M3dView::active3dView();
    MDagPath cameraPath;
    if (view.getCamera(cameraPath) != MS::kSuccess)
        return;

    MStatus status;
    MFnCamera camFn(cameraPath, &status);
    if (status != MS::kSuccess)
        return;

    MVector viewRight = camFn.rightDirection(MSpace::kWorld, &status);
    if (status != MS::kSuccess)
        return;

    MVector normal = m_hitNormal;
    if (normal.length() < 1.0e-8)
        normal = MVector(0.0, 1.0, 0.0);
    normal.normalize();

    MVector tangent = viewRight - normal * (viewRight * normal);
    if (tangent.length() < 1.0e-8)
        tangent = MVector(1.0, 0.0, 0.0);
    tangent.normalize();

    MVector bitangent = normal ^ tangent;
    if (bitangent.length() < 1.0e-8)
        bitangent = MVector(0.0, 0.0, 1.0);
    bitangent.normalize();

    const unsigned int segments = 64;
    MPointArray innerPts;
    innerPts.setLength(segments + 1);
    MPointArray outerPts;
    outerPts.setLength(segments + 1);

    for (unsigned int i = 0; i <= segments; ++i)
    {
        const double t = (2.0 * kPi * static_cast<double>(i)) / static_cast<double>(segments);
        const double cs = std::cos(t);
        const double sn = std::sin(t);
        const MVector innerOffset = tangent * (cs * m_worldRadius) + bitangent * (sn * m_worldRadius);
        const MVector outerOffset = tangent * (cs * m_outerWorldRadius) + bitangent * (sn * m_outerWorldRadius);
        innerPts[i] = m_hitPoint + innerOffset;
        outerPts[i] = m_hitPoint + outerOffset;
    }

    drawMgr.beginDrawable();
    drawMgr.setColor(MColor(1.0f, 1.0f, 0.0f));
    drawMgr.setLineWidth(2.0f);
    drawMgr.mesh(MHWRender::MUIDrawManager::kLineStrip, innerPts);

    drawMgr.setColor(MColor(1.0f, 1.0f, 0.0f, 0.75f));
    drawMgr.setLineWidth(1.5f);
    for (unsigned int i = 0; i < segments; i += 2)
    {
        MPointArray dash;
        dash.append(outerPts[i]);
        dash.append(outerPts[i + 1]);
        drawMgr.mesh(MHWRender::MUIDrawManager::kLineStrip, dash);
    }

    drawMgr.setColor(MColor(1.0f, 0.25f, 0.25f));
    drawMgr.sphere(m_hitPoint, m_worldRadius * 0.04, true);

    drawMgr.setColor(MColor(0.25f, 1.0f, 1.0f));
    const MPoint nEnd = m_hitPoint + (normal * (m_worldRadius * 0.35));
    drawMgr.line(m_hitPoint, nEnd);

    if (m_dragMode != kDragNone && m_dragMode != kDragPaint)
    {
        MString labelA;
        MString labelB;
        bool hasSecondLine = false;

        if (m_dragMode == kDragResizeRadius)
        {
            labelA = "Size: ";
            labelA += m_brushRadiusPx;
        }
        else if (m_dragMode == kDragAdjustStrength)
        {
            labelA = "Strength: ";
            labelA += m_strength;
        }
        else if (m_dragMode == kDragAdjustIterations)
        {
            labelA = "Iterations: ";
            labelA += m_iterations;
        }
        else if (m_dragMode == kDragAdjustBleedCombined)
        {
            labelA = "Bleed Distance: ";
            labelA += m_bleedDistance;
            labelB = "Bleed Falloff: ";
            labelB += m_bleedFalloff;
            hasSecondLine = true;
        }

        const MPoint textPosA =
            m_hitPoint +
            (normal * (m_outerWorldRadius * 0.15)) +
            (tangent * (m_outerWorldRadius * 1.15));

        drawMgr.setColor(MColor(1.0f, 1.0f, 1.0f));
        drawMgr.text(textPosA, labelA, MHWRender::MUIDrawManager::kLeft);

        if (hasSecondLine)
        {
            const MPoint textPosB = textPosA - (bitangent * (m_outerWorldRadius * 0.18));
            drawMgr.text(textPosB, labelB, MHWRender::MUIDrawManager::kLeft);
        }
    }

    drawMgr.endDrawable();
}
