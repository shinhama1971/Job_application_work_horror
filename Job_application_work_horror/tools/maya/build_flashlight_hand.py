# ============================================================================
# ファイルの役割: 一人称で画面の右下に映る「懐中電灯を持った右手」のモデルを作り、FBXに書き出している。
# 実行: "C:\Program Files\Autodesk\Maya2025\bin\mayapy.exe" tools\maya\build_flashlight_hand.py
# 出力: assets\model\flashlight_hand\flashlight_hand.fbx（ゲームは ModelCache で読み込む）
#
# 作り方
# ・手と腕（Skin）と袖（Sleeve）: 指の骨・手のひら・親指の付け根を、カプセルや楕円体の距離関数（SDF）で表し、
#   なめらかにつないだ（smooth min）形を、格子で調べて面に変えている（Surface Nets）。
#   指どうしはなめらかにつながず、押し付けた指のすき間が残るようにしている。
# ・懐中電灯（FlashlightBody / Bezel / Lens / Switch）: 断面の輪郭を回した回転体（旋盤と同じ作り方）。
# ・面を作った後、Mayaで面の数を減らし（polyReduce）、法線と硬い角を決めている。
# ・頂点カラー: R = 環境遮蔽（指が懐中電灯に触れる所などの暗がり。SDFで求めた）、
#   G・B = 部位の印（手は G = 爪、B = 血色。懐中電灯は G = 塗装のはげ、B = 握りのローレット加工）。
#   ゲームのシェーダー（viewModelPS / viewModelSkinPS / viewModelClothPS）がこの印を使っている。
#
# 座標: ゲームの手元の空間（x = 右、y = 上、z = 前。懐中電灯の軸が z）で、cm で作り、
# 書き出すときにゲームの単位（1 = 5cm）へ縮め、Mayaの右手系（前 = -Z）へ直している。
# AssimpのConvertToLeftHandedがzを反転するので、ゲームでは元の向きに戻る。
# ============================================================================

import math
import os
import sys

import numpy as np

import maya.standalone
maya.standalone.initialize(name='python')
import maya.cmds as cmds
import maya.mel as mel
import maya.api.OpenMaya as om

SCRIPT_DIR = os.path.dirname(os.path.abspath(__file__))
PROJECT_DIR = os.path.normpath(os.path.join(SCRIPT_DIR, '..', '..'))
OUTPUT_DIR = os.path.join(PROJECT_DIR, 'assets', 'model', 'flashlight_hand')
OUTPUT_FILE = os.path.join(OUTPUT_DIR, 'flashlight_hand.fbx')

CM_TO_UNIT = 0.2   # ゲームの1単位 = 5cm
TUBE_RADIUS = 1.75  # 握る所の懐中電灯の半径（cm）


# ----------------------------------------------------------------------------
# 距離関数（numpy。p は (N, 3) の点の並び）
# ----------------------------------------------------------------------------
def length(v):
    return np.sqrt(np.sum(v * v, axis=-1))


def smin(a, b, k):
    h = np.clip(0.5 + 0.5 * (b - a) / k, 0.0, 1.0)
    return b * (1.0 - h) + a * h - k * h * (1.0 - h)


def sd_capsule(p, a, b, ra, rb=None):
    """a から b への線分を芯にした、半径が ra から rb へ変わるカプセル"""
    if rb is None:
        rb = ra
    a = np.asarray(a, np.float32)
    b = np.asarray(b, np.float32)
    pa = p - a
    ba = b - a
    h = np.clip((pa @ ba) / float(ba @ ba), 0.0, 1.0)
    return length(pa - h[:, None] * ba) - (ra + (rb - ra) * h)


def sd_ellipsoid(p, center, radii):
    q = (p - np.asarray(center, np.float32)) / np.asarray(radii, np.float32)
    k0 = length(q)
    k1 = length(q / np.asarray(radii, np.float32))
    return k0 * (k0 - 1.0) / np.maximum(k1, 1e-6)


def sd_round_box(p, center, half, rounding):
    q = np.abs(p - np.asarray(center, np.float32)) - (np.asarray(half, np.float32) - rounding)
    outside = length(np.maximum(q, 0.0))
    inside = np.minimum(np.max(q, axis=-1), 0.0)
    return outside + inside - rounding


def polar(radius, degrees, z):
    t = math.radians(degrees)
    return (radius * math.cos(t), radius * math.sin(t), z)


