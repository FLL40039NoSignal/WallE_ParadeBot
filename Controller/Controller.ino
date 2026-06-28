#include <Bluepad32.h>
#include <ESP32Servo.h>

#define WALLE_MAX_GAMEPADS 1
ControllerPtr myControllers[WALLE_MAX_GAMEPADS];

static const int MIN_uS = 1000;
static const int MAX_uS = 2000;
static const int LEFT_MOTOR_PIN = 15;
static const int RIGHT_MOTOR_PIN = 13;
Servo leftMotor;
Servo rightMotor;

// This callback gets called any time a new gamepad is connected.
// Up to 4 gamepads can be connected at the same time.
void onConnectedController(ControllerPtr ctl) {
  bool foundEmptySlot = false;
  for (int i = 0; i < WALLE_MAX_GAMEPADS; i++) {
    if (myControllers[i] == nullptr) {
      Serial.printf("CALLBACK: Controller is connected, index=%d\n", i);
      // Additionally, you can get certain gamepad properties like:
      // Model, VID, PID, BTAddr, flags, etc.
      ControllerProperties properties = ctl->getProperties();
      Serial.printf("Controller model: %s, VID=0x%04x, PID=0x%04x\n", ctl->getModelName().c_str(), properties.vendor_id,
                    properties.product_id);
      myControllers[i] = ctl;
      foundEmptySlot = true;
      break;
    }
  }
  if (!foundEmptySlot) {
    Serial.println("CALLBACK: Controller connected, but could not found empty slot");
  }
}

void onDisconnectedController(ControllerPtr ctl) {
  bool foundController = false;

  for (int i = 0; i < WALLE_MAX_GAMEPADS; i++) {
    if (myControllers[i] == ctl) {
      Serial.printf("CALLBACK: Controller disconnected from index=%d\n", i);
      myControllers[i] = nullptr;
      foundController = true;
      break;
    }
  }

  if (!foundController) {
    Serial.println("CALLBACK: Controller disconnected, but not found in myControllers");
  }
}

void dumpGamepad(ControllerPtr ctl) {
  Serial.printf(
    "idx=%d, battery: %f% dpad: 0x%02x, buttons: 0x%04x, axis L: %4d, %4d, axis R: %4d, %4d, brake: %4d, throttle: %4d, "
    "misc: 0x%02x, gyro x:%6d y:%6d z:%6d, accel x:%6d y:%6d z:%6d\n",
    ctl->index(),        // Controller Index
    ctl->battery() * 100 / 255.0,
    ctl->dpad(),         // D-pad
    ctl->buttons(),      // bitmask of pressed buttons
    ctl->axisX(),        // (-511 - 512) left X Axis
    ctl->axisY(),        // (-511 - 512) left Y axis
    ctl->axisRX(),       // (-511 - 512) right X axis
    ctl->axisRY(),       // (-511 - 512) right Y axis
    ctl->brake(),        // (0 - 1023): brake button
    ctl->throttle(),     // (0 - 1023): throttle (AKA gas) button
    ctl->miscButtons(),  // bitmask of pressed "misc" buttons
    ctl->gyroX(),        // Gyro X
    ctl->gyroY(),        // Gyro Y
    ctl->gyroZ(),        // Gyro Z
    ctl->accelX(),       // Accelerometer X
    ctl->accelY(),       // Accelerometer Y
    ctl->accelZ()        // Accelerometer Z
  );
}

