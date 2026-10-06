/*
  ================================================================
  HER COMFORT — MPU6500 POSITION & POSTURE ENGINEERING TEST FIRMWARE
  ================================================================
  Board:   Seeed Studio XIAO ESP32-C6
  IMU:     MPU6500 (I2C, addr 0x68)
  Pins:    SDA = D4 / GPIO22   SCL = D5 / GPIO23

  PURPOSE
  -------
  This is a CALIBRATION / VALIDATION tool, not a finished product.
  It answers one engineering question: can a single waist-mounted
  MPU6500 reliably separate Lying / Upright / Moving / Still, and
  can trunk-tilt posture be tracked over time?

  It does NOT measure spinal pressure, disc load, or any clinical
  quantity. All posture output is a relative, engineer-defined
  "Posture Load" indicator only — see calculatePostureLoad().

  DATA FLOW
  ---------
  MPU6500
    -> readMPU()                 raw accel/gyro registers
    -> calibrateGyro()           one-time zero-offset removal
    -> low-pass on accel         light smoothing on ax/ay/az
    -> calculateOrientation()    complementary filter -> pitch/roll
    -> updateMotionState()       gyro/accel magnitude -> moving/still
    -> classifyPosition()        candidate Lying/Upright/Moving/Still
    -> confirmPosition()         time-confirmed state (anti-flicker)
    -> classifyPosture()         deviation from calibrated neutral
    -> updatePostureTimer()      duration + hysteresis on posture
    -> calculatePostureLoad()    tilt x duration x movement factor
    -> printSerialData()         CSV line + event logs over Serial

  SERIAL COMMAND PROTOCOL (send as text line ending with \n)
  ------------------------------------------------------------
    CAL_GYRO         re-run gyro zero-offset calibration (keep still)
    CAL_NEUTRAL       calibrate neutral posture (hold correct posture)
    RESET             reset all duration timers and posture load
    START              begin logging/test run
    STOP                pause logging/test run
    MARK:SITTING        write an activity marker line to Serial
    MARK:STANDING
    MARK:LYING
    MARK:WALKING
    MARK:FORWARD_BEND
    MARK:LEAN_LEFT
    MARK:LEAN_RIGHT

  These are the same commands the companion web test-interface sends
  when you press its buttons.

  !!! ALL THRESHOLDS BELOW ARE PLACEHOLDERS !!!
  They are starting points only. Use the CSV log + MARK: events to
  compare known ground-truth activities against sensor output, then
  tune the CONFIGURATION block until states track reliably for you.
*/

#include <Wire.h>

// ================================================================
// PIN / DEVICE CONFIGURATION
// ================================================================
#define SDA_PIN 22          // D4
#define SCL_PIN 23          // D5
#define MPU_ADDR 0x68

// MPU6500 registers (compatible with MPU60x0 map)
#define REG_PWR_MGMT_1   0x6B
#define REG_SMPLRT_DIV   0x19
#define REG_CONFIG       0x1A
#define REG_GYRO_CONFIG  0x1B
#define REG_ACCEL_CONFIG 0x1C
#define REG_ACCEL_XOUT_H 0x3B
#define REG_WHO_AM_I     0x75

// Sensitivity for configured full-scale ranges.
// Accel is configured to +-2g -> 16384 LSB/g.
// Gyro is configured to +-250 dps -> 131 LSB/(deg/s), as specified.
const float ACCEL_LSB_PER_G   = 16384.0f;
const float GYRO_LSB_PER_DPS  = 131.0f;

// ================================================================
// CONFIGURATION — ALL VALUES BELOW ARE PLACEHOLDERS.
// They must be tuned against real recorded data (see MARK: events
// and the CSV log). None of these are medically validated.
// ================================================================

// --- Sensor fusion ---
const float COMPLEMENTARY_ALPHA = 0.98f;   // weight on gyro-integrated angle vs accel angle
const float ACCEL_LPF_ALPHA     = 0.20f;   // low-pass smoothing applied to raw accel (0=no update,1=no filtering)
const float GYRO_DEADBAND_DPS   = 1.5f;    // below this, treat gyro axis as zero (removes drift/noise)

