/*++

// THIS CODE AND INFORMATION IS PROVIDED "AS IS" WITHOUT WARRANTY OF
// ANY KIND, EITHER EXPRESSED OR IMPLIED, INCLUDING BUT NOT LIMITED TO
// THE IMPLIED WARRANTIES OF MERCHANTABILITY AND/OR FITNESS FOR A
// PARTICULAR PURPOSE.

Copyright (C) Microsoft Corporation, All Rights Reserved

Module Name:

    Queue.h

Abstract:

    This module contains the type definitions for the SPB accelerometer's 
    queue callback class.

--*/

#ifndef _QUEUE_H_
#define _QUEUE_H_

#pragma once

class CMyQueue : public Microsoft::WRL::RuntimeClass<Microsoft::WRL::RuntimeClassFlags<Microsoft::WRL::ClassicCom>, IQueueCallbackDeviceIoControl>
{
public:
    CMyQueue();
    virtual ~CMyQueue();

    static HRESULT CreateInstance(_In_ IWDFDevice* pWdfDevice, CMyDevice* pMyDevice);

// COM Interface methods
public:

    // IQueueCallbackDeviceIoControl
    STDMETHOD_ (void, OnDeviceIoControl)(
        _In_ IWDFIoQueue*    pQueue,
        _In_ IWDFIoRequest*  pRequest,
        _In_ ULONG           ControlCode,
             SIZE_T          InputBufferSizeInBytes,
             SIZE_T          OutputBufferSizeInBytes
        );

private:
    // Parent device object
    Microsoft::WRL::ComPtr<CMyDevice>                 m_pParentDevice;
};

#endif // _QUEUE_H_