void processGamepad(ControllerPtr ctl) {
  // There are different ways to query whether a button is pressed.
  // By query each button individually:
  //  a(), b(), x(), y(), l1(), etc...
  if (ctl->a()) {
    static int colorIdx = 0;
    // Some gamepads like DS4 and DualSense support changing the color LED.
    // It is possible to change it by calling:
    switch (colorIdx % 3) {
      case 0:
        // Red
        ctl->setColorLED(255, 0, 0);
        break;
      case 1:
        // Green
        ctl->setColorLED(0, 255, 0);
        break;
      case 2:
        // Blue
        ctl->setColorLED(0, 0, 255);
        break;
    }
    colorIdx++;
  }

  if (ctl->b()) {
    // Turn on the 4 LED. Each bit represents one LED.
    static int led = 0;
    led++;
    // Some gamepads like the DS3, DualSense, Nintendo Wii, Nintendo Switch
    // support changing the "Player LEDs": those 4 LEDs that usually indicate
    // the "gamepad seat".
    // It is possible to change them by calling:
    ctl->setPlayerLEDs(led & 0x0f);
  }

  if (ctl->buttons() & 1) {
    // Some gamepads like DS3, DS4, DualSense, Switch, Xbox One S, Stadia support rumble.
    // It is possible to set it by calling:
    // Some controllers have two motors: "strong motor", "weak motor".
    // It is possible to control them independently.
    ctl->playDualRumble(0 /* delayedStartMs */, 250 /* durationMs */, 0x80 /* weakMagnitude */,
                        0x40 /* strongMagnitude */);
  }

  // Another way to query controller data is by getting the buttons() function.
  // See how the different "dump*" functions dump the Controller info.
  dumpGamepad(ctl);

  static const double FR_SLOPE     =  -1.0 / 512.0;
  static const double FR_INTERCEPT =  0;
  static const double SPIN_SLOPE     =  1.0 / 512.0;
  static const double SPIN_INTERCEPT =  0;

  // FR_Slope et all need to be determined.

  double FrCmd   = FR_SLOPE   * ctl->axisY() + FR_INTERCEPT;
  double SpinCmd = SPIN_SLOPE * ctl->axisX() + SPIN_INTERCEPT;
  //Serial.printf("Fr Cmd: %f, Spin Cmd: %f\n", FrCmd, SpinCmd);

  double moveLeft  = FrCmd + SpinCmd;
  double moveRight = FrCmd - SpinCmd;

  //move left stuff
  if (moveLeft >=1) {
    moveLeft = 1;
  }
   
  if (moveLeft <=-1) {
    moveLeft = -1;
  }

// move right stuff
  if (moveRight >=1) {
    moveRight = 1;
  }
   
  if (moveRight <=-1) {
    moveRight = -1;
  }



  //Serial.printf("move Left: %f, move Right: %f\n", moveLeft, moveRight);

  // TODO: handle move* >1 or <-1

  static const double dirLeft  =  1;
  static const double dirRight = -1;
  static const double PWM_SLOPE     =  90;
  static const double PWM_INTERCEPT =  90;

  double pwmLeft  = PWM_SLOPE * dirLeft  * moveLeft  + PWM_INTERCEPT;
  double pwmRight = PWM_SLOPE * dirRight * moveRight + PWM_INTERCEPT;
  //Serial.printf("PWM Left: %f, PWM Right: %f\n", pwmLeft, pwmRight);

  // Why break into steps? The two lines below are probably close.
  // If they don't produce the behavior you want how do you find the problem?
  // By having steps you can see the actual problem, rather than having to do everything at once...
  // double pwmLeft  = 128 + 0.25 * ctl->axisX() + 0.25 * axisY();
  // double pwmRight = 128 - 0.25 * ctl->axisX() + 0.25 * axisY();

  // TODO write pwmLeft and pwmRight to output pins...
  leftMotor.write(pwmLeft);
  rightMotor.write(pwmRight);
}


void processControllers() {
  for (auto myController : myControllers) {
    if (myController && myController->isConnected() && myController->hasData()) {
      if (myController->isGamepad()) {
        processGamepad(myController);
      } else {
        Serial.println("Unsupported controller");
      }
    }
    else
    {
      // Stop
      leftMotor.write(90);
      rightMotor.write(90);
    }
  }
}

// Arduino setup function. Runs in CPU 1
void setup() {
  Serial.begin(115200);
  Serial.printf("Firmware: %s\n", BP32.firmwareVersion());
  const uint8_t* addr = BP32.localBdAddress();
  Serial.printf("BD Addr: %2X:%2X:%2X:%2X:%2X:%2X\n", addr[0], addr[1], addr[2], addr[3], addr[4], addr[5]);

  // Setup the Bluepad32 callbacks
  BP32.setup(&onConnectedController, &onDisconnectedController);

  // "forgetBluetoothKeys()" should be called when the user performs
  // a "device factory reset", or similar.
  // Calling "forgetBluetoothKeys" in setup() just as an example.
  // Forgetting Bluetooth keys prevents "paired" gamepads to reconnect.
  // But it might also fix some connection / re-connection issues.
  BP32.forgetBluetoothKeys();

  // Enables mouse / touchpad support for gamepads that support them.
  // When enabled, controllers like DualSense and DualShock4 generate two connected devices:
  // - First one: the gamepad
  // - Second one, which is a "virtual device", is a mouse.
  // By default, it is disabled.
  BP32.enableVirtualDevice(false);

  ESP32PWM::allocateTimer(0);
	ESP32PWM::allocateTimer(1);
  
  leftMotor.setPeriodHertz(50);   
	rightMotor.setPeriodHertz(50);
	leftMotor.attach(LEFT_MOTOR_PIN, MIN_uS, MAX_uS);
	rightMotor.attach(RIGHT_MOTOR_PIN, MIN_uS, MAX_uS);

}

// Arduino loop function. Runs in CPU 1.
void loop() {
  // This call fetches all the controllers' data.
  // Call this function in your main loop.
  bool dataUpdated = BP32.update();
  if (dataUpdated)
    processControllers();
  else
  {
   // Stop
    leftMotor.write(90);
    rightMotor.write(90);
  }

  // The main loop must have some kind of "yield to lower priority task" event.
  // Otherwise, the watchdog will get triggered.
  // If your main loop doesn't have one, just add a simple `vTaskDelay(1)`.
  // Detailed info here:
  // https://stackoverflow.com/questions/66278271/task-watchdog-got-triggered-the-tasks-did-not-reset-the-watchdog-in-time
  //     vTaskDelay(1);

  delay(100);
}