/*++
 
// THIS CODE AND INFORMATION IS PROVIDED "AS IS" WITHOUT WARRANTY OF
// ANY KIND, EITHER EXPRESSED OR IMPLIED, INCLUDING BUT NOT LIMITED TO
// THE IMPLIED WARRANTIES OF MERCHANTABILITY AND/OR FITNESS FOR A
// PARTICULAR PURPOSE.

Copyright (C) Microsoft Corporation, All Rights Reserved

Module Name:

    Queue.cpp

Abstract:

    This module contains the implementation of the SPB accelerometer's
    queue callback object.

--*/

#include "Internal.h"
#include "Device.h"

#include "Queue.h"
#include "Queue.tmh"

using namespace Microsoft::WRL;
using namespace Microsoft::WRL::Wrappers;

/////////////////////////////////////////////////////////////////////////
//
//  CMyQueue::CMyQueue
//
//  Object constructor function
//
//  Initialize member variables
//
/////////////////////////////////////////////////////////////////////////
CMyQueue::CMyQueue() :
    m_pParentDevice(nullptr)
{
}

/////////////////////////////////////////////////////////////////////////
//
//  CMyQueue::~CMyQueue
//
//  Object destructor function
//
//
/////////////////////////////////////////////////////////////////////////
CMyQueue::~CMyQueue()
{
 
}

/////////////////////////////////////////////////////////////////////////
//
//  CMyQueue::CreateInstance
//
//  This function supports the COM factory creation system
//
//  Parameters:
//      parentDevice    - A pointer to the CWSSDevice object
//      ppUkwn          - pointer to a pointer to the queue to be returned
//
//  Return Values:
//      S_OK: The queue was created successfully
//
/////////////////////////////////////////////////////////////////////////
HRESULT CMyQueue::CreateInstance(_In_ IWDFDevice*  pWdfDevice, CMyDevice* pMyDevice)
{
    HRESULT hr = S_OK;
    ComPtr<CMyQueue> pMyQueue = nullptr;

    if (nullptr == pMyDevice)
    {
        return E_INVALIDARG;
    }

    hr = MakeAndInitialize<CMyQueue>(&pMyQueue);

    if (FAILED(hr))
    {
        Trace(
            TRACE_LEVEL_ERROR,
            "Failed to create the queue object, %!HRESULT!",
            hr);
    }

    if (SUCCEEDED(hr))
    {
        // Store the parent device object
        pMyQueue->m_pParentDevice = pMyDevice;

        ComPtr<IUnknown> spIUnknown;
        
        hr = pMyQueue.CopyTo(IID_PPV_ARGS(&spIUnknown));

        if (SUCCEEDED(hr))
        {
            // Create the framework queue
            ComPtr<IWDFIoQueue> spDefaultQueue;
            hr = pWdfDevice->CreateIoQueue( spIUnknown.Get(),
                                            TRUE,                        // DefaultQueue
                                            WdfIoQueueDispatchParallel,  // Parallel queue handling 
                                            TRUE,                        // PowerManaged
                                            TRUE,                        // AllowZeroLengthRequests
                                            &spDefaultQueue 
                                            );
            if (FAILED(hr))
            {
                Trace(
                    TRACE_LEVEL_ERROR,
                    "Failed to create default I/O queue, %!HRESULT!", 
                    hr);
            }
        }
    }

    return hr;
}
/////////////////////////////////////////////////////////////////////////
//
//  CMyQueue::OnDeviceIoControl
//
//  This method is called when an IOCTL is sent to the device
//
//  Parameters:
//      pQueue            - pointer to an IO queue
//      pRequest          - pointer to an IO request
//      ControlCode       - The IOCTL to process
//      InputBufferSizeInBytes - the size of the input buffer
//      OutputBufferSizeInBytes - the size of the output buffer
//
/////////////////////////////////////////////////////////////////////////
STDMETHODIMP_ (void) CMyQueue::OnDeviceIoControl(
    _In_ IWDFIoQueue*     pQueue,
    _In_ IWDFIoRequest*   pRequest,
    _In_ ULONG            ControlCode,
         SIZE_T           InputBufferSizeInBytes,
         SIZE_T           OutputBufferSizeInBytes
    )
{
    UNREFERENCED_PARAMETER(pQueue);
    UNREFERENCED_PARAMETER(InputBufferSizeInBytes);
    UNREFERENCED_PARAMETER(OutputBufferSizeInBytes);
    
    DWORD dwWritten = 0;

    if (IS_WPD_IOCTL(ControlCode))
    {
        m_pParentDevice->ProcessIoControl(  pQueue,
                                            pRequest,
                                            ControlCode,
                                            InputBufferSizeInBytes,
                                            OutputBufferSizeInBytes,
                                            &dwWritten);
    }
    else
    {
        // Unsupported request
        pRequest->CompleteWithInformation(HRESULT_FROM_WIN32(ERROR_NOT_SUPPORTED), 0);
    }

}
