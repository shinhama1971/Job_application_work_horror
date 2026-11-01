# ============================================================================
# ファイルの役割: これまでコードで箱や円柱を組んでいた物（人影・ロッカー・扉・配電盤・鍵・ヒューズ・電池・
# 天井の照明・裸電球・排水バルブ）のモデルを作り、1つずつFBXに書き出している。
# 実行: "C:\Program Files\Autodesk\Maya2025\bin\mayapy.exe" tools\maya\build_props.py
# 出力: assets\model\props\*.fbx（ゲームは PropModel::Append で読み込み、今までの頂点バッファの描き方のまま使う）
#
# 作り方
# ・小物: Mayaの基本形（箱・円柱・輪・球）を置き、角を面取り（polyBevel3）して、部位ごとにマテリアルを割り当てている。
#   マテリアルの名前が部位の名前で、ゲームはこの名前ごとに色を決め、描く範囲を分けている（例: 扉の取っ手、照明の発光パネル）。
# ・人影: 手（build_flashlight_hand.py）と同じく、カプセルと楕円体の距離関数をなめらかにつないだ形を、Surface Nets で面にしている。
# ・頂点カラー: 環境遮蔽（すき間やくぼみの暗がり）。各頂点から半球の向きへ光線を飛ばし、自分の形に当たらなかった割合。
#   ゲームは「部位の色 × この値」を頂点の色にしている。
#
# 座標: 各クラスのローカル座標（ゲームの単位、左手系）のまま作り、書き出す前にMayaの右手系へ z を反転している。
# 大きさは、ワールド行列で拡大する前の値ではなく、拡大した後の実際の大きさで作っている
# （ゲーム側が、そのクラスの標準の拡大率で割って、法線も合わせている）。
# ============================================================================

import math
import os

import numpy as np

# 手のスクリプトの距離関数と Surface Nets を使い回している（読み込んだ時点で maya.standalone が初期化される）
from build_flashlight_hand import (
    length, smin, sd_capsule, sd_ellipsoid, surface_nets, taubin_smooth)

import maya.standalone
import maya.cmds as cmds
import maya.mel as mel
import maya.api.OpenMaya as om

SCRIPT_DIR = os.path.dirname(os.path.abspath(__file__))
OUTPUT_DIR = os.path.normpath(os.path.join(SCRIPT_DIR, '..', '..', 'assets', 'model', 'props'))

_parts = []
_materials = {}


# ----------------------------------------------------------------------------
# 置く・面取りする・マテリアルを割り当てる
# ----------------------------------------------------------------------------
def begin():
    global _parts, _materials
    cmds.file(new=True, force=True)
    cmds.loadPlugin('fbxmaya', quiet=True)
    _parts = []
    _materials = {}


def assign(node, material):
    if material not in _materials:
        shader = cmds.shadingNode('lambert', asShader=True, name=material)
        group = cmds.sets(renderable=True, noSurfaceShader=True, empty=True, name=material + 'SG')
        cmds.connectAttr(shader + '.outColor', group + '.surfaceShader', force=True)
        _materials[material] = group
    cmds.sets(node, edit=True, forceElement=_materials[material])
    _parts.append(node)
    return node


def place(node, translate, rotate=(0.0, 0.0, 0.0), scale=(1.0, 1.0, 1.0)):
    cmds.xform(node, scale=scale, rotation=rotate, translation=translate, worldSpace=True)
    return node


def box(material, size, translate=(0.0, 0.0, 0.0), rotate=(0.0, 0.0, 0.0), bevel=0.0, segments=1):
    node = cmds.polyCube(width=size[0], height=size[1], depth=size[2], constructionHistory=False)[0]
    if bevel > 0.0:
        cmds.polyBevel3(node, offset=min(bevel, min(size) * 0.45), offsetAsFraction=False,
                        segments=segments, chamfer=True, constructionHistory=False)
    return assign(place(node, translate, rotate), material)


def cylinder(material, radius, height, translate=(0.0, 0.0, 0.0), rotate=(0.0, 0.0, 0.0), sides=24,
             bevel=0.0, segments=1, scale=(1.0, 1.0, 1.0)):
    """y軸に沿った円柱（rotate で向きを変える）。bevel は上下の縁だけを面取りしている"""
    node = cmds.polyCylinder(radius=radius, height=height, subdivisionsX=sides, subdivisionsY=1,
                             subdivisionsCaps=0, axis=(0, 1, 0), constructionHistory=False)[0]
    if bevel > 0.0:
        cmds.polyBevel3('%s.e[0:%d]' % (node, sides * 2 - 1), offset=min(bevel, radius * 0.45, height * 0.45),
                        offsetAsFraction=False, segments=segments, chamfer=True, constructionHistory=False)
    return assign(place(node, translate, rotate, scale), material)


