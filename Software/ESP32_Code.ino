#include <Arduino.h>
#include <Wire.h>
#include <math.h>
#include <NimBLEDevice.h>

#include <OneWireNg_CurrentPlatform.h>
#include <drivers/DSTherm.h>
#include <utils/Placeholder.h>

// =====================================================
// HER COMFORT - XIAO ESP32-C6
// =====================================================

// PIN MAP
#define MODE_BUTTON_PIN 0     // D0 / GPIO0
#define EMG_PIN 1             // D1 / GPIO1
#define SDA_PIN 22            // D4 / GPIO22
#define SCL_PIN 23            // D5 / GPIO23
#define MOTOR_PIN 17          // D7 / GPIO17
#define TEMP_PIN 19           // D8 / GPIO19
#define HEATER_PIN 18         // D10 / GPIO18


// =====================================================
// EMG
// =====================================================

#define EMG_SAMPLE_RATE 500
#define EMG_BUFFER_SIZE 128

int emgCircularBuffer[EMG_BUFFER_SIZE];
int emgDataIndex = 0;
long emgSum = 0;

int latestEMG = 0;

unsigned long lastEMGSample = 0;


// =====================================================
// HEATER
// =====================================================

const float HEATER_ON_TEMP = 37.0f;
const float HEATER_OFF_TEMP = 38.0f;

const float HEATER_HARD_CUTOFF = 40.0f;

bool heaterOn = false;


// =====================================================
// MOTOR
// =====================================================

#define MOTOR_PWM_FREQ 5000
#define MOTOR_PWM_RESOLUTION 8

const int CONTINUOUS_PWM = 255;
const int PULSE_PWM = 255;

const int HARMONIC_MIN_PWM = 200;
const int HARMONIC_MAX_PWM = 255;

const unsigned long PULSE_ON_TIME = 3000;
const unsigned long PULSE_OFF_TIME = 2000;

const unsigned long HARMONIC_PERIOD = 4000;

const unsigned long MODE_CHANGE_GAP = 1000;


// =====================================================
// MOTOR MODE
// =====================================================

enum MotorMode
{
  MOTOR_OFF,
  MOTOR_CONTINUOUS,
  MOTOR_PULSE,
  MOTOR_HARMONIC
};

MotorMode motorMode = MOTOR_OFF;


// =====================================================
// MOTOR STATE
// =====================================================

bool previousButtonState = HIGH;

unsigned long lastButtonPress = 0;

const unsigned long BUTTON_DEBOUNCE = 250;

unsigned long pulseTimer = 0;

bool pulseMotorOn = true;

unsigned long modeChangeTime = 0;

bool modeGapActive = false;

unsigned long harmonicStartTime = 0;


// =====================================================
// MPU6500
// =====================================================

#define MPU_ADDR 0x68

#define MPU_WHO_AM_I       0x75
#define MPU_PWR_MGMT_1     0x6B
#define MPU_CONFIG         0x1A
#define MPU_GYRO_CONFIG    0x1B
#define MPU_ACCEL_CONFIG   0x1C
#define MPU_ACCEL_CONFIG2  0x1D
#define MPU_ACCEL_XOUT_H   0x3B

#define MPU6500_ID 0x70


// =====================================================
// MPU SCALE
// =====================================================

const float ACCEL_SCALE = 16384.0f;

const float GYRO_SCALE = 131.0f;


// =====================================================
// MPU CALIBRATION
// =====================================================

const int CALIBRATION_SAMPLES = 1000;

float gyroBiasX = 0.0f;
float gyroBiasY = 0.0f;
float gyroBiasZ = 0.0f;

float referenceAx = 0.0f;
float referenceAy = 0.0f;
float referenceAz = 1.0f;

bool calibrationRunning = false;


// =====================================================
// ACCEL LOW PASS FILTER
// =====================================================

const float ACCEL_LPF_CUTOFF_HZ = 5.0f;

float filteredAx = 0.0f;
float filteredAy = 0.0f;
float filteredAz = 1.0f;

bool accelFilterInitialized = false;


// =====================================================
// SENSOR FUSION
// =====================================================

float q0 = 1.0f;
float q1 = 0.0f;
float q2 = 0.0f;
float q3 = 0.0f;

const float FUSION_KP = 0.40f;

const float FUSION_KI = 0.0f;

float integralFBx = 0.0f;
float integralFBy = 0.0f;
float integralFBz = 0.0f;

unsigned long lastFusionMicros = 0;


// =====================================================
// POSITION
// =====================================================

const float UPRIGHT_MAX_ANGLE = 45.0f;

const float LYING_MIN_ANGLE = 70.0f;


// =====================================================
// WALKING
// =====================================================

