/*++
 
// THIS CODE AND INFORMATION IS PROVIDED "AS IS" WITHOUT WARRANTY OF
// ANY KIND, EITHER EXPRESSED OR IMPLIED, INCLUDING BUT NOT LIMITED TO
// THE IMPLIED WARRANTIES OF MERCHANTABILITY AND/OR FITNESS FOR A
// PARTICULAR PURPOSE.

Copyright (C) Microsoft Corporation, All Rights Reserved

Module Name:

    SensorDdi.cpp

Abstract:

    This module implements the ISensorDriver interface which is used
    by the Sensor Class Extension.
--*/


#include "Internal.h"
#include "SensorDevice.h"

#include "AccelerometerDevice.h"

#include "SensorDdi.h"
#include "SensorDdi.tmh"

using namespace Microsoft::WRL;
using namespace Microsoft::WRL::Wrappers;

/////////////////////////////////////////////////////////////////////////
//
//  CSensorDdi::CSensorDdi()
//
//  Constructor
//
/////////////////////////////////////////////////////////////////////////
CSensorDdi::CSensorDdi() :
    m_spClassExtension(nullptr),
    m_pSensorDevice(nullptr)
{

}

/////////////////////////////////////////////////////////////////////////
//
//  CSensorDdi::~CSensorDdi()
//
//  Destructor
//
/////////////////////////////////////////////////////////////////////////
CSensorDdi::~CSensorDdi()
{
}

/////////////////////////////////////////////////////////////////////////
//
//  CSensorDdi::Initialize()
//
//  Initialize function that will set up the sensor device and the sensor
//  driver interface.
//
//  Parameters:
//      pWdfDevice - pointer to a device object
//      pWdfResourcesRaw - pointer the raw resource list
//      pWdfResourcesTranslated - pointer to the translated resource list
//
//  Return Values:
//      status
//
/////////////////////////////////////////////////////////////////////////
HRESULT CSensorDdi::RuntimeClassInitialize(
    _In_ IWDFDevice* pWdfDevice,
    _In_ IWDFCmResourceList * pWdfResourcesRaw,
    _In_ IWDFCmResourceList * pWdfResourcesTranslated
    )
{
    FuncEntry();

    HRESULT hr = S_OK;

    // Create the sensor device object
    ComPtr<CAccelerometerDevice> pAccelerometerDevice = nullptr;
    
    pAccelerometerDevice = Make<CAccelerometerDevice>();

    if (nullptr == pAccelerometerDevice)
    {
        Trace(
            TRACE_LEVEL_ERROR,
            "Failed to create the sensor device, %!HRESULT!",
            hr);

        hr = E_OUTOFMEMORY;
    }

    if (SUCCEEDED(hr))
    {
        m_pSensorDevice = pAccelerometerDevice;
  
        // Initialize the sensor device object
        hr = m_pSensorDevice->Initialize(
            pWdfDevice,
            pWdfResourcesRaw,
            pWdfResourcesTranslated,
            this);
    }

    FuncExit();

    return hr;
}

/////////////////////////////////////////////////////////////////////////
//
//  CSensorDdi::Uninitialize()
//
//  Uninitialize function that will tear down the sensor device and 
//  report manager.
//
//  Parameters:
//
//  Return Values:
//      none
//
/////////////////////////////////////////////////////////////////////////
VOID CSensorDdi::Uninitialize()
{
    FuncEntry();

    // Uninitialize the report manager
    if (m_pSensorDevice != nullptr)
    {
        m_pSensorDevice->Uninitialize();
    }

    FuncExit();
}

/////////////////////////////////////////////////////////////////////////
//
//  CSensorDdi::SetSensorClassExtension
//
//  Custom method to receive a pointer to the sensor class extension. This 
//  pointer is provided from CMyDevice::OnPrepareHardware after the
//  sensor class extension is created.
//
//  Parameters:
//      pClassExtension - interface pointer to the sensor class extension
//
//  Return Values:
//      status
//
/////////////////////////////////////////////////////////////////////////
HRESULT CSensorDdi::SetSensorClassExtension(
    _In_ ISensorClassExtension* pClassExtension
    )
{
    FuncEntry();

    HRESULT hr = S_OK;

    if (pClassExtension == nullptr)
    {
        hr = E_POINTER;
    }
    else
    {
        // Save the class extension interface pointer
        m_spClassExtension = pClassExtension;
    }

    FuncExit();

    return hr;
}

