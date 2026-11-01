# ============================================================================
# ファイルの役割: 当たり判定のある箱（Wall）で置いている小物の、見た目だけのモデルを作り、FBXに書き出している。
# 実行: "C:\Program Files\Autodesk\Maya2025\bin\mayapy.exe" tools\maya\build_wall_props.py
# 出力: assets\model\props\wall_*.fbx（ゲームは Wall::SetVisualModel で、箱の代わりに描く）
#
# ・Wall は 1×1×1 の箱を、配置ファイルに書いた幅・高さ・奥行きに伸ばして使っている。
#   モデルは「標準の大きさ」（下の各関数の見出し）で、原点を中心に作り、ゲームが置いた箱の大きさに合わせて伸ばしている。
# ・長い物（棚・梁・額縁の棒・柱の管）は x を長い向きにして作り、ゲームが箱の一番長い向きへ回している。
# ・マテリアルの名前 Main は、配置ファイルの色で塗る部位。それ以外（書類・取っ手など）は、ゲームが決めた色で塗る。
# ・警告灯とライトの Lens は、配置ファイルの色で塗って光らせる部位（レンズ・電球）。Housing・Shade は光らない枠や笠。
# 置き方・面取り・環境遮蔽の焼き込みは build_props.py と同じ。
# ============================================================================

import math
import random

from build_props import begin, box, cylinder, torus, sphere, finish

import maya.standalone
import maya.cmds as cmds


# ----------------------------------------------------------------------------
# 書類棚。標準 90 × 44 × 6（長い向きが x）と、30 × 44 × 10（奥の部屋の壁際の棚）
# ----------------------------------------------------------------------------
def build_shelf(name, length, depth, seed):
    begin()
    random.seed(seed)
    half_length = length * 0.5
    half_depth = depth * 0.5
    # 4本の柱（L字の鋼材）
    for x in (-half_length + 0.6, half_length - 0.6):
        for z in (-half_depth + 0.4, half_depth - 0.4):
            box('Main', (1.2, 44.0, 0.8), (x, 0.0, z), bevel=0.08)
    # 棚板（縁を折り返した鋼板）
    levels = [-21.0, -10.5, 0.0, 10.5, 21.3]
    for y in levels:
        box('Main', (length - 0.4, 0.5, depth - 0.2), (0.0, y, 0.0), bevel=0.06)
        for z in (-half_depth + 0.15, half_depth - 0.15):
            box('Main', (length - 0.4, 1.0, 0.3), (0.0, y - 0.25, z), bevel=0.05)
    # 両端の筋かい
    for x in (-half_length + 0.6, half_length - 0.6):
        for k in range(4):
            y0 = levels[k]
            box('Main', (0.25, 11.6, 0.25), (x, y0 + 5.25, 0.0), rotate=(math.degrees(math.atan2(depth - 1.0, 10.5)) * (1 if k % 2 else -1), 0.0, 0.0))
    # 棚の中身（ファイル・段ボール箱・紙の束）。ところどころ空けている
    for k in range(4):
        floor = levels[k] + 0.25
        x = -half_length + 1.6
        while x < half_length - 2.0:
            kind = random.random()
            if kind < 0.18:
                x += random.uniform(3.0, 9.0)
                continue
            if kind < 0.55:
                # 背表紙を並べたファイル（少し傾く）
                count = random.randint(3, 8)
                for _ in range(count):
                    width = random.uniform(1.0, 1.8)
                    if x + width > half_length - 1.6:
                        break
                    height = random.uniform(6.5, 8.8)
                    tilt = random.uniform(-6.0, 6.0) if random.random() < 0.3 else 0.0
                    box('Binder', (width, height, depth * 0.72), (x + width * 0.5, floor + height * 0.5, 0.0),
                        rotate=(0.0, 0.0, tilt), bevel=0.08)
                    x += width + 0.05
            elif kind < 0.85:
                width = random.uniform(6.0, 10.0)
                if x + width > half_length - 1.6:
                    break
                height = random.uniform(5.0, 7.0)
                box('Box', (width, height, depth * 0.8), (x + width * 0.5, floor + height * 0.5, 0.0),
                    rotate=(0.0, random.uniform(-4.0, 4.0), 0.0), bevel=0.15)
                x += width + random.uniform(0.3, 1.5)
            else:
                width = random.uniform(5.0, 8.0)
                if x + width > half_length - 1.6:
                    break
                height = random.uniform(1.0, 3.5)
                box('Paper', (width, height, depth * 0.7), (x + width * 0.5, floor + height * 0.5, 0.0),
                    rotate=(0.0, random.uniform(-8.0, 8.0), 0.0), bevel=0.1)
                x += width + random.uniform(0.5, 2.0)
    finish(name, soft_angle=35.0, occlusion_distance=4.0)