const float WALK_AXIS_THRESHOLD = 5.0f;

const float WALK_GYRO_THRESHOLD = 10.0f;

const float WALK_ACCEL_DELTA = 0.04f;


// =====================================================
// POSITION CONFIRMATION
// =====================================================

const unsigned long UPRIGHT_CONFIRM_MS = 1000;
const unsigned long LYING_CONFIRM_MS = 1500;
const unsigned long WALK_CONFIRM_MS = 700;


// =====================================================
// MPU RAW VALUES
// =====================================================

int16_t rawAx = 0;
int16_t rawAy = 0;
int16_t rawAz = 0;

int16_t rawGx = 0;
int16_t rawGy = 0;
int16_t rawGz = 0;


// =====================================================
// BODY ANGLE
// =====================================================

float latestBodyAngle = 0.0f;


// =====================================================
// POSITION ENUM
// =====================================================

enum PositionState
{
  POSITION_UNKNOWN,
  POSITION_UPRIGHT,
  POSITION_LYING,
  POSITION_WALKING
};

PositionState currentPosition = POSITION_UNKNOWN;

PositionState candidatePosition = POSITION_UNKNOWN;

unsigned long candidateStartTime = 0;


// =====================================================
// TEMPERATURE
// =====================================================

OneWireNg_CurrentPlatform oneWire(
  TEMP_PIN,
  false
);

DSTherm ds18b20(
  oneWire
);

Placeholder<DSTherm::Scratchpad> scratchpad;

float latestTemperature = 0.0f;

bool temperatureValid = false;

bool tempConversionRunning = false;

unsigned long tempConversionStart = 0;

unsigned long lastTempRequest = 0;

#define TEMP_CONVERSION_TIME 750


// =====================================================
// BLE
// =====================================================

#define SERVICE_UUID \
"12345678-1234-1234-1234-123456789000"

#define SENSOR_CHAR_UUID \
"12345678-1234-1234-1234-123456789001"

NimBLECharacteristic *sensorCharacteristic;

const unsigned long SEND_INTERVAL = 50;

unsigned long lastSend = 0;


// =====================================================
// MOTOR PWM
// =====================================================

void setMotorPWM(uint8_t pwm)
{
  ledcWrite(
    MOTOR_PIN,
    pwm
  );
}


// =====================================================
// MOTOR MODE NAME
// =====================================================

const char* motorModeName()
{
  switch (motorMode)
  {
    case MOTOR_OFF:
      return "OFF";

    case MOTOR_CONTINUOUS:
      return "CONTINUOUS";

    case MOTOR_PULSE:
      return "PULSE";

    case MOTOR_HARMONIC:
      return "HARMONIC";

    default:
      return "UNKNOWN";
  }
}


// =====================================================
// MOTOR MODE CHANGE
// =====================================================

void nextMotorMode()
{
  setMotorPWM(0);


  if (motorMode == MOTOR_OFF)
  {
    motorMode = MOTOR_CONTINUOUS;
  }

  else if (motorMode == MOTOR_CONTINUOUS)
  {
    motorMode = MOTOR_PULSE;
  }

  else if (motorMode == MOTOR_PULSE)
  {
    motorMode = MOTOR_HARMONIC;
  }

  else
  {
    motorMode = MOTOR_OFF;
  }


  modeChangeTime = millis();

  modeGapActive = true;

  pulseMotorOn = true;

  pulseTimer = millis();

  harmonicStartTime = millis();


  Serial.print("MOTOR MODE: ");

  Serial.println(
    motorModeName()
  );
}


// =====================================================
// BUTTON
// =====================================================

void updateMotorButton()
{
  bool currentButtonState =
    digitalRead(
      MODE_BUTTON_PIN
    );


  if (
    previousButtonState == HIGH &&
    currentButtonState == LOW
  )
  {
    if (
      millis() - lastButtonPress >
      BUTTON_DEBOUNCE
    )
    {
      lastButtonPress = millis();

      nextMotorMode();
    }
  }


  previousButtonState =
    currentButtonState;
}


// =====================================================
// MOTOR CONTROL
// =====================================================