/////////////////////////////////////////////////////////////////////////
//
//  CSensorDdi::Start()
//
//  This method configures the sensor device and places it in standby mode.
//
//  Parameters:
//
//  Return Values:
//      status
//
/////////////////////////////////////////////////////////////////////////
HRESULT CSensorDdi::Start()
{
    FuncEntry();

    HRESULT hr = S_OK;

    if (m_pSensorDevice != nullptr)
    {
        hr = m_pSensorDevice->Start();
    }

    FuncExit();

    return hr;
}

/////////////////////////////////////////////////////////////////////////
//
//  CSensorDdi::Stop()
//
//  This method disables the sensor device.
//
//  Parameters:
//
//  Return Values:
//      status
//
/////////////////////////////////////////////////////////////////////////
HRESULT CSensorDdi::Stop()
{
    FuncEntry();

    HRESULT hr = S_OK;

    if (m_pSensorDevice != nullptr)
    {
        hr = m_pSensorDevice->Stop();
    }

    FuncExit();

    return hr;
}

/////////////////////////////////////////////////////////////////////////
//
//  CSensorDdi::OnGetSupportedSensorObjects
//
//  This method is called by Sensor Class Extension during the initialize
//  procedure to get the list of sensor objects and their supported properties.
//  
//  Parameters:
//      ppSensorCollection - a double IPortableDeviceValuesCollection
//          pointer that receives the set of Sensor property values
//
//  Return Value:
//      status 
//
/////////////////////////////////////////////////////////////////////////
HRESULT CSensorDdi::OnGetSupportedSensorObjects(
    _Out_ IPortableDeviceValuesCollection** ppSensorCollection
    )
{
    Trace(TRACE_LEVEL_VERBOSE, "OnGetSupportedSensorObjects()");

    HRESULT hr = S_OK;

    if (ppSensorCollection == nullptr)
    {
        hr = E_INVALIDARG;
    }

    if (SUCCEEDED(hr))
    {
        // CoCreate a collection to store the sensor object identifiers.
        hr = CoCreateInstance(
            CLSID_PortableDeviceValuesCollection,
            nullptr,
            CLSCTX_INPROC_SERVER,
            IID_PPV_ARGS(ppSensorCollection));
    }

    ComPtr<IPortableDeviceKeyCollection> spKeys;
    ComPtr<IPortableDeviceValues> spValues;
    if (SUCCEEDED(hr))
    {
        // Get the list of supported property keys
        hr = OnGetSupportedProperties(
            m_pSensorDevice->GetSensorObjectID(),
            &spKeys);
    }

    if (SUCCEEDED(hr))
    {
        ComPtr<IWDFFile> spTemp;

        // Get the property values
        hr = OnGetProperties(
            spTemp.Get(),
            m_pSensorDevice->GetSensorObjectID(),
            spKeys.Get(),
            &spValues);
    }

    if (SUCCEEDED(hr))
    {
        hr = (*ppSensorCollection)->Add(spValues.Get());
    }

    if (FAILED(hr))
    {
        Trace(TRACE_LEVEL_ERROR, "OnGetSupportedSensorObjects(ppSensorCollection=0x%p) failed, %!HRESULT!", ppSensorCollection, hr);
    }

    return hr;
}