# ----------------------------------------------------------------------------
# 机の天板 40 × 2 × 24（下に幕板と引き出し）と、机の脚 2 × 23 × 2
# ----------------------------------------------------------------------------
def build_desk_top():
    begin()
    box('Main', (40.0, 1.4, 24.0), (0.0, 0.3, 0.0), bevel=0.25, segments=2)
    for z in (-10.6, 10.6):
        box('Main', (35.0, 3.0, 0.8), (0.0, -1.9, z), bevel=0.1)
    for x in (-17.6, 17.6):
        box('Main', (0.8, 3.0, 20.4), (x, -1.9, 0.0), bevel=0.1)
    box('Main', (12.0, 2.4, 0.4), (9.0, -1.9, -11.1), bevel=0.1)
    sphere('Metal', 0.38, (9.0, -1.9, -11.5), sides=12, rings=8)
    finish('wall_desk_top', soft_angle=35.0, occlusion_distance=3.0)


def build_desk_leg():
    begin()
    box('Main', (1.6, 20.0, 1.6), (0.0, -0.5, 0.0), bevel=0.15)
    box('Main', (2.0, 2.2, 2.0), (0.0, 10.4, 0.0), bevel=0.12)
    box('Main', (1.9, 1.0, 1.9), (0.0, -11.0, 0.0), bevel=0.2, segments=2)
    finish('wall_desk_leg', soft_angle=35.0, occlusion_distance=2.0)


# ----------------------------------------------------------------------------
# 木箱 14 × 14 × 14（板を打ち付けた箱。縁の板と、すき間のある板）
# ----------------------------------------------------------------------------
def build_crate():
    begin()
    random.seed(7)
    half = 7.0
    # 中の暗い箱（すき間から見える）
    box('Dark', (12.8, 12.8, 12.8))
    # 4面の板（3枚ずつ、すき間あり）と、縁の板
    for axis in ('x', 'z'):
        for side in (-1.0, 1.0):
            for k in range(3):
                offset = -4.4 + k * 4.4
                if axis == 'x':
                    box('Main', (0.5, 3.8, 13.0), (side * (half - 0.25), offset, 0.0),
                        rotate=(random.uniform(-0.8, 0.8), 0.0, 0.0), bevel=0.08)
                else:
                    box('Main', (13.0, 3.8, 0.5), (0.0, offset, side * (half - 0.25)),
                        rotate=(0.0, 0.0, random.uniform(-0.8, 0.8)), bevel=0.08)
    for y in (-1.0, 1.0):
        for k in range(4):
            box('Main', (13.6, 0.5, 2.9), (0.0, y * (half - 0.25), -4.9 + k * 3.27), bevel=0.08)
    # 角の柱と斜めの補強
    for x in (-1.0, 1.0):
        for z in (-1.0, 1.0):
            box('Main', (1.6, 14.0, 1.6), (x * (half - 0.6), 0.0, z * (half - 0.6)), bevel=0.12)
    for side in (-1.0, 1.0):
        box('Main', (0.6, 17.0, 1.6), (side * (half + 0.05), 0.0, 0.0), rotate=(45.0, 0.0, 0.0), bevel=0.1)
    finish('wall_crate', soft_angle=30.0, occlusion_distance=3.0)


