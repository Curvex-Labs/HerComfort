#include <Arduino.h>
#include <Wire.h>
#include <math.h>

// =====================================================
// HER COMFORT - XIAO ESP32-C6 PIN MAP
// =====================================================

#define MODE_BUTTON_PIN 0     // D0 / GPIO0

#define SDA_PIN 22            // D4 / GPIO22
#define SCL_PIN 23            // D5 / GPIO23

#define MOTOR_PIN 17          // D7 / GPIO17
#define TEMP_PIN 19           // D8 / GPIO19
#define HEATER_PIN 18         // D10 / GPIO18


// =====================================================
// MPU6500 REGISTERS
// =====================================================

#define MPU_ADDR 0x68

#define MPU_WHO_AM_I       0x75
#define MPU_PWR_MGMT_1     0x6B
#define MPU_CONFIG         0x1A
#define MPU_GYRO_CONFIG    0x1B
#define MPU_ACCEL_CONFIG   0x1C
#define MPU_ACCEL_XOUT_H   0x3B

#define MPU6500_ID 0x70


// =====================================================
// SENSOR SCALE
// =====================================================

// ±2g
const float ACCEL_SCALE = 16384.0f;

// ±250 degrees/sec
const float GYRO_SCALE = 131.0f;


// =====================================================
// CALIBRATION
// =====================================================

const int CALIBRATION_SAMPLES = 1000;

float gyroBiasX = 0;
float gyroBiasY = 0;
float gyroBiasZ = 0;

float referenceAx = 0;
float referenceAy = 0;
float referenceAz = 0;

bool calibrationRunning = false;


// =====================================================
// POSITION THRESHOLDS
//
// THESE ARE TEST VALUES.
// Tune them later using your real belt data.
// =====================================================

// 0 to 45 degrees from calibrated upright
// will be treated as upright candidate.
const float UPRIGHT_MAX_ANGLE = 45.0f;

// 70 degrees or more from calibrated upright
// will be treated as lying candidate.
const float LYING_MIN_ANGLE = 70.0f;


// =====================================================
// WALKING THRESHOLDS
// =====================================================

// Minimum total gyro movement
const float WALK_GYRO_THRESHOLD = 12.0f;

// Acceleration must differ from normal 1g
// by at least this amount.
const float WALK_ACCEL_DELTA = 0.06f;

// Optional axis activity threshold
const float WALK_AXIS_THRESHOLD = 6.0f;


// =====================================================
// CONFIRMATION TIMES
// =====================================================

const unsigned long UPRIGHT_CONFIRM_MS = 1000;
const unsigned long LYING_CONFIRM_MS   = 1500;
const unsigned long WALK_CONFIRM_MS    = 800;


// =====================================================
// RAW SENSOR VALUES
// =====================================================

int16_t rawAx = 0;
int16_t rawAy = 0;
int16_t rawAz = 0;

int16_t rawGx = 0;
int16_t rawGy = 0;
int16_t rawGz = 0;


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
// MPU REGISTER WRITE
// =====================================================

bool writeMPURegister(
  uint8_t reg,
  uint8_t value
)
{
  Wire.beginTransmission(MPU_ADDR);

  Wire.write(reg);
  Wire.write(value);

  return Wire.endTransmission() == 0;
}


// =====================================================
// MPU REGISTER READ
// =====================================================

