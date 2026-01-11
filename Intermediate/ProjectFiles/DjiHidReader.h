// DjiHidReader.h  (Windows only)
#pragma once

#include "CoreMinimal.h"

#if PLATFORM_WINDOWS

#include "HAL/Runnable.h"
#include "HAL/RunnableThread.h"
#include "HAL/CriticalSection.h"

struct FDjiChannels
{
    float Roll = 0.f;  // right stick X
    float Pitch = 0.f;  // right stick Y
    float Yaw = 0.f;  // left stick X
    float Throttle = 0.f;  // left stick Y (non-center)
};

class FDjiHidReader : public FRunnable
{
public:
    static FDjiHidReader& Get();

    bool Start();
    void Stop();

    FDjiChannels GetChannels();

private:
    FDjiHidReader();
    virtual ~FDjiHidReader();

    virtual uint32 Run() override;
    virtual void StopTask() { bRunning = false; }

    bool OpenDevice();
    void CloseDevice();
    void DecodeReport(const uint8* Data, int32 Length);
    bool OpenDevice();
    uint32 Run();


private:
    class FRunnableThread* Thread = nullptr;
    FThreadSafeBool bRunning = false;

    void* DeviceHandle = nullptr; // HANDLE under the hood
    FCriticalSection DataMutex;
    FDjiChannels Channels;
};

#endif // PLATFORM_WINDOWS


