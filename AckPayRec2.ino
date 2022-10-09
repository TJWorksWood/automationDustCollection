// THIS AUTO RECVEIVES ON LOAD ON RADIONUMBER 1
#include <SPI.h>
#include "printf.h"
#include "RF24.h"
#include <Servo.h>

//RADIO PRE SETUP
RF24 radio(7, 8);  // using pin 7 for the CE pin, and pin 8 for the CSN pin

uint8_t address[][6] = { "1Node", "2Node" };

bool radioNumber = 1;  // 0 uses address[0] to transmit, 1 uses address[1] to transmit

struct PayloadStruct {
  bool collectorSideOn = false;
  bool switchSideOn = false;
  bool holdForClosed = false;
};
PayloadStruct payload;

//DUST COLLECTION PRESETUP

const int NUMBER_OF_TOOLS = 2;
const int NUMBER_OF_GATES = 2;

// set up servo # counter
Servo myservo[NUMBER_OF_GATES];

// set up trigger variable
bool collectorIsOn = false;

// set up timing variables
int DC_spindown = 3000;
int DC_on_wait = 750;


String tools[NUMBER_OF_TOOLS] = {"Planer","Miter Saw"}; //, D5, D3
int voltSensor[NUMBER_OF_TOOLS] = {A2,A3};
long int voltBaseline[NUMBER_OF_TOOLS] = {0,0};

//DC right, Y, miter, bandsaw, saw Y, tablesaw, floor sweep
//Set the throw of each gate separately, if needed
int gateMinMax[NUMBER_OF_GATES][2] = {
  /*close, open*/

{80,28},  //planer
{20,75}  //miter
};

//keep track of gates to be toggled ON/OFF for each tool
int gates[NUMBER_OF_TOOLS][NUMBER_OF_GATES] = {
  {1,0},
  {0,1},
};

int mVperAmp = 66; // use 100 for 20A Module and 66 for 30A Module
double ampThreshold = 7.5;

double Voltage = 0;
double VRMS = 0;
double AmpsRMS = 0;



void setup() {

  Serial.begin(115200);
  while (!Serial) {
    // some boards need to wait to ensure access to serial over USB
  }

  // set up servos on pins 3 and 5
  myservo[0].attach(3);
  myservo[1].attach(5);

//LED radio signal indicator YELLOW
pinMode(10, OUTPUT);

//LED power Indicator GREEN
pinMode(9, OUTPUT);

//LED switchSideOn indicator BLUE
pinMode(4, OUTPUT);
  
  // initialize the transceiver on the SPI bus
  if (!radio.begin()) {
    Serial.println(F("radio hardware is not responding!!"));
    while (1) {}  // hold in infinite loop
  }

  Serial.print(F("radioNumber = "));
  Serial.println((int)radioNumber);

  Serial.println(F("THIS IS THE RECEIVER NODE"));

  radio.setPALevel(RF24_PA_MAX);  // RF24_PA_MAX is default.
  radio.setDataRate(RF24_2MBPS);

  // radio.setPayloadSize(sizeof(payload));

  radio.enableDynamicPayloads();  // ACK payloads are dynamically sized

  radio.enableAckPayload();

  // set the TX address of the RX node into the TX pipe
  radio.openWritingPipe(address[radioNumber]);  // always uses pipe 0

  // set the RX address of the TX node into a RX pipe
  radio.openReadingPipe(1, address[!radioNumber]);  // using pipe 1

  // payload.holdForClosed = false;

  Serial.println("XXXXXXXXxxxxxxxXXXXXXXXXXXXXX");


  for(int c=0;c<NUMBER_OF_GATES;c++){
    delay(1000);
    closeGate(c);
    delay(1000);
    openGate(c);
  };

  Serial.println("SETUP COMPLETED GREEN LIGHT ON");
  digitalWrite(9, HIGH);
}