def torus(material, radius, section, translate=(0.0, 0.0, 0.0), rotate=(0.0, 0.0, 0.0), sides=32, rings=10):
    """y軸のまわりの輪（rotate で向きを変える）"""
    node = cmds.polyTorus(radius=radius, sectionRadius=section, subdivisionsX=sides, subdivisionsY=rings,
                          axis=(0, 1, 0), constructionHistory=False)[0]
    return assign(place(node, translate, rotate), material)


def sphere(material, radius, translate=(0.0, 0.0, 0.0), scale=(1.0, 1.0, 1.0), rotate=(0.0, 0.0, 0.0),
           sides=20, rings=12):
    node = cmds.polySphere(radius=radius, subdivisionsX=sides, subdivisionsY=rings, constructionHistory=False)[0]
    return assign(place(node, translate, rotate, scale), material)


def mesh_from_arrays(name, vertices, quads, material):
    """Surface Nets の頂点と四角形から、Mayaのメッシュを作っている（座標はゲームのまま。反転は finish で行う）"""
    points = om.MFloatPointArray([om.MFloatPoint(float(v[0]), float(v[1]), float(v[2])) for v in vertices])
    counts = om.MIntArray([4] * len(quads))
    connects = om.MIntArray([int(i) for quad in quads for i in quad])
    transform = om.MFnMesh().create(points, counts, connects)
    node = cmds.rename(om.MFnDependencyNode(transform).name(), name)
    return assign(node, material)


# ----------------------------------------------------------------------------
# 仕上げ: 1つにまとめ、Mayaの座標へ直し、法線・環境遮蔽を決めて書き出す
# ----------------------------------------------------------------------------
def dag_of(node):
    selection = om.MSelectionList()
    selection.add(node)
    return selection.getDagPath(0)


def signed_volume(fn):
    points = fn.getPoints(om.MSpace.kObject)
    _, vertex_ids = fn.getTriangles()
    p = np.array([(pt.x, pt.y, pt.z) for pt in points], np.float64)
    tri = np.array(vertex_ids, np.int64).reshape(-1, 3)
    return float(np.sum(np.einsum('ij,ij->i', p[tri[:, 0]], np.cross(p[tri[:, 1]], p[tri[:, 2]])))) / 6.0


def bake_ambient_occlusion(mesh, distance, rays=14):
    """各頂点から、法線の向きの半球へ光線を飛ばし、自分の形に当たらなかった割合を頂点カラーにしている"""
    fn = om.MFnMesh(dag_of(mesh))
    points = fn.getPoints(om.MSpace.kObject)
    normals = fn.getVertexNormals(False, om.MSpace.kObject)
    accel = fn.autoUniformGridParams()
    golden = math.pi * (3.0 - math.sqrt(5.0))
    # 法線を+zとした半球の向き（コサインに比例してばらまいている）
    local = []
    for k in range(rays):
        r = math.sqrt((k + 0.5) / rays)
        a = golden * k
        local.append((r * math.cos(a), r * math.sin(a), math.sqrt(max(0.0, 1.0 - r * r))))
    colors = om.MColorArray()
    ids = om.MIntArray()
    for index in range(len(points)):
        n = om.MVector(normals[index]).normal()
        helper = om.MVector(0, 1, 0) if abs(n.y) < 0.9 else om.MVector(1, 0, 0)
        tangent = (helper ^ n).normal()
        bitangent = n ^ tangent
        origin = om.MFloatPoint(points[index]) + om.MFloatVector(n * (distance * 0.01))
        open_rays = 0
        for (x, y, z) in local:
            direction = tangent * x + bitangent * y + n * z
            hit = fn.anyIntersection(origin, om.MFloatVector(direction), om.MSpace.kObject, distance, False,
                                     accelParams=accel)
            if hit is None:
                open_rays += 1
        occlusion = 0.25 + 0.75 * (open_rays / float(rays))
        colors.append(om.MColor((occlusion, occlusion, occlusion, 1.0)))
        ids.append(index)
    fn.createColorSet('colorSet1', True)
    fn.setCurrentColorSetName('colorSet1')
    fn.setVertexColors(colors, ids)