# ----------------------------------------------------------------------------
# 崩れた資材の山 40 × 30 × 30（折れた板と角材、割れたコンクリートの塊）
# ----------------------------------------------------------------------------
def build_debris():
    begin()
    random.seed(11)
    for _ in range(7):
        size = (random.uniform(5.0, 11.0), random.uniform(4.0, 9.0), random.uniform(5.0, 10.0))
        box('Concrete', size, (random.uniform(-14.0, 14.0), -15.0 + size[1] * 0.35, random.uniform(-11.0, 11.0)),
            rotate=(random.uniform(-25, 25), random.uniform(0, 180), random.uniform(-25, 25)), bevel=0.8, segments=1)
    for _ in range(12):
        length = random.uniform(16.0, 34.0)
        box('Main', (length, random.uniform(0.8, 1.6), random.uniform(2.0, 4.0)),
            (random.uniform(-8.0, 8.0), random.uniform(-12.0, 8.0), random.uniform(-8.0, 8.0)),
            rotate=(random.uniform(-40, 40), random.uniform(0, 180), random.uniform(-35, 35)), bevel=0.12)
    finish('wall_debris', soft_angle=30.0, occlusion_distance=5.0)


# ----------------------------------------------------------------------------
# 鋼の梁 60 × 6 × 6（I形。長い向きが x）
# ----------------------------------------------------------------------------
def build_beam():
    begin()
    for y in (-2.6, 2.6):
        box('Main', (60.0, 0.8, 6.0), (0.0, y, 0.0), bevel=0.1)
    box('Main', (60.0, 4.4, 0.8), bevel=0.05)
    for x in (-24.0, 0.0, 24.0):
        for z in (-1.0, 1.0):
            box('Main', (0.5, 4.4, 2.4), (x, 0.0, z * 1.6))
    finish('wall_beam', soft_angle=30.0, occlusion_distance=3.0)


# ----------------------------------------------------------------------------
# 引き出しのキャビネット 28 × 24 × 12（前後の面に引き出し）
# ----------------------------------------------------------------------------
def build_cabinet():
    begin()
    box('Main', (28.0, 23.0, 11.2), (0.0, -0.5, 0.0), bevel=0.3, segments=2)
    box('Main', (28.4, 1.0, 12.0), (0.0, 11.5, 0.0), bevel=0.2)
    for side in (-1.0, 1.0):
        for column in (-1.0, 1.0):
            for row in range(3):
                y = 6.5 - row * 7.0
                box('Main', (12.6, 6.4, 0.4), (column * 6.9, y, side * 5.8), bevel=0.15)
                box('Handle', (4.0, 0.5, 0.7), (column * 6.9, y + 1.6, side * 6.25), bevel=0.15)
                box('Handle', (2.2, 1.0, 0.12), (column * 6.9, y - 1.4, side * 6.05))
    finish('wall_cabinet', soft_angle=30.0, occlusion_distance=3.0)


# ----------------------------------------------------------------------------
# 壁際の配電箱 10 × 30 × 24（扉は -x の面）
# ----------------------------------------------------------------------------
def build_service_box():
    begin()
    box('Main', (9.4, 30.0, 24.0), (0.3, 0.0, 0.0), bevel=0.3, segments=2)
    box('Main', (0.5, 27.0, 21.0), (-4.65, 0.0, 0.0), bevel=0.2)
    for z in (-8.0, 8.0):
        for k in range(6):
            box('Slit', (0.1, 0.5, 5.0), (-4.92, 9.0 - k * 1.3, z))
    for y in (-9.0, 9.0):
        cylinder('Handle', 0.35, 2.4, (-4.9, y, 10.4), sides=10)
    box('Handle', (0.8, 4.0, 1.0), (-5.2, 0.0, -8.5), bevel=0.2)
    box('Handle', (0.1, 3.0, 5.0), (-4.92, -10.5, 0.0))
    finish('wall_service_box', soft_angle=30.0, occlusion_distance=3.0)