/////////////////////////////////////////////////////////////////////////
//
//  CSensorDdi::OnGetSupportedProperties
//
//  This method is called by Sensor Class Extension to get the list of 
//  supported properties for a particular Sensor.
//  
//  Parameters:
//      objectId - string that represents the object whose supported 
//          property keys are being requested
//      ppSupportedProperties - an IPortableDeviceKeyCollection to be populated with 
//          supported PROPERTYKEYs
//
//  Return Value:
//      status 
//
/////////////////////////////////////////////////////////////////////////
HRESULT CSensorDdi::OnGetSupportedProperties(
    _In_  LPWSTR objectId,
    _Out_ IPortableDeviceKeyCollection** ppSupportedProperties
    )
{
    Trace(TRACE_LEVEL_VERBOSE, "OnGetSupportedProperties(objectId=%S)", objectId);

    HRESULT hr = S_OK;

    if (objectId == nullptr || ppSupportedProperties == nullptr)
    {
        hr = E_INVALIDARG;
    }

    if (SUCCEEDED(hr))
    {
        CSensorDevice* pSensor = nullptr;
        hr = GetSensorObject(objectId, &pSensor);

        if (SUCCEEDED(hr))
        {
            hr = pSensor->GetSupportedProperties(ppSupportedProperties);
        }
    }

    if (FAILED(hr))
    {
        Trace(TRACE_LEVEL_ERROR, "OnGetSupportedProperties(objectId=%S, ppSupportedProperties=0x%p) failed, %!HRESULT!",
            objectId, ppSupportedProperties, hr);
    }

    return hr;
}

/////////////////////////////////////////////////////////////////////////
//
//  CSensorDdi::OnGetSupportedDataFields
//
//  This method is called by Sensor Class Extension to get the list of supported data fields
//  for a particular Sensor.
//  
//  Parameters:
//      objectId - string that represents the object whose supported 
//          property keys are being requested
//      ppSupportedDataFields - An IPortableDeviceKeyCollection to be populated with 
//          supported PROPERTYKEYs
//
// Return Value:
//      status
//
/////////////////////////////////////////////////////////////////////////
HRESULT CSensorDdi::OnGetSupportedDataFields(
    _In_  LPWSTR objectId,
    _Out_ IPortableDeviceKeyCollection** ppSupportedDataFields
    )
{
    Trace(TRACE_LEVEL_VERBOSE, "OnGetSupportedDataFields(objectId=%S)", objectId);

    HRESULT hr = S_OK;

    if (objectId == nullptr || ppSupportedDataFields == nullptr)
    {
        hr = E_INVALIDARG;
    }

    if (SUCCEEDED(hr))
    {
        CSensorDevice* pSensor = nullptr;
        hr = GetSensorObject(objectId, &pSensor);

        if (SUCCEEDED(hr))
        {
            hr = pSensor->GetSupportedDataFields(ppSupportedDataFields);
        }
    }

    if (FAILED(hr))
    {
        Trace(TRACE_LEVEL_ERROR, "OnGetSupportedDataFields(objectId=%S, ppSupportedDataFields=0x%p) failed, %!HRESULT!",
            objectId, ppSupportedDataFields, hr);
    }

    return hr;
}

/////////////////////////////////////////////////////////////////////////
//
//  CSensorDdi::OnGetSupportedEvents
//
//  This method is called by Sensor Class Extension to get the list of
//  supported events for a particular Sensor.
//  
//  Parameters:
//      objectId - String that represents the object whose supported 
//          property keys are being requested
//      ppSupportedEvents - A set of GUIDs that represent the supported events
//      pEventCount - Count of the number of events supported
//
//  Return Value:
//      status
//
/////////////////////////////////////////////////////////////////////////
HRESULT CSensorDdi::OnGetSupportedEvents(
    _In_  LPWSTR objectId,
    _Out_ GUID** ppSupportedEvents,
    _Out_ ULONG* pEventCount
    )
{
    Trace(TRACE_LEVEL_VERBOSE, "OnGetSupportedEvents(objectId=%S)", objectId);

    HRESULT hr = S_OK;

    if (objectId == nullptr || ppSupportedEvents == nullptr || pEventCount == nullptr)
    {
        hr = E_INVALIDARG;
    }

    if (SUCCEEDED(hr))
    {
        CSensorDevice* pSensor = nullptr;
        hr = GetSensorObject(objectId, &pSensor);

        if (SUCCEEDED(hr))
        {
            hr = pSensor->GetSupportedEvents(ppSupportedEvents, pEventCount);
        }
    }

    if (FAILED(hr))
    {
        Trace(TRACE_LEVEL_ERROR, "OnGetSupportedEvents(objectId=%S, ppEvents=0x%p, pEventCount=0x%p) failed, %!HRESULT!",
            objectId, ppSupportedEvents, pEventCount, hr);
    }

    return hr;
}