def finish(name, soft_angle=40.0, occlusion_distance=None):
    nodes = list(_parts)
    mesh = nodes[0] if len(nodes) == 1 else cmds.polyUnite(nodes, constructionHistory=False, name=name)[0]
    if len(nodes) == 1 and mesh != name:
        mesh = cmds.rename(mesh, name)
    cmds.delete(mesh, constructionHistory=True)
    cmds.makeIdentity(mesh, apply=True, translate=True, rotate=True, scale=True)
    # ゲームの左手系（z が前）から、Mayaの右手系へ: z を反転し、面の表裏を戻している
    fn = om.MFnMesh(dag_of(mesh))
    points = fn.getPoints(om.MSpace.kObject)
    for i in range(len(points)):
        points[i].z = -points[i].z
    fn.setPoints(points, om.MSpace.kObject)
    cmds.polyNormal(mesh, normalMode=0, userNormalMode=0, constructionHistory=False)
    if signed_volume(om.MFnMesh(dag_of(mesh))) < 0.0:
        cmds.polyNormal(mesh, normalMode=0, userNormalMode=0, constructionHistory=False)
    cmds.polySoftEdge(mesh, angle=soft_angle, constructionHistory=False)
    if occlusion_distance is not None:
        bake_ambient_occlusion(mesh, occlusion_distance)
    cmds.delete(mesh, constructionHistory=True)

    os.makedirs(OUTPUT_DIR, exist_ok=True)
    path = os.path.join(OUTPUT_DIR, name + '.fbx')
    cmds.select(mesh, replace=True)
    for command in ('FBXResetExport', 'FBXExportSmoothingGroups -v true', 'FBXExportTangents -v false',
                    'FBXExportSmoothMesh -v false', 'FBXExportTriangulate -v true', 'FBXExportInAscii -v false',
                    'FBXExportCameras -v false', 'FBXExportLights -v false'):
        mel.eval(command)
    mel.eval('FBXExport -f "%s" -s' % path.replace('\\', '/'))
    print('%-14s %6d triangles  %s' % (name, cmds.polyEvaluate(mesh, triangle=True), sorted(_materials)))


# ----------------------------------------------------------------------------
# 2面のロッカーの扉（Locker）。扉の面が x=0、正面が +X、幅は z、高さは y（中心が原点）
# ----------------------------------------------------------------------------
def build_locker():
    begin()
    box('Panel', (0.4, 44.0, 15.0), (0.2, 0.0, 0.0), bevel=0.12)
    # 下半分の補強の膨らみと、上の名札
    box('Panel', (0.14, 24.0, 10.4), (0.44, -7.5, 0.0), bevel=0.08)
    box('Handle', (0.06, 1.6, 4.2), (0.44, 19.6, 0.0), bevel=0.03)
    # 周りの枠
    box('Frame', (0.24, 1.2, 15.0), (0.45, 21.4, 0.0), bevel=0.08)
    box('Frame', (0.24, 1.2, 15.0), (0.45, -21.4, 0.0), bevel=0.08)
    box('Frame', (0.24, 44.0, 1.2), (0.45, 0.0, 6.9), bevel=0.08)
    box('Frame', (0.24, 44.0, 1.2), (0.45, 0.0, -6.9), bevel=0.08)
    # 目の高さの通気口: 奥の暗いすき間と、斜めに押し出した羽根
    for index in range(6):
        y = 16.4 - index * 1.25
        box('Slit', (0.05, 0.75, 10.4), (0.41, y, 0.0))
        box('Panel', (0.10, 0.85, 10.6), (0.50, y + 0.12, 0.0), rotate=(0.0, 0.0, -38.0), bevel=0.03)
    # 足元の小さな通気口
    for index in range(5):
        box('Slit', (0.05, 0.45, 1.3), (0.41, -19.0, -4.0 + index * 2.0))
    # 取っ手（台座・握り・南京錠を掛ける輪）
    box('Handle', (0.12, 5.4, 1.5), (0.47, 0.0, 5.3), bevel=0.05)
    box('Handle', (0.45, 3.6, 0.6), (0.82, 0.2, 5.3), bevel=0.16, segments=2)
    torus('Handle', 0.45, 0.09, (0.62, -3.3, 5.3), rotate=(0.0, 0.0, 90.0), sides=16, rings=6)
    # 蝶番
    for y in (14.0, -14.0):
        cylinder('Frame', 0.32, 3.0, (0.38, y, -7.45), sides=12, bevel=0.06)
    finish('locker_door', soft_angle=35.0, occlusion_distance=3.0)


