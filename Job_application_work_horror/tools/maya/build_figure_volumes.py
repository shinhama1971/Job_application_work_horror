# ============================================================================
# ファイルの役割: シェーダーの中で光線を進めて描く人の形の「距離場」を、格子に焼いてファイルに書き出している。
# 実行: "C:\Program Files\Autodesk\Maya2025\bin\mayapy.exe" tools\maya\build_figure_volumes.py
# 出力: assets\volume\figure.sdfv（鏡と水たまりにだけ映る黒い人影）、assets\volume\self_body.sdfv（鏡に映る自分の体と色）
#
# ・距離場は、各格子点から一番近い面までの距離（中は負）。シェーダー（shader/figureVolume.hlsli）は3Dテクスチャで読み、
#   光線を「その点の距離だけ」進める（スフィアトレーシング）ことで、少ない回数で面に当たる所を求めている。
# ・人影は、人影のモデル（build_props.py の sd_shadow_man）と同じ形を、鏡の異変の背の高さ（頭の上が約39）に大きくしている。
# ・自分の体は、懐中電灯を前に持った立ち姿（FirstPersonHands と同じ持ち方）。色は一番近い部位の色を、距離と一緒に焼いている。
# ・座標: 足もとが原点、x が右、y が上、z が前（ゲームの単位）。範囲と格子の間隔は shader/figureVolume.hlsli と同じ値にしている。
#
# ファイルの形: 先頭に "SDFV"、版（1）、格子の数 x・y・z、チャンネル数、範囲の最小の角 x・y・z、格子の間隔（リトルエンディアン）、
# 続けて float16 の値（x が一番内側、次に y、最後に z。チャンネルは格子点ごとにまとめている）。SignedDistanceVolume.cpp が読む。
# ============================================================================

import os
import struct

import numpy as np

from build_flashlight_hand import smin, sd_capsule, sd_ellipsoid
from build_props import sd_shadow_man

import maya.standalone

SCRIPT_DIR = os.path.dirname(os.path.abspath(__file__))
OUTPUT_DIR = os.path.normpath(os.path.join(SCRIPT_DIR, '..', '..', 'assets', 'volume'))

# shader/figureVolume.hlsli の FIGURE_VOLUME_* / SELF_VOLUME_* と同じ値
FIGURE_MIN = (-7.5, -0.5, -4.0)
FIGURE_SIZE = (15.0, 40.5, 10.0)
FIGURE_SCALE = 1.25          # 人影のモデル（身長約31）を、頭の上が約39になるよう大きくしている
SELF_MIN = (-8.5, -0.5, -4.5)
SELF_SIZE = (17.0, 42.0, 21.5)
VOXEL = 0.4


def grid_points(minimum, size):
    counts = [int(round(s / VOXEL)) + 1 for s in size]
    axes = [minimum[i] + np.arange(counts[i], dtype=np.float32) * VOXEL for i in range(3)]
    # 並びは z・y・x（x が一番内側）。3Dテクスチャの行・面の並びと同じ
    zz, yy, xx = np.meshgrid(axes[2], axes[1], axes[0], indexing='ij')
    return counts, np.stack([xx, yy, zz], axis=-1).reshape(-1, 3)


def write_volume(name, counts, minimum, channels, values):
    os.makedirs(OUTPUT_DIR, exist_ok=True)
    path = os.path.join(OUTPUT_DIR, name)
    with open(path, 'wb') as file:
        file.write(struct.pack('<4sIIIIIffff', b'SDFV', 1, counts[0], counts[1], counts[2], channels,
                               minimum[0], minimum[1], minimum[2], VOXEL))
        file.write(values.astype(np.float16).tobytes())
    print('%-14s %3d x %3d x %3d  %d ch  %7.1f KB' % (name, counts[0], counts[1], counts[2], channels,
                                                     os.path.getsize(path) / 1024.0))


# ----------------------------------------------------------------------------
# 自分の体（懐中電灯を前に持った立ち姿）。部位ごとの距離と色
# ----------------------------------------------------------------------------
JACKET = (0.16, 0.17, 0.16)
PANTS = (0.10, 0.10, 0.11)
SHOES = (0.05, 0.045, 0.04)
SKIN = (0.34, 0.27, 0.22)
HAIR = (0.035, 0.030, 0.028)
FLASHLIGHT = (0.06, 0.06, 0.07)