def value_noise3(p, scale, seed):
    """なめらかな3次元のノイズ（格子の乱数を3次で補間）。しわと肌のむらに使っている"""
    q = p * scale + seed
    i = np.floor(q)
    f = q - i
    f = f * f * (3.0 - 2.0 * f)
    i = i.astype(np.int64)

    def hash3(ix, iy, iz):
        h = (ix * 73856093) ^ (iy * 19349663) ^ (iz * 83492791)
        h = (h ^ (h >> 13)) * 1274126177
        return ((h & 0xFFFF).astype(np.float32) / 65535.0)

    result = 0.0
    for dx in (0, 1):
        for dy in (0, 1):
            for dz in (0, 1):
                w = (f[:, 0] if dx else 1.0 - f[:, 0]) * \
                    (f[:, 1] if dy else 1.0 - f[:, 1]) * \
                    (f[:, 2] if dz else 1.0 - f[:, 2])
                result = result + w * hash3(i[:, 0] + dx, i[:, 1] + dy, i[:, 2] + dz)
    return result


# ----------------------------------------------------------------------------
# 手の形（握り方は「ドライバーを握る持ち方」: 懐中電灯の軸が前腕とほぼ同じ向きで、
# 4本の指が下から左へ巻き付き、親指が上に沿って前のスイッチへ伸びる）
# ----------------------------------------------------------------------------
# 指: 前後の位置 z、太さ、付け根（MCP）・第2関節（PIP）・第1関節（DIP）・指先の (軸からの距離, 角度)。
# 角度は軸のまわりで、0度 = 右、-90度 = 真下、-180度 = 左。
FINGERS = [
    # name,   z,    radius, MCP,          PIP,          DIP,           tip
    ('index', 2.95, 0.84, (3.15, -2.0), (2.60, -92.0), (2.56, -146.0), (2.53, -186.0)),
    ('middle', 1.12, 0.88, (3.25, -6.0), (2.64, -96.0), (2.60, -152.0), (2.57, -196.0)),
    ('ring', -0.66, 0.83, (3.20, -10.0), (2.59, -98.0), (2.55, -150.0), (2.52, -190.0)),
    ('pinky', -2.36, 0.72, (3.00, -16.0), (2.47, -95.0), (2.44, -140.0), (2.42, -174.0)),
]

THUMB_CMC = (2.55, 0.85, -5.0)
THUMB_MCP = (1.35, 2.55, -1.25)
THUMB_IP = (0.55, 2.70, 1.75)
THUMB_TIP = (0.20, 2.60, 3.80)


def finger_joints(finger):
    _, z, radius, mcp, pip, dip, tip = finger
    return (
        np.array(polar(mcp[0], mcp[1], z), np.float32),
        np.array(polar(pip[0], pip[1], z - 0.10), np.float32),
        np.array(polar(dip[0], dip[1], z - 0.02), np.float32),
        np.array(polar(tip[0], tip[1], z + 0.15), np.float32),
        radius,
    )


def sd_hand(p):
    """手と、袖の中へ入る前腕の距離。指どうしはなめらかにつながず、手のひらとはなめらかにつないでいる"""
    # 手のひら（手の甲が右を向く。懐中電灯の右側に手のひらが当たる）
    palm = sd_round_box(p, (3.05, -0.55, -2.05), (1.18, 1.95, 3.55), 0.95)
    # 手の甲の骨（中手骨）: 手首から各指の付け根へ
    for finger in FINGERS:
        mcp, _, _, _, radius = finger_joints(finger)
        wrist_side = np.array((3.25, -0.55 + (mcp[1] + 0.55) * 0.35, -5.4 + mcp[2] * 0.25), np.float32)
        palm = smin(palm, sd_capsule(p, wrist_side, mcp, radius * 1.05, radius * 1.12), 0.9)
    # 親指の付け根のふくらみ（母指球）と、小指側のふくらみ
    palm = smin(palm, sd_ellipsoid(p, (2.25, 1.40, -3.25), (1.35, 1.25, 2.45)), 0.9)
    palm = smin(palm, sd_ellipsoid(p, (2.65, -2.05, -3.55), (1.25, 1.10, 2.25)), 0.9)

    # 手首と前腕（親指が上なので、縦に広い楕円の断面。xを縮めた空間で測っている）
    flat = p * np.array((1.22, 1.0, 1.0), np.float32)
    wrist = sd_capsule(flat, (3.0 * 1.22, -0.55, -5.3), (3.4 * 1.22, -1.05, -9.2), 2.05, 2.25) / 1.1
    forearm = sd_capsule(flat, (3.4 * 1.22, -1.05, -9.2), (4.8 * 1.22, -3.2, -30.0), 2.25, 3.1) / 1.1
    palm = smin(palm, wrist, 1.1)
    palm = smin(palm, forearm, 1.4)

    # 指（3つの節。関節で少しふくらむ）
    hand = palm
    for finger in FINGERS:
        mcp, pip, dip, tip, radius = finger_joints(finger)
        proximal = sd_capsule(p, mcp, pip, radius * 1.04, radius * 0.98)
        middle = sd_capsule(p, pip, dip, radius * 0.97, radius * 0.92)
        distal = sd_capsule(p, dip, tip, radius * 0.91, radius * 0.80)
        knuckles = np.minimum(
            sd_ellipsoid(p, pip, (radius * 1.04, radius * 1.04, radius * 1.02)),
            sd_ellipsoid(p, dip, (radius * 0.96, radius * 0.96, radius * 0.95)))
        digit = smin(smin(proximal, middle, 0.25), distal, 0.2)
        digit = smin(digit, knuckles, 0.18)
        hand = np.minimum(hand, smin(palm, digit, 0.55))

    # 親指（懐中電灯の上に沿わせ、指先をスイッチの後ろに置いている）
    thumb_metacarpal = sd_capsule(p, THUMB_CMC, THUMB_MCP, 1.25, 1.06)
    thumb_proximal = sd_capsule(p, THUMB_MCP, THUMB_IP, 1.02, 0.95)
    thumb_distal = sd_capsule(p, THUMB_IP, THUMB_TIP, 0.94, 0.82)
    thumb = smin(smin(thumb_metacarpal, thumb_proximal, 0.35), thumb_distal, 0.25)
    hand = smin(hand, thumb, 0.75)

    # 袖の中の前腕は途中で切っている（見えない所の面を作らない）
    return np.maximum(hand, -13.0 - p[:, 2])


