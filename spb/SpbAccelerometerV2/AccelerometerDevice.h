/*++

// THIS CODE AND INFORMATION IS PROVIDED "AS IS" WITHOUT WARRANTY OF
// ANY KIND, EITHER EXPRESSED OR IMPLIED, INCLUDING BUT NOT LIMITED TO
// THE IMPLIED WARRANTIES OF MERCHANTABILITY AND/OR FITNESS FOR A
// PARTICULAR PURPOSE.

Copyright (c) Microsoft Corporation. All rights reserved

Module Name:

    AccelerometerDevice.h

Abstract:

    This module contains the type definitions for the SPB accelerometer's
    accelerometer device class.

    Definitions:
    * ACPI configuration defines and structure
    * Register setting structure
    * Accelerometer device class

    Refer to the .cpp file comment block for more details.

--*/

#ifndef _ACCELEROMETERDEVICE_H_
#define _ACCELEROMETERDEVICE_H_

#pragma once

typedef struct _REGISTER_SETTING
{
    BYTE Register;
    BYTE Value;
} REGISTER_SETTING, *PREGISTER_SETTING;

//
// ACPI configuration defines
//

// The DSM GUID for this device. It should match the GUID
// specified in the device's ACPI DSM entry.
// {7681541E-8827-4239-8D9D-36BE7FE12542}
DEFINE_GUID(ACPI_DSM_GUID, \
    0x7681541e, 0x8827, 0x4239, 0x8d, 0x9d, 0x36, 0xbe, 0x7f, 0xe1, 0x25, 0x42);

#define ACPI_DSM_REQUEST_TIMEOUT  -1000000 //100ms
#define ACPI_DSM_ARGUMENTS_COUNT  4
#define ACPI_DSM_REVISION         1
#define ACPI_DSM_CONFIG_FUNCTION  1
#define ACPI_DSM_CONFIG_COUNT     1

typedef struct _SPB_ACCELEROMETER_CONFIG
{
    BYTE ConfigParam1;
    BYTE ConfigParam2;
    BYTE ConfigParam3;
    BYTE ConfigParam4;
} SPB_ACCELEROMETER_CONFIG, *PSPB_ACCELEROMETER_CONFIG;

class CSpbRequest;

class CAccelerometerDevice :
    public CSensorDevice
{
public:
    CAccelerometerDevice();
    virtual ~CAccelerometerDevice();

// Public methods
public:
    // Interrupt callbacks
    static WUDF_INTERRUPT_ISR      OnInterruptIsr;
    static WUDF_INTERRUPT_WORKITEM OnInterruptWorkItem;

// Required methods for the abstract base CSensorDevice class
public:
    LPWSTR GetSensorObjectID() override;
protected:
    HRESULT InitializeDevice(
        _In_ IWDFDevice* pWdfDevice,
        _In_ IWDFCmResourceList* pWdfResourcesRaw,
        _In_ IWDFCmResourceList* pWdfResourcesTranslated) override;
    const PROPERTYKEY* GetSupportedProperties(_Out_ ULONG* Count) override;
    const PROPERTYKEY* GetSupportedPerDataFieldProperties(_Out_ ULONG* Count) override;
    const PROPERTYKEY* GetSupportedDataFields(_Out_ ULONG* Count) override;
    const PROPERTYKEY* GetSupportedEvents(_Out_ ULONG* Count) override;
    HRESULT GetDefaultSettableProperties(
        _Out_ ULONG* pReportInterval,
        _Out_ IPortableDeviceValues** ppChangeSensitivities) override;
    HRESULT SetDefaultPropertyValues() override;
    HRESULT ConfigureHardware() override;
    HRESULT SetReportInterval(_In_ ULONG ReportInterval) override;
    HRESULT SetChangeSensitivity(_In_ PROPVARIANT* pVar) override;
    HRESULT RequestNewData(_In_ IPortableDeviceValues* pValues) override;
    HRESULT SetDeviceStateStandby() override;
    HRESULT SetDeviceStatePolling() override;
    HRESULT SetDeviceStateEventing() override;
    HRESULT GetTestProperty(
        _In_  REFPROPERTYKEY key,
        _Out_ PROPVARIANT* pVar) override;
    HRESULT SetTestProperty(
        _In_  REFPROPERTYKEY key,
        _In_  PROPVARIANT* pVar) override;

// Private methods
private:
    HRESULT AddDataFieldValue(
        _In_  REFPROPERTYKEY         key, 
        _In_  PROPVARIANT*           pVar,
        _Out_ IPortableDeviceValues* pValues
        );

    HRESULT GetConfigurationData(
        _In_  IWDFDevice* pWdfDevice
        );

    VOID PrepareInputParametersForDsm(
        _Inout_ PACPI_EVAL_INPUT_BUFFER_COMPLEX pInputBuffer,
        _In_ ULONG InputBufferSize,
        _In_ ULONG FunctionIndex
        );

    HRESULT ParseAcpiOutputBuffer(
        _In_  PACPI_EVAL_OUTPUT_BUFFER pOutputBuffer
        );

    HRESULT ParseResources(
        _In_  IWDFDevice* pWdfDevice,
        _In_  IWDFCmResourceList* pWdfResourcesRaw,
        _In_  IWDFCmResourceList* pWdfResourcesTranslated,
        _Out_ LARGE_INTEGER* pRequestId);

    HRESULT InitializeRequest(
        _In_  IWDFDevice* pWdfDevice,
        _In_  LARGE_INTEGER id);

    HRESULT ConnectInterrupt(
        _In_     IWDFDevice* pWdfDevice,
        _In_opt_ PCM_PARTIAL_RESOURCE_DESCRIPTOR RawResource,
        _In_opt_ PCM_PARTIAL_RESOURCE_DESCRIPTOR TranslatedResource);

    HRESULT RequestData(_In_ IPortableDeviceValues * pValues);

    HRESULT ReadRegister(
        _In_                            BYTE   reg,
        _Out_writes_(dataBufferLength)  BYTE*  pDataBuffer,
        _In_                            ULONG  dataBufferLength,
        _In_                            ULONG  delayInUs
        );
    
    HRESULT WriteRegister(
        _In_                           BYTE   reg,
        _In_reads_(dataBufferLength)   BYTE*  pDataBuffer,
        _In_                           ULONG  dataBufferLength
        );

// Private members
private:
    Microsoft::WRL::ComPtr<CSpbRequest>                       m_pSpbRequest;

    // The request data buffer and status members
    BYTE*                                                     m_pDataBuffer;
    Microsoft::WRL::Wrappers::CriticalSection                 m_CriticalSection;

    // Track state of sensor device
    BOOL                                                      m_fInitialized;
    BYTE                                                      m_InterruptsEnabled;

    // Test members		                                      
    ULONG                                                     m_TestRegister;
    ULONG                                                     m_TestDataSize;
};

#endif // _ACCELEROMETERDEVICE_H_