def self_parts(p):
    """(距離, 色) の並び。形は MirrorSelf のカプセルと同じ寸法を、なめらかにつないだもの"""
    parts = []
    legs = None
    for side in (-1.0, 1.0):
        thigh = sd_capsule(p, (side * 2.0, 17.5, 0.0), (side * 2.0, 9.0, 0.3), 2.1, 1.8)
        shin = sd_capsule(p, (side * 2.0, 9.0, 0.3), (side * 2.0, 1.8, 0.0), 1.75, 1.3)
        leg = smin(thigh, shin, 0.4)
        legs = leg if legs is None else np.minimum(legs, leg)
        parts.append((sd_ellipsoid(p, (side * 2.0, 0.9, 0.8), (1.4, 1.0, 2.6)), SHOES))
    pelvis = smin(sd_ellipsoid(p, (0.0, 18.6, 0.0), (4.0, 2.6, 2.6)), legs, 0.8)
    parts.append((pelvis, PANTS))
    torso = sd_capsule(p, (0.0, 19.5, 0.0), (0.0, 28.6, 0.0), 4.3, 4.7)
    torso = smin(torso, sd_ellipsoid(p, (0.0, 26.5, 0.3), (5.2, 4.4, 3.0)), 1.0)
    for side in (-1.0, 1.0):
        torso = smin(torso, sd_ellipsoid(p, (side * 4.6, 30.0, 0.0), (2.4, 2.0, 2.2)), 0.8)
    # 左腕（下へ垂らす）と右腕（懐中電灯を前へ）。袖は手首まで
    left_arm = smin(sd_capsule(p, (-5.4, 30.0, 0.0), (-6.0, 23.5, 0.2), 1.45, 1.3),
                    sd_capsule(p, (-6.0, 23.5, 0.2), (-6.2, 17.6, 0.8), 1.25, 1.1), 0.3)
    right_arm = smin(sd_capsule(p, (5.4, 30.0, 0.0), (5.8, 25.0, 4.5), 1.45, 1.3),
                     sd_capsule(p, (5.8, 25.0, 4.5), (4.6, 26.4, 9.4), 1.25, 1.1), 0.3)
    jacket = smin(smin(torso, left_arm, 0.7), right_arm, 0.7)
    parts.append((jacket, JACKET))
    hands = np.minimum(sd_ellipsoid(p, (-6.2, 16.1, 0.9), (0.9, 1.6, 1.1)),
                       sd_ellipsoid(p, (4.3, 26.6, 10.6), (1.2, 1.3, 1.4)))
    head = smin(sd_capsule(p, (0.0, 31.0, 0.0), (0.0, 34.0, 0.2), 1.4, 1.3),
                sd_ellipsoid(p, (0.0, 37.4, 0.4), (2.4, 2.9, 2.6)), 0.6)
    parts.append((np.minimum(hands, head), SKIN))
    # 髪：頭の上と後ろを覆う
    hair = np.maximum(sd_ellipsoid(p, (0.0, 38.2, -0.1), (2.65, 2.7, 2.85)), (37.0 - p[:, 1]) - np.maximum(-p[:, 2] - 0.6, 0.0))
    parts.append((hair, HAIR))
    # 懐中電灯（握りと、少し太い頭）
    torch = np.minimum(sd_capsule(p, (4.0, 26.8, 8.8), (3.4, 27.6, 14.8), 0.45, 0.45),
                       sd_capsule(p, (3.5, 27.45, 13.6), (3.35, 27.65, 15.2), 0.68, 0.68))
    parts.append((torch, FLASHLIGHT))
    return parts


def build_self():
    counts, points = grid_points(SELF_MIN, SELF_SIZE)
    parts = self_parts(points)
    distances = np.stack([d for d, _ in parts], axis=1)
    colors = np.array([c for _, c in parts], np.float32)
    nearest = np.argmin(distances, axis=1)
    # 体の形は部位をなめらかにつないだ距離。色は一番近い部位の色
    shape = parts[0][0]
    for distance, _ in parts[1:]:
        shape = smin(shape, distance, 0.3)
    values = np.concatenate([shape[:, None], colors[nearest]], axis=1)
    write_volume('self_body.sdfv', counts, SELF_MIN, 4, values)


def build_figure():
    counts, points = grid_points(FIGURE_MIN, FIGURE_SIZE)
    values = sd_shadow_man(points / FIGURE_SCALE) * FIGURE_SCALE
    write_volume('figure.sdfv', counts, FIGURE_MIN, 1, values[:, None])


if __name__ == '__main__':
    try:
        build_figure()
        build_self()
    finally:
        maya.standalone.uninitialize()