# ----------------------------------------------------------------------------
# 扉（Door）。幅30・高さ50・厚さ4（中心が原点、手前が -z）。取っ手は x=+8.4（蝶番の反対側）
# ----------------------------------------------------------------------------
def build_door():
    begin()
    box('Wood', (30.0, 50.0, 4.0), bevel=0.35, segments=2)
    for side in (-1.0, 1.0):
        z = side * 2.0
        for y in (11.0, -11.0):
            # 鏡板（一段高い板）と、そのまわりの縁の飾り
            box('Panel', (19.6, 15.6, 0.5), (0.0, y, z + side * 0.15), bevel=0.25, segments=2)
            box('Wood', (21.6, 0.7, 0.5), (0.0, y + 8.35, z + side * 0.18), bevel=0.2, segments=2)
            box('Wood', (21.6, 0.7, 0.5), (0.0, y - 8.35, z + side * 0.18), bevel=0.2, segments=2)
            box('Wood', (0.7, 17.4, 0.5), (10.45, y, z + side * 0.18), bevel=0.2, segments=2)
            box('Wood', (0.7, 17.4, 0.5), (-10.45, y, z + side * 0.18), bevel=0.2, segments=2)
        # 取っ手（座金・軸・レバー）と鍵穴の座金
        cylinder('Handle', 1.05, 0.3, (8.4, 0.0, z + side * 0.15), rotate=(90.0, 0.0, 0.0), sides=24, bevel=0.08)
        cylinder('Handle', 0.32, 1.3, (8.4, 0.0, z + side * 0.8), rotate=(90.0, 0.0, 0.0), sides=16)
        box('Handle', (3.8, 0.55, 0.6), (6.7, 0.0, z + side * 1.35), bevel=0.22, segments=2)
        box('Handle', (0.85, 1.7, 0.16), (8.4, -2.7, z + side * 0.08), bevel=0.06)
    # 扉の下のすき間から漏れる光（扉が開いても戸口に残る。影は落とさない）
    box('Leak', (28.8, 1.8, 0.36), (0.0, -25.25, -2.44))
    finish('door', soft_angle=35.0, occlusion_distance=4.0)


# ----------------------------------------------------------------------------
# 配電盤（FuseBox）。幅12・高さ18・奥行き1.6（中心が原点、正面が -z）
# ----------------------------------------------------------------------------
def build_fuse_box():
    begin()
    box('Body', (12.0, 18.0, 1.6), bevel=0.25, segments=2)
    box('Cover', (10.4, 16.4, 0.36), (0.0, 0.0, -0.92), bevel=0.14, segments=2)
    for y in (5.5, -5.5):
        cylinder('Body', 0.24, 1.6, (-5.35, y, -1.1), sides=12)
    for x in (-4.6, 4.6):
        for y in (-7.6, 7.6):
            cylinder('Body', 0.28, 0.16, (x, y, -1.16), rotate=(90.0, 0.0, 0.0), sides=10)
    # ヒューズ3本（ホルダー・ガラス管・両端の金具）
    for x in (-3.0, 0.0, 3.0):
        box('Holder', (1.7, 7.4, 0.3), (x, -1.08, -1.2), bevel=0.08)
        cylinder('Fuse', 0.55, 5.4, (x, -1.08, -1.75), sides=16)
        for y in (-3.6, 1.44):
            cylinder('Lever', 0.64, 0.9, (x, y, -1.75), sides=16, bevel=0.08)
    # 注意書きの札
    box('Holder', (4.2, 1.3, 0.08), (0.0, -7.3, -1.12), bevel=0.03)
    # ランプ（枠と丸いガラス）
    cylinder('Body', 1.05, 0.45, (0.0, 5.94, -1.3), rotate=(90.0, 0.0, 0.0), sides=20, bevel=0.1)
    sphere('Lamp', 0.8, (0.0, 5.94, -1.5), scale=(1.0, 1.0, 0.65))
    # 横のレバー
    cylinder('Body', 0.45, 0.9, (5.45, 0.0, -1.3), rotate=(0.0, 0.0, 90.0), sides=14, bevel=0.06)
    box('Lever', (0.35, 4.6, 0.35), (5.75, 1.9, -1.4), rotate=(-12.0, 0.0, 0.0), bevel=0.1)
    sphere('Lever', 0.48, (5.75, 4.2, -1.9))
    finish('fuse_box', soft_angle=35.0, occlusion_distance=2.0)


