/*
 *******************************************************************************
 * USB-MIDI class driver for USB Host Shield 2.0 Library
 * Copyright 2012 Yuuichi Akagawa
 *
 * Idea from LPK25 USB-MIDI to Serial MIDI converter
 *   by Collin Cunningham - makezine.com, narbotic.com
 *
 * for use with USB Host Shield 2.0 from Circuitsathome.com
 * https://github.com/felis/USB_Host_Shield_2.0
 *******************************************************************************
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>
 *******************************************************************************
 */

#include "usbh_midi.h"
//////////////////////////
// MIDI MESAGES 
// midi.org/techspecs/
//////////////////////////
// STATUS BYTES
// 0x8n == noteOff
// 0x9n == noteOn
// 0xAn == afterTouch
// 0xBn == controlChange
//    n == Channel(0x0-0xf)
//////////////////////////
//DATA BYTE 1
// note# == (0-127)
// or
// control# == (0-119)
//////////////////////////
// DATA BYTE 2
// velocity == (0-127)
// or
// controlVal == (0-127)
///////////////////////////////////////////////////////////////////////////////
// USB-MIDI Event Packets
// usb.org - Universal Serial Bus Device Class Definition for MIDI Devices 1.0
///////////////////////////////////////////////////////////////////////////////
//+-------------+-------------+-------------+-------------+
//|   Byte 0    |   Byte 1    |   Byte 2    |   Byte 3    |
//+------+------+-------------+-------------+-------------+
//|Cable | Code |             |             |             |
//|Number|Index |   MIDI_0    |   MIDI_1    |   MIDI_2    |
//|      |Number|             |             |             |
//|(4bit)|(4bit)|   (8bit)    |   (8bit)    |   (8bit)    |
//+------+------+-------------+-------------+-------------+
// CN == 0x0-0xf
//+-----+-----------+-------------------------------------------------------------------
//| CIN |MIDI_x size|Description
//+-----+-----------+-------------------------------------------------------------------
//| 0x0 | 1, 2 or 3 |Miscellaneous function codes. Reserved for future extensions.
//| 0x1 | 1, 2 or 3 |Cable events. Reserved for future expansion.
//| 0x2 |     2     |Two-byte System Common messages like MTC, SongSelect, etc.
//| 0x3 |     3     |Three-byte System Common messages like SPP, etc.
//| 0x4 |     3     |SysEx starts or continues
//| 0x5 |     1     |Single-byte System Common Message or SysEx ends with following single byte.
//| 0x6 |     2     |SysEx ends with following two bytes.
//| 0x7 |     3     |SysEx ends with following three bytes.
//| 0x8 |     3     |Note-off
//| 0x9 |     3     |Note-on
//| 0xA |     3     |Poly-KeyPress
//| 0xB |     3     |Control Change
//| 0xC |     2     |Program Change
//| 0xD |     2     |Channel Pressure
//| 0xE |     3     |PitchBend Change
//| 0xF |     1     |Single Byte
//+-----+-----------+-------------------------------------------------------------------

const uint8_t	MIDI::epDataInIndex  = 1;
const uint8_t	MIDI::epDataOutIndex = 2;
const uint8_t	MIDI::epDataInIndexVSP  = 3;
const uint8_t	MIDI::epDataOutIndexVSP = 4;

MIDI::MIDI(USB *p)
{
  pUsb = p;
  bAddress = 0;
  bNumEP = 1;
  bPollEnable  = false;
  isMidiFound = false;

  // initialize endpoint data structures
  for(uint8_t i=0; i<MIDI_MAX_ENDPOINTS; i++) {
    epInfo[i].epAddr        = 0;
    epInfo[i].maxPktSize  = (i) ? 0 : 8;
    epInfo[i].epAttribs     = 0;
//    epInfo[i].bmNakPower  = (i) ? USB_NAK_NOWAIT : USB_NAK_MAX_POWER;
    epInfo[i].bmNakPower  = (i) ? USB_NAK_NOWAIT : 4;

  }
  // register in USB subsystem
  if (pUsb) {
    pUsb->RegisterDeviceClass(this);
  }
}

