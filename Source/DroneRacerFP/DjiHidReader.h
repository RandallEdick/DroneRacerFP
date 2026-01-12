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
    void Shutdown();           // <-- our own function, NOT FRunnable::Stop

    FDjiChannels GetChannels();

    // FRunnable interface
    virtual uint32 Run() override;
    virtual void Stop() override;   // this is called by Kill(), we keep it light

private:
    FDjiHidReader();
    ~FDjiHidReader();

    bool OpenDevice();
    void CloseDevice();
    void DecodeReport(const uint8* Data, int32 Length);

private:
    FRunnableThread* Thread = nullptr;
    FThreadSafeBool bRunning = false;

    void* DeviceHandle = nullptr;
    uint32 InputReportLen = 0;  
    FCriticalSection DataMutex;
    FDjiChannels Channels;
};


#endif // PLATFORM_WINDOWS


