// THIS AUTO TRANSMITS ON LOAD ON RADIONUMBER 0 
#include <SPI.h>
#include "printf.h"
#include "RF24.h"
#include <Servo.h>

//RADIO PRE SETUP
RF24 radio(7, 8);  // using pin 7 for the CE pin, and pin 8 for the CSN pin

uint8_t address[][6] = { "1Node", "2Node" };

bool radioNumber = 0;  // 0 uses address[0] to transmit, 1 uses address[1] to transmit

struct PayloadStruct {
  bool collectorSideOn;
  bool switchSideOn;
  bool holdForClosed;
};
PayloadStruct payload;

//DUST COLLECTION PRE SETUP
const int NUMBER_OF_TOOLS = 3;
const int NUMBER_OF_GATES = 3;
const int NUMBER_OF_SWITCHES = 1;

// set up servo # counter
Servo myservo[NUMBER_OF_GATES];

//set up dust collector pin
const int dustCollectionRelayPin = 5;

//set up Push-Button connection to Analog pin A0
const int kPinBtn = A0;  
bool buttonIsOn = false;

// set up trigger variable
bool collectorIsOn = false;

// set up timing variables
int DC_spindown = 3000;
int DC_on_wait = 750;


String tools[NUMBER_OF_TOOLS] = {"Jointer", "Table Saw", "Router"}; //D5(same as dustCollectionRelayPin), D4, D3, D2,  "Switch/Floor Sweep/Hose", 
int voltSensor[NUMBER_OF_TOOLS] = {A1,A2,A3};
long int voltBaseline[NUMBER_OF_TOOLS] = {0,0,0};


//Set the throw of each gate separately, if needed
int gateMinMax[NUMBER_OF_GATES][2] = {
  /*open, close*/

  {65,2},//Jointer
  {79,10},//Table Saw
  {60,10}//Router

};

//keep track of gates to be toggled ON/OFF for each tool

int gates[NUMBER_OF_TOOLS][NUMBER_OF_GATES] = {
  {1,0,0},//jointer open
  {0,1,0},//tablesaw open
  {0,0,1},//router open
};


int mVperAmp = 66; // use 185 for 5A Module 100 for 20A Module and 66 for 30A Module
double ampThreshold = 5.5;

double Voltage = 0;
double VRMS = 0;
double AmpsRMS = 0;



void setup() {

  Serial.begin(115200);
  while (!Serial) {
    // some boards need to wait to ensure access to serial over USB
  }

  // // set up servos on pins 3 and 5
  myservo[0].attach(4); //jointer
  myservo[1].attach(3); //tablesaw
  myservo[2].attach(2); //router

// set up and assure dust collector is in OFF position
  pinMode(dustCollectionRelayPin, OUTPUT);
  digitalWrite(dustCollectionRelayPin, HIGH);

// set up on/off switch OUTPUT because ANALOG PIN
  pinMode(kPinBtn, OUTPUT); 
  digitalWrite(kPinBtn, HIGH);
  

//LED radio signal indicator YELLOW
pinMode(10, OUTPUT);

//LED power Indicator GREEN
pinMode(9, OUTPUT);

//LED switchSideOn indicator BLUE
pinMode(6, OUTPUT);


  // initialize the transceiver on the SPI bus
  if (!radio.begin()) {
    Serial.println(F("radio hardware is not responding!!"));
    while (1) {}  // hold in infinite loop
  }

  Serial.print(F("radioNumber = "));
  Serial.println((int)radioNumber);

  Serial.println(F("THIS IS THE TRANSMITTER NODE"));

  radio.setPALevel(RF24_PA_MAX);  // RF24_PA_MAX is default.
  radio.setDataRate(RF24_2MBPS);

  // radio.setPayloadSize(sizeof(payload));

  radio.enableDynamicPayloads();  // ACK payloads are dynamically sized

  radio.enableAckPayload();

  // set the TX address of the RX node into the TX pipe
  radio.openWritingPipe(address[radioNumber]);  // always uses pipe 0

  // set the RX address of the TX node into a RX pipe
  radio.openReadingPipe(1, address[!radioNumber]);  // using pipe 1

  // radio.stopListening();                 // put radio in TX mode

  //SETUP COMPLETED GREEN LIGHT ON





  
  Serial.println("XXXXXXXXxxxxxxxXXXXXXXXXXXXXX");


  for(int c=0;c<NUMBER_OF_GATES;c++){
    delay(1000);
    closeGate(c);
    delay(1000);
    openGate(c);
  };

  Serial.println("SETUP COMPLETED GREEN LIGHT ON");
  digitalWrite(9, HIGH);


} //set-up end