# 袖の芯（手首の少し後ろから肘の方へ）
SLEEVE_A = np.array((3.55, -1.20, -9.6), np.float32)
SLEEVE_B = np.array((5.30, -3.90, -33.0), np.float32)


def sd_sleeve(p):
    axis = SLEEVE_B - SLEEVE_A
    axis_length = float(np.linalg.norm(axis))
    direction = axis / axis_length
    q = p - SLEEVE_A
    along = q @ direction
    radial = q - along[:, None] * direction
    # 芯のまわりの角度（しわの向きを決める）
    side = np.cross(direction, np.array((0.0, 1.0, 0.0), np.float32))
    side /= np.linalg.norm(side)
    up = np.cross(side, direction)
    angle = np.arctan2(radial @ up, radial @ side)
    t = np.clip(along / axis_length, 0.0, 1.0)
    base_radius = 2.95 + (3.75 - 2.95) * t
    # 袖口の近くにたまる横じわ（ゆがんだ輪）と、細かい布のむら
    bunch = np.exp(-np.maximum(along - 1.0, 0.0) * 0.16)
    wrinkle = 0.26 * bunch * np.sin(along * 1.15 + 1.6 * np.sin(angle * 2.0 + along * 0.21)) + \
        0.10 * np.sin(angle * 3.0 + along * 0.55)
    wrinkle += (value_noise3(p, 0.55, 7.0) - 0.5) * 0.22
    # 袖口の折り返し（少し厚い帯）
    cuff = 0.16 * np.clip(1.0 - np.abs(along - 0.85) / 0.85, 0.0, 1.0)
    outer = length(radial) - (base_radius + wrinkle + cuff)
    shell = np.abs(outer + 0.26) - 0.26
    # 袖口で開け、肘の先は画面の外なので切っている
    shell = np.maximum(shell, -along)
    return np.maximum(shell, along - (axis_length - 1.0))


# ----------------------------------------------------------------------------
# 懐中電灯の輪郭（z, 半径）。後ろから順に。z = 0 が握る所の中ほど
# ----------------------------------------------------------------------------
BODY_PROFILE = [
    (-7.55, 0.00), (-7.55, 1.30), (-7.48, 1.62), (-7.30, 1.86), (-6.70, 1.88),   # 後ろのふた
    (-6.55, 1.78), (-6.40, 1.75),                                                 # ふたの継ぎ目
    (5.90, 1.75), (6.10, 1.86), (7.05, 1.86),                                     # 胴と首の継ぎ目
    (8.20, 2.05), (9.40, 2.45), (10.30, 2.66), (10.60, 2.72),                      # 頭へ広がる所
    (11.00, 2.72), (11.08, 2.62), (11.40, 2.62), (11.48, 2.72),                    # 頭の滑り止めの溝
    (11.85, 2.72), (11.93, 2.62), (12.25, 2.62), (12.33, 2.72),
    (12.95, 2.72),
]
BEZEL_PROFILE = [
    (12.95, 2.62), (12.95, 2.86), (13.15, 2.92), (13.70, 2.92), (13.85, 2.84), (13.85, 2.44), (13.55, 2.40),
]
LENS_Z = 13.55
LENS_RADIUS = 2.40
SWITCH_CENTER = (0.0, 1.75, 4.95)
SWITCH_RADII = (0.55, 0.38, 0.78)