void loop() {
  // This device is a RX node
  uint8_t pipe;

  if (radio.available(&pipe)) {  // is there a payload? get the pipe number that recieved it
    digitalWrite(10, HIGH);
    uint8_t bytes = radio.getDynamicPayloadSize();  // get the size of the payload
    PayloadStruct received;
    radio.read(&received, sizeof(received));  // get incoming payload
    Serial.print(F("Payload collectorSideOn: "));
    Serial.println(payload.collectorSideOn);  
    Serial.print(F("Received switchSideOn "));
    Serial.println(received.switchSideOn);  
    Serial.print(F("Payload holdForClosed: "));
    Serial.println(payload.holdForClosed);  
    Serial.println("--------------------------");
//     
      // close all gates on this side (collector side) if other side (switch side) is on
      if(received.switchSideOn){
        digitalWrite(4, HIGH);
        Serial.println("switchSideOn ON");
        if(!payload.holdForClosed){
          for(int c=0;c<NUMBER_OF_GATES;c++){
            closeGate(c);
          };
        

          
          payload.holdForClosed = true;
          delay(DC_on_wait);
        }; 
      } else {
          digitalWrite(4, LOW);
          payload.holdForClosed = false;  
      };

   
    // load the payload for the first received transmission on pipe 0
    radio.write(&payload, sizeof(payload));
    radio.writeAckPayload(1, &payload, sizeof(payload));
    radio.stopListening();
    digitalWrite(10, LOW);
    
  } else {
    Serial.println("No payload");
    digitalWrite(10, LOW);
  };

  
    Serial.println("----------");

    //loop through tools and check
    int activeTool = 10;// a number that will never happen
       for(int i=0;i<NUMBER_OF_TOOLS;i++){
          if( checkForAmperageChange(i)){
            activeTool = i;
            exit;
          }
          // if( i!=0){
          //   if(checkForAmperageChange(0)){
          //     activeTool = 0;
          //     exit;
          //   }
          // }
       }
      if(activeTool != 10){
        // use activeTool for gate processing
        if(collectorIsOn == false){
          payload.collectorSideOn = true;
          radio.write(&payload, sizeof(payload));
          radio.writeAckPayload(1, &payload, sizeof(payload));
          turnOnDustCollection();

          //manage all gate positions
          for(int s=0;s<NUMBER_OF_GATES;s++){
            int pos = gates[activeTool][s];
            if(pos == 1){
              openGate(s);    
            } else {
              closeGate(s);
            }
          }

        }
      } else {
        if(collectorIsOn == true){
          payload.collectorSideOn = false;
          turnOffDustCollection();  
        }
      } 
  radio.write(&payload, sizeof(payload));
  radio.writeAckPayload(1, &payload, sizeof(payload));
  // Serial.println("Starting to Listen");
  radio.startListening();

}  // loop




//AFTER LOOP


//DUST COLLECTION CODE
bool checkForAmperageChange(int which){
  Voltage = getVPP(voltSensor[which]);
  VRMS = (Voltage/2.0) *0.707; 
  AmpsRMS = (VRMS * 1000)/mVperAmp;
  Serial.print(tools[which]+": ");
  Serial.print(AmpsRMS);
  Serial.println(" Amps RMS");


  if(AmpsRMS>ampThreshold){
    Serial.print(tools[which]+": ");
    Serial.print(AmpsRMS);
    Serial.println(" Amps RMS");
    return true;
  }else{
   return false; 
  }
}

void turnOnDustCollection(){
  Serial.println("turnOnDustCollection");
  payload.collectorSideOn = true;
  collectorIsOn = true;
}

void turnOffDustCollection(){
  Serial.println("turnOffDustCollection");
  payload.collectorSideOn = false;
  collectorIsOn = false;
}
 
float getVPP(int sensor){
  float result;
  
  int readValue;             //value read from the sensor
  int maxValue = 0;          // store max value here
  int minValue = 1024;          // store min value here
  
  uint32_t start_time = millis();
  while((millis()-start_time) < 350){ //sample for 1/2 Sec (500 = 1 second)
 
    readValue = analogRead(sensor);
       // see if you have a new maxValue
    if (readValue > maxValue){
        /*record the maximum sensor value*/
        maxValue = readValue;
    }
    if (readValue < minValue){
           /*record the maximum sensor value*/
      minValue = readValue;
    }
   }
   
   // Subtract min from max
   result = ((maxValue - minValue) * 5.0)/1024.0;
      
   return result;
 }

void closeGate(uint8_t num){
  Serial.print("Close Gate: ");
  Serial.print(num);
  Serial.print("   FOR   ");
  Serial.println(tools[num]);

  myservo[num].write(gateMinMax[num][0]);
  // delay(300);
  // if(num = 0){
  //   myservo[num].write(gateMinMax[num][0]-4);
  //   delay(300);
  //   myservo[num].write(gateMinMax[num][0]+2);
  // } else {
  //   myservo[num].write(gateMinMax[num][0]+4);
  //   delay(300);
  //   myservo[num].write(gateMinMax[num][0]-2);
  // }
}

void openGate(uint8_t num){
  Serial.print("Open Gate: ");
  Serial.print(num);
  Serial.print("   FOR   ");
  Serial.println(tools[num]);

  myservo[num].write(gateMinMax[num][1]);
  //   delay(300);
  // if(num = 0){
  //   myservo[num].write(gateMinMax[num][1]+2);
  //   delay(300);
  //   myservo[num].write(gateMinMax[num][1]-4);
  // } else {
  //   myservo[num].write(gateMinMax[num][1]-2);
  //   delay(300);
  //   myservo[num].write(gateMinMax[num][1]+4);
  // }


}
