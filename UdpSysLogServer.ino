//#define DEBUG 1

#include <SPI.h>
#include <Ethernet.h>                       // Load the ethernet library
#include <EthernetUdp.h>                    // Load the UDP library

#define UDP_TX_PACKET_MAX_SIZE 250          // increase UDP max packet size so it can contain a full syslog string from Mikrotik

#include <FastLED.h>                        // Load the LED Library
#define NUM_LEDS 50                         // Total number of LEDs
#define DATA_PIN 4                          // Output pin connected to LED string data line

#define LED_QUEUE_SIZE 6                    // How many blink events should we queue

//#define DOMAPPING 1
#define NUMBER_OF_C_SUBNETS 2               // How many /24 subnets do we need to store mapping data for (only valid if DOMAPPING is defined)
#define LOWEST_SUBNET 4                     // The 3th octet of your IP subnet. (Needed for calculating where to store the mapping)

CRGB leds[NUM_LEDS];                        // This variable will hold all the led colors

byte mac[] = {
  0xAA, 0xBB, 0xCC, 0xDD, 0xEE, 0xFF        // Define a MAC address for our ethernet shield
};

unsigned int localPort = 514;               // local UDP port to listen on

char packetBuffer[UDP_TX_PACKET_MAX_SIZE];  // buffer to hold the incoming packet,

EthernetUDP Udp;                            // An EthernetUDP instance to let us send and receive packets over UDP

CRGB queue[NUM_LEDS][LED_QUEUE_SIZE];       // A queue for storing which LEDs to blink

#if defined DOMAPPING
uint8_t ledMapping[NUMBER_OF_C_SUBNETS][254]; // a place to store our SRC IP <-> LED mapping later on
#endif

void setup() {
#if defined DEBUG
  Serial.begin(9600);                       // Initialize the serial port for debug output

  Serial.println("Starting up...");
#endif

  FastLED.addLeds<NEOPIXEL, DATA_PIN>(leds, NUM_LEDS);
                                            // Initialize the LED library
                                            
  allOff();                                 // Switch off all LEDs
 
  colorWipe(CRGB(255, 0, 0), 50);           // Test all red colors
  colorWipe(CRGB(0, 255, 0), 50);           // Test all green colors
  colorWipe(CRGB(0, 0, 255), 50);           // Test all blue colors

  allOff();                                 // Switch off all LEDs again

  for (uint8_t i=0;i<NUM_LEDS;i++) {        // Initialize the LED queue to all black
    for (uint8_t j=0;j<LED_QUEUE_SIZE;j++) {
      queue[i][j] = CRGB::Black; 
    }
  }

#if defined DOMAPPING
  for (uint8_t i=0;i<NUMBER_OF_C_SUBNETS;i++) {
    for (uint8_t j=0;j<254;j++) {
      ledMapping[i][j] = 255;               // Initialize my ledMapping to all 255
    }
  }
#endif


  
  Ethernet.begin(mac);                      // Initialize the network, use DHCP
  
  Udp.begin(localPort);                     // Start our UDP server

#if defined DEBUG
  Serial.print("Local IP: ");
  Serial.println(Ethernet.localIP());
#endif


}