def profile_radius(z):
    zs = np.array([pt[0] for pt in BODY_PROFILE[2:]], np.float32)
    rs = np.array([pt[1] for pt in BODY_PROFILE[2:]], np.float32)
    return np.interp(z, zs, rs)


def sd_flashlight(p):
    """環境遮蔽を求めるための、懐中電灯のおおよその距離（輪郭の半径の円柱と、スイッチ）"""
    radial = length(p[:, :2])
    d = radial - profile_radius(p[:, 2])
    d = np.maximum(d, -7.55 - p[:, 2])
    d = np.maximum(d, p[:, 2] - 13.85)
    return np.minimum(d, sd_ellipsoid(p, SWITCH_CENTER, SWITCH_RADII))


def sd_scene(p):
    return np.minimum(np.minimum(sd_hand(p), sd_sleeve(p)), sd_flashlight(p))


# ----------------------------------------------------------------------------
# Surface Nets: 格子の点で距離を測り、符号が変わるセルに頂点を1つ置き、
# 符号が変わる格子の辺ごとに、まわりの4つのセルの頂点を四角形でつないでいる
# ----------------------------------------------------------------------------
def surface_nets(sdf, lower, upper, spacing):
    lower = np.asarray(lower, np.float32)
    counts = np.ceil((np.asarray(upper, np.float32) - lower) / spacing).astype(int) + 1
    nx, ny, nz = counts
    xs = lower[0] + np.arange(nx, dtype=np.float32) * spacing
    ys = lower[1] + np.arange(ny, dtype=np.float32) * spacing
    zs = lower[2] + np.arange(nz, dtype=np.float32) * spacing
    grid = np.stack(np.meshgrid(xs, ys, zs, indexing='ij'), axis=-1).reshape(-1, 3)
    values = np.empty(grid.shape[0], np.float32)
    chunk = 1 << 20
    for start in range(0, grid.shape[0], chunk):
        values[start:start + chunk] = sdf(grid[start:start + chunk])
    values = values.reshape(nx, ny, nz)
    del grid

    inside = values < 0.0
    cell_shape = (nx - 1, ny - 1, nz - 1)
    corner_offsets = [(i, j, k) for i in (0, 1) for j in (0, 1) for k in (0, 1)]
    corners = [values[i:i + nx - 1, j:j + ny - 1, k:k + nz - 1] for (i, j, k) in corner_offsets]
    corner_inside = [c < 0.0 for c in corners]
    any_in = np.zeros(cell_shape, bool)
    all_in = np.ones(cell_shape, bool)
    for c in corner_inside:
        any_in |= c
        all_in &= c
    active = any_in & ~all_in
    cell_index = -np.ones(cell_shape, np.int64)
    active_cells = np.argwhere(active)
    cell_index[active] = np.arange(active_cells.shape[0])

    # 12本の辺の交点の平均を頂点の位置にしている
    edges = [(a, b) for a in range(8) for b in range(a + 1, 8)
             if sum(abs(corner_offsets[a][d] - corner_offsets[b][d]) for d in range(3)) == 1]
    sums = np.zeros((active_cells.shape[0], 3), np.float32)
    hits = np.zeros(active_cells.shape[0], np.float32)
    ai, aj, ak = active_cells[:, 0], active_cells[:, 1], active_cells[:, 2]
    for a, b in edges:
        va = corners[a][ai, aj, ak]
        vb = corners[b][ai, aj, ak]
        crossing = (va < 0.0) != (vb < 0.0)
        t = np.where(crossing, va / np.where(crossing, va - vb, 1.0), 0.0)
        pa = np.array(corner_offsets[a], np.float32)
        pb = np.array(corner_offsets[b], np.float32)
        local = pa[None, :] + (pb - pa)[None, :] * t[:, None]
        sums += np.where(crossing[:, None], local, 0.0)
        hits += crossing
    vertices = lower + (active_cells.astype(np.float32) + sums / np.maximum(hits, 1.0)[:, None]) * spacing

    # 四角形: x・y・z方向の格子の辺ごとに、まわりの4つのセル
    quads = []
    for axis in range(3):
        u_axis = (axis + 1) % 3
        v_axis = (axis + 2) % 3
        slicer_a = [slice(None)] * 3
        slicer_b = [slice(None)] * 3
        slicer_a[axis] = slice(0, -1)
        slicer_b[axis] = slice(1, None)
        crossing = inside[tuple(slicer_a)] != inside[tuple(slicer_b)]
        positive = inside[tuple(slicer_a)]  # 手前が中なら、外向きは +axis
        index = np.argwhere(crossing)
        # 外周の辺はまわりのセルがそろわないので除いている
        valid = (index[:, u_axis] >= 1) & (index[:, v_axis] >= 1) & \
            (index[:, u_axis] < counts[u_axis] - 1) & (index[:, v_axis] < counts[v_axis] - 1)
        index = index[valid]
        outward = positive[tuple(index.T)]

        def cell(du, dv):
            c = index.copy()
            c[:, u_axis] -= du
            c[:, v_axis] -= dv
            return cell_index[c[:, 0], c[:, 1], c[:, 2]]

        c0 = cell(1, 1)
        c1 = cell(0, 1)
        c2 = cell(0, 0)
        c3 = cell(1, 0)
        quad = np.stack([c0, c1, c2, c3], axis=1)
        quad[~outward] = quad[~outward][:, ::-1]
        quads.append(quad)
    quads = np.concatenate(quads, axis=0)
    quads = quads[np.all(quads >= 0, axis=1)]
    return vertices, quads