void updateMotor()
{
  unsigned long now = millis();


  // MODE CHANGE GAP
  if (modeGapActive)
  {
    setMotorPWM(0);


    if (
      now - modeChangeTime <
      MODE_CHANGE_GAP
    )
    {
      return;
    }


    modeGapActive = false;

    pulseTimer = now;

    pulseMotorOn = true;

    harmonicStartTime = now;
  }


  // OFF
  if (motorMode == MOTOR_OFF)
  {
    setMotorPWM(0);

    return;
  }


  // CONTINUOUS
  if (motorMode == MOTOR_CONTINUOUS)
  {
    setMotorPWM(
      CONTINUOUS_PWM
    );

    return;
  }


  // PULSE
  if (motorMode == MOTOR_PULSE)
  {
    if (pulseMotorOn)
    {
      setMotorPWM(
        PULSE_PWM
      );


      if (
        now - pulseTimer >=
        PULSE_ON_TIME
      )
      {
        pulseMotorOn = false;

        pulseTimer = now;
      }
    }

    else
    {
      setMotorPWM(0);


      if (
        now - pulseTimer >=
        PULSE_OFF_TIME
      )
      {
        pulseMotorOn = true;

        pulseTimer = now;
      }
    }


    return;
  }


  // HARMONIC
  if (motorMode == MOTOR_HARMONIC)
  {
    float phase =
      (
        (now - harmonicStartTime) %
        HARMONIC_PERIOD
      )
      /
      (float)HARMONIC_PERIOD;


    float wave =
      (
        1.0f -
        cos(
          2.0f *
          PI *
          phase
        )
      )
      /
      2.0f;


    float pwmValue =
      HARMONIC_MIN_PWM +
      wave *
      (
        HARMONIC_MAX_PWM -
        HARMONIC_MIN_PWM
      );


    setMotorPWM(
      (uint8_t)pwmValue
    );

    return;
  }
}


// =====================================================
// HEATER
// =====================================================

void updateHeater()
{
  if (!temperatureValid)
  {
    heaterOn = false;

    digitalWrite(
      HEATER_PIN,
      LOW
    );

    return;
  }


  if (
    latestTemperature >=
    HEATER_HARD_CUTOFF
  )
  {
    heaterOn = false;

    digitalWrite(
      HEATER_PIN,
      LOW
    );

    return;
  }


  if (
    latestTemperature <=
    HEATER_ON_TEMP
  )
  {
    heaterOn = true;
  }

  else if (
    latestTemperature >=
    HEATER_OFF_TEMP
  )
  {
    heaterOn = false;
  }


  digitalWrite(
    HEATER_PIN,
    heaterOn ? HIGH : LOW
  );
}


// =====================================================
// MPU REGISTER WRITE
// =====================================================

bool writeMPURegister(
  uint8_t reg,
  uint8_t value
)
{
  Wire.beginTransmission(
    MPU_ADDR
  );

  Wire.write(reg);

  Wire.write(value);


  return (
    Wire.endTransmission() == 0
  );
}


// =====================================================
// MPU REGISTER READ
// =====================================================

uint8_t readMPURegister(
  uint8_t reg
)
{
  Wire.beginTransmission(
    MPU_ADDR
  );

  Wire.write(reg);


  if (
    Wire.endTransmission(false) != 0
  )
  {
    return 0xFF;
  }


  Wire.requestFrom(
    MPU_ADDR,
    1
  );


  if (
    Wire.available() < 1
  )
  {
    return 0xFF;
  }


  return Wire.read();
}


// =====================================================
// MPU INITIALIZATION
// =====================================================

bool initializeMPU()
{
  Serial.println();

  Serial.println(
    "Initializing MPU6500..."
  );


  Wire.begin(
    SDA_PIN,
    SCL_PIN
  );


  Wire.setClock(
    400000
  );


  delay(150);


  uint8_t whoAmI =
    readMPURegister(
      MPU_WHO_AM_I
    );


  Serial.print(
    "WHO_AM_I = 0x"
  );

  Serial.println(
    whoAmI,
    HEX
  );


  if (
    whoAmI != MPU6500_ID
  )
  {
    Serial.println(
      "WARNING: Expected MPU6500 ID 0x70"
    );
  }


  if (
    !writeMPURegister(
      MPU_PWR_MGMT_1,
      0x00
    )
  )
  {
    Serial.println(
      "ERROR: MPU6500 not responding"
    );

    return false;
  }


  delay(100);


  // Accelerometer +/-2g
  writeMPURegister(
    MPU_ACCEL_CONFIG,
    0x00
  );


  // Gyroscope +/-250 deg/sec
  writeMPURegister(
    MPU_GYRO_CONFIG,
    0x00
  );


  // Gyro low-pass
  writeMPURegister(
    MPU_CONFIG,
    0x03
  );


  // Accelerometer low-pass
  // Approx 10 Hz bandwidth
  writeMPURegister(
    MPU_ACCEL_CONFIG2,
    0x05
  );


  delay(100);


  Serial.println(
    "MPU6500 initialized."
  );


  Serial.println(
    "Accel vibration filtering enabled."
  );


  return true;
}


// =====================================================
// READ MPU
// =====================================================

