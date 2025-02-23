#include "StepperWithLimits.h"
#include "RTDebugOutput.h"
#include "Main.h"


#define STEPPER_WITH_LIMITS_SENSORLESS_CURRENT_THRESHOLD_IN_PERCENT 20
#define MIN_POS_MAX_ENDSTOP STEPS_PER_MOTOR_REVOLUTION * 3 // servo has to drive minimum N steps before it allows the detection of the max endstop

static const uint8_t LIMIT_TRIGGER_VALUE = LOW;                                   // does endstop trigger high or low
static const int32_t ENDSTOP_MOVEMENT = STEPS_PER_MOTOR_REVOLUTION / 100;         // how much to move between trigger checks
static const int32_t ENDSTOP_MOVEMENT_SENSORLESS = ENDSTOP_MOVEMENT * 5;


FastAccelStepperEngine& stepperEngine() {
  static FastAccelStepperEngine myEngine = FastAccelStepperEngine();   // this is a factory and manager for all stepper instances

  static bool firstTime = true;
  if (firstTime) {
     myEngine.init();
     firstTime = false;
  }

  return myEngine;
}



StepperWithLimits::StepperWithLimits(uint8_t pinStep, uint8_t pinDirection, uint8_t pinMin, uint8_t pinMax)
  : _pinMin(pinMin), _pinMax(pinMax)
  , _limitMin(0),    _limitMax(0)
  , _posMin(0),      _posMax(0)
{

  
  pinMode(pinMin, INPUT);
  pinMode(pinMax, INPUT);
  
  
  _stepper = stepperEngine().stepperConnectToPin(pinStep);

  

  // Stepper Parameters
  if (_stepper) {
    _stepper->setDirectionPin(pinDirection, MOTOR_INVERT_MOTOR_DIR);
    _stepper->setAutoEnable(true);
    _stepper->setSpeedInHz(MAXIMUM_STEPPER_SPEED);            // steps/s
    _stepper->setAcceleration(MAXIMUM_STEPPER_ACCELERATION);  // steps/s²

//#if defined(SUPPORT_ESP32_PULSE_COUNTER)
//    _stepper->attachToPulseCounter(1, 0, 0);
//#endif
  }
}


void StepperWithLimits::findMinMaxSensorless(isv57communication * isv57)
{

  if (! hasValidStepper()) return;

  // obtain servo states
  isv57->readServoStates();
  bool endPosDetected = abs( isv57->servo_current_percent) > STEPPER_WITH_LIMITS_SENSORLESS_CURRENT_THRESHOLD_IN_PERCENT;


  int32_t setPosition = _stepper->getCurrentPosition();
  while(!endPosDetected){
    delay(10);
    isv57->readServoStates();
    endPosDetected = abs( isv57->servo_current_percent) > STEPPER_WITH_LIMITS_SENSORLESS_CURRENT_THRESHOLD_IN_PERCENT;

    setPosition = setPosition - ENDSTOP_MOVEMENT_SENSORLESS;
    _stepper->moveTo(setPosition, true);

    //Serial.print("Min_DetValue: ");
    //Serial.println(isv57->servo_current_percent);
  }

  // move away from min position to reduce servos current reading
  _stepper->forceStop();
  setPosition = setPosition + 5 * ENDSTOP_MOVEMENT_SENSORLESS;
  _stepper->moveTo(setPosition, true);
  _stepper->forceStopAndNewPosition(0);
  _stepper->moveTo(0);
  _limitMin = 0;

  // wait N ms to let the endPosDetected become 0 again
  //delay(300);

  // read servo states again
  //isv57->readServoStates();
  endPosDetected = 0;//abs( isv57->servo_current_percent) > STEPPER_WITH_LIMITS_SENSORLESS_CURRENT_THRESHOLD_IN_PERCENT;

  setPosition = _stepper->getCurrentPosition();
  while (!endPosDetected) {
    delay(10);
    isv57->readServoStates();

    // only trigger when difference is significant
    if (setPosition > MIN_POS_MAX_ENDSTOP)
    {
      endPosDetected = abs( isv57->servo_current_percent) > STEPPER_WITH_LIMITS_SENSORLESS_CURRENT_THRESHOLD_IN_PERCENT;  
    }
    

    setPosition = setPosition + ENDSTOP_MOVEMENT_SENSORLESS;
    _stepper->moveTo(setPosition, true);

    //Serial.print("Max_DetValue: ");
    //Serial.println(isv57->servo_current_percent);
  }

  //_stepper->forceStop();
  //setPosition = setPosition - 5 * ENDSTOP_MOVEMENT;

  _limitMax = _stepper->getCurrentPosition();

  // reduce speed and accelerartion
  _stepper->setSpeedInHz(MAXIMUM_STEPPER_SPEED / 4);
  _stepper->setAcceleration(MAXIMUM_STEPPER_ACCELERATION / 4);

  // move to min
  _stepper->moveTo(_posMin, true);

  // increase speed and accelerartion
  _stepper->setAcceleration(MAXIMUM_STEPPER_ACCELERATION);
  _stepper->setSpeedInHz(MAXIMUM_STEPPER_SPEED);




#if defined(SUPPORT_ESP32_PULSE_COUNTER)
  _stepper->clearPulseCounter();
#endif


}


