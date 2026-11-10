import maya.cmds as cmds

try:
    from shiboken2 import wrapInstance
    from PySide2 import QtWidgets, QtCore, QtGui
    import maya.OpenMayaUI as omui
    import maya.mel as mel
    import mayaLib.qt.qtUtils as qtUtils
except Exception:
    wrapInstance = None
    QtWidgets = None
    QtCore = None
    QtGui = None
    omui = None
    mel = None
    qtUtils = None

WINDOW_OBJECT = "siwTabbedToolSettingsWidget"
WORKSPACE_NAME = "siwTabbedToolSettingsWorkspaceControl"


def _install_toolsettings_mel_stubs():
    if mel is None:
        return
    mel.eval(r"""
global proc smoothInfluenceWeightsPaintContextProperties()
{
}
global proc smoothInfluenceWeightsPaintContextValues(string $ctx)
{
}
global proc scaleInfluenceWeightsPaintContextProperties()
{
}
global proc scaleInfluenceWeightsPaintContextValues(string $ctx)
{
}
""")


def _resolve_shape_from_node(node):
    if not node or not cmds.objExists(node):
        return None
    if ".vtx[" in node:
        node = node.split(".vtx[")[0]
    if cmds.nodeType(node) == "mesh":
        try:
            if cmds.getAttr(node + ".intermediateObject"):
                return None
        except Exception:
            pass
        return node
    shapes = cmds.listRelatives(node, shapes=True, fullPath=True) or []
    for s in shapes:
        if cmds.nodeType(s) != "mesh":
            continue
        try:
            if cmds.getAttr(s + ".intermediateObject"):
                continue
        except Exception:
            pass
        return s
    return None


def _selected_mesh_shape():
    sel = cmds.ls(sl=True, long=True) or []
    if not sel:
        return None
    return _resolve_shape_from_node(sel[0])


def _shape_to_transform(shape):
    if not shape or not cmds.objExists(shape):
        return None
    parents = cmds.listRelatives(shape, parent=True, fullPath=True) or []
    return parents[0] if parents else shape


def _skin_cluster_for_shape(shape):
    if not shape or not cmds.objExists(shape):
        return None
    hist = cmds.listHistory(shape, pruneDagObjects=True) or []
    skins = cmds.ls(hist, type="skinCluster") or []
    return skins[0] if skins else None


def _palette_color(idx):
    palette = [
        (255, 0, 0),
        (0, 255, 0),
        (0, 120, 255),
        (255, 255, 0),
        (255, 0, 255),
        (0, 255, 255),
        (255, 128, 0),
        (128, 0, 255),
    ]
    return palette[idx % len(palette)]


class InfluenceTreeWidget(QtWidgets.QTreeWidget):
    def __init__(self, owner, parent=None):
        super(InfluenceTreeWidget, self).__init__(parent)
        self._owner = owner

    def _set_subtree_expanded(self, item, expanded):
        if item is None:
            return
        item.setExpanded(expanded)
        for i in range(item.childCount()):
            self._set_subtree_expanded(item.child(i), expanded)

    def mousePressEvent(self, event):
        try:
            if event.modifiers() & QtCore.Qt.ShiftModifier:
                item = self.itemAt(event.pos())
                col = self.columnAt(event.pos().x())
                if item is not None and col == 1:
                    expanded = not item.isExpanded()
                    self._set_subtree_expanded(item, expanded)
                    event.accept()
                    return
        except Exception:
            pass
        QtWidgets.QTreeWidget.mousePressEvent(self, event)