bool readMPU()
{
  Wire.beginTransmission(
    MPU_ADDR
  );

  Wire.write(
    MPU_ACCEL_XOUT_H
  );


  if (
    Wire.endTransmission(false) != 0
  )
  {
    return false;
  }


  uint8_t received =
    Wire.requestFrom(
      MPU_ADDR,
      14,
      true
    );


  if (received != 14)
  {
    return false;
  }


  rawAx =
    ((int16_t)Wire.read() << 8) |
    Wire.read();

  rawAy =
    ((int16_t)Wire.read() << 8) |
    Wire.read();

  rawAz =
    ((int16_t)Wire.read() << 8) |
    Wire.read();


  // Skip internal temperature
  Wire.read();
  Wire.read();


  rawGx =
    ((int16_t)Wire.read() << 8) |
    Wire.read();

  rawGy =
    ((int16_t)Wire.read() << 8) |
    Wire.read();

  rawGz =
    ((int16_t)Wire.read() << 8) |
    Wire.read();


  return true;
}


// =====================================================
// ACTUATORS OFF
// =====================================================

void disableActuators()
{
  setMotorPWM(0);

  heaterOn = false;

  digitalWrite(
    HEATER_PIN,
    LOW
  );
}


// =====================================================
// CORRECTED SENSOR FUSION INITIALIZATION
// =====================================================

void initializeFusionFromReference()
{
  float rx = referenceAx;
  float ry = referenceAy;
  float rz = referenceAz;


  float mag =
    sqrt(
      rx * rx +
      ry * ry +
      rz * rz
    );


  if (mag < 0.1f)
  {
    q0 = 1.0f;
    q1 = 0.0f;
    q2 = 0.0f;
    q3 = 0.0f;

    return;
  }


  rx /= mag;
  ry /= mag;
  rz /= mag;


  // ==================================================
  // CORRECTED INITIAL QUATERNION
  //
  // Estimated gravity now starts aligned with the
  // calibrated upright reference.
  //
  // This avoids the temporary ~180 degree startup.
  // ==================================================

  if (rz > -0.999f)
  {
    q0 =
      sqrt(
        (1.0f + rz) *
        0.5f
      );


    float denom =
      2.0f *
      q0;


    // CORRECTED SIGNS
    q1 =
      ry /
      denom;

    q2 =
      -rx /
      denom;

    q3 =
      0.0f;
  }

  else
  {
    q0 = 0.0f;
    q1 = 1.0f;
    q2 = 0.0f;
    q3 = 0.0f;
  }


  // Normalize quaternion
  float norm =
    sqrt(
      q0*q0 +
      q1*q1 +
      q2*q2 +
      q3*q3
    );


  if (norm > 0.00001f)
  {
    q0 /= norm;
    q1 /= norm;
    q2 /= norm;
    q3 /= norm;
  }


  integralFBx = 0.0f;
  integralFBy = 0.0f;
  integralFBz = 0.0f;


  filteredAx =
    referenceAx;

  filteredAy =
    referenceAy;

  filteredAz =
    referenceAz;


  accelFilterInitialized =
    true;


  latestBodyAngle =
    0.0f;


  lastFusionMicros =
    micros();
}


// =====================================================
// MPU CALIBRATION
// =====================================================