/////////////////////////////////////////////////////////////////////////
//
//  CSensorDdi::OnGetProperties
//
//  This method is called by Sensor Class Extension to get Sensor
//  property values for a particular Sensor.
//  
//  Parameters:
//      appId - pinter to an IWDFFile interface that represents the file 
//          object for the application requesting property values
//      objectId - string that represents the object whose property
//          key values are being requested
//      pProperties - an IPortableDeviceKeyCollection containing the list of 
//          properties being requested
//      ppPropertyValues - an IPortableDeviceValues pointer that receives the 
//          requested property values
//
//  Return Value:
//      status
//
/////////////////////////////////////////////////////////////////////////
HRESULT CSensorDdi::OnGetProperties(
    _In_  IWDFFile* appId,
    _In_  LPWSTR objectId,
    _In_  IPortableDeviceKeyCollection* pProperties,
    _Out_ IPortableDeviceValues** ppPropertyValues
    )
{
    Trace(TRACE_LEVEL_VERBOSE, "OnGetProperties(appId=0x%p, objectId=%S)", appId, objectId);

    HRESULT hr = S_OK;

    // Note: a null appId is used by the sensor class extension to query
    // the sensor's enumeration properties
    if (objectId == nullptr || pProperties == nullptr || ppPropertyValues == nullptr)
    {
        hr = E_INVALIDARG;
    }

    if (SUCCEEDED(hr))
    {
        CSensorDevice* pSensor = nullptr;
        hr = GetSensorObject(objectId, &pSensor);

        if (SUCCEEDED(hr))
        {
            hr = pSensor->GetProperties(appId, pProperties, ppPropertyValues);
        }
    }

    if (FAILED(hr))
    {
        Trace(TRACE_LEVEL_ERROR, "OnGetProperties(appId=0x%p, objectId=%S, pProperties=0x%p, ppPropertyValues=0x%p) failed, %!HRESULT!",
            appId, objectId, pProperties, ppPropertyValues, hr);
    }

    return hr;
}

/////////////////////////////////////////////////////////////////////////
//
//  CSensorDdi::OnGetDataFields
//
//  This method is called by Sensor Class Extension to get Sensor data fields
//  for a particular Sensor.
//  
//  Parameters:
//      appId - pinter to an IWDFFile interface that represents the file 
//          object for the application requesting data field values
//      objectId - string that represents the object whose data field 
//          values are being requested
//      pDataFields - an IPortableDeviceKeyCollection containing the list of 
//          data fields being requested
//      ppDataFieldValues - an IPortableDeviceValues pointer that receives the 
//          requested data field values
//
//  Return Value:
//      status
//
/////////////////////////////////////////////////////////////////////////
HRESULT CSensorDdi::OnGetDataFields(
    _In_  IWDFFile* appId,
    _In_  LPWSTR objectId,
    _In_  IPortableDeviceKeyCollection* pDataFields,
    _Out_ IPortableDeviceValues** ppDataFieldValues
    )
{
    Trace(TRACE_LEVEL_VERBOSE, "OnGetDataFields(appId=0x%p, objectId=%S)", appId, objectId);

    HRESULT hr = S_OK;

    if (appId == nullptr || objectId == nullptr || pDataFields == nullptr || ppDataFieldValues == nullptr)
    {
        hr = E_INVALIDARG;
    }

    if (SUCCEEDED(hr))
    {
        CSensorDevice* pSensor = nullptr;
        hr = GetSensorObject(objectId, &pSensor);

        if (SUCCEEDED(hr))
        {
            hr = pSensor->GetDataFields(appId, pDataFields, ppDataFieldValues);
        }
    }

    if (FAILED(hr))
    {
        Trace(TRACE_LEVEL_ERROR, "OnGetDataFields(appId=0x%p, objectId=%S, pDataFields=0x%p, ppDataFieldValues=0x%p) failed, %!HRESULT!",
            appId, objectId, pDataFields, ppDataFieldValues, hr);
    }

    return hr;
}