# ----------------------------------------------------------------------------
# 鍵（KeyItem）。長さ約9.4、x が長さの向き（-x が持ち手の輪）
# ----------------------------------------------------------------------------
def build_key():
    begin()
    torus('Key', 1.25, 0.28, (-3.8, 0.0, 0.0), rotate=(90.0, 0.0, 0.0), sides=28, rings=10)
    sphere('Key', 0.42, (-3.8, 1.55, 0.0), scale=(1.0, 0.7, 0.6))
    for x in (-2.35, -1.95):
        cylinder('Key', 0.40, 0.28, (x, 0.0, 0.0), rotate=(0.0, 0.0, 90.0), sides=14, bevel=0.06)
    cylinder('Key', 0.25, 6.2, (1.0, 0.0, 0.0), rotate=(0.0, 0.0, 90.0), sides=12)
    sphere('Key', 0.27, (4.1, 0.0, 0.0), sides=12, rings=8)
    box('Key', (1.6, 1.25, 0.22), (3.3, -0.85, 0.0), bevel=0.05)
    box('Key', (0.38, 0.55, 0.22), (2.85, -1.65, 0.0), bevel=0.04)
    box('Key', (0.38, 0.4, 0.22), (3.75, -1.58, 0.0), bevel=0.04)
    finish('key', soft_angle=40.0, occlusion_distance=1.0)


# ----------------------------------------------------------------------------
# ヒューズ（Item）。縦の筒（高さ約7）、手前（-z）からフィラメントが透けて見える
# ----------------------------------------------------------------------------
def build_fuse():
    begin()
    cylinder('Glass', 1.1, 4.6, sides=24)
    for side in (-1.0, 1.0):
        cylinder('Metal', 1.32, 1.2, (0.0, side * 2.9, 0.0), sides=24, bevel=0.14, segments=2)
        torus('Metal', 1.33, 0.07, (0.0, side * 2.62, 0.0), sides=24, rings=6)
    # ジグザグのフィラメント（ガラスの表面のすぐ手前）
    points = [(0.0, -1.8)] + [(0.28 * (1 if i % 2 == 0 else -1), -1.35 + i * 0.68) for i in range(5)] + [(0.0, 1.8)]
    for (x0, y0), (x1, y1) in zip(points[:-1], points[1:]):
        length_ = math.hypot(x1 - x0, y1 - y0)
        angle = math.degrees(math.atan2(x1 - x0, y1 - y0))
        cylinder('Filament', 0.07, length_, ((x0 + x1) * 0.5, (y0 + y1) * 0.5, -1.08), rotate=(0.0, 0.0, -angle), sides=6)
    finish('fuse', soft_angle=40.0, occlusion_distance=1.0)


# ----------------------------------------------------------------------------
# 電池（BatteryItem）。縦の筒（高さ約6）、上がプラス極
# ----------------------------------------------------------------------------
def build_battery():
    begin()
    cylinder('Body', 0.99, 4.7, sides=28, bevel=0.04)
    for side in (-1.0, 1.0):
        cylinder('Metal', 1.02, 0.4, (0.0, side * 2.52, 0.0), sides=28, bevel=0.13, segments=2)
    cylinder('Metal', 0.45, 0.34, (0.0, 2.89, 0.0), sides=20, bevel=0.1, segments=2)
    cylinder('Charge', 1.03, 1.3, (0.0, 1.0, 0.0), sides=28)
    finish('battery', soft_angle=40.0, occlusion_distance=1.0)


