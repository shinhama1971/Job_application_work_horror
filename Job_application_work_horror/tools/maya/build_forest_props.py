# ============================================================================
# ファイルの役割: 外の森（assets/layout/forest_props.txt）に置く、枯れ木・倒木・岩・枯れた茂みのモデルを作り、FBXに書き出している。
# 実行: "C:\Program Files\Autodesk\Maya2025\bin\mayapy.exe" tools\maya\build_forest_props.py
# 出力: assets\model\props\forest_*.fbx（ゲームは Wall::SetVisualModel で、配置ファイルの箱の代わりに描く）
#
# ・木は、当たり判定の箱を幹だけ（8 × 130 × 8）にし、枝はその箱の外へ伸ばして作っている。
#   ゲームはモデルを「標準の大きさ」で割って箱の大きさに合わせるので、箱を幹の大きさにすれば、枝はそのままの長さで外へ出る
#   （枝には当たらず、幹にだけぶつかる）。
# ・枝は「2点を結ぶ、先の細い円柱」（limb）を、幹から外へ向けて重ねて作っている。
# ・マテリアル Main は、配置ファイルの色で塗る部位（樹皮・岩）。置き方・面取り・環境遮蔽の焼き込みは build_props.py と同じ。
# ============================================================================

import math
import random

from build_props import begin, sphere, finish, assign

import maya.standalone
import maya.cmds as cmds

# 作った枝の先が、狙った位置からどれだけずれたか（向きの計算の検査に使う）
_largest_limb_error = 0.0


def limb(material, start, end, radius_start, radius_end, sides=9):
    """start から end へ伸びる、先の細い円柱を置いている（座標はゲームのまま。反転は finish で行う）"""
    global _largest_limb_error
    dx, dy, dz = end[0] - start[0], end[1] - start[1], end[2] - start[2]
    length = math.sqrt(dx * dx + dy * dy + dz * dz)
    node = cmds.polyCylinder(radius=radius_start, height=length, subdivisionsX=sides, subdivisionsY=1,
                             subdivisionsCaps=0, axis=(0, 1, 0), constructionHistory=False)[0]
    # 上の輪の頂点だけを縮めて、先を細くしている
    cmds.scale(radius_end / radius_start, 1.0, radius_end / radius_start,
               '%s.vtx[%d:%d]' % (node, sides, sides * 2 - 1), pivot=(0.0, length * 0.5, 0.0), relative=True)
    # y 軸を (dx, dy, dz) の向きへ回している：先に z 軸まわりに傾け（+x の側へ）、次に y 軸まわりに回す
    tilt = math.degrees(math.acos(max(-1.0, min(1.0, dy / length))))
    azimuth = math.degrees(math.atan2(-dz, dx))
    cmds.xform(node, rotateOrder='zyx', rotation=(0.0, azimuth, -tilt),
               translation=((start[0] + end[0]) * 0.5, (start[1] + end[1]) * 0.5, (start[2] + end[2]) * 0.5),
               worldSpace=True)
    # 検査：上の輪の中心が end に来ているか
    top = cmds.xform('%s.vtx[%d:%d]' % (node, sides, sides * 2 - 1), query=True, translation=True, worldSpace=True)
    count = len(top) // 3
    center = [sum(top[k::3]) / count for k in range(3)]
    error = math.sqrt(sum((center[k] - end[k]) ** 2 for k in range(3)))
    _largest_limb_error = max(_largest_limb_error, error)
    return assign(node, material)


def direction(azimuth_degrees, tilt_degrees):
    """y 軸から tilt だけ傾け、上から見て azimuth の向きへ向けた単位ベクトル"""
    a = math.radians(azimuth_degrees)
    t = math.radians(tilt_degrees)
    return (math.sin(t) * math.cos(a), math.cos(t), math.sin(t) * math.sin(a))


def branch(start, heading, tilt, length, radius, depth, rng):
    """枝を数節に分けて、少しずつ曲げながら伸ばし、先で小枝に分かれさせている"""
    segments = 3
    point = start
    current_tilt = tilt
    current_heading = heading
    for k in range(segments):
        d = direction(current_heading, current_tilt)
        step = length / segments
        nxt = (point[0] + d[0] * step, point[1] + d[1] * step, point[2] + d[2] * step)
        r0 = radius * (1.0 - k / float(segments) * 0.75)
        r1 = radius * (1.0 - (k + 1) / float(segments) * 0.75)
        limb('Main', point, nxt, max(r0, 0.12), max(r1, 0.08), sides=7 if radius > 0.8 else 5)
        # 小枝
        if depth > 0 and k >= 1 and rng.random() < 0.8:
            branch(nxt, current_heading + rng.uniform(-70.0, 70.0), rng.uniform(25.0, 65.0),
                   length * rng.uniform(0.35, 0.55), max(r1 * 0.7, 0.12), depth - 1, rng)
        point = nxt
        current_tilt = max(10.0, min(100.0, current_tilt + rng.uniform(-14.0, 18.0)))
        current_heading += rng.uniform(-20.0, 20.0)