/////////////////////////////////////////////////////////////////////////
//
//  CSensorDdi::OnSetProperties
//
//  This method is called by Sensor Class Extension to set Sensor properties
//  for a particular Sensor.
//  
//  Parameters:
//      appId - pinter to an IWDFFile interface that represents the file 
//          object for the application specifying property values
//      objectId - string that represents the object whose property
//          key values are being specified
//      pProperties - an IPortableDeviceValues containing the list of 
//          properties being specified
//      ppResults - an IPortableDeviceValues pointer that receives the 
//          list of specified property values if successful or an error code
//
//  Return Value:
//      status
// 
//
/////////////////////////////////////////////////////////////////////////
HRESULT CSensorDdi::OnSetProperties(
    _In_ IWDFFile* appId,
    _In_ LPWSTR objectId,
    _In_ IPortableDeviceValues* pProperties,
    _Out_ IPortableDeviceValues** ppResults
    )
{
    Trace(TRACE_LEVEL_VERBOSE, "OnSetProperties(appId=0x%p, objectId=%S)", appId, objectId);

    HRESULT hr = S_OK;

    if (appId == nullptr || objectId == nullptr || pProperties == nullptr || ppResults == nullptr)
    {
        hr = E_INVALIDARG;
    }

    if (SUCCEEDED(hr))
    {
        CSensorDevice* pSensor = nullptr;
        hr = GetSensorObject(objectId, &pSensor);

        if (SUCCEEDED(hr))
        {
            hr = pSensor->SetProperties(appId, pProperties, ppResults);
        }
    }

    if (FAILED(hr))
    {
        Trace(TRACE_LEVEL_ERROR, "OnSetProperties(appId=0x%p, objectId=%S, pValues=0x%p, ppResults=0x%p) failed, %!HRESULT!", appId, objectId, pProperties, ppResults, hr);
    }

    return hr;
}

/////////////////////////////////////////////////////////////////////////
//
//  CSensorDdi::OnClientConnect
//
//  This method is called by Sensor Class Extension when a client app connects
//  to a Sensor
//  
//  Parameters:
//      appId - file object for the application requesting the conenction
//      objectId - the ID for the sensor to which the client application 
//          is connecting
//
//  Return Value:
//      status
//
/////////////////////////////////////////////////////////////////////////
HRESULT CSensorDdi::OnClientConnect(
    _In_ IWDFFile* appId,
    _In_ LPWSTR objectId
    )
{
    Trace(TRACE_LEVEL_VERBOSE, "OnClientConnect(appId=0x%p, objectId=%S)", appId, objectId);

    HRESULT hr = S_OK;

    if (appId == nullptr || objectId == nullptr)
    {
        hr = E_INVALIDARG;
    }

    if (SUCCEEDED(hr))
    {
        CSensorDevice* pSensor = nullptr;
        hr = GetSensorObject(objectId, &pSensor);

        if (SUCCEEDED(hr))
        {
            hr = pSensor->ClientConnect(appId);
        }
    }

    if (FAILED(hr))
    {
        Trace(TRACE_LEVEL_ERROR, "OnClientConnect(appId=0x%p, objectId=%S) failed, %!HRESULT!", appId, objectId, hr);
    }

    return hr;
}

/////////////////////////////////////////////////////////////////////////
//
//  CSensorDdi::OnClientDisconnect
//
//  This method is called by Sensor Class Extension when a client app 
//  disconnects from a Sensor
//  
//  Parameters:
//      appId - file object for the application requesting 
//          the disconnection
//      objectId - the ID for the sensor from which the client 
//          application is disconnecting
//
//  Return Value:
//      status
//
/////////////////////////////////////////////////////////////////////////
HRESULT CSensorDdi::OnClientDisconnect(
    _In_ IWDFFile* appId,
    _In_ LPWSTR objectId
    )
{
    Trace(TRACE_LEVEL_VERBOSE, "OnClientDisconnect(appId=0x%p, objectId=%S)", appId, objectId);

    HRESULT hr = S_OK;

    if (appId == nullptr || objectId == nullptr)
    {
        hr = E_INVALIDARG;
    }

    if (SUCCEEDED(hr))
    {
        CSensorDevice* pSensor = nullptr;
        hr = GetSensorObject(objectId, &pSensor);

        if (SUCCEEDED(hr))
        {
            hr = pSensor->ClientDisconnect(appId);
        }
    }

    if (FAILED(hr))
    {
        Trace(TRACE_LEVEL_ERROR, "OnClientDisconnect(appId=0x%p, objectId=%S) failed, %!HRESULT!", appId, objectId, hr);
    }

    return hr;
}

