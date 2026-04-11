# -*- mode: python ; coding: utf-8 -*-
# PyInstaller spec：在 PCSoftware 目录下执行
#   python -m PyInstaller --clean -y 4g_nic_pc.spec
# 或在 Windows 上双击 / 运行 build_windows.bat

import os

# PyInstaller 执行 spec 时注入 SPEC（本文件绝对路径）
_SPEC_DIR = os.path.dirname(os.path.abspath(SPEC))
_APP_ICO = os.path.join(_SPEC_DIR, "nic_ble_pc", "app_icon.ico")
_APP_PNG = os.path.join(_SPEC_DIR, "nic_ble_pc", "app_icon.png")

block_cipher = None

a = Analysis(
    ["main.py"],
    pathex=[_SPEC_DIR],
    binaries=[],
    # 与 nic_ble_pc/gui.py 运行时 iconbitmap/iconphoto 一致，否则 onefile 解压目录里可能没有 ico/png
    datas=[
        (_APP_ICO, "nic_ble_pc"),
        (_APP_PNG, "nic_ble_pc"),
    ],
    hiddenimports=[
        "serial",
        "serial.tools.list_ports",
    ],
    hookspath=[],
    hooksconfig={},
    runtime_hooks=[],
    excludes=[
        "bleak",
    ],
    win_no_prefer_redirects=False,
    win_private_assemblies=False,
    cipher=block_cipher,
    noarchive=False,
)

pyz = PYZ(a.pure, a.zipped_data, cipher=block_cipher)

exe = EXE(
    pyz,
    a.scripts,
    a.binaries,
    a.zipfiles,
    a.datas,
    [],
    name="4G_NIC_Admin",
    debug=False,
    bootloader_ignore_signals=False,
    strip=False,
    upx=False,
    upx_exclude=[],
    runtime_tmpdir=None,
    console=False,
    disable_windowed_traceback=False,
    argv_emulation=False,
    target_arch=None,
    codesign_identity=None,
    entitlements_file=None,
    icon=_APP_ICO,
)