# ----------------------------------------------------------------------------
# ポンプ 30 × 26 × 26（台座・モーター・渦巻きの胴・配管）
# ----------------------------------------------------------------------------
def build_pump():
    begin()
    box('Main', (30.0, 2.0, 22.0), (0.0, -12.0, 0.0), bevel=0.3)
    for x in (-12.0, 12.0):
        for z in (-9.0, 9.0):
            cylinder('Main', 0.5, 0.6, (x, -10.8, z), sides=6)
    cylinder('Paint', 6.5, 13.0, (-6.5, -3.0, 0.0), rotate=(0.0, 0.0, 90.0), sides=28, bevel=0.6, segments=2)
    for k in range(6):
        torus('Paint', 6.6, 0.25, (-11.0 + k * 1.8, -3.0, 0.0), rotate=(0.0, 0.0, 90.0), sides=28, rings=6)
    box('Main', (10.0, 4.0, 10.0), (-6.5, -9.0, 0.0), bevel=0.3)
    cylinder('Main', 7.5, 7.0, (6.5, -3.0, 0.0), rotate=(0.0, 0.0, 90.0), sides=28, bevel=0.8, segments=2)
    box('Main', (6.0, 3.0, 8.0), (6.5, -9.5, 0.0), bevel=0.3)
    cylinder('Main', 2.2, 6.0, (6.5, -3.0, 10.0), rotate=(90.0, 0.0, 0.0), sides=18)
    cylinder('Main', 3.0, 0.8, (6.5, -3.0, 12.6), rotate=(90.0, 0.0, 0.0), sides=18, bevel=0.1)
    cylinder('Main', 1.8, 9.0, (6.5, 8.0, 0.0), sides=18)
    cylinder('Main', 2.6, 0.8, (6.5, 12.5, 0.0), sides=18, bevel=0.1)
    finish('wall_pump', soft_angle=35.0, occlusion_distance=4.0)


# ----------------------------------------------------------------------------
# タンク 20 × 34 × 30（脚の付いた縦長のタンク。断面は楕円）
# ----------------------------------------------------------------------------
def build_tank():
    begin()
    cylinder('Main', 1.0, 24.0, (0.0, 1.0, 0.0), sides=32, scale=(9.4, 1.0, 14.0))
    sphere('Main', 1.0, (0.0, 13.0, 0.0), scale=(9.4, 3.5, 14.0), sides=32, rings=16)
    sphere('Main', 1.0, (0.0, -11.0, 0.0), scale=(9.4, 2.5, 14.0), sides=32, rings=16)
    for y in (-6.0, 4.0, 11.0):
        cylinder('Main', 1.0, 0.45, (0.0, y, 0.0), sides=32, scale=(9.65, 1.0, 14.3))
    for x in (-1.0, 1.0):
        for z in (-1.0, 1.0):
            box('Main', (1.2, 6.0, 1.2), (x * 5.5, -14.0, z * 9.5), bevel=0.15)
    cylinder('Main', 1.4, 4.0, (0.0, 17.0, 4.0), sides=16)
    cylinder('Main', 2.0, 0.5, (0.0, 19.0, 4.0), sides=16, bevel=0.1)
    finish('wall_tank', soft_angle=40.0, occlusion_distance=4.0)


# ----------------------------------------------------------------------------
# 排水口 16 × 0.8 × 16（鋳物の格子と枠、下の暗い穴）
# ----------------------------------------------------------------------------
def build_drain():
    begin()
    box('Dark', (14.0, 0.2, 14.0), (0.0, -0.35, 0.0))
    for side in (-1.0, 1.0):
        box('Main', (16.0, 0.8, 1.2), (0.0, 0.0, side * 7.4), bevel=0.15)
        box('Main', (1.2, 0.8, 16.0), (side * 7.4, 0.0, 0.0), bevel=0.15)
    for k in range(9):
        box('Main', (0.6, 0.7, 14.0), (-5.6 + k * 1.4, 0.0, 0.0), bevel=0.1)
    finish('wall_drain', soft_angle=30.0, occlusion_distance=1.5)