def taubin_smooth(vertices, quads, iterations=4, lam=0.5, mu=-0.53):
    """形を縮めずに、格子の細かい段を消している"""
    edges = np.concatenate([quads[:, [0, 1]], quads[:, [1, 2]], quads[:, [2, 3]], quads[:, [3, 0]]], axis=0)
    edges = np.concatenate([edges, edges[:, ::-1]], axis=0)
    degree = np.bincount(edges[:, 0], minlength=vertices.shape[0]).astype(np.float32)
    for _ in range(iterations):
        for factor in (lam, mu):
            neighbor = np.zeros_like(vertices)
            np.add.at(neighbor, edges[:, 0], vertices[edges[:, 1]])
            average = neighbor / np.maximum(degree, 1.0)[:, None]
            vertices = vertices + factor * (average - vertices)
    return vertices


def sdf_normals(sdf, points, epsilon=0.02):
    offsets = np.eye(3, dtype=np.float32) * epsilon
    gradient = np.stack([sdf(points + offsets[i]) - sdf(points - offsets[i]) for i in range(3)], axis=1)
    return gradient / np.maximum(length(gradient), 1e-6)[:, None]


def ambient_occlusion(points, normals):
    """面から法線の向きに数か所で距離を測り、近くに物があるほど暗くしている（SDFの環境遮蔽）"""
    steps = np.array([0.25, 0.6, 1.1, 1.8, 2.8], np.float32)
    weights = np.array([0.30, 0.26, 0.20, 0.14, 0.10], np.float32)
    occlusion = np.zeros(points.shape[0], np.float32)
    for step, weight in zip(steps, weights):
        distance = sd_scene(points + normals * step)
        occlusion += weight * np.clip((step - distance) / step, 0.0, 1.0)
    return np.clip(1.0 - occlusion * 1.25, 0.18, 1.0)


# ----------------------------------------------------------------------------
# 回転体（懐中電灯）
# ----------------------------------------------------------------------------
def lathe(profile, segments=48):
    """(z, 半径) の輪郭を z 軸のまわりに回した面。半径0の点は1つの頂点にまとめている"""
    vertices = []
    uvs = []
    ring_start = []
    arc = [0.0]
    for i in range(1, len(profile)):
        arc.append(arc[-1] + math.hypot(profile[i][0] - profile[i - 1][0], profile[i][1] - profile[i - 1][1]))
    for (z, r), s in zip(profile, arc):
        ring_start.append(len(vertices))
        if r <= 1e-5:
            vertices.append((0.0, 0.0, z))
            uvs.append((0.5, s / arc[-1]))
            continue
        for k in range(segments + 1):
            angle = 2.0 * math.pi * k / segments
            vertices.append((r * math.cos(angle), r * math.sin(angle), z))
            uvs.append((k / segments, s / arc[-1]))
    faces = []
    for i in range(len(profile) - 1):
        a_pole = profile[i][1] <= 1e-5
        b_pole = profile[i + 1][1] <= 1e-5
        for k in range(segments):
            a0 = ring_start[i] + (0 if a_pole else k)
            a1 = ring_start[i] + (0 if a_pole else k + 1)
            b0 = ring_start[i + 1] + (0 if b_pole else k)
            b1 = ring_start[i + 1] + (0 if b_pole else k + 1)
            # 外向き: z が進む向きに輪をつなぐとき、角度の増える向きと合わせて外側が表になる
            if a_pole:
                faces.append([a0, b1, b0])
            elif b_pole:
                faces.append([a0, a1, b0])
            else:
                faces.append([a0, a1, b1, b0])
    return np.array(vertices, np.float32), faces, np.array(uvs, np.float32)


