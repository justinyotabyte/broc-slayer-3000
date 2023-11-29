#define ROTATION_ENCODER 45 // This is where the input is fed.
#define REED_SWITCH 37 // button
#define PULSES_OPEN 310
#define DEBOUNCE_DELAY 50
#define DEBUG 0
#define MASTER_RELAY 53
#define DEAD_ROTATIONS 3
#define PULSES_PER_REVOLUTION 688.421052648

#define MAX_PULSE_PER_MILLISECONDS 1/10
#define MIN_PULSE_PER_MILLISECONDS 2/10

const int knifePins[24] = {47, 40, 46, 38, 32, 31, 30, 24, 23,  22, 4, 3, 49, 43, 42, 41, 35, 34, 33, 27, 26, 25, 7, 6};
const int knifeCount = sizeof(knifePins) / sizeof(knifePins[0]);


int currentPulse = -1; // Variable for saving pulses count. This begins at negative 1 so when the loop starts, it can evalute from 0.
double currentPulseRemainder = 0.0;
int reed_pulses_until_start = DEAD_ROTATIONS;
bool previousPulseState = false;
bool fullyRotated = false;
float previous_millis = 0.0;
bool safety_mode = false;


struct Button {
  int pin;
  int state;
  int lastState;
  unsigned long lastDebounceTime;
};

Button reed_button, rotation_button;

Button setupButton(int pin) {
  Button button;
  button.pin = pin;
  pinMode(pin, INPUT_PULLUP);
  button.state = digitalRead(button.pin);
  button.lastState = button.state;
  button.lastDebounceTime = 0;
  return button;
}


bool debouncedDigitalRead(Button &button) {
  int reading = digitalRead(button.pin);
  bool hasFallen = false; // Default return value indicating no rising edge

  if (reading != button.lastState) {
    button.lastDebounceTime = millis();
  }


  if ((millis() - button.lastDebounceTime) > DEBOUNCE_DELAY) {
    if (reading != button.state) {
      // Button state has changed after debounce
      if (button.state == HIGH && reading == LOW) {
        // Button has transitioned from LOW to HIGH indicating a rising edge
        hasFallen = true;
      }
      button.state = reading;
    }
  }

  button.lastState = reading;
  return hasFallen;
}


void setup() {
  reed_button = setupButton(REED_SWITCH);

  // Setup encoder as input.
  pinMode(ROTATION_ENCODER, INPUT);

  // Set all the knife pins to output
  for (int i = 0; i < knifeCount; i++) {
    pinMode(knifePins[i], OUTPUT);
    digitalWrite(knifePins[i], !DEBUG);
  }

  // Setup relay.
  pinMode(MASTER_RELAY, OUTPUT);
  digitalWrite(MASTER_RELAY, 0);

  Serial.begin(9600);
  Serial.println("Broc slayer 3000 engage");
}

void knifeLogic() {
  for (int i = 0; i < knifeCount; i++) {
    if (knifeShouldBeOpen(i)) {
      digitalWrite(knifePins[i], DEBUG);
    } else {
      digitalWrite(knifePins[i], !DEBUG);
    }
  }
}

bool knifeShouldBeOpen(int knifeIndex) {
  double openPulse = (double) knifeIndex * (PULSES_PER_REVOLUTION / (double) knifeCount);
  double closePulse = round(fmod((openPulse + PULSES_OPEN), PULSES_PER_REVOLUTION));
  openPulse = round(openPulse);

  if (closePulse < openPulse) {
    return ((currentPulse <= closePulse && fullyRotated) || currentPulse >= openPulse);
  }

  return currentPulse <= closePulse && currentPulse >= openPulse;

}

bool deadRoationsElapsed() {

  // The deadzone has already been covered.
  if(reed_pulses_until_start == 0) return true;

  // The reed switch needs to be in contact to check again.
  bool reed_contact = debouncedDigitalRead(reed_button);
  if(!reed_contact) return false;

  // Now we can say a rotation has elapsed and check.
  reed_pulses_until_start -= 1;
  if(reed_pulses_until_start == 0){
    Serial.println("Dead rotations complete.");
    return true;
  }
  return false;



}


float getSpeed(){
  float m = millis();
  float speed = 1 / (m - previous_millis);
  previous_millis = m;
  return speed;
}

bool isWithinSpeedRange(float speed){
  return speed <= MAX_PULSE_PER_MILLISECONDS && speed >= MIN_PULSE_PER_MILLISECONDS;
}

void resetKnives(){
  for (int i = 0; i < knifeCount; i++) {
    digitalWrite(knifePins[i], !DEBUG);
  }
  fullyRotated = false;
}

bool inSafeOperatingRange(bool reed_contact){
  float speed = getSpeed();
  bool within_speed_range = isWithinSpeedRange(speed);

  // If we are over or underspeed, always return false.
  if(!within_speed_range){
    resetKnives();
    safety_mode = true;
    return false;
  }

  // If we are in the correct speed range and we are not in safety mode, business as usual.
  if(!safety_mode){
    return true;
  }

  // If we get to here, we are in the right range, but we are currently in safety mode.
  // Here, we should wait for the reed then return true.
  if(reed_contact){
    safety_mode = false;
    return true;
  }
  
  return false;
   
}

void loop() {


  if(!deadRoationsElapsed()) return;
  

  // Now the main loop is active, we should not do anything until a pulse happens.
  bool pulse_received = digitalRead(ROTATION_ENCODER) == HIGH;

  // Get the state of the reed switch.
  bool reed_hit = debouncedDigitalRead(reed_button);


  if (pulse_received == previousPulseState && !reed_hit) {
    return;
  }

  // Update the previous pulse state to the current state for the next function call.
  previousPulseState = pulse_received;

  // As a pulse has been received, we can run this here.
  if(!inSafeOperatingRange(reed_hit)) return;

  // If there's a reed hit, reset the current pulse count and remainder.
  if (reed_hit) {
    currentPulse = -1;
    currentPulseRemainder = 0.0;
  } else if (!pulse_received) { // If no pulse is received and no reed hit, exit the function.
    return;
  }

  // If it has been received, increment the counter and evalate knives.
  currentPulse = currentPulse + 1;
  if (currentPulse > floor(PULSES_PER_REVOLUTION)) {
    currentPulse = 0;
    fullyRotated = true;
    currentPulseRemainder += PULSES_PER_REVOLUTION - floor(PULSES_PER_REVOLUTION);
  }

  knifeLogic();

  if (currentPulseRemainder >= 1) {
    currentPulseRemainder -= 1;
    currentPulse += 1;
    knifeLogic();
  }

  if (DEBUG) {
    Serial.println("*");
    Serial.println("currentPulse:" + String(currentPulse));
    Serial.println("currentPulseRemainder:" + String(currentPulseRemainder));
  }


}
