#pragma once

#include "CoreMinimal.h"
#include "HAL/Runnable.h"
#include "HAL/CriticalSection.h"
#include "HAL/ThreadSafeBool.h"
#include "HAL/RunnableThread.h"

// Simple channel struct the game can read from
struct FDjiChannels
{
    float Roll = 0.f;  // -1..1
    float Pitch = 0.f;  // -1..1
    float Yaw = 0.f;  // -1..1
    float Throttle = 0.f;  // 0..1
};

// Singleton FRunnable that polls the DJI HID device on a background thread
class FDjiHidReader : public FRunnable
{
public:
    static FDjiHidReader& Get();

    // Start the background thread (safe to call more than once)
    void Start();

    // Stop the thread and close the device (safe to call more than once)
    void Shutdown();

    // Thread-safe getter used by your Drone character
    FDjiChannels GetChannels() const;

    // --- FRunnable interface ---
    virtual bool   Init() override;
    virtual uint32 Run() override;
    virtual void   Stop() override;
    virtual void   Exit() override {}

private:
    FDjiHidReader();
    ~FDjiHidReader();

    FDjiHidReader(const FDjiHidReader&) = delete;
    FDjiHidReader& operator=(const FDjiHidReader&) = delete;

    void OpenDevice();
    void CloseDevice();
    void ParseReport(const uint8* Data, int32 Len);

private:
    FRunnableThread* Thread;
    FThreadSafeBool  bStopRequested;

#if PLATFORM_WINDOWS
    // Stored as a generic pointer so the header doesn’t depend on <windows.h>
    void* DeviceHandle;      // nullptr when not open
    int32 InputReportLen;    // bytes, 0 when unknown
#endif

    mutable FCriticalSection ChannelsMutex;
    FDjiChannels             Channels;
};