// --- Motion / stillness detection ---
const float MOTION_GYRO_THRESHOLD_DPS = 20.0f;  // gyro magnitude above this => "moving"
const float MOTION_ACCEL_THRESHOLD_G  = 0.25f;  // |accel_mag - 1g| above this => "moving"
const float STILL_ACCEL_BAND_G        = 0.08f;  // |accel_mag - 1g| within this AND low gyro => "still"

// --- Position (Lying vs Upright) via total tilt from vertical ---
// tiltMag = sqrt(pitch^2 + roll^2), degrees, 0 = perfectly upright reference
const float UPRIGHT_TILT_MAX_DEG = 35.0f;   // tiltMag below this => candidate UPRIGHT
const float LYING_TILT_MIN_DEG   = 55.0f;   // tiltMag above this => candidate LYING
// Between UPRIGHT_TILT_MAX_DEG and LYING_TILT_MIN_DEG is an intentional
// "ambiguous band" — in that zone position holds its last confirmed value
// rather than guessing, to avoid flicker on real bodies bending/sitting.

const uint32_t POSITION_CONFIRM_MS = 3000; // candidate must hold for this long before becoming confirmed

// --- Posture (deviation from calibrated neutral) ---
const float GOOD_ANGLE_LIMIT_DEG  = 8.0f;   // deviation below this => GOOD
const float MILD_ANGLE_LIMIT_DEG  = 18.0f;  // deviation below this (and above GOOD) => MILD_LEAN
                                             // deviation above this => NEEDS_ATTENTION candidate
const uint32_t POSTURE_CONFIRM_MS = 3000;   // candidate posture state must hold this long to confirm
const uint32_t POSTURE_WARNING_MS = 60000;  // NEEDS_ATTENTION must persist this long before a warning log fires

// --- Posture Load scoring (engineering indicator only, NOT medical) ---
// PostureLoad accumulates while posture is MILD_LEAN or NEEDS_ATTENTION,
// scaled by tilt severity, and is suppressed while the user is MOVING
// (a transient bend while walking should not count as "sustained load").
const float POSTURE_LOAD_LOW_MAX      = 30.0f;
const float POSTURE_LOAD_MODERATE_MAX = 100.0f;
// above POSTURE_LOAD_MODERATE_MAX => HIGH

// --- Calibration sampling ---
const uint16_t GYRO_CAL_SAMPLES    = 400;  // ~2s at 200Hz, device must be still
const uint16_t NEUTRAL_CAL_SAMPLES = 200;  // ~2s at 200Hz, device in correct neutral posture

// --- Loop timing ---
const uint32_t SAMPLE_INTERVAL_MS = 5;    // ~200 Hz sensor read
const uint32_t PRINT_INTERVAL_MS  = 50;   // ~20 Hz serial CSV output (keep graphs smooth, avoid flooding)

// ================================================================
// STATE ENUMS
// ================================================================
enum PositionState { POS_UNKNOWN, POS_UPRIGHT, POS_LYING, POS_MOVING, POS_STILL };
enum PostureState  { POST_UNKNOWN, POST_GOOD, POST_MILD_LEAN, POST_NEEDS_ATTENTION };
enum LeanDirection { LEAN_NONE, LEAN_FORWARD, LEAN_BACKWARD, LEAN_LEFT, LEAN_RIGHT };

const char* positionName(PositionState s) {
  switch (s) {
    case POS_UPRIGHT: return "UPRIGHT";
    case POS_LYING:    return "LYING";
    case POS_MOVING:   return "MOVING";
    case POS_STILL:    return "STILL";
    default:           return "UNKNOWN";
  }
}
const char* postureName(PostureState s) {
  switch (s) {
    case POST_GOOD:            return "GOOD";
    case POST_MILD_LEAN:       return "MILD_LEAN";
    case POST_NEEDS_ATTENTION: return "NEEDS_ATTENTION";
    default:                   return "UNKNOWN";
  }
}
const char* leanName(LeanDirection d) {
  switch (d) {
    case LEAN_FORWARD:  return "FORWARD";
    case LEAN_BACKWARD: return "BACKWARD";
    case LEAN_LEFT:     return "LEFT";
    case LEAN_RIGHT:    return "RIGHT";
    default:             return "NONE";
  }
}

// ================================================================
// GLOBAL STATE
// ================================================================

