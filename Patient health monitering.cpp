#include <LiquidCrystal.h>
#include <SoftwareSerial.h>

// LCD and SoftwareSerial setup
LiquidCrystal lcd(12, 11, 5, 4, 3, 2);
SoftwareSerial ser(9, 10);

// API Key for ThingSpeak
String apiKey = "0000000000000000";

// Pulse Sensor and Temperature Sensor Variables
float pulse = 0;
float temp = 0;

// Pin Definitions
int pulsePin = A0; // Pulse Sensor connected to analog pin 0
int blinkPin = 7;  // Pin to blink LED at each beat
int fadePin = 13;  // Pin for fading LED effect

// Volatile Variables for Pulse Detection
volatile int BPM; // Beats per minute
volatile int Signal; // Raw data from pulse sensor
volatile int IBI = 600; // Interval between beats
volatile boolean Pulse = false; // True when heartbeat is detected
volatile boolean QS = false; // True when a beat is found

// Setup function
void setup() {
    // Initialize LCD
    lcd.begin(16, 2);
    lcd.clear();
    lcd.setCursor(0, 0);
    lcd.print("Patient Health");
    lcd.setCursor(0, 1);
    lcd.print("Monitoring...");
    delay(2000);
    
    // Initialize pins
    pinMode(blinkPin, OUTPUT);
    pinMode(fadePin, OUTPUT);
    
    // Initialize Serial communication
    Serial.begin(115200);
    ser.begin(9600);
    
    // Initialize ESP8266
    initializeESP8266();
    
    // Setup pulse sensor interrupt
    interruptSetup();
}

// Main loop
void loop() {
    serialOutput();
    if (QS == true) { // A heartbeat was found
        fadeRate = 255; // Makes the LED fade effect happen
        serialOutputWhenBeatHappens(); // Output to serial
        QS = false; // Reset the Quantified Self flag for next time
    }
    ledFadeToBeat(); // Makes the LED fade effect happen
    delay(20); // Short delay
    read_temp(); // Read temperature
    esp_8266(); // Send data to ThingSpeak
}

// Function to initialize ESP8266
void initializeESP8266() {
    lcd.clear();
    lcd.setCursor(0, 0);
    lcd.print("Initializing...");
    delay(2000);
    
    // Send AT commands to configure ESP8266
    ser.println("AT");
    delay(1000);
    ser.println("AT+GMR");
    delay(1000);
    ser.println("AT+CWMODE=3");
    delay(1000);
    ser.println("AT+RST");
    delay(5000);
    ser.println("AT+CIPMUX=1");
    delay(1000);
    
    String cmd = "AT+CWJAP=\"YourSSID\",\"YourPassword\""; // Replace with your Wi-Fi credentials
    ser.println(cmd);
    delay(1000);
    ser.println("AT+CIFSR");
    delay(1000);
}

// Function to read temperature
void read_temp() {
    int temp_val = analogRead(A1);
    float mv = (temp_val / 1024.0) * 5000; // Convert to millivolts
    float cel = mv / 10; // Convert to Celsius
    temp = (cel * 9) / 5 + 32; // Convert to Fahrenheit
    
    // Display on LCD
    lcd.clear();
    lcd.setCursor(0, 0);
    lcd.print("BPM: ");
    lcd.print(BPM);
    lcd.setCursor(0, 1);
    lcd.print("Temp: ");
    lcd.print(temp);
    lcd.print(" F");
}

// Function to send data to ThingSpeak
void esp_8266() {
    String cmd = "AT+CIPSTART=4,\"TCP\",\"184.106.153.149\",80"; // ThingSpeak server
    ser.println(cmd);
    Serial.println(cmd);
    if (ser.find("Error")) {
        Serial.println("AT+CIPSTART error");
        return;
    }
    
    String getStr = "GET /update?api_key=" + apiKey + "&field1=" + String(temp) + "&field2=" + String(pulse) + "\r\n\r\n";
    
    // Send data length
    cmd = "AT+CIPSEND=4," + String(getStr.length());
    ser.println(cmd);
    Serial.println(cmd);
    delay(1000);
    ser.print(getStr);
    Serial.println(getStr); // Log the data sent
    delay(3000); // Wait for the next update
}