# ----------------------------------------------------------------------------
# 柱 10 × 38 × 10（面取りした角柱と、上下の台）
# ----------------------------------------------------------------------------
def build_column():
    begin()
    box('Main', (8.6, 32.0, 8.6), bevel=0.6, segments=2)
    for y in (-17.5, 17.5):
        box('Main', (10.0, 3.0, 10.0), (0.0, y, 0.0), bevel=0.35, segments=2)
        box('Main', (9.4, 0.8, 9.4), (0.0, y * 0.86, 0.0), bevel=0.2)
    finish('wall_column', soft_angle=30.0, occlusion_distance=3.0)


# ----------------------------------------------------------------------------
# 管の柱 50 × 3 × 3（長い向きが x。継ぎ目のフランジと、留め金）
# ----------------------------------------------------------------------------
def build_pipe_pole():
    begin()
    cylinder('Main', 1.2, 50.0, rotate=(0.0, 0.0, 90.0), sides=18)
    for x in (-24.6, -8.0, 8.0, 24.6):
        cylinder('Main', 1.5, 0.8, (x, 0.0, 0.0), rotate=(0.0, 0.0, 90.0), sides=18, bevel=0.1)
    finish('wall_pipe_pole', soft_angle=40.0, occlusion_distance=2.0)


# ----------------------------------------------------------------------------
# ロッカーの本体 10 × 44 × 16（扉は Locker が別に描く。上の通気口・脚・継ぎ目）
# ----------------------------------------------------------------------------
def build_locker_body():
    begin()
    box('Main', (10.0, 42.0, 16.0), (0.0, 0.5, 0.0), bevel=0.25, segments=2)
    box('Main', (10.4, 0.8, 16.4), (0.0, 21.8, 0.0), bevel=0.2)
    for x in (-1.0, 1.0):
        for z in (-1.0, 1.0):
            box('Main', (1.2, 1.4, 1.2), (x * 3.8, -21.3, z * 6.8), bevel=0.15)
    for side in (-1.0, 1.0):
        for k in range(5):
            box('Slit', (6.0, 0.05, 0.6), (0.0, 22.22, -2.4 + k * 1.2))
        box('Slit', (9.6, 0.25, 0.05), (0.0, 0.0, side * 8.01))
    finish('wall_locker_body', soft_angle=30.0, occlusion_distance=3.0)


# ----------------------------------------------------------------------------
# 額縁の棒 20 × 2 × 1.8（長い向きが x。断面を上下・前後で対称にし、縦にも横にも使えるようにしている）
# ----------------------------------------------------------------------------
def build_frame_rail():
    begin()
    box('Main', (20.0, 2.0, 1.8), bevel=0.45, segments=3)
    for (y, z) in ((1.0, 0.0), (-1.0, 0.0), (0.0, 0.9), (0.0, -0.9)):
        cylinder('Main', 0.14, 20.0, (0.0, y, z), rotate=(0.0, 0.0, 90.0), sides=8)
    finish('wall_frame_rail', soft_angle=45.0, occlusion_distance=1.0)


# ----------------------------------------------------------------------------
# 壁の警告灯 10 × 4 × 1（長い向きが x。光るレンズを、金属の枠と保護の格子で囲む。前後の両面とも同じ形）
# ----------------------------------------------------------------------------
def build_warning_lamp():
    begin()
    box('Housing', (10.0, 4.0, 0.3), bevel=0.08)
    box('Lens', (8.4, 2.6, 0.75), bevel=0.3, segments=2)
    for y in (-1.6, 1.6):
        box('Housing', (10.0, 0.6, 0.95), (0.0, y, 0.0), bevel=0.1)
    for x in (-4.65, 4.65):
        box('Housing', (0.7, 3.8, 0.95), (x, 0.0, 0.0), bevel=0.1)
    for z in (-0.5, 0.5):
        for x in (-2.8, 0.0, 2.8):
            cylinder('Housing', 0.07, 2.8, (x, 0.0, z), sides=6)
        cylinder('Housing', 0.07, 8.6, (0.0, 0.0, z), rotate=(0.0, 0.0, 90.0), sides=6)
    for x in (-4.65, 4.65):
        for y in (-1.6, 1.6):
            cylinder('Housing', 0.16, 1.05, (x, y, 0.0), rotate=(90.0, 0.0, 0.0), sides=8)
    finish('wall_warning_lamp', soft_angle=35.0, occlusion_distance=1.0)