uint8_t readMPURegister(
  uint8_t reg
)
{
  Wire.beginTransmission(MPU_ADDR);

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
// INITIALIZE MPU6500
// =====================================================

bool initializeMPU()
{
  Serial.println();
  Serial.println("Initializing MPU6500...");

  Wire.begin(
    SDA_PIN,
    SCL_PIN
  );

  Wire.setClock(
    400000
  );

  delay(150);


  // WHO_AM_I
  uint8_t whoAmI =
    readMPURegister(
      MPU_WHO_AM_I
    );

  Serial.print("WHO_AM_I = 0x");
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


  // Wake sensor
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


  // Accelerometer ±2g
  writeMPURegister(
    MPU_ACCEL_CONFIG,
    0x00
  );


  // Gyroscope ±250 deg/sec
  writeMPURegister(
    MPU_GYRO_CONFIG,
    0x00
  );


  // Low pass filter
  writeMPURegister(
    MPU_CONFIG,
    0x03
  );

  delay(100);

  Serial.println(
    "MPU6500 initialized."
  );

  return true;
}


// =====================================================
// READ MPU6500
// =====================================================

bool readMPU()
{
  Wire.beginTransmission(MPU_ADDR);

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


  if (
    received != 14
  )
  {
    return false;
  }


  rawAx =
    ((int16_t)Wire.read() << 8)
    |
    Wire.read();


  rawAy =
    ((int16_t)Wire.read() << 8)
    |
    Wire.read();


  rawAz =
    ((int16_t)Wire.read() << 8)
    |
    Wire.read();


  // Skip MPU internal temperature
  Wire.read();
  Wire.read();


  rawGx =
    ((int16_t)Wire.read() << 8)
    |
    Wire.read();


  rawGy =
    ((int16_t)Wire.read() << 8)
    |
    Wire.read();


  rawGz =
    ((int16_t)Wire.read() << 8)
    |
    Wire.read();


  return true;
}


// =====================================================
// SAFETY - KEEP ACTUATORS OFF DURING CALIBRATION
// =====================================================

void disableActuators()
{
  digitalWrite(
    MOTOR_PIN,
    LOW
  );

  digitalWrite(
    HEATER_PIN,
    LOW
  );
}


// =====================================================
// CALIBRATE MPU6500
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
    "Wear the belt normally."
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


  // Discard first readings
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


    if (
      !readMPU()
    )
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

      Serial.println(
        "%"
      );
    }


    delay(4);
  }


  // ==================================================
  // GYRO BIAS
  // ==================================================

  gyroBiasX =
    gxSum /
    validSamples;

  gyroBiasY =
    gySum /
    validSamples;

  gyroBiasZ =
    gzSum /
    validSamples;


  // ==================================================
  // UPRIGHT GRAVITY REFERENCE
  // ==================================================

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


  if (
    magnitude > 0.1f
  )
  {
    referenceAx /=
      magnitude;

    referenceAy /=
      magnitude;

    referenceAz /=
      magnitude;
  }


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

  Serial.print(
    ", "
  );

  Serial.print(
    gyroBiasY,
    3
  );

  Serial.print(
    ", "
  );

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

  Serial.print(
    ", "
  );

  Serial.print(
    referenceAy,
    3
  );

  Serial.print(
    ", "
  );

  Serial.println(
    referenceAz,
    3
  );


  Serial.println();

  calibrationRunning = false;
}


// =====================================================
// CALCULATE ANGLE FROM UPRIGHT
// =====================================================

float calculateBodyAngle(
  float ax,
  float ay,
  float az
)
{
  float magnitude =
    sqrt(
      ax * ax +
      ay * ay +
      az * az
    );


  if (
    magnitude <
    0.1f
  )
  {
    return 0;
  }


  ax /= magnitude;
  ay /= magnitude;
  az /= magnitude;


  float dot =
      ax * referenceAx
    + ay * referenceAy
    + az * referenceAz;


  dot =
    constrain(
      dot,
      -1.0f,
      1.0f
    );


  return (
    acos(dot)
    *
    180.0f
    /
    PI
  );
}


// =====================================================
// POSITION NAME
// =====================================================