// Raw / derived sensor values
float ax_g, ay_g, az_g;          // accel in g, filtered
float gx_dps, gy_dps, gz_dps;    // gyro in deg/s, offset-corrected
float pitch_deg = 0.0f, roll_deg = 0.0f;
float accelMag_g = 1.0f, gyroMag_dps = 0.0f;

// Gyro calibration offsets
float gyroOffsetX = 0.0f, gyroOffsetY = 0.0f, gyroOffsetZ = 0.0f;
bool gyroCalibrated = false;

// Neutral posture reference
float neutralPitch = 0.0f, neutralRoll = 0.0f;
bool neutralCalibrated = false;

// Position state machine
PositionState candidatePosition = POS_UNKNOWN;
PositionState confirmedPosition = POS_UNKNOWN;
uint32_t candidatePositionSince = 0;
uint32_t positionStateSince = 0;      // when confirmedPosition last changed
bool isMoving = false;

// Session duration totals (ms) per confirmed position
uint32_t totalUprightMs = 0, totalLyingMs = 0, totalMovingMs = 0, totalStillMs = 0;

// Posture state machine
PostureState candidatePosture = POST_UNKNOWN;
PostureState confirmedPosture = POST_UNKNOWN;
LeanDirection currentLeanDirection = LEAN_NONE;
uint32_t candidatePostureSince = 0;
uint32_t postureStateSince = 0;
bool needsAttentionWarningFired = false;

float pitchDeviation = 0.0f, rollDeviation = 0.0f;

// Posture load (engineering indicator, resets via RESET command)
float postureLoadScore = 0.0f;

// Test/logging control
bool testRunning = false;

// Timing
uint32_t lastSampleMs = 0;
uint32_t lastPrintMs = 0;
uint32_t lastLoopMs = 0; // for dt in posture load / duration accumulation

// ================================================================
// I2C LOW-LEVEL HELPERS
// ================================================================
void mpuWrite(uint8_t reg, uint8_t val) {
  Wire.beginTransmission(MPU_ADDR);
  Wire.write(reg);
  Wire.write(val);
  Wire.endTransmission();
}

uint8_t mpuReadByte(uint8_t reg) {
  Wire.beginTransmission(MPU_ADDR);
  Wire.write(reg);
  Wire.endTransmission(false);
  Wire.requestFrom((int)MPU_ADDR, 1);
  return Wire.available() ? Wire.read() : 0;
}

// ================================================================
// SETUP
// ================================================================
void mpuInit() {
  mpuWrite(REG_PWR_MGMT_1, 0x00);   // wake up, use internal clock
  delay(50);
  mpuWrite(REG_SMPLRT_DIV, 0x04);   // sample rate divider (gyro output rate / (1+DIV))
  mpuWrite(REG_CONFIG, 0x03);       // DLPF ~44Hz bandwidth, reduces high-freq noise
  mpuWrite(REG_GYRO_CONFIG, 0x00);  // +-250 dps, matches GYRO_LSB_PER_DPS above
  mpuWrite(REG_ACCEL_CONFIG, 0x00); // +-2g, matches ACCEL_LSB_PER_G above
  delay(50);
}

void setup() {
  Serial.begin(115200);
  uint32_t serialWaitStart = millis();
  while (!Serial && millis() - serialWaitStart < 3000) { /* wait briefly for USB CDC */ }

  Wire.begin(SDA_PIN, SCL_PIN);
  Wire.setClock(400000);

  mpuInit();

  uint8_t who = mpuReadByte(REG_WHO_AM_I);
  Serial.print("# MPU6500 WHO_AM_I: 0x");
  Serial.println(who, HEX);

  Serial.println("# Her Comfort MPU6500 Position & Posture Test Firmware");
  Serial.println("# Send CAL_GYRO (keep device still) first, then CAL_NEUTRAL (wear in correct posture).");
  Serial.println("# Commands: CAL_GYRO, CAL_NEUTRAL, RESET, START, STOP, MARK:<label>");

  calibrateGyro();

  lastLoopMs = millis();
  Serial.println(csvHeader());
}