class _BasePanel(QtWidgets.QWidget):
    CONTEXT_SUBSTR = ""
    TITLE = ""
    HAS_KEEP = False
    HAS_SCALE = False
    COLOR_NOTE = ""

    def __init__(self, parent=None):
        super(_BasePanel, self).__init__(parent)
        self._building = True
        self._tool_job = None
        self._selection_job = None
        self._undo_job = None
        self._redo_job = None
        self._poll_timer = None
        self._active_shape = None
        self._preview_shapes = set()
        self._preview_display_states = {}
        self._tree_items_by_name = {}
        self._build_ui()
        self._building = False
        self.refresh_from_context()
        self.load_selected_mesh()
        self._install_sync()

    def current_context(self):
        ctx = cmds.currentCtx()
        if not ctx or not cmds.contextInfo(ctx, exists=True) or self.CONTEXT_SUBSTR not in ctx:
            ctx = cmds.__getattribute__(self.CONTEXT_SUBSTR)()
        return ctx

    def _ui_alive(self):
        try:
            return bool(self) and self.active_mesh_label is not None
        except Exception:
            return False

    def _build_ui(self):
        layout = QtWidgets.QVBoxLayout(self)
        layout.setContentsMargins(8, 8, 8, 8)
        layout.setSpacing(8)

        title = QtWidgets.QLabel(self.TITLE)
        f = title.font()
        f.setBold(True)
        title.setFont(f)
        layout.addWidget(title)

        self.active_mesh_label = QtWidgets.QLabel("Mesh: <none>")
        self.active_mesh_label.setWordWrap(True)
        layout.addWidget(self.active_mesh_label)

        if self.HAS_KEEP:
            self.keep_ones = QtWidgets.QCheckBox("Keep Ones / Flood Mode")
            self.keep_ones.stateChanged.connect(self.apply_to_context)
            layout.addWidget(self.keep_ones)
        else:
            self.keep_ones = None

        self.strength = self._add_double(layout, "Strength", 0.001, 1.0, 0.2, 3)
        self.iterations = self._add_int(layout, "Iterations", 1, 20, 1)
        self.bleed_distance = self._add_int(layout, "Bleed Distance", 1, 20, 2)
        self.bleed_falloff = self._add_double(layout, "Bleed Falloff", 0.0, 4.0, 1.0, 3)
        self.brush_radius = self._add_double(layout, "Brush Radius", 2.0, 300.0, 40.0, 1)
        self.opacity = self._add_double(layout, "Opacity", 0.0, 1.0, 1.0, 2)

        if self.HAS_SCALE:
            self.scale_factor = self._add_centered_double(layout, "Scale Factor", -10.0, 10.0, 1.25, 3)
        else:
            self.scale_factor = None

        search_row = QtWidgets.QHBoxLayout()
        search_label = QtWidgets.QLabel("Search")
        self.search_field = QtWidgets.QLineEdit()
        self.search_field.setPlaceholderText("Filter influences")
        self.search_field.textChanged.connect(self._apply_influence_filter)
        search_row.addWidget(search_label)
        search_row.addWidget(self.search_field, 1)
        layout.addLayout(search_row)

        layout.addWidget(QtWidgets.QLabel("Influences"))
        self.influence_tree = InfluenceTreeWidget(self)
        self.influence_tree.setColumnCount(2)
        self.influence_tree.setHeaderLabels(["", "Influence"])
        self.influence_tree.setTreePosition(1)
        self.influence_tree.setSelectionMode(QtWidgets.QAbstractItemView.ExtendedSelection)
        self.influence_tree.setAlternatingRowColors(True)
        self.influence_tree.setUniformRowHeights(True)
        self.influence_tree.setRootIsDecorated(True)
        self.influence_tree.setItemsExpandable(True)
        self.influence_tree.setExpandsOnDoubleClick(True)
        self.influence_tree.setColumnWidth(0, 72)
        self.influence_tree.setColumnWidth(1, 260)
        self.influence_tree.itemSelectionChanged.connect(self.apply_to_context)
        layout.addWidget(self.influence_tree, 1)

        row = QtWidgets.QHBoxLayout()
        btn_load = QtWidgets.QPushButton("Load From Selected Mesh")
        btn_load.clicked.connect(self.load_selected_mesh)
        row.addWidget(btn_load)

        btn_clear = QtWidgets.QPushButton("Clear = All")
        btn_clear.clicked.connect(self.clear_influences)
        row.addWidget(btn_clear)
        layout.addLayout(row)

        row2 = QtWidgets.QHBoxLayout()
        btn_activate = QtWidgets.QPushButton("Activate Brush")
        btn_activate.clicked.connect(self.activate_brush)
        row2.addWidget(btn_activate)
        layout.addLayout(row2)

        btn_deactivate = QtWidgets.QPushButton("Deactivate Brush")
        btn_deactivate.clicked.connect(self.deactivate_brush)
        layout.addWidget(btn_deactivate)

        btn_flood = QtWidgets.QPushButton("Flood Apply")
        btn_flood.clicked.connect(self.flood_apply)
        layout.addWidget(btn_flood)

        self.display_opacity = None
        self.display_darken = None

        note_lines = []
        if self.COLOR_NOTE:
            note_lines.append(self.COLOR_NOTE)
        note_lines.extend([
            "MMB drag = Brush Size",
            "Shift + MMB drag = Strength",
            "Ctrl + MMB drag = Iterations",
            "Ctrl + Shift + MMB drag = Bleed Distance (horizontal) + Bleed Falloff (vertical)",
            "1 influence selected = grayscale preview",
            "2+ selected = blended selected colors",
            "No selected influences = all influence colors",
        ])
        note = QtWidgets.QLabel("\n".join(note_lines))
        note.setWordWrap(True)
        layout.addWidget(note)

    def _install_sync(self):
        # UI sync must never register undo/redo or tool-change scriptJobs.
        # Retain selection sync and the existing non-undoable polling timer.
        for attr in ("_tool_job", "_selection_job", "_undo_job", "_redo_job"):
            try:
                job = getattr(self, attr, None)
                if job and cmds.scriptJob(exists=job):
                    cmds.scriptJob(kill=job, force=True)
            except Exception:
                pass
            setattr(self, attr, None)
        try:
            self._selection_job = cmds.scriptJob(
                event=["SelectionChanged", self._on_selection_changed],
                protected=True)
        except Exception:
            self._selection_job = None
        if self._poll_timer:
            self._poll_timer.stop()
        self._poll_timer = QtCore.QTimer(self)
        self._poll_timer.setInterval(150)
        self._poll_timer.timeout.connect(self._poll_context_values)
        self._poll_timer.start()

    def _add_double(self, parent, label, mn, mx, value, decimals):
        box = QtWidgets.QGroupBox(label)
        lay = QtWidgets.QHBoxLayout(box)
        slider = QtWidgets.QSlider(QtCore.Qt.Horizontal)
        slider.setRange(0, 1000)
        spin = QtWidgets.QDoubleSpinBox()
        spin.setDecimals(decimals)
        spin.setRange(mn, mx)
        spin.setValue(value)

        def sync_from_spin(v):
            slider.blockSignals(True)
            t = (float(v) - mn) / (mx - mn) if mx > mn else 0.0
            slider.setValue(int(round(t * 1000.0)))
            slider.blockSignals(False)
            if not self._building:
                self.apply_to_context()

        def sync_from_slider(v):
            spin.blockSignals(True)
            t = float(v) / 1000.0
            spin.setValue(mn + ((mx - mn) * t))
            spin.blockSignals(False)
            if not self._building:
                self.apply_to_context()

        spin.valueChanged.connect(sync_from_spin)
        slider.valueChanged.connect(sync_from_slider)
        sync_from_spin(value)
        lay.addWidget(slider, 1)
        lay.addWidget(spin)
        parent.addWidget(box)
        return spin

    def _add_centered_double(self, parent, label, mn, mx, value, decimals):
        box = QtWidgets.QGroupBox(label)
        lay = QtWidgets.QHBoxLayout(box)
        slider = QtWidgets.QSlider(QtCore.Qt.Horizontal)
        slider.setRange(-1000, 1000)
        spin = QtWidgets.QDoubleSpinBox()
        spin.setDecimals(decimals)
        spin.setRange(mn, mx)
        spin.setValue(value)
        max_abs = max(abs(mn), abs(mx), 1.0)

        def sync_from_spin(v):
            slider.blockSignals(True)
            t = max(-1.0, min(1.0, float(v) / max_abs))
            slider.setValue(int(round(t * 1000.0)))
            slider.blockSignals(False)
            if not self._building:
                self.apply_to_context()

        def sync_from_slider(v):
            spin.blockSignals(True)
            t = float(v) / 1000.0
            spin.setValue(max_abs * t)
            spin.blockSignals(False)
            if not self._building:
                self.apply_to_context()

        spin.valueChanged.connect(sync_from_spin)
        slider.valueChanged.connect(sync_from_slider)
        sync_from_spin(value)
        lay.addWidget(slider, 1)
        lay.addWidget(spin)
        parent.addWidget(box)
        return spin

    def _safe_scale_factor_value(self):
        if not self.HAS_SCALE or self.scale_factor is None:
            return None
        v = float(self.scale_factor.value())
        if abs(v) < 1.0e-8:
            return 1.0e-5
        return v

    def _add_int(self, parent, label, mn, mx, value):
        box = QtWidgets.QGroupBox(label)
        lay = QtWidgets.QHBoxLayout(box)
        slider = QtWidgets.QSlider(QtCore.Qt.Horizontal)
        slider.setRange(mn, mx)
        spin = QtWidgets.QSpinBox()
        spin.setRange(mn, mx)
        spin.setValue(value)

        def sync_from_spin(v):
            slider.blockSignals(True)
            slider.setValue(int(v))
            slider.blockSignals(False)
            if not self._building:
                self.apply_to_context()

        def sync_from_slider(v):
            spin.blockSignals(True)
            spin.setValue(int(v))
            spin.blockSignals(False)
            if not self._building:
                self.apply_to_context()

        spin.valueChanged.connect(sync_from_spin)
        slider.valueChanged.connect(sync_from_slider)
        sync_from_spin(value)
        lay.addWidget(slider, 1)
        lay.addWidget(spin)
        parent.addWidget(box)
        return spin

    def _capture_current_vert_selection(self):
        all_sel = cmds.ls(sl=True, fl=True, long=True) or []
        return [s for s in all_sel if ".vtx[" in s]

    def _restore_vert_selection_for_shape(self, verts, shape):

        transform = _shape_to_transform(shape) if shape else None

        if verts and shape:

            valid = []

            for v in verts:

                if ".vtx[" not in v:
                    continue

                base = v.split(".vtx[")[0]

                if base == shape and cmds.objExists(base):
                    valid.append(v)

            if valid:

                try:

                    if transform and cmds.objExists(transform):

                        try:

                            cmds.hilite(transform, r=True)


                        except Exception:

                            pass

                    try:

                        cmds.selectMode(object=True)


                    except Exception:

                        pass

                    try:

                        cmds.selectMode(component=True)


                    except Exception:

                        pass

                    try:

                        cmds.selectType(polymeshVertex=True)


                    except Exception:

                        pass

                    cmds.select(valid, r=True)

                    return True


                except Exception:

                    pass

        return False

    def _deferred_restore_vert_selection_for_shape(self, verts, shape):
        def _restore():
            try:
                self._restore_vert_selection_for_shape(verts, shape)
            except Exception:
                pass

        try:
            QtCore.QTimer.singleShot(0, _restore)
            QtCore.QTimer.singleShot(1, _restore)
            QtCore.QTimer.singleShot(10, _restore)
            QtCore.QTimer.singleShot(30, _restore)
        except Exception:
            _restore()

    def _after_flood_refresh_and_restore(self, stored_verts, shape):
        def _run():
            try:
                ctx = self.current_context()
                try:
                    if self.HAS_KEEP:
                        cmds.smoothInfluenceWeightsPaintContext(ctx, e=True, refreshPreview=True)
                    else:
                        cmds.scaleInfluenceWeightsPaintContext(ctx, e=True, refreshPreview=True)
                except Exception:
                    pass

                try:
                    self._deferred_restore_vert_selection_for_shape(stored_verts, shape)
                except Exception:
                    pass

                self._mark_preview_shape(shape)
            except Exception:
                pass

        try:
            QtCore.QTimer.singleShot(0, _run)
            QtCore.QTimer.singleShot(10, _run)
        except Exception:
            _run()

    def _disable_preview_on_shape(self, shape):
        if not shape or not cmds.objExists(shape):
            return
        previous = self._preview_display_states.pop(shape, None)
        try:
            if cmds.attributeQuery("displayColors", node=shape, exists=True):
                value = previous[0] if previous is not None else 0
                cmds.setAttr(shape + ".displayColors", value)
        except Exception:
            pass
        try:
            if previous is not None and previous[1] is not None:
                cmds.polyOptions(shape, colorShadedDisplay=bool(previous[1]))
            else:
                cmds.polyOptions(shape, colorShadedDisplay=False)
        except Exception:
            pass

    def _clear_all_preview_shapes(self):
        for shape in list(self._preview_shapes):
            self._disable_preview_on_shape(shape)
        self._preview_shapes.clear()

    def _mark_preview_shape(self, shape):
        if not shape or not cmds.objExists(shape):
            return
        if shape not in self._preview_shapes:
            # Both tabs may preview the same mesh. Reuse the FIRST snapshot,
            # rather than recording the other tab's already-coloured state.
            previous = None
            window = self.window()
            for name in ("smoothTab", "scaleTab"):
                other = getattr(window, name, None)
                if other is not None and other is not self:
                    previous = other._preview_display_states.get(shape)
                    if previous is not None:
                        break
            if previous is None:
                display_colors = 0
                shaded_colors = None
                try:
                    display_colors = cmds.getAttr(shape + ".displayColors")
                except Exception:
                    pass
                try:
                    shaded_colors = cmds.polyOptions(shape, q=True, colorShadedDisplay=True)
                    if isinstance(shaded_colors, (list, tuple)):
                        shaded_colors = shaded_colors[0] if shaded_colors else None
                except Exception:
                    pass
                previous = (display_colors, shaded_colors)
            self._preview_display_states[shape] = previous
        self._preview_shapes.add(shape)
        try:
            cmds.polyOptions(shape, colorShadedDisplay=True)
        except Exception:
            pass

    def deactivate_brush(self):
        """Restore shading even if the paint context was dropped manually."""
        window = self.window()
        panels = [getattr(window, name, None) for name in ("smoothTab", "scaleTab")]
        panels = [panel for panel in panels if panel is not None]
        if self not in panels:
            panels.append(self)
        originals = {}
        shapes = set()
        for panel in panels:
            for shape, state in panel._preview_display_states.items():
                originals.setdefault(shape, state)
            shapes.update(panel._preview_shapes)
            if panel._active_shape:
                shapes.add(panel._active_shape)
        shapes.update(originals)
        selected = _selected_mesh_shape()
        if selected:
            shapes.add(selected)
        cmds.setToolTo("selectSuperContext")
        for panel in panels:
            panel._preview_shapes.clear()
            panel._preview_display_states.clear()
        for shape in shapes:
            if not shape or not cmds.objExists(shape):
                continue
            state = originals.get(shape)
            # If the tool was dropped and tracking was lost, clear SIW colours.
            display_colors = state[0] if state is not None else 0
            shaded_colors = bool(state[1]) if state is not None and state[1] is not None else False
            try:
                if cmds.attributeQuery("displayColors", node=shape, exists=True):
                    cmds.setAttr(shape + ".displayColors", display_colors)
            except Exception:
                pass
            try:
                cmds.polyOptions(shape, colorShadedDisplay=shaded_colors)
            except Exception:
                pass
        cmds.refresh(force=True)

    def _set_active_shape(self, shape):
        if shape == self._active_shape:
            return
        if self._active_shape and self._active_shape != shape:
            self._disable_preview_on_shape(self._active_shape)
        self._active_shape = shape
        self._mark_preview_shape(shape)
        self.active_mesh_label.setText("Mesh: {0}".format(shape) if shape else "Mesh: <none>")

    def _on_tool_changed(self):
        if not self._ui_alive():
            return
        ctx = cmds.currentCtx()
        if not ctx or self.CONTEXT_SUBSTR not in ctx:
            self._clear_all_preview_shapes()
            return
        self.refresh_from_context()

    def _on_selection_changed(self):
        if not self._ui_alive():
            return
        sel_shape = _selected_mesh_shape()
        if not self._active_shape and sel_shape:
            try:
                self.active_mesh_label.setText("Mesh: {0}".format(sel_shape))
            except RuntimeError:
                return

    def _on_undo_redo(self):
        if not self._ui_alive():
            return
        if not self._active_shape:
            return
        ctx = cmds.currentCtx()
        try:
            if ctx and self.CONTEXT_SUBSTR in ctx:
                if self.HAS_KEEP:
                    cmds.smoothInfluenceWeightsPaintContext(ctx, e=True, refreshPreview=True)
                else:
                    cmds.scaleInfluenceWeightsPaintContext(ctx, e=True, refreshPreview=True)
            else:
                if cmds.objExists(self._active_shape) and cmds.attributeQuery("displayColors", node=self._active_shape,
                                                                              exists=True):
                    was_on = cmds.getAttr(self._active_shape + ".displayColors")
                    cmds.setAttr(self._active_shape + ".displayColors", 0)
                    cmds.setAttr(self._active_shape + ".displayColors", was_on)
        except Exception:
            pass

    def _is_influence_item(self, item):
        return bool(item and item.data(1, QtCore.Qt.UserRole) is not None)

    def _selected_influence_items(self):
        return [item for item in self.influence_tree.selectedItems() if self._is_influence_item(item)]

    def selected_influences(self):
        return [item.data(1, QtCore.Qt.UserRole) for item in self._selected_influence_items()]

    def _set_item_visuals(self, item, idx, single_selected):
        if QtGui is None:
            return
        r, g, b = _palette_color(idx)
        display_opacity = float(self.display_opacity.value()) if getattr(self, "display_opacity",
                                                                         None) is not None else 1.0
        darken = float(self.display_darken.value()) if getattr(self, "display_darken", None) is not None else 0.0

        scale = max(0.0, 1.0 - darken)
        rr = int(max(0, min(255, r * scale)))
        gg = int(max(0, min(255, g * scale)))
        bb = int(max(0, min(255, b * scale)))
        aa = int(max(0, min(255, 255.0 * display_opacity)))

        color = QtGui.QColor(rr, gg, bb, aa)
        item.setText(0, u"■")
        item.setForeground(0, QtGui.QBrush(color))
        font = item.font(0)
        font.setPointSize(max(font.pointSize(), 30))
        font.setBold(True)
        item.setFont(0, font)
        item.setSizeHint(0, QtCore.QSize(72, 36))
        item.setSizeHint(1, QtCore.QSize(0, 30))
        if single_selected and item.isSelected():
            item.setForeground(1, QtGui.QBrush(QtGui.QColor(255, 255, 255)))
        elif single_selected:
            item.setForeground(1, QtGui.QBrush(QtGui.QColor(180, 180, 180)))
        else:
            item.setForeground(1, QtGui.QBrush(color))

    def update_influence_list_colors(self):
        if QtGui is None:
            return
        single_selected = (len(self.selected_influences()) == 1)
        for idx, (name, item) in enumerate(self._tree_items_by_name.items()):
            self._set_item_visuals(item, idx, single_selected)
        self.influence_tree.viewport().update()
        self.influence_tree.update()

    def _build_influence_tree(self, influences):
        selected_before = set(self.selected_influences())
        self.influence_tree.blockSignals(True)
        self.influence_tree.clear()
        self._tree_items_by_name = {}
        long_map = {}
        for inf in influences:
            long_name = (cmds.ls(inf, long=True) or [inf])[0]
            long_map[long_name] = inf
        influence_longs = list(long_map.keys())
        influence_set = set(influence_longs)
        children_map = {inf: [] for inf in influence_longs}
        roots = []
        for inf in influence_longs:
            parents = cmds.listRelatives(inf, parent=True, fullPath=True) or []
            parent = parents[0] if parents else None
            if parent in influence_set:
                children_map[parent].append(inf)
            else:
                roots.append(inf)

        def add_item(parent_item, inf_long):
            display_name = inf_long.split("|")[-1] if "|" in inf_long else inf_long
            item = QtWidgets.QTreeWidgetItem(parent_item if parent_item is not None else self.influence_tree)
            item.setText(1, display_name)
            item.setData(1, QtCore.Qt.UserRole, long_map[inf_long])
            self._tree_items_by_name[long_map[inf_long]] = item
            if long_map[inf_long] in selected_before:
                item.setSelected(True)
            for child in children_map.get(inf_long, []):
                add_item(item, child)

        for root in roots:
            add_item(None, root)
        self.influence_tree.collapseAll()
        self.influence_tree.blockSignals(False)
        self._apply_influence_filter(self.search_field.text())
        self.update_influence_list_colors()

    def _apply_influence_filter(self, text):
        query = (text or "").strip().lower()

        def visit(item):
            name = item.data(1, QtCore.Qt.UserRole)
            if name is not None:
                visible = (not query) or (query in name.lower()) or (query in item.text(1).lower())
                item.setHidden(not visible)
                return visible
            child_visible = False
            for i in range(item.childCount()):
                child_visible = visit(item.child(i)) or child_visible
            visible = child_visible or (not query) or (query in item.text(1).lower())
            item.setHidden(not visible)
            if visible and query:
                item.setExpanded(True)
            return visible

        for i in range(self.influence_tree.topLevelItemCount()):
            visit(self.influence_tree.topLevelItem(i))

    def load_selected_mesh(self):
        stored_verts = self._capture_current_vert_selection()

        shape = _selected_mesh_shape()
        if not shape:
            cmds.warning("Select a skinned mesh or vertices first.")
            return
        self._clear_all_preview_shapes()
        self._set_active_shape(shape)
        self.refresh_influences()
        self._restore_vert_selection_for_shape(stored_verts, shape)

    def refresh_influences(self):
        shape = self._active_shape
        skin = _skin_cluster_for_shape(shape)
        infs = cmds.skinCluster(skin, q=True, influence=True) or [] if skin else []
        self._build_influence_tree(infs)

    def _edit_context(self, ctx):
        kwargs = dict(
            strength=float(self.strength.value()),
            iterations=int(self.iterations.value()),
            bleedDistance=int(self.bleed_distance.value()),
            bleedFalloff=float(self.bleed_falloff.value()),
            brushRadius=float(self.brush_radius.value()),
            opacity=float(self.opacity.value()),
            displayOpacity=float(self.display_opacity.value()),
            displayDarken=float(self.display_darken.value()),
        )
        if self.HAS_KEEP:
            kwargs["keepOnes"] = self.keep_ones.isChecked()
            cmds.smoothInfluenceWeightsPaintContext(ctx, e=True, **kwargs)
        else:
            kwargs["scaleFactor"] = self._safe_scale_factor_value()
            cmds.scaleInfluenceWeightsPaintContext(ctx, e=True, **kwargs)

    def apply_influences_to_context(self, ctx):
        if mel is None:
            return
        if self.HAS_KEEP:
            infs = self.selected_influences()
            cmd = "smoothInfluenceWeightsPaintContext -e "
            for inf in infs:
                cmd += '-influence "{0}" '.format(inf.replace('"', '\\"'))
            cmd += ctx
            mel.eval(cmd)
            return
        infs = self.selected_influences()
        cmd = "scaleInfluenceWeightsPaintContext -e "
        for inf in infs:
            cmd += '-influence "{0}" '.format(inf.replace('"', '\\"'))
        cmd += '-scaleFactor {0} '.format(self._safe_scale_factor_value())
        cmd += ctx
        mel.eval(cmd)

    def activate_brush(self):
        stored_verts = self._capture_current_vert_selection()
        if not self._active_shape:
            self.load_selected_mesh()
            if not self._active_shape:
                return
            stored_verts = self._capture_current_vert_selection()
        ctx = self.current_context()
        self._edit_context(ctx)
        self.apply_influences_to_context(ctx)
        cmds.setToolTo(ctx)
        try:
            if self.HAS_KEEP:
                cmds.smoothInfluenceWeightsPaintContext(ctx, e=True, refreshPreview=True)
                cmds.smoothInfluenceWeightsPaintContext(ctx, e=True, refreshPreview=True)
            else:
                cmds.scaleInfluenceWeightsPaintContext(ctx, e=True, refreshPreview=True)
                cmds.scaleInfluenceWeightsPaintContext(ctx, e=True, refreshPreview=True)
        except Exception:
            pass
        self._restore_vert_selection_for_shape(stored_verts, self._active_shape)
        self._deferred_restore_vert_selection_for_shape(stored_verts, self._active_shape)
        self._mark_preview_shape(self._active_shape)

    def clear_influences(self):
        stored_verts = self._capture_current_vert_selection()
        self.influence_tree.blockSignals(True)
        self.influence_tree.clearSelection()
        self.influence_tree.blockSignals(False)
        self.update_influence_list_colors()
        new_ctx = cmds.__getattribute__(self.CONTEXT_SUBSTR)()
        self._edit_context(new_ctx)
        self.apply_influences_to_context(new_ctx)
        cmds.setToolTo(new_ctx)
        try:
            if self.HAS_KEEP:
                cmds.smoothInfluenceWeightsPaintContext(new_ctx, e=True, refreshPreview=True)
            else:
                cmds.scaleInfluenceWeightsPaintContext(new_ctx, e=True, refreshPreview=True)
        except Exception:
            pass
        self._restore_vert_selection_for_shape(stored_verts, self._active_shape)
        self._deferred_restore_vert_selection_for_shape(stored_verts, self._active_shape)
        self._mark_preview_shape(self._active_shape)

    def apply_to_context(self):
        if self._building or not self._active_shape:
            return
        stored_verts = self._capture_current_vert_selection()
        self.update_influence_list_colors()
        ctx = self.current_context()
        self._edit_context(ctx)
        self.apply_influences_to_context(ctx)
        try:
            if self.HAS_KEEP:
                cmds.smoothInfluenceWeightsPaintContext(ctx, e=True, refreshPreview=True)
            else:
                cmds.scaleInfluenceWeightsPaintContext(ctx, e=True, refreshPreview=True)
        except Exception:
            pass
        self._restore_vert_selection_for_shape(stored_verts, self._active_shape)
        self._deferred_restore_vert_selection_for_shape(stored_verts, self._active_shape)
        self._mark_preview_shape(self._active_shape)

    def refresh_from_context(self):
        ctx = cmds.currentCtx()
        if not ctx or self.CONTEXT_SUBSTR not in ctx:
            return
        self._building = True
        try:
            if self.HAS_KEEP:
                self.keep_ones.setChecked(bool(cmds.smoothInfluenceWeightsPaintContext(ctx, q=True, keepOnes=True)))
                self.strength.setValue(float(cmds.smoothInfluenceWeightsPaintContext(ctx, q=True, strength=True)))
                self.iterations.setValue(int(cmds.smoothInfluenceWeightsPaintContext(ctx, q=True, iterations=True)))
                self.bleed_distance.setValue(
                    int(cmds.smoothInfluenceWeightsPaintContext(ctx, q=True, bleedDistance=True)))
                self.bleed_falloff.setValue(
                    float(cmds.smoothInfluenceWeightsPaintContext(ctx, q=True, bleedFalloff=True)))
                self.brush_radius.setValue(
                    float(cmds.smoothInfluenceWeightsPaintContext(ctx, q=True, brushRadius=True)))
                self.opacity.setValue(float(cmds.smoothInfluenceWeightsPaintContext(ctx, q=True, opacity=True)))
                if self.display_opacity is not None:
                    self.display_opacity.setValue(
                        float(cmds.smoothInfluenceWeightsPaintContext(ctx, q=True, displayOpacity=True)))
                if self.display_darken is not None:
                    self.display_darken.setValue(
                        float(cmds.smoothInfluenceWeightsPaintContext(ctx, q=True, displayDarken=True)))
            else:
                self.strength.setValue(float(cmds.scaleInfluenceWeightsPaintContext(ctx, q=True, strength=True)))
                self.iterations.setValue(int(cmds.scaleInfluenceWeightsPaintContext(ctx, q=True, iterations=True)))
                self.bleed_distance.setValue(
                    int(cmds.scaleInfluenceWeightsPaintContext(ctx, q=True, bleedDistance=True)))
                self.bleed_falloff.setValue(
                    float(cmds.scaleInfluenceWeightsPaintContext(ctx, q=True, bleedFalloff=True)))
                self.brush_radius.setValue(float(cmds.scaleInfluenceWeightsPaintContext(ctx, q=True, brushRadius=True)))
                self.opacity.setValue(float(cmds.scaleInfluenceWeightsPaintContext(ctx, q=True, opacity=True)))
                if self.display_opacity is not None:
                    self.display_opacity.setValue(
                        float(cmds.scaleInfluenceWeightsPaintContext(ctx, q=True, displayOpacity=True)))
                if self.display_darken is not None:
                    self.display_darken.setValue(
                        float(cmds.scaleInfluenceWeightsPaintContext(ctx, q=True, displayDarken=True)))
                self.scale_factor.setValue(float(cmds.scaleInfluenceWeightsPaintContext(ctx, q=True, scaleFactor=True)))
        except Exception:
            pass
        finally:
            self._building = False

    def _poll_context_values(self):
        if not self._ui_alive():
            return
        ctx = cmds.currentCtx()
        if not ctx or self.CONTEXT_SUBSTR not in ctx:
            return
        try:
            if self.HAS_KEEP:
                radius = float(cmds.smoothInfluenceWeightsPaintContext(ctx, q=True, brushRadius=True))
                strength = float(cmds.smoothInfluenceWeightsPaintContext(ctx, q=True, strength=True))
                keep_ones = bool(cmds.smoothInfluenceWeightsPaintContext(ctx, q=True, keepOnes=True))
                iterations = int(cmds.smoothInfluenceWeightsPaintContext(ctx, q=True, iterations=True))
                bleed_distance = int(cmds.smoothInfluenceWeightsPaintContext(ctx, q=True, bleedDistance=True))
                bleed_falloff = float(cmds.smoothInfluenceWeightsPaintContext(ctx, q=True, bleedFalloff=True))
                opacity = float(cmds.smoothInfluenceWeightsPaintContext(ctx, q=True, opacity=True))
                display_opacity = float(cmds.smoothInfluenceWeightsPaintContext(ctx, q=True, displayOpacity=True))
                display_darken = float(cmds.smoothInfluenceWeightsPaintContext(ctx, q=True, displayDarken=True))
            else:
                radius = float(cmds.scaleInfluenceWeightsPaintContext(ctx, q=True, brushRadius=True))
                strength = float(cmds.scaleInfluenceWeightsPaintContext(ctx, q=True, strength=True))
                iterations = int(cmds.scaleInfluenceWeightsPaintContext(ctx, q=True, iterations=True))
                bleed_distance = int(cmds.scaleInfluenceWeightsPaintContext(ctx, q=True, bleedDistance=True))
                bleed_falloff = float(cmds.scaleInfluenceWeightsPaintContext(ctx, q=True, bleedFalloff=True))
                opacity = float(cmds.scaleInfluenceWeightsPaintContext(ctx, q=True, opacity=True))
                display_opacity = float(cmds.scaleInfluenceWeightsPaintContext(ctx, q=True, displayOpacity=True))
                display_darken = float(cmds.scaleInfluenceWeightsPaintContext(ctx, q=True, displayDarken=True))
                scale_factor = float(cmds.scaleInfluenceWeightsPaintContext(ctx, q=True, scaleFactor=True))
        except Exception:
            return
        self._building = True
        try:
            if abs(self.brush_radius.value() - radius) > 0.01:
                self.brush_radius.setValue(radius)
            if abs(self.strength.value() - strength) > 0.0001:
                self.strength.setValue(strength)
            if self.HAS_KEEP and self.keep_ones.isChecked() != keep_ones:
                self.keep_ones.setChecked(keep_ones)
            if self.iterations.value() != iterations:
                self.iterations.setValue(iterations)
            if self.bleed_distance.value() != bleed_distance:
                self.bleed_distance.setValue(bleed_distance)
            if abs(self.bleed_falloff.value() - bleed_falloff) > 0.0001:
                self.bleed_falloff.setValue(bleed_falloff)
            if abs(self.opacity.value() - opacity) > 0.0001:
                self.opacity.setValue(opacity)
            if self.display_opacity is not None and abs(self.display_opacity.value() - display_opacity) > 0.0001:
                self.display_opacity.setValue(display_opacity)
            if self.display_darken is not None and abs(self.display_darken.value() - display_darken) > 0.0001:
                self.display_darken.setValue(display_darken)
            if self.HAS_SCALE and abs(self.scale_factor.value() - scale_factor) > 0.0001:
                self.scale_factor.setValue(scale_factor)
        finally:
            self._building = False

    def flood_apply(self):
        stored_verts = self._capture_current_vert_selection()
        if not self._active_shape:
            self.load_selected_mesh()
            if not self._active_shape:
                return
            stored_verts = self._capture_current_vert_selection()

        mesh = _shape_to_transform(self._active_shape)
        if not mesh:
            cmds.warning("No active mesh loaded.")
            return

        kwargs = dict(
            geometry=mesh,
            strength=float(self.strength.value()),
            iterations=int(self.iterations.value()),
            bleedDistance=int(self.bleed_distance.value()),
            bleedFalloff=float(self.bleed_falloff.value()),
        )

        if self.HAS_KEEP:
            kwargs["keepOnes"] = self.keep_ones.isChecked()
        else:
            kwargs["scaleFactor"] = self._safe_scale_factor_value()

        kwargs["selectionOnly"] = bool(stored_verts)

        infs = self.selected_influences()
        if infs:
            kwargs["influence"] = infs

        if self.HAS_KEEP:
            cmds.smoothInfluenceWeights(**kwargs)
        else:
            cmds.scaleInfluenceWeights(**kwargs)

        ctx = self.current_context()
        try:
            if self.HAS_KEEP:
                cmds.smoothInfluenceWeightsPaintContext(ctx, e=True, refreshPreview=True)
            else:
                cmds.scaleInfluenceWeightsPaintContext(ctx, e=True, refreshPreview=True)
        except Exception:
            pass

        self._deferred_restore_vert_selection_for_shape(stored_verts, self._active_shape)
        self._mark_preview_shape(self._active_shape)

    def export_common_settings(self):
        return {
            "strength": float(self.strength.value()),
            "iterations": int(self.iterations.value()),
            "bleed_distance": int(self.bleed_distance.value()),
            "bleed_falloff": float(self.bleed_falloff.value()),
            "brush_radius": float(self.brush_radius.value()),
            "opacity": float(self.opacity.value()),
            "display_opacity": float(self.display_opacity.value()) if self.display_opacity is not None else 1.0,
            "display_darken": float(self.display_darken.value()) if self.display_darken is not None else 0.0,
        }

    def import_common_settings(self, settings):
        self._building = True
        try:
            self.strength.setValue(settings.get("strength", float(self.strength.value())))
            self.iterations.setValue(settings.get("iterations", int(self.iterations.value())))
            self.bleed_distance.setValue(settings.get("bleed_distance", int(self.bleed_distance.value())))
            self.bleed_falloff.setValue(settings.get("bleed_falloff", float(self.bleed_falloff.value())))
            self.brush_radius.setValue(settings.get("brush_radius", float(self.brush_radius.value())))
            self.opacity.setValue(settings.get("opacity", float(self.opacity.value())))
            if self.display_opacity is not None:
                self.display_opacity.setValue(settings.get("display_opacity", float(self.display_opacity.value())))
            if self.display_darken is not None:
                self.display_darken.setValue(settings.get("display_darken", float(self.display_darken.value())))
        finally:
            self._building = False

    def closeEvent(self, event):
        try:
            for attr in ["_tool_job", "_selection_job", "_undo_job", "_redo_job"]:
                job = getattr(self, attr, None)
                if job and cmds.scriptJob(exists=job):
                    cmds.scriptJob(kill=job, force=True)
        except Exception:
            pass
        try:
            if self._poll_timer:
                self._poll_timer.stop()
                self._poll_timer.deleteLater()
        except Exception:
            pass
        self._clear_all_preview_shapes()
        super(_BasePanel, self).closeEvent(event)