const char* positionName(
  PositionState state
)
{
  switch (
    state
  )
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
// READ AND CLASSIFY POSITION
// =====================================================

void updatePosition()
{
  if (
    !readMPU()
  )
  {
    Serial.println(
      "POSITION: SENSOR ERROR"
    );

    return;
  }


  // ==================================================
  // ACCEL VALUES
  // ==================================================

  float ax =
    rawAx /
    ACCEL_SCALE;

  float ay =
    rawAy /
    ACCEL_SCALE;

  float az =
    rawAz /
    ACCEL_SCALE;


  // ==================================================
  // CALIBRATED GYRO
  // ==================================================

  float gx =
    rawGx /
    GYRO_SCALE
    -
    gyroBiasX;

  float gy =
    rawGy /
    GYRO_SCALE
    -
    gyroBiasY;

  float gz =
    rawGz /
    GYRO_SCALE
    -
    gyroBiasZ;


  // Deadband
  if (
    fabs(gx) < 0.5f
  )
  {
    gx = 0;
  }

  if (
    fabs(gy) < 0.5f
  )
  {
    gy = 0;
  }

  if (
    fabs(gz) < 0.5f
  )
  {
    gz = 0;
  }


  // ==================================================
  // BODY ANGLE
  // ==================================================

  float bodyAngle =
    calculateBodyAngle(
      ax,
      ay,
      az
    );


  // ==================================================
  // TOTAL GYRO MOVEMENT
  // ==================================================

  float gyroMagnitude =
    sqrt(
      gx * gx +
      gy * gy +
      gz * gz
    );


  // ==================================================
  // ACCEL MAGNITUDE
  // ==================================================

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


  // ==================================================
  // NUMBER OF ACTIVE GYRO AXES
  // ==================================================

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


  // ==================================================
  // WALKING CANDIDATE
  // ==================================================

  bool walkingCandidate =
    (
      activeGyroAxes >= 2
      &&
      gyroMagnitude >
      WALK_GYRO_THRESHOLD
      &&
      accelDelta >
      WALK_ACCEL_DELTA
    );


  // ==================================================
  // DECIDE CANDIDATE POSITION
  // ==================================================

  PositionState detectedCandidate =
    POSITION_UNKNOWN;

  unsigned long confirmTime =
    1000;


  // --------------------------------------------------
  // WALKING HAS HIGHEST PRIORITY
  // --------------------------------------------------

  if (
    walkingCandidate
  )
  {
    detectedCandidate =
      POSITION_WALKING;

    confirmTime =
      WALK_CONFIRM_MS;
  }


  // --------------------------------------------------
  // LYING
  // --------------------------------------------------

  else if (
    bodyAngle >=
    LYING_MIN_ANGLE
  )
  {
    detectedCandidate =
      POSITION_LYING;

    confirmTime =
      LYING_CONFIRM_MS;
  }


  // --------------------------------------------------
  // UPRIGHT
  // --------------------------------------------------

  else if (
    bodyAngle <=
    UPRIGHT_MAX_ANGLE
  )
  {
    detectedCandidate =
      POSITION_UPRIGHT;

    confirmTime =
      UPRIGHT_CONFIRM_MS;
  }


  // --------------------------------------------------
  // TRANSITION AREA
  //
  // 45 to 70 degrees:
  // keep previous confirmed position
  // instead of constantly switching.
  // --------------------------------------------------

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


  // ==================================================
  // TIME CONFIRMATION
  // ==================================================

  updatePositionState(
    detectedCandidate,
    confirmTime
  );


  // ==================================================
  // CLEAN SERIAL OUTPUT
  // ==================================================

  Serial.print(
    "POSITION: "
  );

  Serial.println(
    positionName(
      currentPosition
    )
  );
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


  // Button reserved
  pinMode(
    MODE_BUTTON_PIN,
    INPUT_PULLUP
  );


  // Motor
  pinMode(
    MOTOR_PIN,
    OUTPUT
  );

  digitalWrite(
    MOTOR_PIN,
    LOW
  );


  // Heater
  pinMode(
    HEATER_PIN,
    OUTPUT
  );

  digitalWrite(
    HEATER_PIN,
    LOW
  );


  // Initialize MPU6500
  if (
    !initializeMPU()
  )
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


  // Calibrate while worn upright
  calibrateMPU();


  currentPosition =
    POSITION_UPRIGHT;

  candidatePosition =
    POSITION_UPRIGHT;

  candidateStartTime =
    millis();
}


// =====================================================
// LOOP
// =====================================================

void loop()
{
  if (
    calibrationRunning
  )
  {
    disableActuators();

    return;
  }


  updatePosition();


  // 10 classifications per second
  delay(100);
}