/* Connection initialization of an MIDI Device */
uint8_t MIDI::Init(uint8_t parent, uint8_t port, bool lowspeed)
{
  uint8_t    buf[DESC_BUFF_SIZE];
  uint8_t    rcode;
  UsbDevice  *p = NULL;
  EpInfo     *oldep_ptr = NULL;
  uint8_t    num_of_conf;  // number of configurations
  
  // get memory address of USB device address pool
  AddressPool &addrPool = pUsb->GetAddressPool();
#ifdef DEBUG
  USBTRACE("\rMIDI Init\r\n");
#endif
  // check if address has already been assigned to an instance
  if (bAddress) {
    return USB_ERROR_CLASS_INSTANCE_ALREADY_IN_USE;
  }
  // Get pointer to pseudo device with address 0 assigned
  p = addrPool.GetUsbDevicePtr(0);
  if (!p) {
    return USB_ERROR_ADDRESS_NOT_FOUND_IN_POOL;
  }
  if (!p->epinfo) {
    return USB_ERROR_EPINFO_IS_NULL;
  }

  // Save old pointer to EP_RECORD of address 0
  oldep_ptr = p->epinfo;

  // Temporary assign new pointer to epInfo to p->epinfo in order to avoid toggle inconsistence
  p->epinfo = epInfo;
  p->lowspeed = lowspeed;

  // Get device descriptor
  rcode = pUsb->getDevDescr( 0, 0, sizeof(USB_DEVICE_DESCRIPTOR), (uint8_t*)buf );
  vid = (uint16_t)buf[8] + (uint16_t)buf[9] <<8;
  pid = (uint16_t)buf[10] + (uint16_t)buf[11] <<8;
  // Restore p->epinfo
  p->epinfo = oldep_ptr;

  if( rcode ){ 
    goto FailGetDevDescr;
  }

  // Allocate new address according to device class
  bAddress = addrPool.AllocAddress(parent, false, port);
  if (!bAddress) {
    return USB_ERROR_OUT_OF_ADDRESS_SPACE_IN_POOL;
  }

  // Extract Max Packet Size from device descriptor
  epInfo[0].maxPktSize = (uint8_t)((USB_DEVICE_DESCRIPTOR*)buf)->bMaxPacketSize0; 

  // Assign new address to the device
  rcode = pUsb->setAddr( 0, 0, bAddress );
  if (rcode) {
    p->lowspeed = false;
    addrPool.FreeAddress(bAddress);
    bAddress = 0;
    return rcode;
  }//if (rcode...
#ifdef DEBUG
  USBTRACE2("Addr:", bAddress);
#endif  
  p->lowspeed = false;

  //get pointer to assigned address record
  p = addrPool.GetUsbDevicePtr(bAddress);
  if (!p) {
    return USB_ERROR_ADDRESS_NOT_FOUND_IN_POOL;
  }
  p->lowspeed = lowspeed;

  num_of_conf = ((USB_DEVICE_DESCRIPTOR*)buf)->bNumConfigurations;

  // Assign epInfo to epinfo pointer
  rcode = pUsb->setEpInfoEntry(bAddress, 1, epInfo);
  if (rcode) {
#ifdef DEBUG
    USBTRACE("setEpInfoEntry failed");
#endif
    goto FailSetDevTblEntry;
  }
#ifdef DEBUG
  USBTRACE2("NC:", num_of_conf);
#endif
  for (uint8_t i=0; i<num_of_conf; i++) {
    parseConfigDescr(bAddress, i);
    if (bNumEP > 1)
      break;
  } // for
#ifdef DEBUG
  USBTRACE2("NumEP:", bNumEP);
#endif
  if( bConfNum == 0 ){  //Device not found.
    goto FailGetConfDescr;
  }

  if( !isMidiFound ){ //MIDI Device not found. Try first Bulk transfer device
    epInfo[epDataInIndex].epAddr		= epInfo[epDataInIndexVSP].epAddr;
    epInfo[epDataInIndex].maxPktSize	= epInfo[epDataInIndexVSP].maxPktSize;
    epInfo[epDataOutIndex].epAddr		= epInfo[epDataOutIndexVSP].epAddr;
    epInfo[epDataOutIndex].maxPktSize	= epInfo[epDataOutIndexVSP].maxPktSize;
  }

  // Assign epInfo to epinfo pointer
  rcode = pUsb->setEpInfoEntry(bAddress, bNumEP, epInfo);
#ifdef DEBUG
  USBTRACE2("Conf:", bConfNum);
#endif
  // Set Configuration Value
  rcode = pUsb->setConf(bAddress, 0, bConfNum);
  if (rcode) {
    goto FailSetConfDescr;
  }
#ifdef DEBUG
  USBTRACE("Init done.");
#endif
  bPollEnable = true;
  return 0;
FailGetDevDescr:
FailSetDevTblEntry:
FailGetConfDescr:
FailSetConfDescr:
  Release();
  return rcode;
}