class SmoothPanel(_BasePanel):
    CONTEXT_SUBSTR = "smoothInfluenceWeightsPaintContext"
    TITLE = "Smooth Influence Weights Paint"
    HAS_KEEP = True


class ScalePanel(_BasePanel):
    CONTEXT_SUBSTR = "scaleInfluenceWeightsPaintContext"
    TITLE = "Scale Influence Weights Paint"
    HAS_SCALE = True
    COLOR_NOTE = "Scale brush color is purple."


class PaintTab(QtWidgets.QWidget):
    def __init__(self, parent=None):
        super(PaintTab, self).__init__(parent)
        l = QtWidgets.QVBoxLayout(self)
        l.addWidget(QtWidgets.QLabel("This tab switches to Maya Paint Skin Weights."))
        btn = QtWidgets.QPushButton("Activate Maya Paint Skin Weights")
        btn.clicked.connect(self.activate)
        l.addWidget(btn)
        l.addWidget(QtWidgets.QLabel("Use Maya Tool Settings for the native paint UI."))
        l.addStretch(1)

    def activate(self):
        try:
            mel.eval("ArtPaintSkinWeightsToolOptions;")
        except Exception:
            try:
                mel.eval("ArtPaintSkinWeightsTool;")
            except Exception:
                cmds.warning("Could not activate Maya Paint Skin Weights tool.")


