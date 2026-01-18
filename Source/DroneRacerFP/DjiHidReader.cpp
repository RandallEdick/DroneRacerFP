// DjiHidReader.cpp

#include "DjiHidReader.h"

#include "HAL/RunnableThread.h"
#include "Misc/ScopeLock.h"

DEFINE_LOG_CATEGORY(LogDjiHid);

TUniquePtr<FDjiHidReader> FDjiHidReader::Instance;
FCriticalSection          FDjiHidReader::InstanceMutex;

#if PLATFORM_WINDOWS

#include "Windows/AllowWindowsPlatformTypes.h"
#include <windows.h>
#include <hidsdi.h>
#include <setupapi.h>
#include <dbt.h>
#include "Windows/HideWindowsPlatformTypes.h"

#endif // PLATFORM_WINDOWS

// ============================================================================
// Singleton
// ============================================================================

FDjiHidReader& FDjiHidReader::Get()
{
	FScopeLock Lock(&InstanceMutex);

	if (!Instance.IsValid())
	{
		Instance = MakeUnique<FDjiHidReader>();
	}
	return *Instance.Get();
}

bool FDjiHidReader::IsCreated()
{
	FScopeLock Lock(&InstanceMutex);
	return Instance.IsValid();
}

// ============================================================================
// FDjiHidReader
// ============================================================================

FDjiHidReader::FDjiHidReader(const FString& InDevicePath, uint32 InInputReportLen)
	: DevicePath(InDevicePath)
	, InputReportLen(InInputReportLen)
	, Thread(nullptr)
	, bStopRequested(false)
#if PLATFORM_WINDOWS
	, DeviceHandle(nullptr)
	, StopEvent(nullptr)
#endif
{
}

FDjiHidReader::~FDjiHidReader()
{
	Shutdown();
}

bool FDjiHidReader::Start()
{
	if (Thread)
	{
		return true; // already running
	}

#if PLATFORM_WINDOWS
	if (!StopEvent)
	{
		HANDLE NewStop = CreateEvent(nullptr, true, false, nullptr);
		if (!NewStop)
		{
			UE_LOG(LogDjiHid, Error, TEXT("DJI: Failed to create StopEvent. GetLastError=%d"), GetLastError());
			return false;
		}
		StopEvent = NewStop;
	}
#endif

	bStopRequested.AtomicSet(false);

	Thread = FRunnableThread::Create(
		this,
		TEXT("FDjiHidReaderThread"),
		0,
		TPri_Normal
	);

	if (!Thread)
	{
		UE_LOG(LogDjiHid, Error, TEXT("DJI: Failed to create FDjiHidReader thread."));
#if PLATFORM_WINDOWS
		if (StopEvent)
		{
			CloseHandle(static_cast<HANDLE>(StopEvent));
			StopEvent = nullptr;
		}
#endif
		return false;
	}

	return true;
}

void FDjiHidReader::Shutdown()
{
	if (!Thread)
	{
#if PLATFORM_WINDOWS
		CloseWindowsHandles();
#endif
		return;
	}

	bStopRequested.AtomicSet(true);

#if PLATFORM_WINDOWS
	if (StopEvent)
	{
		SetEvent(static_cast<HANDLE>(StopEvent));
	}
#endif

	Thread->WaitForCompletion();
	delete Thread;
	Thread = nullptr;

#if PLATFORM_WINDOWS
	CloseWindowsHandles();
#endif
}

void FDjiHidReader::Stop()
{
	bStopRequested.AtomicSet(true);

#if PLATFORM_WINDOWS
	if (StopEvent)
	{
		SetEvent(static_cast<HANDLE>(StopEvent));
	}
#endif
}