# ----------------------------------------------------------------------------
# 天井の照明（CeilingLight）。標準の大きさ 22 × 0.56 × 10（中心が原点、下に発光パネル）
# ----------------------------------------------------------------------------
def build_ceiling_light():
    begin()
    box('Body', (22.0, 0.56, 10.0), bevel=0.08)
    # パネルのまわりの縁
    box('Body', (19.4, 0.16, 0.8), (0.0, -0.32, 3.8), bevel=0.04)
    box('Body', (19.4, 0.16, 0.8), (0.0, -0.32, -3.8), bevel=0.04)
    box('Body', (0.8, 0.16, 8.4), (9.3, -0.32, 0.0), bevel=0.04)
    box('Body', (0.8, 0.16, 8.4), (-9.3, -0.32, 0.0), bevel=0.04)
    box('Panel', (17.6, 0.22, 6.8), (0.0, -0.36, 0.0))
    # 格子のルーバー
    for z in (-1.7, 0.0, 1.7):
        box('Body', (17.6, 0.28, 0.12), (0.0, -0.52, z))
    for i in range(-2, 3):
        box('Body', (0.12, 0.28, 6.8), (i * 3.5, -0.52, 0.0))
    # 四隅のねじ
    for x in (-10.4, 10.4):
        for z in (-4.4, 4.4):
            cylinder('Body', 0.22, 0.1, (x, -0.31, z), sides=8)
    finish('ceiling_light', soft_angle=35.0, occlusion_distance=0.8)


# ----------------------------------------------------------------------------
# 裸電球のソケットと電球（HangingBulb）。上端（コードの先）が原点、下へ伸びる
# ----------------------------------------------------------------------------
def build_bulb():
    begin()
    cylinder('Socket', 0.30, 0.25, (0.0, -0.12, 0.0), sides=16, bevel=0.06)
    cylinder('Socket', 0.42, 0.75, (0.0, -0.6, 0.0), sides=20, bevel=0.08, segments=2)
    for y in (-0.42, -0.72):
        torus('Socket', 0.42, 0.035, (0.0, y, 0.0), sides=20, rings=5)
    cylinder('Socket', 0.36, 0.22, (0.0, -1.05, 0.0), sides=20)
    sphere('Bulb', 0.38, (0.0, -1.3, 0.0), scale=(1.0, 0.8, 1.0), sides=20, rings=10)
    sphere('Bulb', 0.9, (0.0, -2.0, 0.0), scale=(1.0, 1.25, 1.0), sides=24, rings=16)
    finish('hanging_bulb', soft_angle=60.0, occlusion_distance=0.6)


# ----------------------------------------------------------------------------
# 排水バルブ（DrainValve）。壁の表面が z=0、部屋の側が -z。
# ハンドル（Wheel・Hub）は回すため、原点を中心に z=0 の面で作っている（ゲームが z=-5.2 へずらして回す）
# ----------------------------------------------------------------------------
def build_valve():
    begin()
    box('Base', (9.0, 9.0, 0.8), (0.0, 0.0, -0.4), bevel=0.2, segments=2)
    for x in (-3.6, 3.6):
        for y in (-3.6, 3.6):
            cylinder('Base', 0.38, 0.35, (x, y, -0.95), rotate=(90.0, 0.0, 0.0), sides=6)
    # 壁を伝う配管と、継ぎ目のフランジ・壁に留める金具
    cylinder('Pipe', 1.2, 38.0, (0.0, 19.0, -1.6), sides=20)
    cylinder('Pipe', 1.2, 12.0, (0.0, -6.0, -1.6), sides=20)
    for y in (9.0, 22.0, 34.0, -8.0):
        cylinder('Base', 1.55, 0.6, (0.0, y, -1.6), sides=20, bevel=0.1)
    for y in (15.0, 28.0):
        box('Base', (3.4, 0.9, 1.4), (0.0, y, -0.7), bevel=0.12)
    sphere('Base', 1.75, (0.0, 0.0, -1.6), sides=20, rings=12)
    # 手前へ出る配管・フランジ・ボルト・弁の軸
    cylinder('Pipe', 1.25, 2.6, (0.0, 0.0, -3.2), rotate=(90.0, 0.0, 0.0), sides=20)
    cylinder('Base', 1.95, 0.5, (0.0, 0.0, -4.4), rotate=(90.0, 0.0, 0.0), sides=24, bevel=0.1)
    for k in range(6):
        a = 2.0 * math.pi * k / 6.0
        cylinder('Base', 0.2, 0.3, (math.cos(a) * 1.6, math.sin(a) * 1.6, -4.75), rotate=(90.0, 0.0, 0.0), sides=6)
    cylinder('Base', 0.35, 1.0, (0.0, 0.0, -4.9), rotate=(90.0, 0.0, 0.0), sides=12)
    # ハンドル（原点が中心）
    torus('Wheel', 4.2, 0.42, rotate=(90.0, 0.0, 0.0), sides=40, rings=10)
    for k in range(4):
        angle = 45.0 + 90.0 * k
        a = math.radians(angle)
        cylinder('Wheel', 0.26, 4.0, (math.cos(a) * 2.1, math.sin(a) * 2.1, 0.0),
                 rotate=(0.0, 0.0, angle - 90.0), sides=10)
    cylinder('Hub', 0.9, 1.2, rotate=(90.0, 0.0, 0.0), sides=16, bevel=0.15, segments=2)
    cylinder('Hub', 0.32, 1.8, (0.0, 4.2, -0.9), rotate=(90.0, 0.0, 0.0), sides=12)
    sphere('Hub', 0.45, (0.0, 4.2, -1.85), sides=12, rings=8)
    finish('drain_valve', soft_angle=40.0, occlusion_distance=2.5)