void loop() {
  // This device is a TX node
  uint8_t pipe;

  unsigned long start_timer = micros();                  // start the timer
  bool report = radio.write(&payload, sizeof(payload));  // transmit & save the report      
  unsigned long end_timer = micros();                    // end the timer
  radio.stopListening(); 
  //  if (report) {
      
  if (radio.available(&pipe)) {  // is there an ACK payload? grab the pipe number that received it
    digitalWrite(10, HIGH);
    PayloadStruct received;
    radio.read(&received, sizeof(received));  // get incoming ACK payload
    Serial.print(F("Received collectorSideOn: "));
    Serial.println(received.collectorSideOn);  // print outgoing counter
    Serial.print(F("Payload switchSideOn: "));
    Serial.println(payload.switchSideOn);  // print outgoing counter
    Serial.print(F("Payload holdForClosed: "));
    Serial.println(payload.holdForClosed);  // print outgoing counter
    Serial.println("--------------------------");
    
    // use payload to turn collector on/off from other side
//        payload.collectorSideOn = received.collectorSideOn;

    //close all gates on this side if other side is on
    if(received.collectorSideOn){
      digitalWrite(6, HIGH);
      Serial.println("collectorSideOn ON");
      if(!received.holdForClosed){
        payload.collectorSideOn = received.collectorSideOn;
        for(int c=0;c<NUMBER_OF_GATES;c++){
          closeGate(c);
        };
        // delay(DC_on_wait);
        turnOnDustCollection();
        Serial.println("DUST COLLECTOR ON");
        payload.holdForClosed = true;
      }; 
    } else if(!received.collectorSideOn && payload.holdForClosed){
    // } else if(!payload.collectorSideOn && payload.holdForClosed){
      Serial.println("collectorSideOn OFF");
      Serial.println("DUST COLLECTOR SPINNING DOWN");
        delay(DC_spindown);
        turnOffDustCollection();
        Serial.println("DUST COLLECTOR OFF");
        digitalWrite(6, LOW);
        payload.holdForClosed = false;
    }; 
    digitalWrite(10, LOW);
        
   } else {
     Serial.println("No payload received");
     digitalWrite(10, LOW);
   };


  Serial.println("----------");




  //loop through tools and check
  int activeTool = 10;  // a number that will never happen
  PayloadStruct received;
      for(int i=0;i<NUMBER_OF_TOOLS;i++){
        if( checkForAmperageChange(i) && !payload.collectorSideOn){
          activeTool = i;
          exit;
        }
      }
    if(activeTool != 10){
      // use activeTool for gate processing
      if(collectorIsOn == false){
        payload.switchSideOn = true;
        radio.write(&payload, sizeof(payload));
        //manage all gate positions
        for(int s=0;s<NUMBER_OF_GATES;s++){
          int pos = gates[activeTool][s];
          if(pos == 1){
            openGate(s);    
          } else {
            closeGate(s);
          }
        }
        payload.switchSideOn = true;
        radio.write(&payload, sizeof(payload));
        delay(DC_on_wait);
        turnOnDustCollection();
      }
    } else {
      if(collectorIsOn == true && !buttonIsOn && !payload.collectorSideOn){
        Serial.print("Button now. ");
        Serial.println(buttonIsOn);
    Serial.print(F("Received collectorSideOn: "));
    Serial.println(received.collectorSideOn);
        Serial.print(F("collectorIsOn: "));
    Serial.println(collectorIsOn);
        Serial.println("DUST COLLECTOR SPINNING DOWN");
        delay(DC_spindown);
        turnOffDustCollection();  
      }
    }

    // setting up button to close all gates then turn DC on
  if(digitalRead(kPinBtn) == HIGH && !buttonIsOn){
    Serial.println("BUTTON ON");
    // activeTool = 10;
    buttonIsOn = true;
    //close all gate positions on both sides
    payload.switchSideOn = true;
    radio.write(&payload, sizeof(payload));
    for(int s=0;s<NUMBER_OF_GATES;s++){
        closeGate(s);
      }
    payload.switchSideOn = true;
    radio.write(&payload, sizeof(payload));
    radio.writeAckPayload(1, &payload, sizeof(payload));

    delay(DC_on_wait);
    turnOnDustCollection();
    Serial.print("Button. ");
    Serial.println(buttonIsOn);
  }else {
    if(digitalRead(kPinBtn) == LOW && buttonIsOn){
      Serial.println("BUTTON OFF");
      turnOffDustCollection(); 
      payload.switchSideOn = false;  
      buttonIsOn = false;   
    }
  }


  radio.write(&payload, sizeof(payload));
  radio.writeAckPayload(1, &payload, sizeof(payload));
  Serial.println("Starting to Listen");

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
  digitalWrite(dustCollectionRelayPin, LOW);
  collectorIsOn = true;
}

void turnOffDustCollection(){
  Serial.println("turnOffDustCollection");
  digitalWrite(dustCollectionRelayPin, HIGH);
  payload.collectorSideOn = false;
  payload.switchSideOn = false;
  collectorIsOn = false;
}
 
float getVPP(int sensor){
  float result;
  
  int readValue;             //value read from the sensor
  int maxValue = 0;          // store max value here
  int minValue = 1024;          // store min value here
  
  uint32_t start_time = millis();
  while((millis()-start_time) < 150){ //sample for 1/2 Sec (500 = 1 second)
 
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

  myservo[num].write(gateMinMax[num][1]);
  delay(300);
  myservo[num].write(gateMinMax[num][1]+7);
  delay(200);
  myservo[num].write(gateMinMax[num][1]-2);  
  Serial.print("Closed Gate at ");
  Serial.println(myservo[num].read());
}

void openGate(uint8_t num){
  Serial.print("Open Gate: ");
  Serial.print(num);
  Serial.print("   FOR   ");
  Serial.println(tools[num]);
  
  myservo[num].write(gateMinMax[num][0]);
  delay(300);
  myservo[num].write(gateMinMax[num][0]-7);
  delay(200);
  myservo[num].write(gateMinMax[num][0]+2);
  Serial.print("Open Gate at ");
  Serial.println(myservo[num].read());
}