void calibrateMPU()
{
  calibrationRunning = true;

  disableActuators();


  Serial.println();

  Serial.println(
    "======================================"
  );

  Serial.println(
    " HER COMFORT MPU6500 CALIBRATION"
  );

  Serial.println(
    "======================================"
  );


  Serial.println();

  Serial.println(
    "Wear belt normally."
  );

  Serial.println(
    "Sit or stand STRAIGHT."
  );

  Serial.println(
    "Keep completely STILL."
  );


  Serial.println();

  Serial.println(
    "Starting in 3 seconds..."
  );


  delay(3000);


  double axSum = 0;
  double aySum = 0;
  double azSum = 0;

  double gxSum = 0;
  double gySum = 0;
  double gzSum = 0;


  for (
    int i = 0;
    i < 100;
    i++
  )
  {
    readMPU();

    delay(5);
  }


  int validSamples = 0;


  while (
    validSamples <
    CALIBRATION_SAMPLES
  )
  {
    disableActuators();


    if (!readMPU())
    {
      continue;
    }


    float ax =
      rawAx /
      ACCEL_SCALE;

    float ay =
      rawAy /
      ACCEL_SCALE;

    float az =
      rawAz /
      ACCEL_SCALE;


    float gx =
      rawGx /
      GYRO_SCALE;

    float gy =
      rawGy /
      GYRO_SCALE;

    float gz =
      rawGz /
      GYRO_SCALE;


    axSum += ax;
    aySum += ay;
    azSum += az;

    gxSum += gx;
    gySum += gy;
    gzSum += gz;


    validSamples++;


    if (
      validSamples % 100 == 0
    )
    {
      Serial.print(
        "Calibration: "
      );

      Serial.print(
        validSamples / 10
      );

      Serial.println("%");
    }


    delay(4);
  }


  gyroBiasX =
    gxSum /
    validSamples;

  gyroBiasY =
    gySum /
    validSamples;

  gyroBiasZ =
    gzSum /
    validSamples;


  referenceAx =
    axSum /
    validSamples;

  referenceAy =
    aySum /
    validSamples;

  referenceAz =
    azSum /
    validSamples;


  float magnitude =
    sqrt(
      referenceAx * referenceAx +
      referenceAy * referenceAy +
      referenceAz * referenceAz
    );


  if (magnitude > 0.1f)
  {
    referenceAx /= magnitude;
    referenceAy /= magnitude;
    referenceAz /= magnitude;
  }


  initializeFusionFromReference();


  Serial.println();

  Serial.println(
    "CALIBRATION COMPLETE"
  );


  Serial.print(
    "Gyro bias: "
  );

  Serial.print(
    gyroBiasX,
    3
  );

  Serial.print(", ");

  Serial.print(
    gyroBiasY,
    3
  );

  Serial.print(", ");

  Serial.println(
    gyroBiasZ,
    3
  );


  Serial.print(
    "Upright reference: "
  );

  Serial.print(
    referenceAx,
    3
  );

  Serial.print(", ");

  Serial.print(
    referenceAy,
    3
  );

  Serial.print(", ");

  Serial.println(
    referenceAz,
    3
  );


  Serial.println(
    "Sensor fusion initialized."
  );


  calibrationRunning = false;
}


// =====================================================
// ACCEL LOW PASS FILTER
// =====================================================

void updateAccelLowPass(
  float ax,
  float ay,
  float az,
  float dt
)
{
  if (!accelFilterInitialized)
  {
    filteredAx = ax;
    filteredAy = ay;
    filteredAz = az;

    accelFilterInitialized = true;

    return;
  }


  float rc =
    1.0f /
    (
      2.0f *
      PI *
      ACCEL_LPF_CUTOFF_HZ
    );


  float alpha =
    dt /
    (
      rc +
      dt
    );


  alpha =
    constrain(
      alpha,
      0.0f,
      1.0f
    );


  filteredAx +=
    alpha *
    (
      ax -
      filteredAx
    );


  filteredAy +=
    alpha *
    (
      ay -
      filteredAy
    );


  filteredAz +=
    alpha *
    (
      az -
      filteredAz
    );
}


// =====================================================
// MAHONY SENSOR FUSION
// =====================================================

void updateFusion(
  float gx,
  float gy,
  float gz,
  float ax,
  float ay,
  float az,
  float dt
)
{
  gx *= DEG_TO_RAD;
  gy *= DEG_TO_RAD;
  gz *= DEG_TO_RAD;


  float accelNorm =
    sqrt(
      ax * ax +
      ay * ay +
      az * az
    );


  bool useAccel =
    (
      accelNorm > 0.75f &&
      accelNorm < 1.25f
    );


  if (useAccel)
  {
    ax /= accelNorm;
    ay /= accelNorm;
    az /= accelNorm;


    float vx =
      2.0f *
      (
        q1 * q3 -
        q0 * q2
      );


    float vy =
      2.0f *
      (
        q0 * q1 +
        q2 * q3
      );


    float vz =
      q0*q0 -
      q1*q1 -
      q2*q2 +
      q3*q3;


    float ex =
      ay * vz -
      az * vy;


    float ey =
      az * vx -
      ax * vz;


    float ez =
      ax * vy -
      ay * vx;


    if (FUSION_KI > 0.0f)
    {
      integralFBx +=
        FUSION_KI *
        ex *
        dt;

      integralFBy +=
        FUSION_KI *
        ey *
        dt;

      integralFBz +=
        FUSION_KI *
        ez *
        dt;


      gx += integralFBx;
      gy += integralFBy;
      gz += integralFBz;
    }


    gx +=
      FUSION_KP *
      ex;

    gy +=
      FUSION_KP *
      ey;

    gz +=
      FUSION_KP *
      ez;
  }


  float halfDt =
    0.5f *
    dt;


  float qa = q0;
  float qb = q1;
  float qc = q2;
  float qd = q3;


  q0 +=
    (
      -qb * gx -
      qc * gy -
      qd * gz
    )
    *
    halfDt;


  q1 +=
    (
      qa * gx +
      qc * gz -
      qd * gy
    )
    *
    halfDt;


  q2 +=
    (
      qa * gy -
      qb * gz +
      qd * gx
    )
    *
    halfDt;


  q3 +=
    (
      qa * gz +
      qb * gy -
      qc * gx
    )
    *
    halfDt;


  float norm =
    sqrt(
      q0*q0 +
      q1*q1 +
      q2*q2 +
      q3*q3
    );


  if (norm > 0.00001f)
  {
    q0 /= norm;
    q1 /= norm;
    q2 /= norm;
    q3 /= norm;
  }
}


