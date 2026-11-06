#pragma once

#include <maya/MPxContext.h>
#include <maya/MPxContextCommand.h>
#include <maya/MEvent.h>
#include <maya/M3dView.h>
#include <maya/MGlobal.h>
#include <maya/MString.h>
#include <maya/MStringArray.h>
#include <maya/MPoint.h>
#include <maya/MVector.h>
#include <maya/MDagPath.h>
#include <maya/MObject.h>
#include <maya/MIntArray.h>
#include <maya/MColor.h>
#include <maya/MUIDrawManager.h>
#include <maya/MFrameContext.h>
#include <maya/MSyntax.h>
#include <set>
#include "BrushSolverCache.h"
#include "SmoothInfluenceWeightsStrokeUndoCmd.h"

class SmoothInfluenceWeightsPaintContext : public MPxContext
{
public:
    SmoothInfluenceWeightsPaintContext();
    ~SmoothInfluenceWeightsPaintContext() override;

    void toolOnSetup(MEvent& event) override;
    void toolOffCleanup() override;
    MStatus doEnterRegion(MEvent& event) override;

    void getClassName(MString& name) const override;

    MStatus doPress(MEvent& event) override;
    MStatus doDrag(MEvent& event) override;
    MStatus doRelease(MEvent& event) override;

    MStatus doPress(MEvent& event,
                    MHWRender::MUIDrawManager& drawMgr,
                    const MHWRender::MFrameContext& context) override;
    MStatus doDrag(MEvent& event,
                   MHWRender::MUIDrawManager& drawMgr,
                   const MHWRender::MFrameContext& context) override;
    MStatus doRelease(MEvent& event,
                      MHWRender::MUIDrawManager& drawMgr,
                      const MHWRender::MFrameContext& context) override;
    MStatus doPtrMoved(MEvent& event,
                       MHWRender::MUIDrawManager& drawMgr,
                       const MHWRender::MFrameContext& context) override;

    static void* creator();

    void setKeepOnes(bool value);
    void setStrength(double value);
    void setIterations(int value);
    void setBleedDistance(int value);
    void setBleedFalloff(double value);
    void setBrushRadius(double value);
    void setFalloffDistance(double value);
    void setOpacity(double value);
    void setDisplayOpacity(double value);
    void setDisplayDarken(double value);
    void setInfluences(const MStringArray& influences);
    void refreshPreview();

    bool keepOnes() const { return m_keepOnes; }
    double strength() const { return m_strength; }
    int iterations() const { return m_iterations; }
    int bleedDistance() const { return m_bleedDistance; }
    double bleedFalloff() const { return m_bleedFalloff; }
    double brushRadius() const { return m_brushRadiusPx; }
    double falloffDistance() const { return m_falloffDistancePx; }
    double opacity() const { return m_opacity; }
    double displayOpacity() const { return m_displayOpacity; }
    double displayDarken() const { return m_displayDarken; }
    const MStringArray& influences() const { return m_influences; }

private:
    enum DragMode
    {
        kDragNone = 0,
        kDragPaint,
        kDragResizeRadius,
        kDragAdjustStrength,
        kDragAdjustIterations,
        kDragAdjustBleedCombined,
        kDragAdjustFalloffDistance
    };

    void markToolSettingsDirty() const;

    bool updateHitFromMouse(short x, short y);
    bool resolveTargetMesh(MDagPath& outMeshPath) const;
    bool resolveSkinCluster(const MDagPath& meshPath, MObject& outSkinObj) const;
    bool collectVertsAndOpacities(const MDagPath& meshPath,
                                  const MPoint& center,
                                  double innerRadius,
                                  double outerRadius,
                                  double opacity,
                                  MIntArray& outVerts,
                                  MDoubleArray& outOpacities) const;
    bool computeAverageNormal(const MDagPath& meshPath,
                              const MPoint& center,
                              double radius,
                              MVector& outNormal) const;
    double computeWorldRadius(const MPoint& hitPoint) const;
    double computeWorldRadiusForPixels(const MPoint& hitPoint, double radiusPx) const;
    bool applyCommandToVerts(const MDagPath& meshPath, const MIntArray& verts, const MDoubleArray& opacities);
    void resetStrokeUndoState();
    bool appendStrokeUndoVerts(const MDagPath& meshPath, const MIntArray& verts);
    bool readWeightsForVerts(const MObject& skinObj,
                             const MDagPath& meshPath,
                             const MIntArray& verts,
                             MDoubleArray& outWeights,
                             MIntArray* outInfluenceIndices = nullptr,
                             unsigned int* outInfluenceCount = nullptr) const;
    void finalizeStrokeUndo();
    void drawBrush(MHWRender::MUIDrawManager& drawMgr) const;
    bool shouldStamp(const MPoint& point) const;
    void recordStamp(const MPoint& point);

    void beginResizeMode(short x, short y, DragMode mode);
    void updateResizeMode(short x, short y);
    void endResizeMode();

    MStatus handlePress(MEvent& event, MHWRender::MUIDrawManager* drawMgr);
    MStatus handleDrag(MEvent& event, MHWRender::MUIDrawManager* drawMgr);
    MStatus handleRelease(MEvent& event, MHWRender::MUIDrawManager* drawMgr);

    bool updatePreviewColors(const MDagPath& meshPath) const;
    bool updatePreviewColorsSubset(const MDagPath& meshPath, const MIntArray& verts) const;

private:
    bool m_keepOnes;
    double m_strength;
    int m_iterations;
    int m_bleedDistance;
    double m_bleedFalloff;
    double m_brushRadiusPx;
    double m_falloffDistancePx;
    double m_opacity;
    double m_displayOpacity;
    double m_displayDarken;
    double m_stampSpacing;
    MStringArray m_influences;

    bool m_isDragging;
    bool m_hasCursorData;
    bool m_hasLastStamp;
    MPoint m_hitPoint;
    MVector m_hitNormal;
    MPoint m_lastStampPoint;
    double m_worldRadius;
    double m_outerWorldRadius;

    DragMode m_dragMode;
    short m_dragStartX;
    short m_dragStartY;
    double m_dragStartBrushRadiusPx;
    double m_dragStartStrength;
    int m_dragStartIterations;
    int m_dragStartBleedDistance;
    double m_dragStartBleedFalloff;
    double m_dragStartFalloffDistance;
    double m_dragStartFalloffDistancePx;
    double m_dragStartOpacity;

    mutable BrushSolverCache m_solverCache;
    std::set<int> m_strokeUndoVertSet;
    MIntArray m_strokeUndoVerts;
    MDoubleArray m_strokeUndoBeforeWeights;
    MDagPath m_strokeUndoMeshPath;
    MObject m_strokeUndoSkinObj;
    bool m_hasStrokeUndoData;
};

class SmoothInfluenceWeightsPaintContextCmd : public MPxContextCommand
{
public:
    SmoothInfluenceWeightsPaintContextCmd();
    ~SmoothInfluenceWeightsPaintContextCmd() override;

    MPxContext* makeObj() override;
    static void* creator();

    MStatus appendSyntax() override;
    MStatus doEditFlags() override;
    MStatus doQueryFlags() override;

private:
    SmoothInfluenceWeightsPaintContext* m_ctx;
};