// ================================================================
// MAIN LOOP — millis()-based, no delay() in the hot path
// ================================================================
void loop() {
  uint32_t now = millis();

  handleSerialCommands();

  if (now - lastSampleMs >= SAMPLE_INTERVAL_MS) {
    float dt = (now - lastLoopMs) / 1000.0f;
    if (dt <= 0) dt = SAMPLE_INTERVAL_MS / 1000.0f;
    lastLoopMs = now;
    lastSampleMs = now;

    readMPU();
    calculateOrientation(dt);
    updateMotionState();

    classifyPosition();
    confirmPosition(now);
    accumulatePositionDuration(dt);

    if (neutralCalibrated) {
      classifyPosture();
      confirmPosture(now);
      updatePostureTimer(now);
      calculatePostureLoad(dt);
    }
  }

  if (testRunning && (now - lastPrintMs >= PRINT_INTERVAL_MS)) {
    lastPrintMs = now;
    printSerialData(now);
  }
}

// ================================================================
// 1. readMPU() — read raw registers, convert, remove gyro offset,
//    apply a light low-pass filter to accelerometer.
// ================================================================
void readMPU() {
  Wire.beginTransmission(MPU_ADDR);
  Wire.write(REG_ACCEL_XOUT_H);
  Wire.endTransmission(false);
  Wire.requestFrom((int)MPU_ADDR, 14);

  if (Wire.available() < 14) return; // skip this sample on I2C hiccup

  int16_t rawAx = (Wire.read() << 8) | Wire.read();
  int16_t rawAy = (Wire.read() << 8) | Wire.read();
  int16_t rawAz = (Wire.read() << 8) | Wire.read();
  Wire.read(); Wire.read(); // temperature, unused
  int16_t rawGx = (Wire.read() << 8) | Wire.read();
  int16_t rawGy = (Wire.read() << 8) | Wire.read();
  int16_t rawGz = (Wire.read() << 8) | Wire.read();

  float newAx = rawAx / ACCEL_LSB_PER_G;
  float newAy = rawAy / ACCEL_LSB_PER_G;
  float newAz = rawAz / ACCEL_LSB_PER_G;

  // Light low-pass filter on accel to reduce vibration/step noise
  ax_g = ax_g + ACCEL_LPF_ALPHA * (newAx - ax_g);
  ay_g = ay_g + ACCEL_LPF_ALPHA * (newAy - ay_g);
  az_g = az_g + ACCEL_LPF_ALPHA * (newAz - az_g);

  float newGx = (rawGx / GYRO_LSB_PER_DPS) - gyroOffsetX;
  float newGy = (rawGy / GYRO_LSB_PER_DPS) - gyroOffsetY;
  float newGz = (rawGz / GYRO_LSB_PER_DPS) - gyroOffsetZ;

  // Deadband: kill small residual drift/noise per axis
  gx_dps = (fabs(newGx) < GYRO_DEADBAND_DPS) ? 0.0f : newGx;
  gy_dps = (fabs(newGy) < GYRO_DEADBAND_DPS) ? 0.0f : newGy;
  gz_dps = (fabs(newGz) < GYRO_DEADBAND_DPS) ? 0.0f : newGz;

  accelMag_g = sqrtf(ax_g * ax_g + ay_g * ay_g + az_g * az_g);
  gyroMag_dps = sqrtf(gx_dps * gx_dps + gy_dps * gy_dps + gz_dps * gz_dps);
}

// ================================================================
// 2. calibrateGyro() — device must be stationary on a flat surface.
//    Averages raw gyro to find zero-rate offsets.
// ================================================================
void calibrateGyro() {
  Serial.println("# CAL_GYRO: keep device stationary...");
  double sumX = 0, sumY = 0, sumZ = 0;

  for (uint16_t i = 0; i < GYRO_CAL_SAMPLES; i++) {
    Wire.beginTransmission(MPU_ADDR);
    Wire.write(REG_ACCEL_XOUT_H);
    Wire.endTransmission(false);
    Wire.requestFrom((int)MPU_ADDR, 14);
    if (Wire.available() < 14) { i--; continue; }

    Wire.read(); Wire.read(); Wire.read(); Wire.read(); Wire.read(); Wire.read(); // accel, discard
    Wire.read(); Wire.read(); // temp, discard
    int16_t rawGx = (Wire.read() << 8) | Wire.read();
    int16_t rawGy = (Wire.read() << 8) | Wire.read();
    int16_t rawGz = (Wire.read() << 8) | Wire.read();

    sumX += rawGx / GYRO_LSB_PER_DPS;
    sumY += rawGy / GYRO_LSB_PER_DPS;
    sumZ += rawGz / GYRO_LSB_PER_DPS;
    delay(5);
  }

  gyroOffsetX = sumX / GYRO_CAL_SAMPLES;
  gyroOffsetY = sumY / GYRO_CAL_SAMPLES;
  gyroOffsetZ = sumZ / GYRO_CAL_SAMPLES;
  gyroCalibrated = true;

  Serial.print("# CAL_GYRO done. offsets(dps) x=");
  Serial.print(gyroOffsetX, 3);
  Serial.print(" y=");
  Serial.print(gyroOffsetY, 3);
  Serial.print(" z=");
  Serial.println(gyroOffsetZ, 3);
}