def ellipsoid_mesh(center, radii, rings=10, segments=20):
    vertices = []
    faces = []
    for i in range(rings + 1):
        phi = math.pi * i / rings
        for k in range(segments):
            theta = 2.0 * math.pi * k / segments
            vertices.append((center[0] + radii[0] * math.sin(phi) * math.cos(theta),
                             center[1] + radii[1] * math.cos(phi),
                             center[2] + radii[2] * math.sin(phi) * math.sin(theta)))
    for i in range(rings):
        for k in range(segments):
            a = i * segments + k
            b = i * segments + (k + 1) % segments
            c = (i + 1) * segments + (k + 1) % segments
            d = (i + 1) * segments + k
            faces.append([a, b, c, d])
    return np.array(vertices, np.float32), faces


def disk_mesh(z, radius, segments=48):
    vertices = [(0.0, 0.0, z)]
    for k in range(segments):
        angle = 2.0 * math.pi * k / segments
        vertices.append((radius * math.cos(angle), radius * math.sin(angle), z))
    faces = [[0, 1 + k, 1 + (k + 1) % segments] for k in range(segments)]  # 前（+z）が表
    uvs = [(0.5, 0.5)] + [(0.5 + 0.5 * math.cos(2.0 * math.pi * k / segments),
                           0.5 + 0.5 * math.sin(2.0 * math.pi * k / segments)) for k in range(segments)]
    return np.array(vertices, np.float32), faces, np.array(uvs, np.float32)


# ----------------------------------------------------------------------------
# Mayaのメッシュ作成
# ----------------------------------------------------------------------------
def to_maya(points):
    """ゲームの手元の空間（cm、左手系）から、Mayaの空間（ゲームの単位、右手系で前が-Z）へ"""
    converted = points * CM_TO_UNIT
    converted[:, 2] *= -1.0
    return converted


def from_maya(points):
    converted = points.copy()
    converted[:, 2] *= -1.0
    return converted / CM_TO_UNIT


def create_mesh(name, vertices, faces, uvs=None):
    """zを反転すると面の表裏が逆になるので、頂点の並びも逆にしてから作っている"""
    maya_vertices = to_maya(np.asarray(vertices, np.float32))
    points = om.MFloatPointArray([om.MFloatPoint(float(v[0]), float(v[1]), float(v[2])) for v in maya_vertices])
    counts = om.MIntArray()
    connects = om.MIntArray()
    for face in faces:
        face = list(face)[::-1]
        counts.append(len(face))
        for index in face:
            connects.append(int(index))
    mesh_fn = om.MFnMesh()
    transform = mesh_fn.create(points, counts, connects)
    if uvs is not None:
        mesh_fn.setUVs(om.MFloatArray([float(u) for u in uvs[:, 0]]), om.MFloatArray([float(v) for v in uvs[:, 1]]))
        mesh_fn.assignUVs(counts, connects)
    node = om.MFnDependencyNode(transform).name()
    return cmds.rename(node, name)


def clean_non_manifold(mesh):
    """薄い布を格子で面にすると、辺を3枚以上の面が共有する所ができるので、面の数を減らす前に直している"""
    cmds.select(mesh, replace=True)
    mel.eval('polyCleanupArgList 4 { "0","1","0","0","0","0","0","0","0","1e-05","0","1e-05","0","1e-05","0","2","0","0" }')
    cmds.delete(mesh, constructionHistory=True)


def assign_material(mesh, name, color):
    shader = cmds.shadingNode('lambert', asShader=True, name=name)
    cmds.setAttr(shader + '.color', color[0], color[1], color[2], type='double3')
    group = cmds.sets(renderable=True, noSurfaceShader=True, empty=True, name=name + 'SG')
    cmds.connectAttr(shader + '.outColor', group + '.surfaceShader', force=True)
    cmds.sets(mesh, edit=True, forceElement=group)


def mesh_points(mesh):
    selection = om.MSelectionList()
    selection.add(mesh)
    fn = om.MFnMesh(selection.getDagPath(0))
    raw = fn.getPoints(om.MSpace.kObject)
    return from_maya(np.array([(p.x, p.y, p.z) for p in raw], np.float32)), fn


def set_vertex_colors(mesh, colors):
    points, fn = mesh_points(mesh)
    color_array = om.MColorArray([om.MColor((float(c[0]), float(c[1]), float(c[2]), 1.0)) for c in colors])
    fn.createColorSet('colorSet1', True)
    fn.setCurrentColorSetName('colorSet1')
    fn.setVertexColors(color_array, om.MIntArray(list(range(len(colors)))))