# ----------------------------------------------------------------------------
# 枯れ木。標準 8 × 130 × 8（幹の当たり判定の箱。中心が原点で、根元が y=-65）。枝は箱の外へ広がる
# ----------------------------------------------------------------------------
def build_dead_tree(name, seed, fork):
    begin()
    rng = random.Random(seed)
    base = -65.0
    # 根元の張り出し（地面へ潜り込む太い根）
    for k in range(5):
        heading = k * 72.0 + rng.uniform(-15.0, 15.0)
        d = direction(heading, 100.0)
        start = (0.0, base + 6.0, 0.0)
        end = (d[0] * 9.0, base - 1.0, d[2] * 9.0)
        limb('Main', start, end, 2.6, 0.6, sides=7)
    # 幹：少しずつ曲がりながら上へ。fork なら途中で2本に分かれる
    point = (0.0, base - 1.0, 0.0)
    radius = 4.0
    heights = [base + 14.0, base + 34.0, base + 56.0, base + 80.0]
    for h in heights:
        nxt = (point[0] + rng.uniform(-2.5, 2.5), h, point[2] + rng.uniform(-2.5, 2.5))
        limb('Main', point, nxt, radius, radius * 0.86, sides=11)
        radius *= 0.86
        point = nxt
    leaders = [(rng.uniform(0.0, 360.0), 0.0)] if not fork else [(rng.uniform(0.0, 360.0), 18.0)]
    if fork:
        leaders.append((leaders[0][0] + 160.0, 22.0))
    for heading, lean in leaders:
        top = point
        r = radius
        for k in range(3):
            d = direction(heading + rng.uniform(-15.0, 15.0), lean + k * 4.0)
            nxt = (top[0] + d[0] * 16.0, top[1] + d[1] * 16.0, top[2] + d[2] * 16.0)
            limb('Main', top, nxt, r, r * 0.72, sides=9)
            r *= 0.72
            top = nxt
        # 先の細い枝
        branch(top, heading + rng.uniform(-40.0, 40.0), 20.0, 14.0, max(r, 0.4), 1, rng)
    # 横へ伸びる大きな枝（幹の途中から）
    for k in range(6):
        h = base + rng.uniform(40.0, 95.0)
        branch((rng.uniform(-1.5, 1.5), h, rng.uniform(-1.5, 1.5)), k * 60.0 + rng.uniform(-20.0, 20.0),
               rng.uniform(45.0, 80.0), rng.uniform(20.0, 32.0), rng.uniform(1.1, 1.6), 2, rng)
    finish(name, soft_angle=70.0, occlusion_distance=6.0)


# ----------------------------------------------------------------------------
# 倒木。標準 60 × 8 × 8（長い向きが x。ゲームが箱の一番長い向きへ回す）
# ----------------------------------------------------------------------------
def build_fallen_log():
    begin()
    rng = random.Random(21)
    limb('Main', (-30.0, -0.3, 0.0), (-10.0, 0.2, 0.4), 3.7, 3.4, sides=12)
    limb('Main', (-10.0, 0.2, 0.4), (12.0, -0.2, -0.3), 3.4, 3.0, sides=12)
    limb('Main', (12.0, -0.2, -0.3), (30.0, 0.1, 0.2), 3.0, 2.4, sides=12)
    # 折れた枝の付け根
    for x in (-18.0, -2.0, 9.0, 21.0):
        heading = rng.uniform(0.0, 360.0)
        d = direction(heading, rng.uniform(30.0, 70.0))
        start = (x, 1.5, 0.0)
        limb('Main', start, (x + d[0] * 9.0, 1.5 + d[1] * 9.0, d[2] * 9.0), 0.9, 0.3, sides=6)
    finish('forest_fallen_log', soft_angle=70.0, occlusion_distance=4.0)


# ----------------------------------------------------------------------------
# 岩。標準 14 × 9 × 12（いくつかの潰れた球を重ねている）
# ----------------------------------------------------------------------------
def build_rock():
    begin()
    sphere('Main', 6.2, (0.0, -0.6, 0.0), scale=(1.10, 0.72, 0.95), sides=12, rings=8)
    sphere('Main', 4.0, (3.2, 0.6, 1.4), scale=(1.0, 0.85, 0.9), rotate=(12.0, 30.0, 0.0), sides=10, rings=7)
    sphere('Main', 3.4, (-3.4, -0.4, -2.0), scale=(1.1, 0.7, 1.0), rotate=(0.0, -20.0, 8.0), sides=10, rings=7)
    finish('forest_rock', soft_angle=35.0, occlusion_distance=3.0)


# ----------------------------------------------------------------------------
# 枯れた茂み。標準 20 × 12 × 20（根元から細い枝が上と外へ広がる）
# ----------------------------------------------------------------------------
def build_brush():
    begin()
    rng = random.Random(33)
    for k in range(9):
        branch((rng.uniform(-1.0, 1.0), -6.0, rng.uniform(-1.0, 1.0)), k * 40.0 + rng.uniform(-15.0, 15.0),
               rng.uniform(25.0, 60.0), rng.uniform(9.0, 13.0), 0.35, 1, rng)
    finish('forest_brush', soft_angle=70.0, occlusion_distance=None)


if __name__ == '__main__':
    try:
        build_dead_tree('forest_tree_dead', 11, False)
        build_dead_tree('forest_tree_fork', 17, True)
        build_fallen_log()
        build_rock()
        build_brush()
        print('largest limb end error: %.4f' % _largest_limb_error)
    finally:
        maya.standalone.uninitialize()