// =====================================================
// FUSED BODY ANGLE
// =====================================================

float calculateFusedBodyAngle()
{
  float gx =
    2.0f *
    (
      q1 * q3 -
      q0 * q2
    );


  float gy =
    2.0f *
    (
      q0 * q1 +
      q2 * q3
    );


  float gz =
    q0*q0 -
    q1*q1 -
    q2*q2 +
    q3*q3;


  float dot =
      gx * referenceAx
    + gy * referenceAy
    + gz * referenceAz;


  dot =
    constrain(
      dot,
      -1.0f,
      1.0f
    );


  return
    acos(dot) *
    180.0f /
    PI;
}


// =====================================================
// POSITION NAME
// =====================================================

const char* positionName(
  PositionState state
)
{
  switch (state)
  {
    case POSITION_UPRIGHT:
      return "UPRIGHT";

    case POSITION_LYING:
      return "LYING";

    case POSITION_WALKING:
      return "WALKING";

    default:
      return "UNKNOWN";
  }
}


// =====================================================
// POSITION CONFIRMATION
// =====================================================

void updatePositionState(
  PositionState newCandidate,
  unsigned long confirmTime
)
{
  if (
    newCandidate !=
    candidatePosition
  )
  {
    candidatePosition =
      newCandidate;

    candidateStartTime =
      millis();

    return;
  }


  if (
    millis() -
    candidateStartTime >=
    confirmTime
  )
  {
    currentPosition =
      candidatePosition;
  }
}


// =====================================================
// POSITION UPDATE
// =====================================================

void updatePosition()
{
  if (!readMPU())
  {
    return;
  }


  unsigned long nowMicros =
    micros();


  if (lastFusionMicros == 0)
  {
    lastFusionMicros =
      nowMicros;

    return;
  }


  float dt =
    (
      nowMicros -
      lastFusionMicros
    )
    /
    1000000.0f;


  lastFusionMicros =
    nowMicros;


  if (
    dt <= 0.0f ||
    dt > 0.05f
  )
  {
    dt = 0.01f;
  }


  // ACCEL
  float ax =
    rawAx /
    ACCEL_SCALE;

  float ay =
    rawAy /
    ACCEL_SCALE;

  float az =
    rawAz /
    ACCEL_SCALE;


  // GYRO
  float gx =
    rawGx /
    GYRO_SCALE -
    gyroBiasX;

  float gy =
    rawGy /
    GYRO_SCALE -
    gyroBiasY;

  float gz =
    rawGz /
    GYRO_SCALE -
    gyroBiasZ;


  // Deadband
  if (fabs(gx) < 0.5f)
  {
    gx = 0;
  }

  if (fabs(gy) < 0.5f)
  {
    gy = 0;
  }

  if (fabs(gz) < 0.5f)
  {
    gz = 0;
  }


  // SOFTWARE ACCEL FILTER
  updateAccelLowPass(
    ax,
    ay,
    az,
    dt
  );


  // SENSOR FUSION
  updateFusion(
    gx,
    gy,
    gz,
    filteredAx,
    filteredAy,
    filteredAz,
    dt
  );


  // BODY ANGLE
  latestBodyAngle =
    calculateFusedBodyAngle();


  // ==================================================
  // WALKING
  // ==================================================

  float gyroMagnitude =
    sqrt(
      gx * gx +
      gy * gy +
      gz * gz
    );


  float accelMagnitude =
    sqrt(
      ax * ax +
      ay * ay +
      az * az
    );


  float accelDelta =
    fabs(
      accelMagnitude -
      1.0f
    );


  int activeGyroAxes = 0;


  if (
    fabs(gx) >
    WALK_AXIS_THRESHOLD
  )
  {
    activeGyroAxes++;
  }


  if (
    fabs(gy) >
    WALK_AXIS_THRESHOLD
  )
  {
    activeGyroAxes++;
  }


  if (
    fabs(gz) >
    WALK_AXIS_THRESHOLD
  )
  {
    activeGyroAxes++;
  }


  bool allAxesMoving =
    (
      fabs(gx) >
      WALK_AXIS_THRESHOLD
      &&
      fabs(gy) >
      WALK_AXIS_THRESHOLD
      &&
      fabs(gz) >
      WALK_AXIS_THRESHOLD
    );


  bool walkingCandidate =
    (
      (
        activeGyroAxes >= 2
        &&
        gyroMagnitude >
        WALK_GYRO_THRESHOLD
        &&
        accelDelta >
        WALK_ACCEL_DELTA
      )

      ||

      (
        allAxesMoving
        &&
        gyroMagnitude >
        WALK_GYRO_THRESHOLD
      )
    );


  PositionState detectedCandidate =
    POSITION_UNKNOWN;


  unsigned long confirmTime =
    1000;


  if (walkingCandidate)
  {
    detectedCandidate =
      POSITION_WALKING;

    confirmTime =
      WALK_CONFIRM_MS;
  }


  else if (
    latestBodyAngle >=
    LYING_MIN_ANGLE
  )
  {
    detectedCandidate =
      POSITION_LYING;

    confirmTime =
      LYING_CONFIRM_MS;
  }


  else if (
    latestBodyAngle <=
    UPRIGHT_MAX_ANGLE
  )
  {
    detectedCandidate =
      POSITION_UPRIGHT;

    confirmTime =
      UPRIGHT_CONFIRM_MS;
  }


  else
  {
    if (
      currentPosition !=
      POSITION_UNKNOWN
    )
    {
      detectedCandidate =
        currentPosition;
    }

    else
    {
      detectedCandidate =
        POSITION_UPRIGHT;
    }


    confirmTime =
      500;
  }


  updatePositionState(
    detectedCandidate,
    confirmTime
  );
}