# ----------------------------------------------------------------------------
# 人影（ShadowMan）。足元が原点、身長約31、+z がプレイヤーの方。
# 細長い手足と、前へ傾いた頭。腕は膝の下まで垂れている
# ----------------------------------------------------------------------------
def sd_shadow_man(p):
    body = sd_ellipsoid(p, (0.0, 15.3, 0.0), (2.3, 1.6, 1.4))
    body = smin(body, sd_capsule(p, (0.0, 15.5, 0.0), (0.0, 19.5, 0.3), 1.55, 1.75), 0.9)
    body = smin(body, sd_ellipsoid(p, (0.0, 21.6, 0.45), (2.85, 3.5, 1.55)), 1.0)
    body = smin(body, sd_capsule(p, (0.0, 24.6, 0.7), (0.0, 26.9, 1.35), 0.72, 0.62), 0.6)
    body = smin(body, sd_ellipsoid(p, (0.0, 28.3, 1.85), (1.5, 1.95, 1.7)), 0.5)
    figure = body
    for side in (-1.0, 1.0):
        # 脚（腿・すね・足）
        hip = (side * 1.3, 15.0, 0.0)
        knee = (side * 1.42, 7.7, 0.35)
        ankle = (side * 1.5, 0.95, -0.05)
        leg = sd_capsule(p, hip, knee, 1.15, 0.82)
        leg = smin(leg, sd_capsule(p, knee, ankle, 0.8, 0.5), 0.35)
        leg = smin(leg, sd_capsule(p, ankle, (side * 1.6, 0.42, 2.0), 0.48, 0.38), 0.3)
        figure = smin(figure, leg, 0.6)
        # 腕（肩・上腕・前腕・手・長い指）
        shoulder = (side * 2.95, 24.4, 0.75)
        elbow = (side * 3.45, 17.5, 0.55)
        wrist = (side * 3.6, 10.9, 1.0)
        palm = (side * 3.62, 8.6, 1.1)
        arm = sd_capsule(p, shoulder, elbow, 0.78, 0.6)
        arm = smin(arm, sd_capsule(p, elbow, wrist, 0.58, 0.42), 0.3)
        arm = smin(arm, sd_capsule(p, wrist, palm, 0.46, 0.36), 0.25)
        for finger in (-1.0, 0.0, 1.0):
            tip = (side * (3.62 + finger * 0.05), 6.3 + abs(finger) * 0.35, 1.1 + finger * 0.32)
            arm = np.minimum(arm, sd_capsule(p, palm, tip, 0.17, 0.11))
        figure = smin(figure, arm, 0.7)
    return figure


def build_shadow_man():
    begin()
    vertices, quads = surface_nets(sd_shadow_man, (-5.6, -0.4, -2.6), (5.6, 31.0, 4.4), 0.11)
    vertices = taubin_smooth(vertices, quads, iterations=4)
    mesh = mesh_from_arrays('shadow_man', vertices, quads, 'Body')
    cmds.polyReduce(mesh, version=1, termination=0, percentage=93, keepQuadsWeight=0.0, keepBorder=True,
                    replaceOriginal=True, triangulate=True, cachingReduce=False)
    cmds.delete(mesh, constructionHistory=True)
    finish('shadow_man', soft_angle=180.0)


if __name__ == '__main__':
    try:
        build_locker()
        build_door()
        build_fuse_box()
        build_key()
        build_fuse()
        build_battery()
        build_ceiling_light()
        build_bulb()
        build_valve()
        build_shadow_man()
    finally:
        maya.standalone.uninitialize()