uint32 FDjiHidReader::Run()
{
#if !PLATFORM_WINDOWS
	UE_LOG(LogDjiHid, Warning, TEXT("DJI: FDjiHidReader only implemented for Windows."));
	return 0;
#else
	UE_LOG(LogDjiHid, Warning, TEXT("DJI: Run loop starting"));

	if (!OpenDevice())
	{
		UE_LOG(LogDjiHid, Error, TEXT("DJI: Failed to open HID device: %s"), *DevicePath);
		return 0;
	}

	const uint32 ReportLen = (InputReportLen > 0u) ? InputReportLen : 64u;

	TArray<uint8> Buffer;
	Buffer.SetNumUninitialized(ReportLen);

	while (!bStopRequested)
	{
		HANDLE Handle = static_cast<HANDLE>(DeviceHandle);
		if (!Handle)
		{
			UE_LOG(LogDjiHid, Warning, TEXT("DJI: DeviceHandle is null, exiting read loop."));
			break;
		}

		OVERLAPPED Overlapped = {};
		Overlapped.hEvent = CreateEvent(nullptr, true, false, nullptr);
		if (!Overlapped.hEvent)
		{
			UE_LOG(LogDjiHid, Error, TEXT("DJI: Failed to create overlapped event. GetLastError=%d"), GetLastError());
			break;
		}

		DWORD BytesRead = 0;
		BOOL bReadOk = ReadFile(
			Handle,
			Buffer.GetData(),
			ReportLen,
			&BytesRead,
			&Overlapped
		);

		if (!bReadOk)
		{
			const DWORD Err = GetLastError();

			if (Err == ERROR_IO_PENDING)
			{
				HANDLE WaitHandles[2];
				DWORD HandleCount = 0;

				WaitHandles[HandleCount++] = Overlapped.hEvent;

				HANDLE StopHandle = static_cast<HANDLE>(StopEvent);
				if (StopHandle)
				{
					WaitHandles[HandleCount++] = StopHandle;
				}

				const DWORD WaitRes = WaitForMultipleObjects(
					HandleCount,
					WaitHandles,
					false,      // bWaitAll = false (use 'false', not FALSE macro)
					INFINITE
				);

				if (WaitRes == WAIT_OBJECT_0)
				{
					// Overlapped I/O completed
					if (!GetOverlappedResult(Handle, &Overlapped, &BytesRead, false))
					{
						const DWORD ResErr = GetLastError();
						UE_LOG(LogDjiHid, Warning, TEXT("DJI: GetOverlappedResult failed. Err=%d"), ResErr);

						CloseHandle(Overlapped.hEvent);

						if (ResErr == ERROR_DEVICE_NOT_CONNECTED || ResErr == ERROR_GEN_FAILURE)
						{
							UE_LOG(LogDjiHid, Warning, TEXT("DJI: Device disconnected during overlapped read (Err=%d). Exiting read loop."), ResErr);
							break;
						}

						if (bStopRequested)
						{
							break;
						}
						continue;
					}
				}
				else if (StopHandle && WaitRes == WAIT_OBJECT_0 + 1)
				{
					// StopEvent signaled
					CancelIoEx(Handle, &Overlapped);
					CloseHandle(Overlapped.hEvent);
					UE_LOG(LogDjiHid, Verbose, TEXT("DJI: Stop event signaled; breaking read loop."));
					break;
				}
				else
				{
					// Unexpected wait result
					UE_LOG(LogDjiHid, Warning, TEXT("DJI: WaitForMultipleObjects returned 0x%08X"), WaitRes);
					CancelIoEx(Handle, &Overlapped);
					CloseHandle(Overlapped.hEvent);
					break;
				}
			}
			else if (Err == ERROR_DEVICE_NOT_CONNECTED || Err == ERROR_GEN_FAILURE)
			{
				// ERR=31 (ERROR_GEN_FAILURE) or device not connected => treat as graceful disconnect
				UE_LOG(LogDjiHid, Warning, TEXT("DJI: Device disconnected or generic failure (Err=%d). Exiting read loop."), Err);
				CloseHandle(Overlapped.hEvent);
				break;
			}
			else
			{
				UE_LOG(LogDjiHid, Warning, TEXT("DJI: ReadFile failed immediately. Err=%d"), Err);
				CloseHandle(Overlapped.hEvent);

				if (bStopRequested)
				{
					break;
				}
				continue;
			}
		}

		// Done with the overlapped event
		CloseHandle(Overlapped.hEvent);

		if (bStopRequested)
		{
			break;
		}

		if (BytesRead > 0)
		{
			TArray<uint8> Copy;
			Copy.Append(Buffer.GetData(), BytesRead);

			// Broadcast to any listeners
			OnInputReport.Broadcast(Copy);

			// ------------------ UPDATE CHANNELS HERE ------------------
			{
				FScopeLock Lock(&ChannelsMutex);

				// TODO: Replace this with your actual DJI parsing logic.
				// For now this just zeroes them each read so it compiles and runs.
				Channels.LeftXRaw = 0;
				Channels.LeftYRaw = 0;
				Channels.RightXRaw = 0;
				Channels.RightYRaw = 0;
				Channels.LeftX01 = 0.f;
				Channels.LeftY01 = 0.f;
				Channels.RightX01 = 0.f;
				Channels.RightY01 = 0.f;
				Channels.Throttle01 = 0.f;
			}
			// ----------------------------------------------------------
		}
	}

	UE_LOG(LogDjiHid, Warning, TEXT("DJI: Run loop exiting"));

	return 0;
#endif // PLATFORM_WINDOWS
}

void FDjiHidReader::Exit()
{
	// Nothing special; cleanup in Shutdown / destructor.
}

// ============================================================================
// Channels API
// ============================================================================

FDjiHidReader::FDjiChannels FDjiHidReader::GetChannels() const
{
	FScopeLock Lock(&ChannelsMutex);
	return Channels; // return a copy
}

// ============================================================================
// Windows-specific helpers
// ============================================================================

#if PLATFORM_WINDOWS

bool FDjiHidReader::OpenDevice()
{
	if (DeviceHandle)
	{
		return true; // already open
	}

	const FString& Path = DevicePath;
	if (Path.IsEmpty())
	{
		UE_LOG(LogDjiHid, Error, TEXT("DJI: DevicePath is empty in OpenDevice()."));
		return false;
	}

	UE_LOG(LogDjiHid, Warning, TEXT("DJI: Opening device path: %s"), *Path);

	HANDLE Handle = CreateFileW(
		*Path,
		GENERIC_READ | GENERIC_WRITE,
		FILE_SHARE_READ | FILE_SHARE_WRITE,
		nullptr,
		OPEN_EXISTING,
		FILE_FLAG_OVERLAPPED,
		nullptr
	);

	if (Handle == INVALID_HANDLE_VALUE)
	{
		const DWORD Err = GetLastError();
		UE_LOG(LogDjiHid, Error, TEXT("DJI: CreateFileW failed for HID device. Err=%d"), Err);
		DeviceHandle = nullptr;
		return false;
	}

	DeviceHandle = Handle;

	// (Optional) Query HID capabilities here and adjust InputReportLen

	return true;
}

void FDjiHidReader::CloseWindowsHandles()
{
	HANDLE Handle = static_cast<HANDLE>(DeviceHandle);
	HANDLE StopHandle = static_cast<HANDLE>(StopEvent);

	if (Handle)
	{
		CancelIoEx(Handle, nullptr);
		CloseHandle(Handle);
		DeviceHandle = nullptr;
	}

	if (StopHandle)
	{
		CloseHandle(StopHandle);
		StopEvent = nullptr;
	}
}

#endif // PLATFORM_WINDOWS