// =====================================================
// TEMPERATURE
// =====================================================

void updateTemperature()
{
  unsigned long now =
    millis();


  if (
    !tempConversionRunning
    &&
    now - lastTempRequest >= 1000
  )
  {
    lastTempRequest = now;


    OneWireNg::ErrorCode ec =
      ds18b20.convertTempAll(
        0,
        false
      );


    if (
      ec ==
      OneWireNg::EC_SUCCESS
    )
    {
      tempConversionRunning = true;

      tempConversionStart = now;
    }

    else
    {
      temperatureValid = false;
    }
  }


  if (
    tempConversionRunning
    &&
    now - tempConversionStart >=
    TEMP_CONVERSION_TIME
  )
  {
    tempConversionRunning = false;


    OneWireNg::ErrorCode ec =
      ds18b20.readScratchpadSingle(
        scratchpad,
        false
      );


    if (
      ec ==
      OneWireNg::EC_SUCCESS
    )
    {
      DSTherm::Scratchpad *sp =
        scratchpad;


      float temp =
        sp->getTemp() /
        1000.0f;


      if (
        temp > -20.0f &&
        temp < 80.0f
      )
      {
        latestTemperature = temp;

        temperatureValid = true;
      }

      else
      {
        temperatureValid = false;
      }
    }

    else
    {
      temperatureValid = false;
    }
  }
}


// =====================================================
// EMG ENVELOPE
// =====================================================

int getEMGEnvelope(
  int abs_emg
)
{
  emgSum -=
    emgCircularBuffer[
      emgDataIndex
    ];


  emgSum +=
    abs_emg;


  emgCircularBuffer[
    emgDataIndex
  ] =
    abs_emg;


  emgDataIndex =
    (
      emgDataIndex + 1
    )
    %
    EMG_BUFFER_SIZE;


  return
    (
      emgSum /
      EMG_BUFFER_SIZE
    )
    *
    2;
}


// =====================================================
// EMG FILTER
// =====================================================

float EMGFilter(float input)
{
  float output = input;


  {
    static float z1 = 0;
    static float z2 = 0;

    float x =
      output -
      0.05159732 * z1 -
      0.36347401 * z2;

    output =
      0.01856301 * x +
      0.03712602 * z1 +
      0.01856301 * z2;

    z2 = z1;
    z1 = x;
  }


  {
    static float z1 = 0;
    static float z2 = 0;

    float x =
      output -
      (-0.53945795 * z1) -
      0.39764934 * z2;

    output =
      x -
      2.0f * z1 +
      z2;

    z2 = z1;
    z1 = x;
  }


  {
    static float z1 = 0;
    static float z2 = 0;

    float x =
      output -
      0.47319594 * z1 -
      0.70744137 * z2;

    output =
      x +
      2.0f * z1 +
      z2;

    z2 = z1;
    z1 = x;
  }


  {
    static float z1 = 0;
    static float z2 = 0;

    float x =
      output -
      (-1.00211112 * z1) -
      0.74520226 * z2;

    output =
      x -
      2.0f * z1 +
      z2;

    z2 = z1;
    z1 = x;
  }


  return output;
}