# ----------------------------------------------------------------------------
# 安全地帯の裸電球 2.6 × 3.2 × 2.6（上に金属のソケット、下に光る電球）
# ----------------------------------------------------------------------------
def build_safe_bulb():
    begin()
    cylinder('Housing', 0.5, 0.9, (0.0, 1.15, 0.0), sides=16, bevel=0.08)
    for y in (0.95, 1.25):
        torus('Housing', 0.5, 0.05, (0.0, y, 0.0), sides=16, rings=5)
    cylinder('Housing', 0.42, 0.3, (0.0, 0.6, 0.0), sides=16)
    sphere('Lens', 0.45, (0.0, 0.35, 0.0), scale=(1.0, 0.8, 1.0), sides=16, rings=8)
    sphere('Lens', 1.25, (0.0, -0.4, 0.0), scale=(1.0, 0.95, 1.0), sides=24, rings=16)
    finish('wall_safe_bulb', soft_angle=60.0, occlusion_distance=0.6)


# ----------------------------------------------------------------------------
# 卓上ライトの笠 5 × 3 × 5（金属の笠と、下から見える光る電球の面）・腕 0.8 × 9 × 0.8・台 4 × 1 × 4
# ----------------------------------------------------------------------------
def build_desk_lamp_shade():
    begin()
    sides = 32
    shade = cylinder('Shade', 2.5, 2.5, (0.0, -0.25, 0.0), sides=sides)
    # 上の輪を縮めて円すい台の笠にし、下のふたを消して口を開けている
    cmds.scale(0.42, 1.0, 0.42, '%s.vtx[%d:%d]' % (shade, sides, sides * 2 - 1), pivot=(0.0, 1.0, 0.0), relative=True)
    cmds.delete('%s.f[%d]' % (shade, sides))
    # 内側の面（口から見える笠の裏）
    inner = cylinder('Lens', 2.4, 2.4, (0.0, -0.25, 0.0), sides=sides)
    cmds.scale(0.42, 1.0, 0.42, '%s.vtx[%d:%d]' % (inner, sides, sides * 2 - 1), pivot=(0.0, 0.95, 0.0), relative=True)
    cmds.delete('%s.f[%d]' % (inner, sides))
    cmds.polyNormal(inner, normalMode=0, userNormalMode=0, constructionHistory=False)
    torus('Housing', 2.5, 0.09, (0.0, -1.5, 0.0), sides=32, rings=6)
    cylinder('Housing', 0.55, 0.5, (0.0, 1.25, 0.0), sides=16, bevel=0.1)
    sphere('Lens', 0.75, (0.0, -0.6, 0.0), sides=16, rings=10)
    finish('wall_desk_lamp_shade', soft_angle=50.0, occlusion_distance=None)


def build_desk_lamp_arm():
    begin()
    cylinder('Main', 0.22, 8.4, sides=12)
    for y in (-4.1, 0.0, 4.1):
        sphere('Main', 0.38, (0.0, y, 0.0), sides=12, rings=8)
    cylinder('Main', 0.32, 0.9, (0.0, 0.0, 0.0), rotate=(90.0, 0.0, 0.0), sides=12)
    finish('wall_desk_lamp_arm', soft_angle=40.0, occlusion_distance=0.6)


def build_desk_lamp_base():
    begin()
    cylinder('Main', 2.0, 0.6, (0.0, -0.2, 0.0), sides=28, bevel=0.2, segments=2)
    cylinder('Main', 0.8, 0.5, (0.0, 0.25, 0.0), sides=20, bevel=0.1)
    box('Housing', (0.5, 0.35, 0.8), (1.3, 0.25, 0.0), bevel=0.08)
    finish('wall_desk_lamp_base', soft_angle=40.0, occlusion_distance=0.8)