/////////////////////////////////////////////////////////////////////////
//
//  CSensorDdi::OnClientSubscribeToEvents
//
//  This method is called by Sensor Class Extension when a client subscribes to
//  events by calling SetEventSink
//  
//  Parameters:
//      appId - file object for the application subscribing to events
//      objectId - the ID for the sensor from which the client 
//          application is subscribing to events
//
// Return Value:
//      status
//
/////////////////////////////////////////////////////////////////////////
HRESULT CSensorDdi::OnClientSubscribeToEvents(
    _In_ IWDFFile* appId,
    _In_ LPWSTR objectId
    )
{
    Trace(TRACE_LEVEL_VERBOSE, "OnClientSubscribeToEvents(appId=0x%p, objectId=%S)", appId, objectId);

    HRESULT hr = S_OK;

    if (appId == nullptr || objectId == nullptr)
    {
        hr = E_INVALIDARG;
    }

    if (SUCCEEDED(hr))
    {
        CSensorDevice* pSensor = nullptr;
        hr = GetSensorObject(objectId, &pSensor);

        if (SUCCEEDED(hr))
        {
            hr = pSensor->ClientSubscribeToEvents(appId);
        }
    }

    if (FAILED(hr))
    {
        Trace(TRACE_LEVEL_ERROR, "OnClientSubscribeToEvents(appId=0x%p, objectId=%S) failed, %!HRESULT!", appId, objectId, hr);
    }

    return hr;
}

/////////////////////////////////////////////////////////////////////////
//
//  CSensorDdi::OnClientUnsubscribeFromEvents
//
//  This method is called by Sensor Class Extension when a client unsubscribes
//  from events by calling SetEventSink(nullptr)
//  
//  Parameters:
//      appId - file object for the application unsubscribing from events
//      objectId - the ID for the sensor from which the client 
//          application is unsubscribing from events
//
// Return Value:
//      status
//
/////////////////////////////////////////////////////////////////////////
HRESULT CSensorDdi::OnClientUnsubscribeFromEvents(
    _In_ IWDFFile* appId,
    _In_ LPWSTR objectId
    )
{
    Trace(TRACE_LEVEL_VERBOSE, "OnClientUnsubscribeFromEvents(appId=0x%p, objectId=%S)", appId, objectId);

    HRESULT hr = S_OK;

    if (appId == nullptr || objectId == nullptr)
    {
        hr = E_INVALIDARG;
    }

    if (SUCCEEDED(hr))
    {
        CSensorDevice* pSensor = nullptr;
        hr = GetSensorObject(objectId, &pSensor);

        if (SUCCEEDED(hr))
        {
            hr = pSensor->ClientUnsubscribeFromEvents(appId);
        }
    }

    if (FAILED(hr))
    {
        Trace(TRACE_LEVEL_ERROR, "OnClientUnsubscribeFromEvents(appId=0x%p, objectId=%S) failed, %!HRESULT!", appId, objectId, hr);
    }

    return hr;
}