def smoothstep(edge0, edge1, x):
    t = np.clip((x - edge0) / (edge1 - edge0), 0.0, 1.0)
    return t * t * (3.0 - 2.0 * t)


def segment_parameter(points, a, b):
    ba = b - a
    h = np.clip(((points - a) @ ba) / float(ba @ ba), 0.0, 1.0)
    distance = length(points - a - h[:, None] * ba)
    return h, distance


def skin_colors(points, normals):
    """R = 環境遮蔽、G = 爪、B = 血色（指先・関節・指の付け根が赤みを帯びる）"""
    occlusion = ambient_occlusion(points, normals)
    nail = np.zeros(points.shape[0], np.float32)
    redness = np.zeros(points.shape[0], np.float32)
    for finger in FINGERS:
        mcp, pip, dip, tip, radius = finger_joints(finger)
        h, distance = segment_parameter(points, dip, tip)
        outward = points[:, :2] / np.maximum(length(points[:, :2]), 1e-5)[:, None]
        facing = np.sum(normals[:, :2] * outward, axis=1)
        # 指先の側の6割、外を向いた面の幅の狭い所が爪
        along = smoothstep(0.30, 0.48, h) * (1.0 - smoothstep(0.98, 1.08, h))
        nail = np.maximum(nail, along * smoothstep(0.62, 0.80, facing) * (distance < radius * 1.25))
        redness = np.maximum(redness, smoothstep(radius * 2.2, 0.0, length(points - tip)) * 0.8)
        for joint in (pip, dip):
            redness = np.maximum(redness, smoothstep(radius * 1.6, radius * 0.6, length(points - joint)) * 0.45)
        redness = np.maximum(redness, smoothstep(radius * 2.0, radius * 0.8, length(points - mcp)) * 0.35)
    h, distance = segment_parameter(points, np.array(THUMB_IP, np.float32), np.array(THUMB_TIP, np.float32))
    along = smoothstep(0.32, 0.50, h) * (1.0 - smoothstep(0.98, 1.08, h))
    nail = np.maximum(nail, along * smoothstep(0.55, 0.78, normals[:, 1]) * (distance < 1.2))
    redness = np.maximum(redness, smoothstep(2.0, 0.0, length(points - np.array(THUMB_TIP, np.float32))) * 0.8)
    # 肌のむら（そばかすのような小さな色の差）
    redness = np.clip(redness + (value_noise3(points, 1.6, 3.0) - 0.5) * 0.25, 0.0, 1.0)
    return np.stack([occlusion, nail, redness], axis=1)


def flashlight_colors(points, normals, wear_seed):
    """R = 環境遮蔽、G = 塗装のはげ（角と、握る所の手の当たる所）、B = ローレット加工の範囲"""
    occlusion = ambient_occlusion(points, normals)
    radial = length(points[:, :2])
    # 輪郭の角（半径の変わる所）ほど、塗装がはげている
    corner = np.zeros(points.shape[0], np.float32)
    for (z0, r0), (z1, r1) in zip(BODY_PROFILE[:-1], BODY_PROFILE[1:]):
        if abs(r1 - r0) > 0.05:
            corner = np.maximum(corner, smoothstep(0.35, 0.0, np.abs(points[:, 2] - z0)) * (radial > 1.2))
            corner = np.maximum(corner, smoothstep(0.35, 0.0, np.abs(points[:, 2] - z1)) * (radial > 1.2))
    noise = value_noise3(points, 1.3, wear_seed)
    grip = smoothstep(-5.0, -4.2, points[:, 2]) * (1.0 - smoothstep(3.6, 4.4, points[:, 2]))
    wear = np.clip(corner * smoothstep(0.35, 0.65, noise) + grip * smoothstep(0.62, 0.80, noise) * 0.7, 0.0, 1.0)
    knurl = grip * (radial < 1.80) * (radial > 1.6)
    return np.stack([occlusion, wear, knurl], axis=1)