void StepperWithLimits::findMinMaxEndstops() {
  if (! hasValidStepper()) return;

  int32_t setPosition = _stepper->getCurrentPosition();
  while(! (LIMIT_TRIGGER_VALUE == digitalRead(_pinMin))){
    setPosition = setPosition - ENDSTOP_MOVEMENT;
    _stepper->moveTo(setPosition, true);
  }

  
  _stepper->forceStopAndNewPosition(0);
  _stepper->moveTo(0);
  _limitMin = 0;

  setPosition = _stepper->getCurrentPosition();
  while(! (LIMIT_TRIGGER_VALUE == digitalRead(_pinMax))){
    setPosition = setPosition + ENDSTOP_MOVEMENT;
    _stepper->moveTo(setPosition, true);
  }

  _limitMax = _stepper->getCurrentPosition();

  _stepper->moveTo(_posMin, true);
#if defined(SUPPORT_ESP32_PULSE_COUNTER)
  _stepper->clearPulseCounter();
#endif
}

void StepperWithLimits::updatePedalMinMaxPos(uint8_t pedalStartPosPct, uint8_t pedalEndPosPct) {
  int32_t limitRange = _limitMax - _limitMin;
  _posMin = _limitMin + ((limitRange * pedalStartPosPct) / 100);
  _posMax = _limitMin + ((limitRange * pedalEndPosPct) / 100);
}

void StepperWithLimits::refindMinLimit() {
  int32_t setPosition = _stepper->getCurrentPosition();
  while(! (LIMIT_TRIGGER_VALUE == digitalRead(_pinMin))){
    setPosition = setPosition - ENDSTOP_MOVEMENT;
    _stepper->moveTo(setPosition, true);
  }
  _stepper->forceStopAndNewPosition(_limitMin);
}

void StepperWithLimits::refindMinLimitSensorless(isv57communication * isv57) {

  // obtain servo states
  isv57->readServoStates();
  bool endPosDetected = abs( isv57->servo_current_percent) > STEPPER_WITH_LIMITS_SENSORLESS_CURRENT_THRESHOLD_IN_PERCENT;


  int32_t setPosition = _stepper->getCurrentPosition();
  while(!endPosDetected){
    delay(10);
    isv57->readServoStates();
    endPosDetected = abs( isv57->servo_current_percent) > STEPPER_WITH_LIMITS_SENSORLESS_CURRENT_THRESHOLD_IN_PERCENT;

    setPosition = setPosition - ENDSTOP_MOVEMENT_SENSORLESS;
    _stepper->moveTo(setPosition, true);

    //Serial.print("Min_DetValue: ");
    //Serial.println(isv57->servo_current_percent);
  }

  // move away from min position to reduce servos current reading
  _stepper->forceStop();
  setPosition = setPosition + 5 * ENDSTOP_MOVEMENT_SENSORLESS;
  _stepper->moveTo(setPosition, true);
  _stepper->forceStopAndNewPosition(_limitMin);
}

void StepperWithLimits::checkLimitsAndResetIfNecessary() {
  // in case the stepper loses its position and therefore an endstop is triggered reset position
  if (LIMIT_TRIGGER_VALUE == digitalRead(_pinMin)) {
    _stepper->forceStopAndNewPosition(_limitMin);
    _stepper->moveTo(_posMin, true);
  }
  if (LIMIT_TRIGGER_VALUE == digitalRead(_pinMax)) {
    _stepper->forceStopAndNewPosition(_limitMin);
    _stepper->moveTo(_posMax, true);
  }
}

int8_t StepperWithLimits::moveTo(int32_t position, bool blocking) {
  return _stepper->moveTo(position, blocking);
}

int32_t StepperWithLimits::getCurrentPositionSteps() const {
  return _stepper->getCurrentPosition() - _posMin;
}


double StepperWithLimits::getCurrentPositionFraction() const {
  return double(getCurrentPositionSteps()) / getTravelSteps();
}

double StepperWithLimits::getCurrentPositionFractionFromExternalPos(int32_t extPos_i32) const {
  return double(extPos_i32) / getTravelSteps();
}

int32_t StepperWithLimits::getTargetPositionSteps() const {
  return _stepper->getPositionAfterCommandsCompleted();
}


void StepperWithLimits::printStates()
{
  int32_t currentStepperPos = _stepper->getCurrentPosition();
  int32_t currentStepperVel = _stepper->getCurrentSpeedInUs();
  int32_t currentStepperVel2 = _stepper->getCurrentSpeedInMilliHz();


  //Serial.println(currentStepperVel);
  
  int32_t currentStepperAccel = _stepper->getCurrentAcceleration();

  static RTDebugOutput<int32_t, 4> rtDebugFilter({ "Pos", "Vel", "Vel2", "Accel"});
  rtDebugFilter.offerData({ currentStepperPos, currentStepperVel, currentStepperVel2, currentStepperAccel});
}


void StepperWithLimits::setSpeed(uint32_t speedInStepsPerSecond) 
{
  _stepper->setSpeedInHz(speedInStepsPerSecond);            // steps/s 
}

bool StepperWithLimits::isAtMinPos()
{

  bool isNotRunning = !_stepper->isRunning();
  bool isAtMinPos = getCurrentPositionSteps() == 0;

  return isAtMinPos && isNotRunning;
}
bool StepperWithLimits::correctPos(int32_t posOffset)
{
  // 
  int32_t stepOffset =(int32_t)constrain(posOffset, -10, 10);

  // correct pos
  _stepper->setCurrentPosition(_stepper->getCurrentPosition() + stepOffset);
  return 1;
}