// Function to setup interrupts
void interruptSetup() {
    // Initializes Timer2 to throw an interrupt every 2mS
    TCCR2A = 0x02; // CTC mode
    TCCR2B = 0x06; // 256 prescaler
    OCR2A = 0x7C; // Set the top of the count to 124 for 500Hz sample rate
    TIMSK2 = 0x02; // Enable interrupt on match
    sei(); // Enable global interrupts
}

// Function to handle LED fading
void ledFadeToBeat() {
    static int fadeRate = 0; // Fade rate variable
    fadeRate -= 15; // Decrease fade value
    fadeRate = constrain(fadeRate, 0, 255); // Keep fade value within range
    analogWrite(fadePin, fadeRate); // Fade LED
}

// Function to output serial data
void serialOutput() {
    // Output data to serial monitor
    sendDataToSerial('S', Signal);
}

// Function to output data when a heartbeat is detected
void serialOutputWhenBeatHappens() {
    sendDataToSerial('B', BPM); // Send heart rate with a 'B' prefix
    sendDataToSerial('Q', IBI); // Send time between beats with a 'Q' prefix
}

// Function to send data to serial
void sendDataToSerial(char symbol, int data) {
    Serial.print(symbol);
    Serial.println(data);
}

// Interrupt Service Routine for pulse detection
ISR(TIMER2_COMPA_vect) {
    cli(); // Disable interrupts
    Signal = analogRead(pulsePin); // Read the Pulse Sensor
    static unsigned long sampleCounter = 0; // Keep track of time
    sampleCounter += 2; // Increment sample counter
    static int lastBeatTime = 0; // Last beat time
    int N = sampleCounter - lastBeatTime; // Time since last beat

    // Find peak and trough of the pulse wave
    static int P = 512; // Peak
    static int T = 512; // Trough
    static int thresh = 525; // Threshold
    static int amp = 100; // Amplitude

    if (Signal < thresh && N > (IBI / 5) * 3) { // Avoid noise
        if (Signal < T) {
            T = Signal; // Update trough
        }
    }
    if (Signal > thresh && Signal > P) { // Update peak
        P = Signal;
    }

    // Look for heartbeat
    if (N > 250) { // Avoid high frequency noise
        if ((Signal > thresh) && (Pulse == false) && (N > (IBI / 5) * 3)) {
            Pulse = true; // Set pulse flag
            digitalWrite(blinkPin, HIGH); // Turn on LED
            IBI = sampleCounter - lastBeatTime; // Measure time between beats
            lastBeatTime = sampleCounter; // Update last beat time

            // Calculate BPM
            static int rate[10]; // Array to hold last ten IBI values
            static boolean firstBeat = true; // First beat flag
            static boolean secondBeat = false; // Second beat flag

            if (secondBeat) {
                secondBeat = false; // Clear second beat flag
                for (int i = 0; i <= 9; i++) {
                    rate[i] = IBI; // Seed the running total
                }
            }
            if (firstBeat) {
                firstBeat = false; // Clear first beat flag
                secondBeat = true; // Set second beat flag
                sei(); // Enable interrupts
                return; // Discard unreliable IBI value
            }

            // Keep a running total of the last 10 IBI values
            word runningTotal = 0; // Clear running total
            for (int i = 0; i <= 8; i++) {
                rate[i] = rate[i + 1]; // Shift data in the rate array
                runningTotal += rate[i]; // Add up the oldest IBI values
            }
            rate[9] = IBI; // Add latest IBI to the rate array
            runningTotal += rate[9]; // Add latest IBI to running total
            runningTotal /= 10; // Average the last 10 IBI values
            BPM = 60000 / runningTotal; // Calculate BPM
            QS = true; // Set Quantified Self flag
            pulse = BPM; // Update pulse variable
        }
    }

    if (Signal < thresh && Pulse == true) { // Beat is over
        digitalWrite(blinkPin, LOW); // Turn off LED
        Pulse = false; // Reset pulse flag
        amp = P - T; // Get amplitude of pulse wave
        thresh = amp / 2 + T; // Set threshold
        P = thresh; // Reset peak
        T = thresh; // Reset trough
    }

    if (N > 2500) { // If 2.5 seconds go by without a beat
        thresh = 512; // Set threshold default
        P = 512; // Set peak default
        T = 512; // Set trough default
        lastBeatTime = sampleCounter; // Update last beat time
        firstBeat = true; // Reset flags
        secondBeat = false;
    }
    sei(); // Enable interrupts
}