/////////////////////////////////////////////////////////////////////////
//
//  CSensorDdi::OnProcessWpdMessage
//
//  This method handles Windows Portable Device (WPD) commands that the 
//  ISensorClassExtension::ProcessIoControl method does not handle internally
//  
//  Parameters:
//      pPortableDeviceValuesParamsUnknown - the object that is associated 
//          with this IUnknown interface contains the parameters for the 
//          WPD command
//      pPortableDeviceValuesResultsUnknown - the object that is associated 
//          with this IUnknown interface stores the results for the WPD command
//
//  Return Value:
//      status
//
/////////////////////////////////////////////////////////////////////////
HRESULT CSensorDdi::OnProcessWpdMessage(
    _In_ IUnknown* pPortableDeviceValuesParamsUnknown,
    _In_ IUnknown* pPortableDeviceValuesResultsUnknown
    )
{
    Trace(TRACE_LEVEL_VERBOSE, "OnProcessWpdMessage()");

    HRESULT hr = S_OK;

    if (pPortableDeviceValuesParamsUnknown == nullptr || pPortableDeviceValuesResultsUnknown == nullptr)
    {
        hr = E_INVALIDARG;
    }

    if (SUCCEEDED(hr))
    {
        hr = E_NOTIMPL;
    }

    if (FAILED(hr))
    {
        Trace(TRACE_LEVEL_ERROR, "OnProcessWpdMessage(pParams=0x%p, pResults=0x%p) failed, %!HRESULT!", pPortableDeviceValuesParamsUnknown, pPortableDeviceValuesResultsUnknown, hr);
    }

    return hr;
}

///////////////////////////////////////////////////////////////////////////
//
//  CSensorDdi::PostDataEvent
//
//  This method is called to post a new data event to the class extension.
//
//  Parameters:
//      SensorId - the SensorId with event
//      pDataValues - data values to post
//
//  Return Value:
//      status
//
///////////////////////////////////////////////////////////////////////////
HRESULT CSensorDdi::PostDataEvent(_In_ LPWSTR SensorId, _In_ IPortableDeviceValues* pDataValues)
{
    ComPtr<IPortableDeviceValuesCollection> spCollection;
    HRESULT hr = CoCreateInstance(
        CLSID_PortableDeviceValuesCollection,
        NULL,
        CLSCTX_INPROC_SERVER,
        IID_PPV_ARGS(&spCollection));

    if (SUCCEEDED(hr))
    {
        hr = spCollection->Add(pDataValues);

        if (SUCCEEDED(hr))
        {
            hr = m_spClassExtension->PostEvent(SensorId, spCollection.Get());
        }
    }

    if (FAILED(hr))
    {
        Trace(TRACE_LEVEL_ERROR, "Failed to post data event, %!HRESULT!", hr);
    }

    return hr;
}

///////////////////////////////////////////////////////////////////////////
//
//  CSensorDdi::PostStateChange
//
//  This method is called to post a state change event to the class extension.
//
//  Parameters:
//      SensorId - the SensorId with event
//      state - new sensor state
//
//  Return Value:
//      status
//
///////////////////////////////////////////////////////////////////////////
HRESULT CSensorDdi::PostStateChange(_In_ LPWSTR SensorId, _In_ SensorState state)
{
    HRESULT hr = m_spClassExtension->PostStateChange(SensorId, state);

    if (FAILED(hr))
    {
        Trace(TRACE_LEVEL_ERROR, "Failed to post state change event, %!HRESULT!", hr);
    }

    return hr;
}

///////////////////////////////////////////////////////////////////////////
//
//  CSensorDdi::GetSensorObject
//
//  Returns the sensor device with the specified object ID.
//
//  Parameters:
//      objectId - sensor object ID to retrieve
//      ppSensor - point to a location for the sensor device
//
//  Return Value:
//      status
//
///////////////////////////////////////////////////////////////////////////
HRESULT CSensorDdi::GetSensorObject(_In_ LPWSTR objectId, _Outptr_ CSensorDevice** ppSensor)
{
    HRESULT hr = HRESULT_FROM_WIN32(ERROR_NOT_FOUND);
    *ppSensor = nullptr;
    if (wcscmp(objectId, m_pSensorDevice->GetSensorObjectID()) == 0)
    {
        *ppSensor = m_pSensorDevice.Get();
        hr = S_OK;
    }
    else if (wcscmp(objectId, m_pSensorDevice->GetSensorObjectID()) == 0)
    {
        // TODO: add info trace for WPD device obj id
    }
    return hr;
}
