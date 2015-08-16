/*++

// THIS CODE AND INFORMATION IS PROVIDED "AS IS" WITHOUT WARRANTY OF
// ANY KIND, EITHER EXPRESSED OR IMPLIED, INCLUDING BUT NOT LIMITED TO
// THE IMPLIED WARRANTIES OF MERCHANTABILITY AND/OR FITNESS FOR A
// PARTICULAR PURPOSE.

Copyright (C) Microsoft Corporation, All Rights Reserved

Module Name:

    Driver.h

Abstract:

    This module contains the type definitions for the SPB accelerometer's
    driver callback class.

--*/

#ifndef _DRIVER_H_
#define _DRIVER_H_

#pragma once

//
// This class handles driver events for the SPB accelerometer driver.  In 
// particular it supports the OnDeviceAdd event, which occurs when the driver 
// is called to setup per-device handlers for a new device stack.
//

class CMyDriver : public Microsoft::WRL::RuntimeClass<Microsoft::WRL::RuntimeClassFlags<Microsoft::WRL::ClassicCom>, IDriverEntry>
{
public:
    CMyDriver();

public:

    // IDriverEntry
    STDMETHOD  (OnInitialize)(_In_ IWDFDriver* pDriver);
    STDMETHOD  (OnDeviceAdd)(
        _In_ IWDFDriver* pDriver, 
        _In_ IWDFDeviceInitialize* pDeviceInit);
    STDMETHOD_ (void, OnDeinitialize)(_In_ IWDFDriver* pDriver);
};

//
// Creates a factory for the SpbAccelerometerDriver coclass that is used with CoCreateInstance.
//
CoCreatableClassWithFactory(SpbAccelerometerDriver, Microsoft::WRL::SimpleClassFactory<CMyDriver>)
    
#endif // _DRIVER_H_