# ----------------------------------------------------------------------------
# 天井沿いの長い配管 300 × 3 × 3（長い向きが x。約30ごとの継ぎ目のフランジと、約60ごとに天井から吊る金具）
# ----------------------------------------------------------------------------
def build_pipe_run():
    begin()
    cylinder('Main', 1.15, 300.0, rotate=(0.0, 0.0, 90.0), sides=16)
    for k in range(11):
        x = -150.0 + 15.0 + k * 27.0
        cylinder('Main', 1.5, 0.7, (x, 0.0, 0.0), rotate=(0.0, 0.0, 90.0), sides=16, bevel=0.1)
    for k in range(5):
        x = -120.0 + k * 60.0
        torus('Main', 1.25, 0.18, (x + 6.0, 0.0, 0.0), rotate=(0.0, 0.0, 90.0), sides=16, rings=6)
        box('Main', (0.3, 4.6, 0.3), (x + 6.0, 3.6, 0.0))
        box('Main', (1.6, 0.3, 1.6), (x + 6.0, 5.8, 0.0), bevel=0.05)
    finish('wall_pipe_run', soft_angle=40.0, occlusion_distance=2.0)


# ----------------------------------------------------------------------------
# 扉の上の表示灯 10 × 5 × 1（光る表示面を金属の枠で囲み、上に壁へ留める金具。前後の両面とも同じ形）
# ----------------------------------------------------------------------------
def build_door_indicator():
    begin()
    box('Housing', (10.0, 5.0, 0.4), bevel=0.1)
    box('Lens', (8.6, 3.4, 0.8), (0.0, -0.2, 0.0), bevel=0.15, segments=2)
    for y in (-2.25, 1.85):
        box('Housing', (10.0, 0.5, 1.0), (0.0, y, 0.0), bevel=0.1)
    for x in (-4.65, 4.65):
        box('Housing', (0.7, 4.6, 1.0), (x, -0.1, 0.0), bevel=0.1)
    for x in (-3.0, 3.0):
        box('Housing', (0.6, 0.9, 0.6), (x, 2.55, 0.0), bevel=0.08)
    for x in (-4.65, 4.65):
        for y in (-2.25, 1.85):
            cylinder('Housing', 0.15, 1.1, (x, y, 0.0), rotate=(90.0, 0.0, 0.0), sides=8)
    finish('wall_door_indicator', soft_angle=35.0, occlusion_distance=1.0)


# ----------------------------------------------------------------------------
# 電球を吊るすコード 0.4 × 2.5 × 0.4（布で巻いたコードと、天井の金具）
# ----------------------------------------------------------------------------
def build_hanging_cord():
    begin()
    cylinder('Main', 0.11, 2.3, (0.0, -0.1, 0.0), sides=8)
    for k in range(6):
        torus('Main', 0.11, 0.025, (0.0, -1.0 + k * 0.36, 0.0), sides=8, rings=4)
    cylinder('Main', 0.55, 0.3, (0.0, 1.1, 0.0), sides=16, bevel=0.08)
    finish('wall_hanging_cord', soft_angle=60.0, occlusion_distance=None)


if __name__ == '__main__':
    try:
        build_shelf('wall_shelf_long', 90.0, 6.0, 3)
        build_shelf('wall_shelf_short', 30.0, 10.0, 5)
        build_desk_top()
        build_desk_leg()
        build_crate()
        build_debris()
        build_beam()
        build_cabinet()
        build_service_box()
        build_pump()
        build_tank()
        build_drain()
        build_column()
        build_pipe_pole()
        build_locker_body()
        build_frame_rail()
        build_warning_lamp()
        build_safe_bulb()
        build_desk_lamp_shade()
        build_desk_lamp_arm()
        build_desk_lamp_base()
        build_pipe_run()
        build_door_indicator()
        build_hanging_cord()
    finally:
        maya.standalone.uninitialize()
