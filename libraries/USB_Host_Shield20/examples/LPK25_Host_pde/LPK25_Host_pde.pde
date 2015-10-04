//////////////////////////////////////////////////////
// LPK25 USB-MIDI to Serial MIDI converter
// for use with USB Host Shield from Circuitsathome.com
// by Collin Cunningham - makezine.com, narbotic.com
// 
//////////////////////////////////////////////////////

#include <Spi.h>  // NOTE: alternate Spi Library from Circuitsathome.com
#include <Max3421e.h>
#include <Usb.h>
#include <avr/pgmspace.h>

//	For general USB-MIDI compliance, we need to determine which endpoint has a direction of IN
//	and likely also need to get the poll duration(EP_POLL), not sure if necessary though
//	

/*   USB ERROR CODES
 hrSUCCESS   0x00
 hrBUSY      0x01
 hrBADREQ    0x02
 hrUNDEF     0x03
 hrNAK       0x04
 hrSTALL     0x05
 hrTOGERR    0x06
 hrWRONGPID  0x07
 hrBADBC     0x08
 hrPIDERR    0x09
 hrPKTERR    0x0A
 hrCRCERR    0x0B
 hrKERR      0x0C
 hrJERR      0x0D
 hrTIMEOUT   0x0E
 hrBABBLE    0x0F
 */
 
//////////////////////////
// MIDI MESAGES 
// midi.org/techspecs/
//////////////////////////
// STATUS BYTES
// 0x80 == noteOn
// 0x90 == noteOff
// 0xA0 == afterTouch
// 0xB0 == controlChange
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
//////////////////////////

/* LPK25 data taken from descriptors */
// descriptors retrieved using descriptor_parser.pde (Usb library example sketch)
#define LPK25_ADDR            1
#define LPK25_VID_LO          0xFFFFFFE8  // Akai VID
#define LPK25_VID_HI          0x09
#define LPK25_PID_LO          0x76  // Batch Device
#define LPK25_PID_HI          0x00
#define LPK25_CONFIG          1
#define LPK25_IF              1
#define LPK25_NUM_EP          2
#define EP_MAXPKTSIZE         64
#define EP_BULK		      0x02 
#define EP_POLL               0x00	// 0x0B for Korg nanoKey
#define CONTROL_EP            0
#define OUTPUT_EP             1
#define INPUT_EP              1
#define LPK25_01_REPORT_LEN   0x09
#define LPK25_DESCR_LEN       0x0C

EP_RECORD ep_record[ LPK25_NUM_EP ];  //endpoint record structure for the LPK25 controller

char descrBuf[ 12 ] = { 
  0 };	//buffer for device description data
char buf[ 4 ] = { 
  0 };			//buffer for USB-MIDI data
char oldBuf[ 4 ] = { 
  0 };		//buffer for old USB-MIDI data
byte outBuf[ 3 ] = { 
  0 };		//buffer for outgoing MIDI data

MAX3421E Max;
USB Usb;

void setup() {
  Serial.begin( 31250 );  //MIDI baudrate, no worky with Arduino IDE
  Max.powerOn();
  delay(200);
}

void loop() {

  Max.Task();
  Usb.Task();

  if( Usb.getUsbTaskState() == USB_STATE_CONFIGURING ) {  //wait for addressing state
    LPK25_init();
    Usb.setUsbTaskState( USB_STATE_RUNNING );
  }
  if( Usb.getUsbTaskState() == USB_STATE_RUNNING ) {  //poll the LPK25 Controller 
    LPK25_poll();
  }
}

/* Initialize LPK25 Controller */
void LPK25_init( void ){

  byte rcode = 0;  //return code
  byte i;

  /* Initialize data structures for endpoints of device 1*/
  ep_record[ CONTROL_EP ] = *( Usb.getDevTableEntry( 0,0 ));  //copy endpoint 0 parameters
  ep_record[ OUTPUT_EP ].epAddr = 0x01;    // Output endpoint, 0x02 for Korg nanoKey
  ep_record[ OUTPUT_EP ].Attr  = EP_BULK;
  ep_record[ OUTPUT_EP ].MaxPktSize = EP_MAXPKTSIZE;
  ep_record[ OUTPUT_EP ].Interval  = EP_POLL;
  ep_record[ OUTPUT_EP ].sndToggle = bmSNDTOG0;
  ep_record[ OUTPUT_EP ].rcvToggle = bmRCVTOG0;
  ep_record[ INPUT_EP	].epAddr = 0x01;			// Input endpoint, 0x02 for Korg nanoKey 
  ep_record[ INPUT_EP ].Attr  = EP_BULK;
  ep_record[ INPUT_EP ].MaxPktSize = EP_MAXPKTSIZE;
  ep_record[ INPUT_EP ].Interval  = EP_POLL;
  ep_record[ INPUT_EP ].sndToggle = bmSNDTOG0;
  ep_record[ INPUT_EP ].rcvToggle = bmRCVTOG0;

  Usb.setDevTableEntry( LPK25_ADDR, ep_record );             //plug kbd.endpoint parameters to devtable

  /* read the device descriptor and check VID and PID*/
  rcode = Usb.getDevDescr( LPK25_ADDR, ep_record[ CONTROL_EP ].epAddr, LPK25_DESCR_LEN, descrBuf );
  if( rcode ) {
    Serial.print("Error attempting read device descriptor. Return code :");
    Serial.println( rcode, HEX );
    while(1);  //stop
  }

  // Test device for Akai LPK25
  if((descrBuf[ 8 ] != LPK25_VID_LO) || (descrBuf[ 9 ] != LPK25_VID_HI) || (descrBuf[ 10 ] != LPK25_PID_LO) || (descrBuf[ 11 ] != LPK25_PID_HI) ){
    Serial.print("Unsupported USB Device");
    while(1);  //stop
  }

  /*
  // Test device for Korg nanoKey
   if((descrBuf[ 8 ] != 0x44) || (descrBuf[ 9 ] != 0x09) || (descrBuf[ 10 ] != 0x0D) || (descrBuf[ 11 ] != 0x01) ){
   Serial.print("Unsupported USB Device");
   while(1);  //stop   
   }
   */


  /* Configure device */
  rcode = Usb.setConf( LPK25_ADDR, ep_record[ CONTROL_EP ].epAddr, LPK25_CONFIG );                    
  if( rcode ) {
    Serial.print("Error attempting to configure LPK25. Return code :");
    Serial.println( rcode, HEX );
    while(1);  //stop
  }
  Serial.println("Akai LPK25 initialized");
  Serial.println("");
  Serial.println("");
  Serial.println("");
  delay(200);
}

// Poll LPK25 and print result
void LPK25_poll( void ){

  byte rcode = 0;     //return code
  /* poll LPK25 */
  rcode = Usb.inTransfer( LPK25_ADDR, ep_record[ INPUT_EP ].epAddr, LPK25_01_REPORT_LEN, buf );
  if( rcode != 0 ) {
    return;
  }
  process_report();
  return;
}


void process_report(void){
  byte i, codeIndexNumber;
  for( i = 0; i < 4; i++) {  //check for new information
    if( buf[ i ] != oldBuf[ i ] ) { //new info in buffer
      break;
    }
  }
  if( i == 4 ) {
    return;  //all bytes are the same
  }

  else{
    outBuf[0] = byte(buf[1]);
    outBuf[1] = byte(buf[2]);
    outBuf[2] = byte(buf[3]);

  //MIDI Output
  Serial.write(outBuf, 3);

  }	
  return;
}