def build():
    cmds.file(new=True, force=True)
    cmds.loadPlugin('fbxmaya', quiet=True)
    os.makedirs(OUTPUT_DIR, exist_ok=True)

    # 手と前腕
    print('hand: surface nets')
    vertices, quads = surface_nets(sd_hand, (-3.9, -4.6, -13.3), (6.4, 4.4, 6.0), 0.09)
    vertices = taubin_smooth(vertices, quads, iterations=4)
    print('hand: %d vertices %d quads' % (vertices.shape[0], quads.shape[0]))
    skin = create_mesh('Hand', vertices, quads)
    cmds.polyReduce(skin, version=1, termination=0, percentage=86, keepQuadsWeight=0.0,
                    keepBorder=True, keepColorBorder=False, keepHardEdge=False,
                    cachingReduce=False, replaceOriginal=True, triangulate=True)
    cmds.delete(skin, constructionHistory=True)

    # 袖
    print('sleeve: surface nets')
    vertices, quads = surface_nets(sd_sleeve, (-1.6, -9.6, -33.5), (10.6, 3.8, -8.5), 0.11)
    vertices = taubin_smooth(vertices, quads, iterations=3)
    sleeve = create_mesh('Sleeve', vertices, quads)
    clean_non_manifold(sleeve)
    cmds.polyReduce(sleeve, version=1, termination=0, percentage=93, keepQuadsWeight=0.0,
                    keepBorder=True, cachingReduce=False, replaceOriginal=True, triangulate=True)
    cmds.delete(sleeve, constructionHistory=True)

    # 懐中電灯
    body_vertices, body_faces, body_uvs = lathe(BODY_PROFILE + [(12.95, 2.62)], segments=48)
    body = create_mesh('FlashlightBody', body_vertices, body_faces, body_uvs)
    bezel_vertices, bezel_faces, bezel_uvs = lathe(BEZEL_PROFILE, segments=48)
    bezel = create_mesh('Bezel', bezel_vertices, bezel_faces, bezel_uvs)
    lens_vertices, lens_faces, lens_uvs = disk_mesh(LENS_Z, LENS_RADIUS)
    lens = create_mesh('Lens', lens_vertices, lens_faces, lens_uvs)
    switch_vertices, switch_faces = ellipsoid_mesh(SWITCH_CENTER, SWITCH_RADII)
    switch = create_mesh('Switch', switch_vertices, switch_faces)

    # 法線: 手と袖はなめらかに、懐中電灯は輪郭の角を硬くしている
    for mesh in (skin, sleeve, switch, lens):
        cmds.polySoftEdge(mesh, angle=180, constructionHistory=False)
    for mesh in (body, bezel):
        cmds.polySoftEdge(mesh, angle=32, constructionHistory=False)

    # 頂点カラー（環境遮蔽と部位の印）
    print('vertex colors')
    points, _ = mesh_points(skin)
    set_vertex_colors(skin, skin_colors(points, sdf_normals(sd_hand, points)))
    points, _ = mesh_points(sleeve)
    normals = sdf_normals(sd_sleeve, points)
    set_vertex_colors(sleeve, np.stack([ambient_occlusion(points, normals),
                                        value_noise3(points, 0.9, 11.0), np.zeros(points.shape[0])], axis=1))
    for mesh, seed in ((body, 1.0), (bezel, 5.0)):
        points, _ = mesh_points(mesh)
        set_vertex_colors(mesh, flashlight_colors(points, sdf_normals(sd_flashlight, points), seed))
    for mesh in (lens, switch):
        points, _ = mesh_points(mesh)
        set_vertex_colors(mesh, np.stack([ambient_occlusion(points, sdf_normals(sd_flashlight, points)),
                                          np.zeros(points.shape[0]), np.zeros(points.shape[0])], axis=1))

    # マテリアル（ゲームは名前でシェーダーと材質を選んでいる）
    assign_material(skin, 'Skin', (0.62, 0.45, 0.37))
    assign_material(sleeve, 'Sleeve', (0.13, 0.14, 0.13))
    assign_material(body, 'FlashlightBody', (0.09, 0.09, 0.10))
    assign_material(bezel, 'Bezel', (0.60, 0.58, 0.55))
    assign_material(lens, 'Lens', (0.85, 0.88, 0.90))
    assign_material(switch, 'Switch', (0.05, 0.05, 0.05))

    cmds.select([skin, sleeve, body, bezel, lens, switch], replace=True)
    for command in ('FBXResetExport', 'FBXExportSmoothingGroups -v true', 'FBXExportHardEdges -v false',
                    'FBXExportTangents -v false', 'FBXExportSmoothMesh -v false', 'FBXExportTriangulate -v true',
                    'FBXExportInAscii -v false', 'FBXExportCameras -v false', 'FBXExportLights -v false',
                    'FBXExportEmbeddedTextures -v false'):
        mel.eval(command)
    mel.eval('FBXExport -f "%s" -s' % OUTPUT_FILE.replace('\\', '/'))
    for mesh in (skin, sleeve, body, bezel, lens, switch):
        print('%s: %d triangles' % (mesh, cmds.polyEvaluate(mesh, triangle=True)))
    print('written: ' + OUTPUT_FILE)


if __name__ == '__main__':
    try:
        build()
    finally:
        maya.standalone.uninitialize()
