/*
 * This file is a part of the open source stm32plus library.
 * Copyright (c) 2011,2012,2013,2014 Andy Brown <www.andybrown.me.uk>
 * Please see website for licensing terms.
 */

#pragma once


namespace stm32plus {
  namespace usb {


    /**
     * Template base class for USB CDC devices. The usual control endpoint 0 is inherited
     * as is the mandatory IN interrupt endpoint for notifications to the host at address 1.
     * Subclasses should provide the endpoints they require in the features list. e.g. bulk
     * IN/OUT endpoints.
     *
     * @tparam TPhy the PHY implementation
     * @tparam TConfigurationDescriptor A structure that holds the complete config descriptor
     * @tparam Features... The device feature classes
     */

    template<class TPhy,class TConfigurationDescriptor,template <class> class... Features>
    class CdcDevice : public Device<TPhy>,
                      public ControlEndpointFeature<Device<TPhy>>,
                      public InterruptInEndpointFeature<1,Device<TPhy>>,
                      public Features<Device<TPhy>>... {


      public:

        /*
         * Parameters for the CDC device
         */

        struct Parameters : Device<TPhy>::Parameters,
                            ControlEndpointFeature<Device<TPhy>>::Parameters,
                            InterruptInEndpointFeature<1,Device<TPhy>>::Parameters,
                            Features<Device<TPhy>>::Parameters... {

          uint8_t cdc_cmd_poll_interval;         // default is 16ms

          Parameters() {
            cdc_cmd_poll_interval=16;
          }
        };

      protected:

        enum {
          COMMAND_EP_ADDRESS = EndpointDescriptor::IN | 1     // command endpoint address
        };

        enum {
          MAX_COMMAND_EP_PACKET_SIZE = 16,                    // command max packet size
        };

        TConfigurationDescriptor  _configurationDescriptor;
        uint8_t _opCode;
        uint8_t _commandSize;
        uint8_t _commandBuffer[MAX_COMMAND_EP_PACKET_SIZE];

      protected:
        void onEvent(UsbEventDescriptor& event);
        void onCdcSetup(DeviceClassSdkSetupEvent& event);

      public:
        CdcDevice();
        ~CdcDevice();

        bool initialise(Parameters& params);
    };


    /**
     * Constructor
     */

    template<class TPhy,class TConfigurationDescriptor,template <class> class... Features>
    inline CdcDevice<TPhy,TConfigurationDescriptor,Features...>::CdcDevice()
      : ControlEndpointFeature<Device<TPhy>>(static_cast<Device<TPhy>&>(*this)),
        InterruptInEndpointFeature<1,Device<TPhy>>(static_cast<Device<TPhy>&>(*this)),
        Features<Device<TPhy>>(static_cast<Device<TPhy>&>(*this))... {

      // reset state

      _opCode=0;

      // subscribe to USB events

      this->UsbEventSender.insertSubscriber(
          UsbEventSourceSlot::bind(this,&CdcDevice<TPhy,TConfigurationDescriptor,Features...>::onEvent)
        );
    }


    /**
     * Destructor
     */

    template<class TPhy,class TConfigurationDescriptor,template <class> class... Features>
    inline CdcDevice<TPhy,TConfigurationDescriptor,Features...>::~CdcDevice() {

      // unsubscribe from USB events

      this->UsbEventSender.removeSubscriber(
          UsbEventSourceSlot::bind(this,&CdcDevice<TPhy,TConfigurationDescriptor,Features...>::onEvent)
        );
    }


    /**
     * Initialise the class
     * @param params The parameters class
     * @return true if it worked
     */

    template<class TPhy,class TConfigurationDescriptor,template <class> class... Features>
    inline bool CdcDevice<TPhy,TConfigurationDescriptor,Features...>::initialise(Parameters& params) {

      // initialise upwards

      if(!Device<TPhy>::initialise(params) ||
         !ControlEndpointFeature<Device<TPhy>>::initialise(params) ||
         !RecursiveBoolInitWithParams<CdcDevice,Features<Device<TPhy>>...>::tinit(this,params))
        return false;

      // set up the command endpoint descriptor

      _configurationDescriptor.commandEndpoint.bEndpointAddress=COMMAND_EP_ADDRESS;
      _configurationDescriptor.commandEndpoint.bmAttributes=EndpointDescriptor::INTERRUPT;
      _configurationDescriptor.commandEndpoint.wMaxPacketSize=MAX_COMMAND_EP_PACKET_SIZE;     // max packet size
      _configurationDescriptor.commandEndpoint.bInterval=params.cdc_cmd_poll_interval;        // default is 16ms

      // link UsbEventSource class into the SDK structure

      USBD_RegisterClass(&this->_deviceHandle,static_cast<UsbEventSource *>(this));
      return true;
    }


    /**
     * Event handler for device events
     * @param event The event descriptor
     */

    template<class TPhy,class TConfigurationDescriptor,template <class> class... Features>
    __attribute__((noinline)) inline void CdcDevice<TPhy,TConfigurationDescriptor,Features...>::onEvent(UsbEventDescriptor& event) {

      // check for handled events

      switch(event.eventType) {

        case UsbEventDescriptor::EventType::CLASS_INIT:
          USBD_LL_OpenEP(&this->_deviceHandle,COMMAND_EP_ADDRESS,EndpointDescriptor::INTERRUPT,MAX_COMMAND_EP_PACKET_SIZE);
          break;

        case UsbEventDescriptor::EventType::CLASS_DEINIT:
          USBD_LL_CloseEP(&this->_deviceHandle,COMMAND_EP_ADDRESS);
          break;

        case UsbEventDescriptor::EventType::CLASS_SETUP:
          onCdcSetup(static_cast<DeviceClassSdkSetupEvent&>(event));
          break;

        default:
          break;
      }
    }


    /**
     * Handle the CDC setup requests
     * @param event the event containg value being requested
     */

    template<class TPhy,class TConfigurationDescriptor,template <class> class... Features>
    inline void CdcDevice<TPhy,TConfigurationDescriptor,Features...>::onCdcSetup(DeviceClassSdkSetupEvent& event) {

      // interested in class requests

      if((event.request.bmRequest & USB_REQ_TYPE_MASK)!=USB_REQ_TYPE_CLASS)
        return;

      if(event.request.wLength) {

        if((event.request.bmRequest & 0x80)!=0) {

          // raise the control event

          this->UsbEventSender.raiseEvent(
              CdcControlEvent(event.request.bRequest,_commandBuffer,event.request.wLength)
            );

          // send the message on the control endpoint

          USBD_CtlSendData(&this->_deviceHandle,_commandBuffer,event.request.wLength);
        }
        else {

          _opCode=event.request.bRequest;
          _commandSize=event.request.wLength;

          USBD_CtlPrepareRx(&this->_deviceHandle,_commandBuffer,event.request.wLength);
        }
      }
      else {

        // raise the control event

        this->UsbEventSender.raiseEvent(CdcControlEvent(event.request.bRequest,nullptr,0));
      }
    }
  }
}