// =====================================================
// EMG UPDATE
// =====================================================

void updateEMG()
{
  unsigned long now =
    micros();


  const unsigned long interval =
    1000000UL /
    EMG_SAMPLE_RATE;


  if (
    now - lastEMGSample >=
    interval
  )
  {
    lastEMGSample +=
      interval;


    int rawADC =
      analogRead(
        EMG_PIN
      );


    // 12-bit -> 10-bit
    int sensorValue =
      rawADC >>
      2;


    float filteredSignal =
      EMGFilter(
        sensorValue
      );


    int rectifiedSignal =
      abs(
        (int)filteredSignal
      );


    latestEMG =
      getEMGEnvelope(
        rectifiedSignal
      );
  }
}


// =====================================================
// SETUP
// =====================================================

void setup()
{
  Serial.begin(
    115200
  );


  delay(1000);


  // BUTTON
  pinMode(
    MODE_BUTTON_PIN,
    INPUT_PULLUP
  );


  // EMG
  analogReadResolution(
    12
  );


  pinMode(
    EMG_PIN,
    INPUT
  );


  for (
    int i = 0;
    i < EMG_BUFFER_SIZE;
    i++
  )
  {
    emgCircularBuffer[i] = 0;
  }


  // MOTOR
  ledcAttach(
    MOTOR_PIN,
    MOTOR_PWM_FREQ,
    MOTOR_PWM_RESOLUTION
  );


  setMotorPWM(0);


  // HEATER
  pinMode(
    HEATER_PIN,
    OUTPUT
  );


  heaterOn = false;


  digitalWrite(
    HEATER_PIN,
    LOW
  );


  // MPU
  if (!initializeMPU())
  {
    Serial.println(
      "MPU6500 INITIALIZATION FAILED"
    );


    while (true)
    {
      disableActuators();

      delay(100);
    }
  }


  // CALIBRATION
  calibrateMPU();


  currentPosition =
    POSITION_UPRIGHT;

  candidatePosition =
    POSITION_UPRIGHT;

  candidateStartTime =
    millis();


  // BLE
  NimBLEDevice::init(
    "Her Comfort"
  );


  NimBLEServer *server =
    NimBLEDevice::createServer();


  NimBLEService *service =
    server->createService(
      SERVICE_UUID
    );


  sensorCharacteristic =
    service->createCharacteristic(
      SENSOR_CHAR_UUID,

      NIMBLE_PROPERTY::READ |
      NIMBLE_PROPERTY::NOTIFY
    );


  service->start();


  NimBLEAdvertising *advertising =
    NimBLEDevice::getAdvertising();


  advertising->addServiceUUID(
    SERVICE_UUID
  );


  advertising->start();


  Serial.println();

  Serial.println(
    "HER COMFORT BLE READY"
  );


  Serial.println(
    "MOTOR MODE: OFF"
  );


  Serial.println(
    "HEATER: WAITING FOR TEMPERATURE"
  );


  Serial.println(
    "EMG: D1 / GPIO1 READY"
  );


  Serial.println(
    "MPU SENSOR FUSION: ACTIVE"
  );
}


// =====================================================
// LOOP
// =====================================================

void loop()
{
  // EMG
  updateEMG();


  // BUTTON
  updateMotorButton();


  // MOTOR
  updateMotor();


  // TEMPERATURE
  updateTemperature();


  // HEATER
  updateHeater();


  // MPU
  updatePosition();


  // ==================================================
  // BLE + SERIAL
  // ==================================================

  if (
    millis() -
    lastSend >=
    SEND_INTERVAL
  )
  {
    lastSend =
      millis();


    String data = "{";


    // Temperature
    data +=
      "\"temperature\":";

    data +=
      String(
        latestTemperature,
        2
      );


    // Position
    data +=
      ",\"position\":\"";

    data +=
      positionName(
        currentPosition
      );

    data +=
      "\"";


    // Body Angle
    data +=
      ",\"bodyAngle\":";

    data +=
      String(
        latestBodyAngle,
        1
      );


    // Motor
    data +=
      ",\"motorMode\":\"";

    data +=
      motorModeName();

    data +=
      "\"";


    // Heater
    data +=
      ",\"heater\":\"";

    data +=
      heaterOn ?
      "ON" :
      "OFF";

    data +=
      "\"";


    // EMG
    data +=
      ",\"emg\":";

    data +=
      String(
        latestEMG
      );


    data +=
      "}";


    sensorCharacteristic->setValue(
      data.c_str()
    );


    sensorCharacteristic->notify();


    Serial.println(
      data
    );
  }
}