class SIWTabbedPanel(QtWidgets.QWidget):
    def __init__(self, parent=None):
        super(SIWTabbedPanel, self).__init__(parent)
        _install_toolsettings_mel_stubs()
        self._stored_vert_selections = [[], [], [], [], []]
        self._store_write_index = 0
        self._next_select_slot = 0
        l = QtWidgets.QVBoxLayout(self)
        self.tabs = QtWidgets.QTabWidget()
        self.smoothTab = SmoothPanel()
        self.scaleTab = ScalePanel()
        self.paintTab = PaintTab()
        self.tabs.addTab(self.smoothTab, "Smooth")
        self.tabs.addTab(self.scaleTab, "Scale")
        self.tabs.addTab(self.paintTab, "Paint")
        self._last_tab_idx = 0
        self.tabs.currentChanged.connect(self.on_tab_changed)
        l.addWidget(self.tabs, 1)

        self.selection_section = qtUtils.FrameLayoutWidget("Selection", collapsed=False)
        self._build_selection_section()
        l.addWidget(self.selection_section, 0)

        self.draw_section = qtUtils.FrameLayoutWidget("Draw", collapsed=True)
        self._build_draw_section()
        l.addWidget(self.draw_section, 0)

        self.smoothTab.display_opacity = self.display_opacity
        self.smoothTab.display_darken = self.display_darken
        self.scaleTab.display_opacity = self.display_opacity
        self.scaleTab.display_darken = self.display_darken

        self._update_store_button_labels()
        self._refresh_display_settings()

    def _add_section_double(self, parent, label, mn, mx, value, decimals, callback):
        box = QtWidgets.QGroupBox(label)
        lay = QtWidgets.QHBoxLayout(box)

        slider = QtWidgets.QSlider(QtCore.Qt.Horizontal)
        slider.setRange(0, 1000)

        spin = QtWidgets.QDoubleSpinBox()
        spin.setDecimals(decimals)
        spin.setRange(mn, mx)
        spin.setValue(value)

        def sync_from_spin(v):
            slider.blockSignals(True)
            t = (float(v) - mn) / (mx - mn) if mx > mn else 0.0
            slider.setValue(int(round(t * 1000.0)))
            slider.blockSignals(False)
            callback()

        def sync_from_slider(v):
            spin.blockSignals(True)
            t = float(v) / 1000.0
            spin.setValue(mn + ((mx - mn) * t))
            spin.blockSignals(False)
            callback()

        spin.valueChanged.connect(sync_from_spin)
        slider.valueChanged.connect(sync_from_slider)

        slider.blockSignals(True)
        t = (float(value) - mn) / (mx - mn) if mx > mn else 0.0
        slider.setValue(int(round(t * 1000.0)))
        slider.blockSignals(False)

        lay.addWidget(slider, 1)
        lay.addWidget(spin)
        parent.add(box)
        return spin

    def _refresh_display_settings(self):
        if not hasattr(self, "display_opacity") or self.display_opacity is None:
            return
        if not hasattr(self, "display_darken") or self.display_darken is None:
            return

        for panel in (self.smoothTab, self.scaleTab):
            panel.display_opacity = self.display_opacity
            panel.display_darken = self.display_darken
            panel.update_influence_list_colors()

            ctx = cmds.currentCtx()
            try:
                if ctx and panel.CONTEXT_SUBSTR in ctx:
                    if panel.HAS_KEEP:
                        cmds.smoothInfluenceWeightsPaintContext(
                            ctx, e=True,
                            displayOpacity=float(self.display_opacity.value()),
                            displayDarken=float(self.display_darken.value()),
                            refreshPreview=True
                        )
                    else:
                        cmds.scaleInfluenceWeightsPaintContext(
                            ctx, e=True,
                            displayOpacity=float(self.display_opacity.value()),
                            displayDarken=float(self.display_darken.value()),
                            refreshPreview=True
                        )
            except Exception:
                pass

    def _build_selection_section(self):
        grid = QtWidgets.QGridLayout()
        grid.setContentsMargins(0, 0, 0, 0)
        grid.setHorizontalSpacing(6)
        grid.setVerticalSpacing(6)

        self.btn_store_verts = QtWidgets.QPushButton()
        self.btn_store_verts.clicked.connect(self.store_verts_selection)
        grid.addWidget(self.btn_store_verts, 0, 0)

        self.btn_recall_verts = QtWidgets.QPushButton()
        self.btn_recall_verts.clicked.connect(self.recall_verts_selection)
        grid.addWidget(self.btn_recall_verts, 0, 1)

        self.btn_invert_selection = QtWidgets.QPushButton("Invert")
        self.btn_invert_selection.clicked.connect(self.invert_vert_selection)
        grid.addWidget(self.btn_invert_selection, 1, 0)

        self.btn_unassigned = QtWidgets.QPushButton("Clear Stores")
        self.btn_unassigned.clicked.connect(self.clear_stored_selections)
        grid.addWidget(self.btn_unassigned, 1, 1)

        self.btn_grow_selection = QtWidgets.QPushButton("Grow Selection")
        self.btn_grow_selection.clicked.connect(self.grow_vert_selection)
        grid.addWidget(self.btn_grow_selection, 2, 0)

        self.btn_shrink_selection = QtWidgets.QPushButton("Shrink Selection")
        self.btn_shrink_selection.clicked.connect(self.shrink_vert_selection)
        grid.addWidget(self.btn_shrink_selection, 2, 1)

        self.selection_section.add(grid)

    def _build_draw_section(self):
        self.display_opacity = self._add_section_double(
            self.draw_section, "Display Opacity", 0.0, 1.0, 1.0, 2, self._refresh_display_settings
        )
        self.display_darken = self._add_section_double(
            self.draw_section, "Display Darken", 0.0, 1.0, 0.0, 2, self._refresh_display_settings
        )

    def _update_store_button_labels(self):
        used_slots = []
        for i, slot in enumerate(self._stored_vert_selections):
            existing = [v for v in slot if cmds.objExists(v.split(".vtx[")[0])]
            if existing:
                used_slots.append(i)

        self.btn_store_verts.setText("Store Verts ({0})".format(len(used_slots) if used_slots else ""))

        if not used_slots:
            self.btn_recall_verts.setText("Select ()")
            return

        if self._next_select_slot not in used_slots:
            self._next_select_slot = used_slots[0]

        self.btn_recall_verts.setText("Select ({0})".format(self._next_select_slot + 1))

    def store_verts_selection(self):
        sel = cmds.ls(sl=True, fl=True, long=True) or []
        verts = [s for s in sel if ".vtx[" in s]
        if not verts:
            shape = _selected_mesh_shape()
            if not shape:
                cmds.warning("Select a mesh or vertices first.")
                return
            verts = cmds.ls(shape + ".vtx[*]", fl=True, long=True) or []
        if not verts:
            cmds.warning("No vertices found to store.")
            return

        write_slot = self._store_write_index
        self._stored_vert_selections[write_slot] = list(verts)
        self._store_write_index = (self._store_write_index + 1) % 5

        used_slots = [i for i, slot in enumerate(self._stored_vert_selections) if slot]
        if self._next_select_slot not in used_slots:
            self._next_select_slot = write_slot

        self._update_store_button_labels()

    def recall_verts_selection(self):
        used_slots = []
        for i, slot in enumerate(self._stored_vert_selections):
            existing = [v for v in slot if cmds.objExists(v.split(".vtx[")[0])]
            if existing:
                used_slots.append((i, existing))

        if not used_slots:
            cmds.warning("No stored vertex selections.")
            self._update_store_button_labels()
            return

        used_slot_indices = [i for i, _ in used_slots]

        if self._next_select_slot not in used_slot_indices:
            self._next_select_slot = used_slot_indices[0]

        slot_dict = dict(used_slots)
        verts = slot_dict[self._next_select_slot]
        cmds.select(verts, r=True)

        current_pos = used_slot_indices.index(self._next_select_slot)
        next_pos = (current_pos + 1) % len(used_slot_indices)
        self._next_select_slot = used_slot_indices[next_pos]

        self._update_store_button_labels()

    def clear_stored_selections(self):
        self._stored_vert_selections = [[], [], [], [], []]
        self._store_write_index = 0
        self._next_select_slot = 0
        self._update_store_button_labels()

    def invert_vert_selection(self):
        shape = _selected_mesh_shape()
        if not shape:
            cmds.warning("Select a mesh or vertices first.")
            return

        all_verts = cmds.ls(shape + ".vtx[*]", fl=True, long=True) or []
        all_vert_set = set(all_verts)

        sel = cmds.ls(sl=True, fl=True, long=True) or []
        current = set([s for s in sel if s in all_vert_set])

        inv = [v for v in all_verts if v not in current]
        cmds.select(inv, r=True)

    def grow_vert_selection(self):
        try:
            mel.eval("GrowPolygonSelectionRegion;")
        except Exception:
            cmds.warning("Could not grow polygon selection.")

    def shrink_vert_selection(self):
        try:
            mel.eval("ShrinkPolygonSelectionRegion;")
        except Exception:
            cmds.warning("Could not shrink polygon selection.")

    def on_tab_changed(self, idx):
        stored_verts = []
        try:
            stored_verts = self.smoothTab._capture_current_vert_selection()
        except Exception:
            pass
        source = self.smoothTab if self._last_tab_idx == 0 else self.scaleTab if self._last_tab_idx == 1 else None
        target = self.smoothTab if idx == 0 else self.scaleTab if idx == 1 else None
        if source is not None and target is not None and source is not target:
            target.import_common_settings(source.export_common_settings())
        self._last_tab_idx = idx
        if idx == 0:
            self.smoothTab.activate_brush()
            cmds.refresh()
            self.smoothTab.clear_influences()
            cmds.refresh()
            self.smoothTab._deferred_restore_vert_selection_for_shape(stored_verts,
                                                                      self.smoothTab._active_shape or _selected_mesh_shape())
        elif idx == 1:
            self.scaleTab.activate_brush()
            cmds.refresh()
            self.scaleTab.clear_influences()
            cmds.refresh()
            self.scaleTab._deferred_restore_vert_selection_for_shape(stored_verts,
                                                                     self.scaleTab._active_shape or _selected_mesh_shape())
        elif idx == 2:
            self.paintTab.activate()
        self._refresh_display_settings()


def show():
    if QtWidgets is None:
        raise RuntimeError("PySide2 is not available.")
    _install_toolsettings_mel_stubs()
    if cmds.workspaceControl(WORKSPACE_NAME, exists=True):
        cmds.deleteUI(WORKSPACE_NAME)
    ctrl = cmds.workspaceControl(
        WORKSPACE_NAME,
        label="SIW Tool Settings",
        retain=False,
        floating=False
    )
    parent_ptr = omui.MQtUtil.findControl(ctrl)
    parent = wrapInstance(int(parent_ptr), QtWidgets.QWidget)
    widget = SIWTabbedPanel(parent=parent)
    layout = parent.layout()
    if layout is None:
        layout = QtWidgets.QVBoxLayout(parent)
        layout.setContentsMargins(0, 0, 0, 0)
    layout.addWidget(widget)
    return widget