// ================================================================
// 6. calibrateNeutralPosture() — user sits/stands in correct
//    posture, then this establishes reference pitch/roll = "0 dev".
// ================================================================
void calibrateNeutralPosture() {
  Serial.println("# CAL_NEUTRAL: hold correct neutral posture...");
  double sumPitch = 0, sumRoll = 0;
  uint16_t collected = 0;

  for (uint16_t i = 0; i < NEUTRAL_CAL_SAMPLES; i++) {
    readMPU();
    calculateOrientation(SAMPLE_INTERVAL_MS / 1000.0f);
    sumPitch += pitch_deg;
    sumRoll += roll_deg;
    collected++;
    delay(SAMPLE_INTERVAL_MS);
  }

  neutralPitch = sumPitch / collected;
  neutralRoll = sumRoll / collected;
  neutralCalibrated = true;

  Serial.print("# CAL_NEUTRAL done. reference pitch=");
  Serial.print(neutralPitch, 2);
  Serial.print(" roll=");
  Serial.println(neutralRoll, 2);
}

// ================================================================
// 4. calculateOrientation() — complementary filter combining
//    accelerometer tilt angle with integrated gyro rate.
// ================================================================
void calculateOrientation(float dt) {
  // Accelerometer-derived angles (per spec formulas)
  float accelRoll  = atan2f(ay_g, az_g) * 180.0f / PI;
  float accelPitch = atan2f(-ax_g, sqrtf(ay_g * ay_g + az_g * az_g)) * 180.0f / PI;

  // Gyro-integrated angles (short-term accurate, drifts over time)
  float gyroPitch = pitch_deg + gy_dps * dt;
  float gyroRoll  = roll_deg + gx_dps * dt;

  // Complementary fusion: trust gyro short-term, accel long-term
  pitch_deg = COMPLEMENTARY_ALPHA * gyroPitch + (1.0f - COMPLEMENTARY_ALPHA) * accelPitch;
  roll_deg  = COMPLEMENTARY_ALPHA * gyroRoll  + (1.0f - COMPLEMENTARY_ALPHA) * accelRoll;
}

// ================================================================
// updateMotionState() — moving vs still, from gyro + dynamic accel.
// ================================================================
void updateMotionState() {
  float accelDeviation = fabsf(accelMag_g - 1.0f);
  isMoving = (gyroMag_dps > MOTION_GYRO_THRESHOLD_DPS) ||
             (accelDeviation > MOTION_ACCEL_THRESHOLD_G);
}

// ================================================================
// 5. classifyPosition() — candidate Lying/Upright/Moving/Still
//    from total tilt magnitude + motion flag. NOT time-confirmed
//    yet; see confirmPosition().
// ================================================================
void classifyPosition() {
  // Motion takes priority: a "moving" event overrides tilt-based
  // lying/upright classification, since tilt is unreliable mid-motion.
  if (isMoving) {
    candidatePosition = POS_MOVING;
    return;
  }

  float tiltMag = sqrtf(pitch_deg * pitch_deg + roll_deg * roll_deg);

  if (tiltMag <= UPRIGHT_TILT_MAX_DEG) {
    candidatePosition = POS_UPRIGHT;
  } else if (tiltMag >= LYING_TILT_MIN_DEG) {
    candidatePosition = POS_LYING;
  } else {
    // Ambiguous band: neither confidently upright nor lying.
    // If accel magnitude is settled and gyro is quiet, call it STILL
    // rather than guessing a position.
    float accelDeviation = fabsf(accelMag_g - 1.0f);
    if (accelDeviation <= STILL_ACCEL_BAND_G && gyroMag_dps < MOTION_GYRO_THRESHOLD_DPS) {
      candidatePosition = POS_STILL;
    }
    // else: leave candidatePosition unchanged (holds last candidate)
  }
}

