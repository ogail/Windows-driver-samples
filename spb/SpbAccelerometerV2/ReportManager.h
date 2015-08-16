/*++

// THIS CODE AND INFORMATION IS PROVIDED "AS IS" WITHOUT WARRANTY OF
// ANY KIND, EITHER EXPRESSED OR IMPLIED, INCLUDING BUT NOT LIMITED TO
// THE IMPLIED WARRANTIES OF MERCHANTABILITY AND/OR FITNESS FOR A
// PARTICULAR PURPOSE.

Copyright (c) Microsoft Corporation. All rights reserved

Module Name:

    ReportManager.h

Abstract:

    This module contains the type definitions for the SPB accelerometer's
    report manager class.

    Refer to the .cpp file comment block for more details.

--*/

#pragma once

class CSensorDevice;

class CReportManager : public Microsoft::WRL::RuntimeClass<Microsoft::WRL::RuntimeClassFlags<Microsoft::WRL::ClassicCom>, IUnknown>
{
// Public methods
public:
    CReportManager();
    ~CReportManager();
    
    VOID Initialize(
        _In_ CSensorDevice* pSensorDdi,
        _In_ ULONG initialReportInterval);
    VOID Uninitialize();
    VOID SetReportInterval(_In_ ULONG reportInterval);
    VOID NewDataAvailable();

// Private methods
private:
    static DWORD WINAPI _DataEventingThreadProc(_In_ LPVOID pvData);
    VOID ActivateDataEventingThread();
    VOID DeactivateDataEventingThread();
    BOOL IsDataEventingThreadActive() { return m_fDataEventingThreadActive; }

// Members
private:
    ULONGLONG                                                  m_LastReportTickCount;
    ULONG                                                      m_ReportInterval;
    CSensorDevice*                                             m_pSensorDevice;

    //Eventing
    HANDLE                                                     m_hDataEvent;
    HANDLE                                                     m_hDataEventingThread;
    BOOL                                                       m_fDataEventingThreadActive;
    Microsoft::WRL::Wrappers::CriticalSection                  m_ThreadCS;

};