void loop() {
  uint8_t packetSize = Udp.parsePacket();       // Check for available data    
  
  if (packetSize) {                         // if there's data available, read a packet
    #if defined DEBUG
    Serial.print("Received packet of size ");
    Serial.println(packetSize);
    Serial.print("From ");
    IPAddress remote = Udp.remoteIP();
    for (int i = 0; i < 4; i++) {
      Serial.print(remote[i], DEC);
      if (i < 3) {
        Serial.print(".");
      }
    }
    Serial.print(", port ");
    Serial.println(Udp.remotePort());
    #endif
    
    // read the packet into packetBufffer
    Udp.read(packetBuffer, UDP_TX_PACKET_MAX_SIZE);

    #if defined DEBUG
    //clear the rest of the buffer
    for (uint8_t i=packetSize;i<UDP_TX_PACKET_MAX_SIZE;i++) {
      packetBuffer[i] = ' ';
    }
    Serial.println("Contents:");
    Serial.println(packetBuffer);
    #endif

    // firewall,info |255;000;000 forward: in:bridge-LAN out:ether1-WAN, src-mac 34:e6:d7:19:d3:46, proto UDP, 10.99.5.254:62414->86.39.3.2:52713, NAT (10.99.5.254:62414->10.99.0.65:62414)->86.39.3.2:52713, len 56

    if ((packetSize>=26)&&(packetBuffer[14] == '|')) {
      char buf[] = "000";
      strncpy(buf, &packetBuffer[15], 3);
      uint8_t red = atoi(buf);
      strncpy(buf, &packetBuffer[19], 3);
      uint8_t green = atoi(buf);
      strncpy(buf, &packetBuffer[23], 3);
      uint8_t blue = atoi(buf);
      
      CRGB color = CRGB(red, green, blue);

      #if defined DOMAPPING
      uint8_t startposition = 25;
      uint8_t found = 0;
      while ((startposition<UDP_TX_PACKET_MAX_SIZE) && (found < 1)) {
        if (packetBuffer[startposition++] == '.') {
          found++;
        }
      }
      found=0;
      while ((startposition>=0) && (found < 1)) {
        if (packetBuffer[startposition--] == ',') {
          found++;
        }
      }
      startposition+=2;

      uint8_t srcip[4];
      
      uint8_t endposition = startposition+1;
      found = 0;
      while ((endposition<UDP_TX_PACKET_MAX_SIZE-1) && (found < 4)) {
        endposition++;
        if ((found<3)&&(packetBuffer[endposition] == '.')){
          char ipbuf[endposition-startposition+1];
          strncpy(ipbuf, &packetBuffer[startposition], endposition-startposition);
          ipbuf[sizeof(ipbuf)-1] = 0;
          srcip[found++] = atoi(ipbuf);
          startposition = endposition+1;
        }
        if ((packetBuffer[endposition] == ':')||(packetBuffer[endposition] == '-')){
          char ipbuf[endposition-startposition+1];
          strncpy(ipbuf, &packetBuffer[startposition], endposition-startposition);
          ipbuf[sizeof(ipbuf)-1] = 0;
          srcip[found] = atoi(ipbuf);

          found=4;
        }
      }
      endposition--;
      #endif

      uint8_t ledIndex = int(random(0, NUM_LEDS-1));                 // Guess a random LED index

      uint8_t initialLedIndex = ledIndex;
      bool keepTesting = true;
      while ((keepTesting)&&(leds[ledIndex])) {                  // Check if this LED is free
        ledIndex++;

        if (ledIndex > NUM_LEDS-1) ledIndex = 0;              
        if (ledIndex == initialLedIndex) keepTesting = false;   // No more free leds, just use this one
      }

      #if defined DOMAPPING
      if (found==4) {                                           // We have a source IP, check which LED was assigned to it
        #if defined DEBUG
        Serial.print("SRC: ");
        for (int i = 0; i < 4; i++) {
          Serial.print(srcip[i], DEC);
          if (i < 3) {
            Serial.print(".");
          }
        }
        Serial.println();
        #endif

        int subnetIndex = srcip[2]-LOWEST_SUBNET;
        if ((subnetIndex > 0) && (subnetIndex < NUMBER_OF_C_SUBNETS)) {
          if (ledMapping[subnetIndex][srcip[3]-1] == 255) {
            ledMapping[subnetIndex][srcip[3]-1] = ledIndex;        // Save the SRC IP <-> LED mapping for future use
          } else {
            ledIndex = ledMapping[subnetIndex][srcip[3]-1];
          }
        }
      }
      #endif
      
      # if defined DEBUG
      Serial.print("Blinking led at index ");
      Serial.print(ledIndex);
      Serial.print(" in color ");
      Serial.print(red);
      Serial.print(",");
      Serial.print(green);
      Serial.print(",");
      Serial.println(blue);
      #endif

      // Now queue the led to blink in the correct color
      uint8_t i = 0;
      while ((i<LED_QUEUE_SIZE)&&(queue[ledIndex][i])) {
        i++;
      }
      if (i<LED_QUEUE_SIZE) {           // If queue is full just ignore this event
        queue[ledIndex][i] = color;
      }
    }
  }

  for (uint8_t i=0;i<NUM_LEDS;i++) {
    if (queue[i][0]) {
      leds[i] = queue[i][0];
      queue[i][0] /= 2;
    } else {
      if (LED_QUEUE_SIZE>1) {
        if (queue[i][1]) {
          for (uint8_t j=0;j<LED_QUEUE_SIZE-1;j++) {
            queue[i][j] = queue[i][j+1];
          }
          queue[i][LED_QUEUE_SIZE-1] = CRGB::Black;
        }
      }
    }
    if ((leds[i]) && !(queue[i][0])) {
      leds[i] = CRGB::Black;
    }
  }
  FastLED.show();

  Ethernet.maintain();                  // Check if we need to renew our DHCP lease
  delay(10);
}

void allOff() {
  setAll(CRGB::Black);
}

void setAll(CRGB color) {
  for(uint8_t i=0; i<NUM_LEDS; i++) {
    leds[i] = color;
  }
  FastLED.show();
}

// Fill the dots one after the other with a color
void colorWipe(CRGB c, uint8_t wait) {
  for(uint8_t i=0; i<NUM_LEDS; i++) {
      leds[i] = c;
      FastLED.show();
      delay(wait);
  }
}