// ================================================================
// confirmPosition() — a candidate must persist for
// POSITION_CONFIRM_MS before becoming the confirmed state. This is
// the anti-flicker guard requested in the spec.
// ================================================================
void confirmPosition(uint32_t now) {
  static PositionState lastCandidate = POS_UNKNOWN;

  if (candidatePosition != lastCandidate) {
    candidatePositionSince = now;
    lastCandidate = candidatePosition;
  }

  if (candidatePosition != confirmedPosition &&
      (now - candidatePositionSince) >= POSITION_CONFIRM_MS) {
    Serial.print("POSITION CHANGE: ");
    Serial.print(positionName(confirmedPosition));
    Serial.print(" -> ");
    Serial.println(positionName(candidatePosition));

    confirmedPosition = candidatePosition;
    positionStateSince = now;
  }
}

void accumulatePositionDuration(float dt) {
  uint32_t ms = (uint32_t)(dt * 1000.0f);
  switch (confirmedPosition) {
    case POS_UPRIGHT: totalUprightMs += ms; break;
    case POS_LYING:    totalLyingMs += ms;   break;
    case POS_MOVING:   totalMovingMs += ms;  break;
    case POS_STILL:    totalStillMs += ms;   break;
    default: break;
  }
}

// ================================================================
// 7. classifyPosture() — deviation from calibrated neutral pitch/roll.
// ================================================================
void classifyPosture() {
  pitchDeviation = pitch_deg - neutralPitch;
  rollDeviation = roll_deg - neutralRoll;

  float devMag = sqrtf(pitchDeviation * pitchDeviation + rollDeviation * rollDeviation);

  if (devMag <= GOOD_ANGLE_LIMIT_DEG) {
    candidatePosture = POST_GOOD;
    currentLeanDirection = LEAN_NONE;
  } else {
    // Determine dominant lean direction from whichever deviation is larger
    if (fabsf(pitchDeviation) >= fabsf(rollDeviation)) {
      currentLeanDirection = (pitchDeviation > 0) ? LEAN_FORWARD : LEAN_BACKWARD;
    } else {
      currentLeanDirection = (rollDeviation > 0) ? LEAN_RIGHT : LEAN_LEFT;
    }

    candidatePosture = (devMag <= MILD_ANGLE_LIMIT_DEG) ? POST_MILD_LEAN : POST_NEEDS_ATTENTION;
  }
}

void confirmPosture(uint32_t now) {
  static PostureState lastCandidate = POST_UNKNOWN;

  if (candidatePosture != lastCandidate) {
    candidatePostureSince = now;
    lastCandidate = candidatePosture;
  }

  if (candidatePosture != confirmedPosture &&
      (now - candidatePostureSince) >= POSTURE_CONFIRM_MS) {
    confirmedPosture = candidatePosture;
    postureStateSince = now;
    needsAttentionWarningFired = false;
  }
}

// ================================================================
// updatePostureTimer() — tracks how long NEEDS_ATTENTION has been
// held, and fires a one-shot warning log after POSTURE_WARNING_MS.
// ================================================================
void updatePostureTimer(uint32_t now) {
  if (confirmedPosture == POST_NEEDS_ATTENTION) {
    uint32_t heldMs = now - postureStateSince;
    if (heldMs >= POSTURE_WARNING_MS && !needsAttentionWarningFired) {
      Serial.print("POSTURE WARNING: ");
      Serial.print(leanName(currentLeanDirection));
      Serial.print(" lean for ");
      Serial.print(heldMs / 1000);
      Serial.println("s");
      needsAttentionWarningFired = true;
    }
  }
}