/* get and parse config descriptor */
void MIDI::parseConfigDescr( byte addr, byte conf )
{
  uint8_t buf[ DESC_BUFF_SIZE ];
  uint8_t* buf_ptr = buf;
  byte rcode;
  byte descr_length;
  byte descr_type;
  unsigned int total_length;
  USB_ENDPOINT_DESCRIPTOR *epDesc;
  boolean isMidi = false;

  // get configuration descriptor (get descriptor size only)
  rcode = pUsb->getConfDescr( addr, 0, 4, conf, buf );
  if( rcode ){
    return;
  }  
  total_length = buf[2] | ((int)buf[3] << 8);
  if( total_length > DESC_BUFF_SIZE ) {    //check if total length is larger than buffer
    total_length = DESC_BUFF_SIZE;
  }

  // get configuration descriptor (all)
  rcode = pUsb->getConfDescr( addr, 0, total_length, conf, buf ); //get the whole descriptor
  if( rcode ){
    return;
  }  

  //parsing descriptors
  while( buf_ptr < buf + total_length ) {  
    descr_length = *( buf_ptr );
    descr_type   = *( buf_ptr + 1 );
    switch( descr_type ) {
      case USB_DESCRIPTOR_CONFIGURATION :
        bConfNum = buf_ptr[5];
        break;
      case  USB_DESCRIPTOR_INTERFACE :
        if( buf_ptr[5] == USB_CLASS_AUDIO && buf_ptr[6] == USB_SUBCLASS_MIDISTREAMING ) {  //p[5]; bInterfaceClass = 1(Audio), p[6]; bInterfaceSubClass = 3(MIDI Streaming)
          isMidiFound = true; //MIDI device found.
          isMidi      = true;
        }else{
#ifdef DEBUG
          Serial.print("No MIDI Device\n");
#endif
//          buf_ptr += total_length + 1;
//          bConfNum = 0;
            isMidi = false;
        }
        break;
      case USB_DESCRIPTOR_ENDPOINT :
        epDesc = (USB_ENDPOINT_DESCRIPTOR *)buf_ptr;
        if ((epDesc->bmAttributes & 0x02) == 2) {//bulk
          uint8_t index;
          if( isMidi )
              index = ((epDesc->bEndpointAddress & 0x80) == 0x80) ? epDataInIndex : epDataOutIndex;
          else
              index = ((epDesc->bEndpointAddress & 0x80) == 0x80) ? epDataInIndexVSP : epDataOutIndexVSP;
          epInfo[index].epAddr		= (epDesc->bEndpointAddress & 0x0F);
          epInfo[index].maxPktSize	= (uint8_t)epDesc->wMaxPacketSize;
          bNumEP ++;
#ifdef DEBUG
          PrintEndpointDescriptor(epDesc);
#endif
        }
        break;
      default:
        break;
    }//switch( descr_type  
    buf_ptr += descr_length;    //advance buffer pointer
  }//while( buf_ptr <=...
}

/* Performs a cleanup after failed Init() attempt */
uint8_t MIDI::Release()
{
  pUsb->GetAddressPool().FreeAddress(bAddress);
  bNumEP       = 1;		//must have to be reset to 1	
  bAddress     = 0;
  bPollEnable  = false;
  return 0;
}

/* Receive data from MIDI device */
uint8_t MIDI::RcvData(uint16_t *bytes_rcvd, uint8_t *dataptr)
{
  bytes_rcvd[0] = (uint16_t)epInfo[epDataInIndex].maxPktSize;
  return pUsb->inTransfer(bAddress, epInfo[epDataInIndex].epAddr, bytes_rcvd, dataptr);
}

