#!/usr/bin/env python3
"""
打包脚本：生成单文件 EXE
运行：python build.py
"""
import subprocess
import sys
import os

def main():
    script_dir = os.path.dirname(os.path.abspath(__file__))
    os.chdir(script_dir)

    cmd = [
        sys.executable, "-m", "PyInstaller",
        "--onefile",                        # 单文件
        "--windowed",                       # 无控制台窗口
        "--name", "ShenHaiYuTool",          # EXE 名称
        "--distpath", "dist",               # 输出目录
        "--workpath", "build_tmp",          # 临时目录
        "--specpath", ".",
        "--add-data", f"ui{os.pathsep}ui",  # 包含 ui 目录
        "main.py"
    ]

    print("正在打包，请稍候...")
    result = subprocess.run(cmd, cwd=script_dir)

    if result.returncode == 0:
        exe = os.path.join(script_dir, "dist", "ShenHaiYuTool.exe")
        size = os.path.getsize(exe) / 1024 / 1024
        print(f"\n[OK] 打包成功！")
        print(f"   输出: {exe}")
        print(f"   大小: {size:.1f} MB")
    else:
        print("\n[FAIL] 打包失败，请检查错误信息")
        sys.exit(1)

if __name__ == "__main__":
    main()
