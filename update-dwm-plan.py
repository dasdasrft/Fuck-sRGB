#!/usr/bin/env python3
"""
Fuck-sRGB: DWM Shader 签名与偏移量自动提取与更新工具

当 Windows 大版本更新导致 dwmcore.dll 重新编译、现有托盘程序提示:
"Could not find all six verified SDR shaders in dwmcore.dll" 时，
运行此脚本即可自动从当前系统的 dwmcore.dll 中提取最新的 6 个着色器 Hash 与偏移量，
并自动重新生成 tray-patch-plan.h！
"""

import os
import sys
import struct
import hashlib

SCRIPT_DIR = os.path.dirname(os.path.abspath(__file__))
PLAN_FILE = os.path.join(SCRIPT_DIR, "tray-patch-plan.h")
DEFAULT_DLL = r"C:\Windows\System32\dwmcore.dll"

def extract_plan(dll_path: str = DEFAULT_DLL):
    if not os.path.isfile(dll_path):
        print(f"[-] 错误: 未找到目标文件: {dll_path}")
        return False

    print(f"[*] 正在分析: {dll_path}")
    with open(dll_path, "rb") as f:
        disk = f.read()

    # 1. 解析 PE 结构，定位 .rdata 段
    if disk[:2] != b"MZ":
        print("[-] 错误: 非有效 PE 文件")
        return False

    e_lfanew = struct.unpack("<I", disk[0x3c:0x40])[0]
    if disk[e_lfanew:e_lfanew+4] != b"PE\x00\x00":
        print("[-] 错误: 非有效 NT PE 头")
        return False

    num_sections = struct.unpack("<H", disk[e_lfanew+6:e_lfanew+8])[0]
    opt_hdr_size = struct.unpack("<H", disk[e_lfanew+20:e_lfanew+22])[0]
    section_table = e_lfanew + 24 + opt_hdr_size

    rdata = None
    for i in range(num_sections):
        sec = disk[section_table + i*40 : section_table + (i+1)*40]
        name = sec[:8].rstrip(b"\x00")
        if name == b".rdata":
            v_size, v_addr, raw_size, raw_ptr = struct.unpack("<IIII", sec[8:24])
            rdata = disk[raw_ptr : raw_ptr + raw_size]
            break

    if not rdata:
        print("[-] 错误: 未能在 dwmcore.dll 中找到 .rdata 段")
        return False

    print(f"[+] 成功定位 .rdata 只读段 (大小: {len(rdata)} 字节)")

    # 2. 定义标准 sRGB EOTF 解码曲线的 4 组向量常数 (float4)
    # original = { 2.4f, 0.04045f, 0.055f, 0.94786733f }
    c0 = struct.pack("<ffff", 2.4, 2.4, 2.4, 0.0)
    c1 = struct.pack("<ffff", 0.04045, 0.04045, 0.04045, 0.0)
    c2 = struct.pack("<ffff", 0.055, 0.055, 0.055, 0.0)
    c3 = struct.pack("<ffff", 0.94786733, 0.94786733, 0.94786733, 0.0)

    # 3. 扫描 .rdata 中所有的 DXBC Library 容器
    pos = 0
    candidate_libraries = []

    while True:
        pos = rdata.find(b"DXBC", pos)
        if pos == -1:
            break
        if pos + 28 <= len(rdata):
            ver, size = struct.unpack("<II", rdata[pos+20:pos+28])
            if ver == 1 and pos + size <= len(rdata):
                blob = rdata[pos : pos + size]
                count = struct.unpack("<I", blob[28:32])[0]
                if 2 <= count <= (size - 32) // 4:
                    next_part = 32 + count * 4
                    is_lib = True
                    parts = []
                    for j in range(count):
                        part_off = struct.unpack("<I", blob[32+j*4 : 36+j*4])[0]
                        if part_off != next_part or size - part_off < 8:
                            is_lib = False
                            break
                        part_len = struct.unpack("<I", blob[part_off+4 : part_off+8])[0]
                        payload_off = part_off + 8
                        next_part = payload_off + part_len
                        tag = blob[part_off : part_off + 4]
                        if j == count - 1:
                            if tag != b"LIBH" or next_part != size:
                                is_lib = False
                        else:
                            if tag != b"LIBF":
                                is_lib = False
                            else:
                                parts.append((payload_off, part_len))
                    if is_lib and parts:
                        matched_in_lib = []
                        for idx, (p_off, p_len) in enumerate(parts):
                            shader = blob[p_off : p_off + p_len]
                            o0 = shader.find(c0)
                            o1 = shader.find(c1)
                            o2 = shader.find(c2)
                            o3 = shader.find(c3)
                            if o0 != -1 and o1 != -1 and o2 != -1 and o3 != -1:
                                sha = hashlib.sha256(shader).digest()
                                matched_in_lib.append({
                                    "fn_idx": idx,
                                    "size": p_len,
                                    "hash": sha,
                                    "offsets": (o0, o1, o2, o3)
                                })
                        if matched_in_lib:
                            candidate_libraries.append(matched_in_lib)
        pos += 4

    print(f"[+] 找到包含 SDR sRGB 曲线转换的着色器库数量: {len(candidate_libraries)}")

    # 4. 提取负责 DWM 核心桌面窗口合成的 3 个核心着色器库 (每个库提取第 16/17 号对应函数)
    selected_shaders = []
    for lib in candidate_libraries:
        for item in lib:
            # 匹配典型的核心管线着色器 (索引 16 与 17)
            if item["fn_idx"] in (16, 17):
                selected_shaders.append(item)

    if len(selected_shaders) != 6:
        print(f"[!] 警告: 预期匹配 6 个核心着色器，当前匹配到 {len(selected_shaders)} 个。")
        # 如果由于 Windows 版本差异函数索引略有偏移，采用前 3 个库各自的前 2 个匹配项
        if len(candidate_libraries) >= 3:
            selected_shaders = []
            for lib in candidate_libraries[:3]:
                selected_shaders.extend(lib[:2])

    if len(selected_shaders) != 6:
        print(f"[-] 提取失败: 未能精准捕获完整的 6 个着色器。")
        return False

    print(f"[+] 成功捕获全部 6 个 SDR 目标着色器！正在生成 {os.path.basename(PLAN_FILE)} ...")

    # 5. 生成 C++ 头文件 tray-patch-plan.h
    lines = [
        "// Full SHA-256 and shader-relative offsets of the verified SDR-to-scRGB shaders.",
        "// Auto-generated by update-dwm-plan.py",
        "constexpr ShaderSignature shaders[] = {"
    ]

    for s in selected_shaders:
        size_hex = f"0x{s['size']:x}"
        hash_bytes = ", ".join(f"0x{b:02x}" for b in s["hash"])
        offsets_hex = ", ".join(f"0x{off:x}" for off in s["offsets"])
        lines.append(f"    {{{size_hex}, {{{hash_bytes}}}, {{{offsets_hex}}}}},")

    lines.append("};\n")

    content = "\n".join(lines)
    with open(PLAN_FILE, "w", encoding="utf-8") as f:
        f.write(content)

    print(f"🎉 成功！已将最新的 6 个着色器签名与偏移量更新至:\n    {PLAN_FILE}")
    return True

if __name__ == "__main__":
    target = sys.argv[1] if len(sys.argv) > 1 else DEFAULT_DLL
    extract_plan(target)
