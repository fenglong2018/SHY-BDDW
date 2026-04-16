#!/usr/bin/env python3
"""
打包脚本：生成单文件可执行程序
- Windows: dist/ShenHaiYuTool.exe
- Linux:   dist/ShenHaiYuTool

运行：python3 build.py
"""
import subprocess
import sys
import os
import platform
import shutil

VENV_DIR = os.path.join(os.path.dirname(os.path.abspath(__file__)), ".venv")
DEPS = ["PyQt6", "pyserial", "pyinstaller"]


def in_venv():
    return sys.prefix != sys.base_prefix


def ensure_apt_deps():
    """确保 venv 和 pip 的系统包已安装（仅 Debian/Ubuntu）"""
    if platform.system() != "Linux":
        return
    if not shutil.which("apt"):
        return
    ver = sys.version_info.minor
    pkgs = [f"python3.{ver}-venv", "python3-pip"]
    missing = []
    for pkg in pkgs:
        r = subprocess.run(["dpkg", "-s", pkg],
                           stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
        if r.returncode != 0:
            missing.append(pkg)
    if missing:
        print(f"自动安装系统包: {' '.join(missing)}")
        subprocess.run(["sudo", "apt", "install", "-y"] + missing, check=True)


def create_venv():
    ensure_apt_deps()
    # 清理残缺的 venv
    if os.path.exists(VENV_DIR):
        shutil.rmtree(VENV_DIR)
    print(f"创建虚拟环境: {VENV_DIR}")
    subprocess.run([sys.executable, "-m", "venv", "--copies", VENV_DIR], check=True)
    # 确保 pip 可用
    subprocess.run([venv_python(), "-m", "ensurepip", "--upgrade"],
                   stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)


def venv_python():
    if platform.system() == "Windows":
        return os.path.join(VENV_DIR, "Scripts", "python.exe")
    return os.path.join(VENV_DIR, "bin", "python3")


def install_deps(python):
    print(f"安装依赖: {' '.join(DEPS)}")
    subprocess.run([python, "-m", "pip", "install", "--upgrade", "pip", "-q"], check=True)
    subprocess.run([python, "-m", "pip", "install"] + DEPS + ["-q"], check=True)


def build(python):
    script_dir = os.path.dirname(os.path.abspath(__file__))
    is_windows = platform.system() == "Windows"
    app_name = "ShenHaiYuTool"

    cmd = [
        python, "-m", "PyInstaller",
        "--onefile",
        "--name", app_name,
        "--distpath", "dist",
        "--workpath", "build_tmp",
        "--specpath", ".",
        "--add-data", f"ui{os.pathsep}ui",
        "main.py"
    ]
    if is_windows:
        cmd.insert(3, "--windowed")

    print(f"平台: {platform.system()} {platform.machine()}")
    print("正在打包，请稍候...")
    result = subprocess.run(cmd, cwd=script_dir)

    if result.returncode == 0:
        exe = os.path.join(script_dir, "dist", app_name + (".exe" if is_windows else ""))
        size = os.path.getsize(exe) / 1024 / 1024
        print(f"\n[OK] 打包成功！")
        print(f"   输出: {exe}")
        print(f"   大小: {size:.1f} MB")
        if not is_windows:
            os.chmod(exe, 0o755)
            print(f"   已设置可执行权限 (chmod +x)")
    else:
        print("\n[FAIL] 打包失败，请检查错误信息")
        sys.exit(1)


def main():
    # 如果已在 venv 里，直接打包
    if in_venv():
        build(sys.executable)
        return

    # 创建 venv（会自动安装缺失的系统包并清理残缺环境）
    create_venv()
    install_deps(venv_python())
    build(venv_python())


if __name__ == "__main__":
    main()