// ================================================================
// calculatePostureLoad() — engineering-only indicator combining
// tilt severity x duration x movement factor. NOT a clinical or
// spinal-pressure measurement — see header comment.
// ================================================================
void calculatePostureLoad(float dt) {
  float devMag = sqrtf(pitchDeviation * pitchDeviation + rollDeviation * rollDeviation);

  // Only accumulate load while posture is sustained (not GOOD) and
  // the user is not actively moving (a transient bend while walking
  // should not count as sustained postural strain).
  if ((confirmedPosture == POST_MILD_LEAN || confirmedPosture == POST_NEEDS_ATTENTION) &&
      confirmedPosition != POS_MOVING) {
    float severity = devMag / MILD_ANGLE_LIMIT_DEG;        // normalized tilt severity
    float movementFactor = 1.0f;                            // could be reduced if micro-movement detected
    postureLoadScore += severity * dt * movementFactor;
  } else if (confirmedPosture == POST_GOOD) {
    // slow decay back toward zero when posture returns to good
    postureLoadScore = fmaxf(0.0f, postureLoadScore - dt * 0.5f);
  }
}

const char* postureLoadLevel() {
  if (postureLoadScore <= POSTURE_LOAD_LOW_MAX) return "LOW";
  if (postureLoadScore <= POSTURE_LOAD_MODERATE_MAX) return "MODERATE";
  return "HIGH";
}

// ================================================================
// SERIAL COMMAND HANDLING
// ================================================================
void handleSerialCommands() {
  if (!Serial.available()) return;

  String cmd = Serial.readStringUntil('\n');
  cmd.trim();
  if (cmd.length() == 0) return;

  if (cmd == "CAL_GYRO") {
    calibrateGyro();
  } else if (cmd == "CAL_NEUTRAL") {
    calibrateNeutralPosture();
  } else if (cmd == "RESET") {
    totalUprightMs = totalLyingMs = totalMovingMs = totalStillMs = 0;
    postureLoadScore = 0.0f;
    needsAttentionWarningFired = false;
    Serial.println("# RESET done: timers and posture load cleared.");
  } else if (cmd == "START") {
    testRunning = true;
    Serial.println("# START: logging enabled.");
  } else if (cmd == "STOP") {
    testRunning = false;
    Serial.println("# STOP: logging paused.");
  } else if (cmd.startsWith("MARK:")) {
    Serial.print("EVENT MARK: ");
    Serial.println(cmd.substring(5));
  } else {
    Serial.print("# Unknown command: ");
    Serial.println(cmd);
  }
}

// ================================================================
// 11. printSerialData() — CSV line matching the header below, plus
//     the human-readable state-change logs printed elsewhere.
// ================================================================
String csvHeader() {
  return "timestamp_ms,ax,ay,az,gx,gy,gz,pitch,roll,gyro_mag,accel_mag,"
         "candidate_position,confirmed_position,posture_status,"
         "pitch_deviation,roll_deviation,lean_direction,posture_load_score,posture_load_level,"
         "upright_ms,lying_ms,moving_ms,still_ms";
}

void printSerialData(uint32_t now) {
  Serial.print(now); Serial.print(',');
  Serial.print(ax_g, 3); Serial.print(',');
  Serial.print(ay_g, 3); Serial.print(',');
  Serial.print(az_g, 3); Serial.print(',');
  Serial.print(gx_dps, 2); Serial.print(',');
  Serial.print(gy_dps, 2); Serial.print(',');
  Serial.print(gz_dps, 2); Serial.print(',');
  Serial.print(pitch_deg, 2); Serial.print(',');
  Serial.print(roll_deg, 2); Serial.print(',');
  Serial.print(gyroMag_dps, 2); Serial.print(',');
  Serial.print(accelMag_g, 3); Serial.print(',');
  Serial.print(positionName(candidatePosition)); Serial.print(',');
  Serial.print(positionName(confirmedPosition)); Serial.print(',');
  Serial.print(neutralCalibrated ? postureName(confirmedPosture) : "UNCALIBRATED"); Serial.print(',');
  Serial.print(pitchDeviation, 2); Serial.print(',');
  Serial.print(rollDeviation, 2); Serial.print(',');
  Serial.print(leanName(currentLeanDirection)); Serial.print(',');
  Serial.print(postureLoadScore, 1); Serial.print(',');
  Serial.print(postureLoadLevel()); Serial.print(',');
  Serial.print(totalUprightMs); Serial.print(',');
  Serial.print(totalLyingMs); Serial.print(',');
  Serial.print(totalMovingMs); Serial.print(',');
  Serial.println(totalStillMs);
}