/* Receive data from MIDI device */
bool MIDI::RcvData(uint8_t *outBuf)
{
  byte i, codeIndexNumber;
  byte rcode = 0;     //return code
  uint16_t  rcvd;
  uint8_t rcvbuf[64];

  if( bPollEnable == false ) return false;

  rcode = RcvData( &rcvd,  rcvbuf);
  if( rcode != 0 ) {
    return false;
  }
  
  //if all data is zero, no valid data received.
  if( rcvbuf[0] == 0 && rcvbuf[1] == 0 && rcvbuf[2] == 0 && rcvbuf[3] == 0 ) {
    return false;
  }
  outBuf[0] = rcvbuf[1];
  outBuf[1] = rcvbuf[2];
  outBuf[2] = rcvbuf[3];
  return true;
}

/* Send data to MIDI device */
uint8_t MIDI::SendDataMulti(uint8_t *dataptr, byte nCable)
{
  byte buf[4];
  byte msg;
  uint8_t rc;
  byte i=0;

  //Byte 0
  buf[0] = (nCable << 4) | 0x4;

  while(i < 0xff) {
    for(i=1; i<=3; i++){
      buf[i] = *dataptr;
      if( *dataptr == 0xf7 ){
        buf[0] = (nCable << 4) | (0x4 + i);
        for(i=i+1; i<=3; i++){
          buf[i] = 0;
        }
        i=0xfe;	//data end
      }else{
        dataptr++;
      }
    }
    rc = pUsb->outTransfer(bAddress, epInfo[epDataOutIndex].epAddr, 4, buf);
    if(rc != 0)
     break;
  }
  return(rc);
}

/* Send data to MIDI device */
uint8_t MIDI::SendData(uint8_t *dataptr, byte nCable)
{
  byte buf[4];
  byte msg;

  msg = dataptr[0];
  // SysEx long message ?
  if( msg == 0xf0 )
  {
     return SendDataMulti(dataptr, nCable);
  }

  buf[0] = (nCable << 4) | (msg >> 4);
  if( msg < 0xf0 ) msg = msg & 0xf0;
  

  //Building USB-MIDI Event Packets
  buf[1] = dataptr[0];
  buf[2] = dataptr[1];
  buf[3] = dataptr[2];

  switch(msg) {
    //3 bytes message
    case 0xf2 : //system common message(SPP)
      buf[0] = (nCable << 4) | 3;
    case 0x80 : //Note off
    case 0x90 : //Note on
    case 0xa0 : //Poly KeyPress
    case 0xb0 : //Control Change
    case 0xe0 : //PitchBend Change
        break;

    //2 bytes message
    case 0xf1 : //system common message(MTC)
    case 0xf3 : //system common message(SongSelect)
      buf[0] = (nCable << 4) | 2;
    case 0xc0 : //Program Change
    case 0xd0 : //Channel Pressure
      buf[3] = 0;
      break;
    //1 bytes message
    case 0xf8 : //system realtime message
    case 0xf9 : //system realtime message
    case 0xfa : //system realtime message
    case 0xfb : //system realtime message
    case 0xfc : //system realtime message
    case 0xfe : //system realtime message
    case 0xff : //system realtime message
    default :
      buf[2] = 0;
      buf[3] = 0;
      break;
  }
  return pUsb->outTransfer(bAddress, epInfo[epDataOutIndex].epAddr, 4, buf);
}

#ifdef DEBUG
void MIDI::PrintEndpointDescriptor( const USB_ENDPOINT_DESCRIPTOR* ep_ptr )
{
	Notify(PSTR("Endpoint descriptor:"));
	Notify(PSTR("\r\nLength:\t\t"));
	PrintHex<uint8_t>(ep_ptr->bLength);
	Notify(PSTR("\r\nType:\t\t"));
	PrintHex<uint8_t>(ep_ptr->bDescriptorType);
	Notify(PSTR("\r\nAddress:\t"));
	PrintHex<uint8_t>(ep_ptr->bEndpointAddress);
	Notify(PSTR("\r\nAttributes:\t"));
	PrintHex<uint8_t>(ep_ptr->bmAttributes);
	Notify(PSTR("\r\nMaxPktSize:\t"));
	PrintHex<uint16_t>(ep_ptr->wMaxPacketSize);
	Notify(PSTR("\r\nPoll Intrv:\t"));
	PrintHex<uint8_t>(ep_ptr->bInterval);
	Notify(PSTR("\r\n"));
}
#endif
