// DjiHidReader.cpp
#if PLATFORM_WINDOWS

#include "DjiHidReader.h"

#include "Windows/AllowWindowsPlatformTypes.h"
#include <windows.h>
#include <setupapi.h>
#include <hidsdi.h>
#include "Windows/HideWindowsPlatformTypes.h"

#pragma comment(lib, "setupapi.lib")
#pragma comment(lib, "hid.lib")

static const USHORT DJI_VENDOR = 0x2CA3;
static const USHORT DJI_PRODUCT = 0x1020;

FDjiHidReader& FDjiHidReader::Get()
{
    static FDjiHidReader Instance;
    return Instance;
}

FDjiHidReader::FDjiHidReader() {}

FDjiHidReader::~FDjiHidReader()
{
    Stop();
}

bool FDjiHidReader::Start()
{
    if (Thread)
        return true;

    if (!OpenDevice())
        return false;

    bRunning = true;
    Thread = FRunnableThread::Create(this, TEXT("DjiHidReaderThread"));
    return Thread != nullptr;
}

void FDjiHidReader::Stop()
{
    bRunning = false;
    if (Thread)
    {
        Thread->Kill(true);
        delete Thread;
        Thread = nullptr;
    }
    CloseDevice();
}

FDjiChannels FDjiHidReader::GetChannels()
{
    FScopeLock Lock(&DataMutex);
    return Channels;
}
bool FDjiHidReader::OpenDevice()
{
    CloseDevice();

    GUID HidGuid;
    HidD_GetHidGuid(&HidGuid);

    HDEVINFO DevInfo = SetupDiGetClassDevs(&HidGuid, nullptr, nullptr,
        DIGCF_PRESENT | DIGCF_DEVICEINTERFACE);
    if (DevInfo == INVALID_HANDLE_VALUE)
        return false;

    SP_DEVICE_INTERFACE_DATA IfData;
    IfData.cbSize = sizeof(IfData);

    bool bFound = false;

    for (DWORD i = 0; SetupDiEnumDeviceInterfaces(DevInfo, nullptr, &HidGuid, i, &IfData); ++i)
    {
        DWORD RequiredSize = 0;
        SetupDiGetDeviceInterfaceDetail(DevInfo, &IfData, nullptr, 0, &RequiredSize, nullptr);
        if (RequiredSize == 0) continue;

        TArray<uint8> Buffer;
        Buffer.SetNumUninitialized(RequiredSize);

        auto* Detail = reinterpret_cast<SP_DEVICE_INTERFACE_DETAIL_DATA*>(Buffer.GetData());
        Detail->cbSize = sizeof(SP_DEVICE_INTERFACE_DETAIL_DATA);

        if (!SetupDiGetDeviceInterfaceDetail(DevInfo, &IfData, Detail,
            RequiredSize, nullptr, nullptr))
        {
            continue;
        }

        HANDLE Handle = CreateFileW(
            Detail->DevicePath,
            GENERIC_READ | GENERIC_WRITE,
            FILE_SHARE_READ | FILE_SHARE_WRITE,
            nullptr,
            OPEN_EXISTING,
            FILE_FLAG_OVERLAPPED, // or 0 for blocking
            nullptr);

        if (Handle == INVALID_HANDLE_VALUE)
            continue;

        HIDD_ATTRIBUTES Attr;
        Attr.Size = sizeof(Attr);
        if (HidD_GetAttributes(Handle, &Attr))
        {
            if (Attr.VendorID == DJI_VENDOR && Attr.ProductID == DJI_PRODUCT)
            {
                DeviceHandle = Handle;
                bFound = true;
                break;
            }
        }

        CloseHandle(Handle);
    }

    SetupDiDestroyDeviceInfoList(DevInfo);
    return bFound;
}

void FDjiHidReader::CloseDevice()
{
    if (DeviceHandle)
    {
        CloseHandle(DeviceHandle);
        DeviceHandle = nullptr;
    }
}

uint32 FDjiHidReader::Run()
{
    if (!DeviceHandle)
        return 0;

    // You may want to query report length; for now assume <= 64
    const DWORD ReportLen = 64;
    uint8 Buffer[ReportLen];

    while (bRunning)
    {
        DWORD BytesRead = 0;
        BOOL ok = ReadFile(DeviceHandle, Buffer, ReportLen, &BytesRead, nullptr);
        if (!ok || BytesRead == 0)
        {
            FPlatformProcess::Sleep(0.005f);
            continue;
        }

        DecodeReport(Buffer, (int32)BytesRead);
    }

    return 0;
}
void FDjiHidReader::DecodeReport(const uint8* Data, int32 Length)
{
    // TEMP: log first few bytes so you can see them in Output Log
    FString Hex;
    for (int32 i = 0; i < Length; ++i)
    {
        Hex += FString::Printf(TEXT("%02X "), Data[i]);
    }
    UE_LOG(LogTemp, Verbose, TEXT("DJI HID: %s"), *Hex);

    // TODO: once you identify the bytes for channels, replace this with real decoding
}
