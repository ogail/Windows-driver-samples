/*++

// THIS CODE AND INFORMATION IS PROVIDED "AS IS" WITHOUT WARRANTY OF
// ANY KIND, EITHER EXPRESSED OR IMPLIED, INCLUDING BUT NOT LIMITED TO
// THE IMPLIED WARRANTIES OF MERCHANTABILITY AND/OR FITNESS FOR A
// PARTICULAR PURPOSE.

Copyright (c) Microsoft Corporation. All rights reserved

Module Name:

    SpbRequest.h

Abstract:

    This module contains the type definitions for the SPB accelerometer's
    SPB request class.

--*/

#ifndef _SPBREQUEST_H_
#define _SPBREQUEST_H_

#pragma once

#define SPB_REQUEST_TIMEOUT -1000000 //100ms

class CSpbRequest : public Microsoft::WRL::RuntimeClass<Microsoft::WRL::RuntimeClassFlags<Microsoft::WRL::ClassicCom>, IUnknown>
{
public:
    CSpbRequest();
    virtual ~CSpbRequest();

// Public methods
public:
    HRESULT Initialize(
        _In_  IWDFDevice*        pWdfDevice,
        _In_  PCWSTR             pszTargetPath
        );

    HRESULT CreateAndSendWrite(
        _In_reads_(inBufferSize)  BYTE*   pInBuffer,
        _In_                      SIZE_T  inBufferSize
        );

    HRESULT CreateAndSendWriteReadSequence(
        _In_reads_(inBufferSize)     BYTE*   pInBuffer,
        _In_                         SIZE_T  inBufferSize,
        _Out_writes_(outBufferSize)  BYTE*   pOutBuffer,
        _In_                         SIZE_T  outBufferSize,
        _In_                         ULONG   delayInUs
        );

    HRESULT Cancel();

// Private methods
private:
    HRESULT CreateAndSendIoctl(
        _In_                      ULONG       ioctlCode,
        _In_reads_(inBufferSize)  BYTE*       pInBuffer,
        _In_                      SIZE_T      inBufferSize,
        _Out_                     ULONG_PTR*  pbytesTransferred
        );

// Private members
private:
    // Interface pointers
    Microsoft::WRL::ComPtr<IWDFDriver>                  m_spWdfDriver;
    Microsoft::WRL::ComPtr<IWDFDevice>                  m_spWdfDevice;
    Microsoft::WRL::ComPtr<IWDFRemoteTarget>            m_spRemoteTarget;
    Microsoft::WRL::ComPtr<IWDFIoRequest>               m_spWdfIoRequest;
    Microsoft::WRL::ComPtr<IWDFMemory>                  m_spWdfMemory;

    // Protect request instance
    Microsoft::WRL::Wrappers::CriticalSection           m_CriticalSection;

    // Track initialization state of request
    BOOL                                                m_fInitialized;
};

#endif // _SPBREQUEST_H_
