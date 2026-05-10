/* DRAX Dome rotator controller                     */
/* Copyright Heather Lomond 2019,2020, 2021, 2022   */
/****************************************************/

/* todo
 * ----
 * write cal routines - just a stub at the moment
 * get opto sensors working for position
 * EEPROM routines tested.
 * do cal
 * sort maths and calibration values
 * sort park position and detection
 * sort API text below
 */

/*-------------------------------------------------------------------------------------*/
/* revision history:                                                                   */
/*    1.0 - March 2020 Initial Code                                                    */
/*    1.1 - Aug   2022 New coding after COVID                                          */
/*    1.2 - Jan   2024 Adding Cal and Park (as opposed to home)                        */
/*-------------------------------------------------------------------------------------*/
/* API Design:                                                                                                 */
/*   available commands:                                                                                       */
/*     CALIBRATE_xxx.x : xxx.x is the azimuth (in degrees) of the centre of the dome opening after a clockwise */
/*                          rotation meets the home sensor. xxx.x stored in EEPROM, ANGLE_HOME                 */
/*                       Need to cal in same direction each time so:                                           */
/*                         rotate anticlockwise until out of home                                              */
/*                         keep rotating for 1 second to be well clear                                         */
/*                         Stop rotation                                                                       */
/*                         start clockwise rotation                                                            */
/*                         on first sense of home reset counter to 0                                           */
/*                         keep couting until out of home, record out of home reading, T_HOME                  */
/*                         continue rotation till first sense of back in home                                  */
/*                         store counts = T_360                                                                */
/*                         stop rotation                                                                       */
/*                         keep counting for 2 seconds (to let motor overrun and calibrate how far it goes)    */
/*                         store counts = T_now                                                                */
/*                         overrun count, T_OVERRUN = T_now - T_360                                            */
/*                       later we will need degrees per pulse = 360/T_360                                      */
/*                         store in EEPROM                                                                     */
/*                         store Overrun, T_OVERRUN, in EEPROM, and store T_HOME in EEPROM                     */
/*                       Also store a calibration check magic number to detect CAL has been done               */
/*     GOTO_xxx.x      : choose shortest path and rotate dome to xxx.x aximuth (degrees)                       */
/*                         after INIT we know where we are so:                                                 */
/*                           calculate shortest direction to new coords                                        */
/*                           rotate to required counts corrected for overrun                                   */
/*                           keep track of counts all the time                                                 */
/*     HOME_           : choose shortest path and home the dome                                                */
/*     STOP_           : stop all motors in whatever mode                                                      */
/*                                                                                                             */
/* Hardware Design:                                                                                            */
/*   We want a buzzer to sound when the dome is about to rotate                                                */
/*   LEDs for status so:                                                                                       */
/*     Error        : Red                                                                                      */
/*     Ready        : Green                                                                                    */
/*     Turn CCW     : Red                                                                                      */
/*     Turn Cw      : Red                                                                                      */
/*     In home      : Yellow                                                                                   */
/*   drive is into an opto coupler:                                                                            */
/*      CCW/CW output: 5V=CW, 0=CCW, setup first before                                                        */
/*      Motor drive  : 5V=motor on, 0v=motor off                                                               */
/*      Buzzer=low power sounder and transitor drive? needs 2kHz output                                        */
/* approx sizes:                                                                                               */
/*      num teeth on rack            : 400                                                                     */
/*      num teeth on pinion          :  10                                                                     */
/*      num teeth on motor drive     :  20                                                                     */
/*      num posiitons per pinion turn:  10                                                                     */
/*    conclusion, use 32 bit storage for numbers                                                               */

/*-------------------------*/
/*----- INCLUDE FILES -----*/
/*-------------------------*/

#include <EEPROM.h>

/*-------------------------*/
/*-------- DEFINES --------*/
/*-------------------------*/

/* Set #DEBUG if we are not using an ascom driver */
#define DEBUG

/* define how long it should take the dome to do a full roation */
#define DOME_FULL_ROTATION_TIME_SECS 100

/*------------Define the EEPROM locations being used ----------------*/
/* number of pulses for full rotation */
#define EEPROM_COUNTS_PER_360  (0*sizeof(long))
/* number of pulses that the motor will overrun */
#define EEPROM_COUNTS_OVERRUN  (1*sizeof(long))
/* the number of counts that the home sensor is active for */
#define EEPROM_COUNTS_IN_HOME  (2*sizeof(long))
/* the angle that the dome is at when in home */
/* note this is stored as 100*angle to make it an integer */
#define EEPROM_HOME_ANGLE_X100 (3*sizeof(long))
/* the angle that the dome is at when in park */
/* note this is stored as 100*angle to make it an integer */
#define EEPROM_PARK_ANGLE_X100 (4*sizeof(long))
/* the magic number to be sure CAL has been done */
#define EEPROM_CAL_CHECK       (5*sizeof(long))

/* define the LED pin connections */
#define LED_CW     2
#define LED_CCW    3
#define LED_READY  4
#define LED_ERROR  5
#define LED_HOME   6
#define LED_ON   1
#define LED_OFF  0

/* On the Driver board, we have opto couplers to control the Motor pins */
#define MOTOR_DIR       A0
#define MOTOR_CTRL      A2
#define MOTOR_ON      1
#define MOTOR_OFF     0
#define MOTOR_DIR_CW  1
#define MOTOR_DIR_CCW 0

/* the buzzer output pin */
/* this wants to be a pin we can set a freq on */
#define BUZZER       12
#define BUZZER_ON  1
#define BUZZER_OFF 0
/* the period of half a wavelength at the buzzer freq (2400Hz) */
#define BUZZER_2400_HZ  208

/* the inputs from the posn sensor and the home sensor */
/* these have to be on interrupts to work properly  */
#define HOME_SENSOR  8

/* The position sensor is an optical sensor and a wheel */
#define POSN_INPUT_A 10
#define POSN_INPUT_B 11

/* the magic number used to tell if CAL has been done or not */
#define CAL_CHECK_NUMBER 1234567

/* internal state machine states */
#define STATE_INIT    1   
#define STATE_STOPPED 2
#define STATE_SLEWING 3
#define STATE_HOMEING 4
#define STATE_PARKING 5
#define STATE_CAL     6
#define STATE_ERROR   7

#define INIT_INIT             0
#define INIT_BUZZED           1
#define INIT_GET_INTO_HOME    2
#define INIT_GET_OUT_OF_HOME  3
#define INIT_2_SECS           4
#define INIT_BACK_INTO_HOME   5

/* the number of mS delay between executions of loop */
#define BACKGROUND_LOOP_TIME  10

/*-------------------------*/
/*---- GLOBAL VARIABLES----*/
/*-------------------------*/

uint8_t state;
uint8_t init_state;

/* these are the calibration values we derive from EEPROM values */
double   cal_degrees_per_pulse;
double   cal_home_angle;
double   cal_park_angle;
uint32_t cal_counts_in_home;
uint32_t cal_counts_overrun;

/* various abgle stores we need */
double goto_angle;
double new_home_angle;  /* angle we are given by cal program for home sensor */
double current_angle;

int32_t t_now;

/* we have various timers that are used to check for error situations */
uint32_t init_timer;
uint32_t homeing_timer;
uint32_t parking_timer;
uint32_t goto_timer;
uint32_t cal_timer;

/*-------------------------*/
/*------- FUNCTIONS -------*/
/*-------------------------*/

/*-------------------------------------------------------------------------------------*/
uint32_t eeprom_read32(uint8_t addr) {
/*-------------------------------------------------------------------------------------*/
/*-------------------------------------------------------------------------------------*/
    uint32_t val;

    EEPROM.get(addr, val);
    return val;
}

/*-------------------------------------------------------------------------------------*/
void eeprom_write32(uint8_t addr, uint32_t val) {
/*-------------------------------------------------------------------------------------*/
/*-------------------------------------------------------------------------------------*/
    EEPROM.put(addr, val); 
}

/*-------------------------------------------------------------------------------------*/
void led_set(uint8_t led, uint8_t led_state) {
/*-------------------------------------------------------------------------------------*/
/*-------------------------------------------------------------------------------------*/
    digitalWrite(led, led_state);
}

/*-------------------------------------------------------------------------------------*/
void motor_set(uint8_t motor, uint8_t motor_state) {
/*-------------------------------------------------------------------------------------*/
/*-------------------------------------------------------------------------------------*/
    digitalWrite(motor, motor_state);
}

/*-------------------------------------------------------------------------------------*/
void buzzer_sound(uint16_t buzz_time) {
/*-------------------------------------------------------------------------------------*/
/* buzz_time: duration of the buzz in mS                                               */
/*-------------------------------------------------------------------------------------*/
    uint32_t buzz_start;
    
    buzz_start = millis();
    do {
      digitalWrite(BUZZER, BUZZER_ON);
      delayMicroseconds(BUZZER_2400_HZ);
      digitalWrite(BUZZER, BUZZER_OFF);
      delayMicroseconds(BUZZER_2400_HZ);
    } while (millis() < buzz_start + buzz_time);
}

/*-------------------------------------------------------------------------------------*/
bool at_home() {
/*-------------------------------------------------------------------------------------*/
/* we need to debounce the magnetic switch                                             */
/*-------------------------------------------------------------------------------------*/
    static uint8_t debounce=0;
  
    if (0 == digitalRead(HOME_SENSOR)) {
        if (debounce >= 2) {
            led_set(LED_HOME, LED_ON);
            return true;
        } else {
            debounce++;
            led_set(LED_HOME, LED_OFF);
            return false;
        }
    } else {
        debounce = 0;
        led_set(LED_HOME, LED_OFF);
        return false;
    }
}

/*-------------------------------------------------------------------------------------*/
bool at_park() {
/*-------------------------------------------------------------------------------------*/
/* we need to debounce the magnetic switch                                             */
/*-------------------------------------------------------------------------------------*/
    bool parked;

    /* we decide we are there if we are within 0.1 degree */
    parked = (fabs(current_angle - cal_park_angle) < 0.1);
    return parked;
}

/*-------------------------------------------------------------------------------------*/
bool cal_check() {
/*-------------------------------------------------------------------------------------*/
/* this checks to see if we have a valid calibration in EEPROM                         */
/* if not then we stop in the error state and do nothing until a cal is demanded       */
/* if we do have a cal then the values are read from the EEPROM and prepped for use    */
/*-------------------------------------------------------------------------------------*/
    uint32_t cal_check_value;
    bool result;
    
    /* now we read out the EEPROM Data */
    /* first, check that we have programmed the eeprom already */
    cal_check_value = eeprom_read32(EEPROM_CAL_CHECK);
    if (CAL_CHECK_NUMBER == cal_check_value) {
        /* if this test passes, we have already calibrated so we can go ahead and get all the numbers out of the EEPROM */
        cal_counts_overrun    = eeprom_read32(EEPROM_COUNTS_OVERRUN);
        cal_counts_in_home    = eeprom_read32(EEPROM_COUNTS_IN_HOME);
        cal_degrees_per_pulse = 360.0 / ((double)eeprom_read32(EEPROM_COUNTS_PER_360));
        cal_home_angle        = ((double)eeprom_read32(EEPROM_HOME_ANGLE_X100)) / 100.0;
        cal_park_angle        = ((double)eeprom_read32(EEPROM_PARK_ANGLE_X100)) / 100.0;
        result = true;
    } else {
        result = false;
        /* we get here if it is a virgin part with no cal */
        /* we just leave the error light on and in the error state */
    }

    return result;
}

/*-------------------------------------------------------------------------------------*/
void check_posn_sensors() {
/*-------------------------------------------------------------------------------------*/
/* Here we process the 2 quadrature position encoder inputs                            */
/*     how fast are pulses coming in?                                                  */
/*     motor runs at 1400 rpm. gearbox ratio is 30:1, number of                        */
/*     pulses per turn of sensor wheel = 6                                             */
/*     so 1 / (1400 / 60 / 30 * 6) = 0.21 seconds per pulse                                  */
/*     so we can poll the sensors and debounce them no problem (no interrupts)         */
/*-------------------------------------------------------------------------------------*/
    static uint8_t debounce_a=0;
    static uint8_t debounce_b=0;
    static uint8_t input_a=0;
    static uint8_t input_b=0;
    static uint8_t old_input_a=0;
  
    /* record the current state of input A to detect transitions */
    old_input_a = input_a;
    
    /* now we debounce the 2 inputs */
    if (1==digitalRead(POSN_INPUT_A)) {
        if (debounce_a == 3) input_a = 1;
        else debounce_a++;
    } else {
        if (debounce_a == 0) input_a = 0;
        else debounce_a--;
    }
    if (1==digitalRead(POSN_INPUT_B)) {
        if (debounce_b == 3) input_b = 1;
        else debounce_b++;
    } else {
        if (debounce_b == 0) input_b = 0;
        else debounce_b--;
    }
    /* we are only interested in a rising edge on one (A) input. Then:      */
    /*   if encoder_b is low, then we are going CC so add one pulse angle   */
    /*   if encoder_b is high then we are going CCW so subtract pulse angle */
    if ((0 == old_input_a) && (1 == input_a)) {
        if (1 == input_b) {
            current_angle += cal_degrees_per_pulse;
            /* need to make sure we stay in 0 to 360 degrees */
            if (current_angle >= 360.0) current_angle -= 360.0;
        }
        else { /* we are going CCW */
            current_angle -= cal_degrees_per_pulse;
            if (current_angle < 0.0) current_angle += 360.0;
        }
    }
//    Serial.print(current_angle);
}

/*-------------------------------------------------------------------------------------*/
void setup() {
/*-------------------------------------------------------------------------------------*/
/* The standard Arduino Setup routine - Called once at the very start of the program   */
/*-------------------------------------------------------------------------------------*/
    /* here we do the basic setup of the dome rotator program */
    /* first we setup the serial port to communicate with the PC based Driver (ASCOM) */
    /* or the serial terminal if we are debugging                                     */
#ifdef DEBUG
    Serial.begin(115200);
#else
    Serial.begin(57000);
#endif
    Serial.flush();

    /* to be safe we will always start in error state and clear it if all goes well */
    state = STATE_ERROR;
    
    /* reset the offset counter to 0 temporarily until we know where we are */
    t_now = 0;

    /* now we set up all the IO and initialise it */
    /* first off, setup and sound the buzzer briefly */
    pinMode(BUZZER,        OUTPUT);
    buzzer_sound(200);

    /* next do the LEDs and do a cycle check on all of them for 0.5S */
    pinMode(LED_READY,     OUTPUT);
    pinMode(LED_CW,        OUTPUT);
    pinMode(LED_CCW,       OUTPUT);
    pinMode(LED_ERROR,     OUTPUT);
    pinMode(LED_HOME,      OUTPUT);
    led_set(LED_READY,     LED_ON);
    led_set(LED_CW,        LED_OFF);
    led_set(LED_CCW,       LED_OFF);
    led_set(LED_ERROR,     LED_OFF);
    led_set(LED_HOME,      LED_OFF);
    delay(500);
    led_set(LED_READY,     LED_OFF);
    led_set(LED_CCW,       LED_ON);
    delay(500);
    led_set(LED_CCW,       LED_OFF);
    led_set(LED_CW,        LED_ON);
    delay(500);
    led_set(LED_CW,        LED_OFF);
    led_set(LED_ERROR,     LED_ON);
    delay(500);
    led_set(LED_ERROR,     LED_OFF);
    led_set(LED_HOME,      LED_ON);
    delay(500);
    led_set(LED_HOME,      LED_OFF);

    pinMode(MOTOR_DIR,     OUTPUT);
    pinMode(MOTOR_CTRL,    OUTPUT);
    motor_set(MOTOR_DIR,   MOTOR_DIR_CW);
    motor_set(MOTOR_CTRL,  MOTOR_OFF);
    
    pinMode(HOME_SENSOR,   INPUT_PULLUP);
    
    pinMode(POSN_INPUT_A,  INPUT);
    pinMode(POSN_INPUT_B,  INPUT);

    if (!cal_check()) {
        state = STATE_ERROR;
        led_set(LED_ERROR, LED_ON);
    } else {
        state = STATE_INIT;
        init_state = INIT_INIT;
        led_set(LED_ERROR, LED_OFF);
    }
}

/*-------------------------------------------------------------------------------------*/
void do_ccw() {
/*-------------------------------------------------------------------------------------*/
/* Starts the motor driving Counter Clockwise (CCW) and sets the LEDS                  */
/*-------------------------------------------------------------------------------------*/
    led_set(LED_CW,  LED_OFF);
    led_set(LED_CCW, LED_ON);
    motor_set(MOTOR_DIR,  MOTOR_DIR_CCW);
    motor_set(MOTOR_CTRL, MOTOR_ON);
}

/*-------------------------------------------------------------------------------------*/
void do_cw() {
/*-------------------------------------------------------------------------------------*/
/* Starts the motor driving Clockwise (CW) and sets the LEDS                           */
/*-------------------------------------------------------------------------------------*/
    led_set(LED_CW,  LED_ON);
    led_set(LED_CCW, LED_OFF);
    motor_set(MOTOR_DIR,  MOTOR_DIR_CW);
    motor_set(MOTOR_CTRL, MOTOR_ON);
}

/*-------------------------------------------------------------------------------------*/
void do_init() {
/*-------------------------------------------------------------------------------------*/
/* here we initialise the dome to a known state and position                           */
/* we do this by:                                                                      */
/*    run buzzer as a warning                                                          */
/*    if we are not in home, rotate CCW until we are in home                           */
/*    keep rotating CCW to get out of home and go on for 2 secs more                   */
/*    stop and rotate CW to go back into home and stop as soon as we are back in       */
/* at this point we will be at the CAL angle for home                                  */
/*-------------------------------------------------------------------------------------*/
    switch (init_state) {
      /* ------------------------------- */
      case INIT_INIT:
        init_timer = millis(); /* we need to time the buzzer */
        delay(200);
        buzzer_sound(500);
        init_state = INIT_BUZZED;
        break;
        
      /* ------------------------------- */
      case INIT_BUZZED:
        /* we have finished buzzing so we can get the dome into home position */
        init_timer = millis();
        /* if we have started in home, then we go straight to GET_OUT_OF_HOME */
        if (at_home()) {
            init_state = INIT_GET_OUT_OF_HOME;
        } else { /* otherwise we go CCW into home */
            init_state = INIT_GET_INTO_HOME;
        }
        /* either way we start going CCW */
        do_ccw();
        break;
        
      /* ------------------------------- */
      case INIT_GET_INTO_HOME:
        /* for safety we check if we have been going for more than a full rotation (based on time) */
        if (millis( ) - init_timer >= DOME_FULL_ROTATION_TIME_SECS * 1000) {
            abort_slew();
            state = STATE_ERROR;
        } else {
            /* if all going ok, then we wait till we are in home */
            if (at_home()) {
                init_timer = millis();
                init_state = INIT_GET_OUT_OF_HOME;
            }
        }
        break;
        
      /* ------------------------------- */
      case INIT_GET_OUT_OF_HOME:
        if (millis() - init_timer < 6000) {
            if (!at_home()) {
                /* we have got out of home so now we go on for 2 seconds */
                init_timer = millis();
                init_state = INIT_2_SECS;
            }
        } else { /* we have timed out, so all bets are off */
            abort_slew();
            state = STATE_ERROR;          
        }
        break;
        
      /* ------------------------------- */
      case INIT_2_SECS:
        if (millis() - init_timer > 2000) {
            /* 2 secs is up so now we go back CW */
            init_timer = millis();
            init_state = INIT_BACK_INTO_HOME;
            do_cw();
        }
        break;
        
      /* ------------------------------- */
      case INIT_BACK_INTO_HOME:
        /* we will go into an infinite loop waiting here but need a safety timeout of 3 seconds */
        while ((!at_home()) && (millis() - init_timer < 6000)) {}
        /* as soon as we got back into home we stop */
        abort_slew();
        if (at_home()) {
            state = STATE_STOPPED;
            /* we know where we are now, so setup the current angle for future ref */
            current_angle = cal_home_angle;
        } else {
            state = STATE_ERROR;
        }
        break;
        
      /* ------------------------------- */
      default:
        state = STATE_ERROR;   /* should never get here */
  }
}

int cal_pulse_counts = 0;

/*-------------------------------------------------------------------------------------*/
void abort_slew() {
/*-------------------------------------------------------------------------------------*/
/* go into the stop state either from a command, or to reset for another state         */
/*-------------------------------------------------------------------------------------*/
    state = STATE_STOPPED;            /* Set state machine to STOPPED */
    motor_set(MOTOR_CTRL, MOTOR_OFF); /* turn off the motor */
                                      /* no need to worry about CW/CCW */
    led_set(LED_CW, LED_OFF);         /* reset the LEDs */
    led_set(LED_CCW,LED_OFF);
}

/*-------------------------------------------------------------------------------------*/
void cal_pulse_counter() {
/*-------------------------------------------------------------------------------------*/
/*-------------------------------------------------------------------------------------*/
    static int old_input_a = 0;
    static int debounce_a = 0;
    int input_a;
    
    /* debounce the input a pin */
    if (1==digitalRead(POSN_INPUT_A)) {
        if (debounce_a == 3) input_a = 1;
        else debounce_a++;
    } else {
        if (debounce_a == 0) input_a = 0;
        else debounce_a--;
    }

    /* we are only interested in a rising edge on one (A) input     */
    if ((0 == old_input_a) && (1 == input_a)) {
        cal_pulse_counts++;
    }
}

/*-------------------------------------------------------------------------------------*/
void do_cal() {
/*-------------------------------------------------------------------------------------*/
/* first we need to get to the home position:                                          */
/*  if in home, turn CCW until out of home                                             */
/*  turn CW. when you go into home start counter.                                      */
/*  keep going, when come out of home record in_home                                   */
/*  when go back into home record counts_per_360                                       */
/*  when next edge, turn off motor                                                     */
/*  record how many more pulses you get (overrrun)                                     */
/*-------------------------------------------------------------------------------------*/
    uint32_t new_counts_overrun;
    uint32_t new_counts_in_home;
    uint32_t new_counts_per_360;
    int wait;

#ifdef DEBUG
    delay(3000);
    Serial.print("2#"); /* send success */
    state = STATE_STOPPED;
    return;
#endif

    /* we want to be sure we have come into home in a CW direction */
    /* so, if we are in home we first get out of it CCW            */
    if (at_home()) {
        do_ccw();
        while (at_home()) {}
        abort_slew();
    }
    delay(1000); /* make sure it has come to a stop before reversing it */
    
    /* now we go into home CW - might be a long time if we are just out of it */
    do_cw();
    while (!at_home()) {};
    
    /* once we are in home, we count the number of pulses in home */
    while (at_home()) {
        cal_pulse_counter();
    }
    new_counts_in_home = cal_pulse_counts;

    /* next we keep going till we go all the way round 360, counting pulses all the time */
    while (!at_home()) {
        cal_pulse_counter();
    };
    new_counts_per_360 = cal_pulse_counts;

    /* now we tell it to stop and count the number of overrun pulses */
    /* note we assume it will stop in less than 1 second             */
    abort_slew();
    for (wait = 0; wait < 1000; wait++) {
        cal_pulse_counter();
        delay(1);
    }    

    /* finally we save all the data to the EEPROM            */ 
    /* along with the home position we have been sent        */
    /* and a magic number so we know we have been calibrated */
    eeprom_write32(EEPROM_COUNTS_OVERRUN,  new_counts_overrun);
    eeprom_write32(EEPROM_COUNTS_IN_HOME,  new_counts_in_home);
    eeprom_write32(EEPROM_COUNTS_PER_360,  new_counts_per_360);
    eeprom_write32(EEPROM_HOME_ANGLE_X100, new_home_angle);
    eeprom_write32(EEPROM_CAL_CHECK,       CAL_CHECK_NUMBER);

    state = STATE_STOPPED;
    Serial.print("2#");  /* we have finished cal with SUCCESS */
}

/*-------------------------------------------------------------------------------------*/
void check_commands() {
/*-------------------------------------------------------------------------------------*/
/* Check for incomin ASCOM command (Note: we include all of them for completness)      */
/*-------------------------------------------------------------------------------------*/
    String cmd;
    String num_string;
    double mod_angle;

    if (Serial.available() > 0) {
        cmd = Serial.readStringUntil('_');
        /* --------------------------------------------------------------------- */
        /* this is a special case and not a standard defined ASCOM command       */
        /* we will call this from the Qt calibration program                     */
        if (cmd == "CALIBRATE") {
            num_string = Serial.readStringUntil('#');
            new_home_angle = num_string.toDouble();
            cal_timer = millis();
            Serial.print("1#");  /* we have started calibration */
            state = STATE_CAL;

        /* --------------------------------------------------------------------- */
        } else if (cmd == "AbortSlew")      {
            /* shut down immediatly no matter what                               */
            /* if we haven't finished init, go into an error state afterwards    */
            /* so can't put the AbortSlew command outside of the if              */
            if (state == STATE_INIT) {
                Serial.print("0#");
                abort_slew();
                state = STATE_ERROR;
            } else {
                Serial.print("1#");
                abort_slew();
            }
        /* --------------------------------------------------------------------- */
        } else if (cmd == "Altitude")       {  /* Altitude not implemented       */
        /* --------------------------------------------------------------------- */
        } else if (cmd == "AtHome")         {
            if (at_home()) Serial.print("1#");
            else           Serial.print("0#");
        /* --------------------------------------------------------------------- */
        } else if (cmd == "AtPark")         {
            if (at_park()) Serial.print("1#");
            else           Serial.print("0#");
        /* --------------------------------------------------------------------- */
        } else if (cmd == "Azimuth")        {
            Serial.print(current_angle);
            Serial.print("#");
        /* --------------------------------------------------------------------- */
        } else if (cmd == "CanFindHome")    {  /* CanFindHome no code needed     */
        /* --------------------------------------------------------------------- */
        } else if (cmd == "CanPark")        {  /* CanPark no code needed         */
        /* --------------------------------------------------------------------- */
        } else if (cmd == "CanSetAltitude") {  /* CanSetAltitude not implemented */
        /* --------------------------------------------------------------------- */
        } else if (cmd == "CanSetAzimuth")  {  /* CanSetAzimuth no code needed   */
        /* --------------------------------------------------------------------- */
        } else if (cmd == "CanSetPark")     {  /* CanSetPark no code needed      */
        /* --------------------------------------------------------------------- */
        } else if (cmd == "CanSetShutter")  {  /* CanSetShutter not implemented  */
        /* --------------------------------------------------------------------- */
        } else if (cmd == "CanSlave")       {  /* CanSlave not implemented       */
        /* --------------------------------------------------------------------- */
        } else if (cmd == "CanSyncAzimuth") {  /* CanSyncAzimuth not implemented */
        /* --------------------------------------------------------------------- */
        } else if (cmd == "CloseShutter")   {  /* CloseShutter not implemented   */
        /* --------------------------------------------------------------------- */
        } else if (cmd == "FindHome")       {
            /* ensure we are out of init and not in error */
            if ((state == STATE_INIT) || (state == STATE_ERROR)) {
                Serial.print("0#");
            } else {
                /* first reply to say we got the message */
                Serial.print("1#");
                /* make sure we are stopped */
                if (state != STATE_STOPPED) abort_slew();
                /* if we are not already at home we need to command it */
                if (!at_home()) {
                    state = STATE_HOMEING;
                    /* next we find which way to go and start turning */
                    mod_angle = current_angle - cal_home_angle;
                    if (mod_angle < 0) mod_angle += 360.0;
                    if (mod_angle < 180.0) {
                        do_cw();
                    } else {
                        do_ccw();
                    }
                    /* For safety, record the time we started the park operation */
                    homeing_timer = millis();
                } /* if we are already in home then we do nothing */
            }
        /* --------------------------------------------------------------------- */
        } else if (cmd == "Park")           {  
            /* ensure we are out of init and not in error */
            if ((state == STATE_INIT) || (state == STATE_ERROR)) {
                Serial.print("0#");
            } else {
                /* first reply to say we got the message */
                Serial.print("1#");
                /* make sure we are stopped */
                if (state != STATE_STOPPED) abort_slew();
                /* if we are not already in park we need to command it */
                if (!at_park()) {
                    state = STATE_PARKING;
                    /* next we find which way to go and start turning */
                    mod_angle = current_angle - cal_park_angle;
                    if (mod_angle < 0) mod_angle += 360.0;
                    if (mod_angle < 180.0) {
                        do_cw();
                    } else {
                        do_ccw();
                    }
                    /* For safety, record the time we started the park operation */
                    parking_timer = millis();
                } /* if we are already in park then we do nothing */
            }
        /* --------------------------------------------------------------------- */
        } else if (cmd == "SetPark")        {
            /* ensure we are out of init and not in error */
            if ((state == STATE_INIT) || (state == STATE_ERROR)) {
                Serial.print("0#");
            } else {
                if (state != STATE_STOPPED) {
                    abort_slew();
                    Serial.print("0#");
                }
                Serial.print("1#");
                cal_park_angle = current_angle;
                eeprom_write32(EEPROM_PARK_ANGLE_X100,  (int)(cal_park_angle * 100));    
            }
        /* --------------------------------------------------------------------- */
        } else if (cmd == "ShutterStatus")  {  /* ShutterStatus not implemented  */
        /* --------------------------------------------------------------------- */
        } else if (cmd == "Slaved")         {  /* Slaved not implemented         */
        /* --------------------------------------------------------------------- */
        } else if (cmd == "SlewToAltitude") {  /* SlewToAltitude not implemented */
        /* --------------------------------------------------------------------- */
        } else if (cmd == "SlewToAzimuth")  {
            /* ensure we are out of init and not in error */
            if ((state == STATE_INIT) || (state == STATE_ERROR)) {
                Serial.print("0#");
            } else {
                Serial.print("1#");
                /* first we undo any previous commands */
                if (state != STATE_STOPPED) abort_slew();
                /* then we find which way to go */
                num_string = Serial.readStringUntil('_');
                goto_angle = num_string.toDouble();
                mod_angle = current_angle-goto_angle;
                if (mod_angle < 0) mod_angle += 360.0;
                if (mod_angle < 180.0) {
                    do_cw();
                } else {
                    do_ccw();
                }
                goto_timer = millis();
                state = STATE_SLEWING;
            }
        /* --------------------------------------------------------------------- */
        } else if (cmd == "Slewing")        {
            if ((state == STATE_STOPPED) || (state == STATE_ERROR)) Serial.print("0#");
            else Serial.print("1#"); /* init, cal, slwwing or homing */
        /* --------------------------------------------------------------------- */
        } else if (cmd == "SyncToAzimuth")  {  /* SyncToAzimuth not implemented  */
        }
    }
}

/*-------------------------------------------------------------------------------------*/
void loop() {
/*-------------------------------------------------------------------------------------*/
/* the standard Arduino loop function - called repeatedly forever                      */
/*-------------------------------------------------------------------------------------*/
    check_commands();
    check_posn_sensors();
   
    switch (state) {
      /* ------------------------------- */
      case STATE_INIT:
        do_init();
        state = STATE_STOPPED;
        break;

      /* ------------------------------- */
      case STATE_STOPPED:
        /* check the home situation to keep the LED OK if we manually move the dome */
        at_home(); 
        /* make sure the ready LED is on */
        led_set(LED_READY, LED_ON);
        /* nothing to do in this state, just wait */
        break;

      /* ------------------------------- */
      case STATE_CAL:
        do_cal();
        break;

      /* ------------------------------- */
      case STATE_HOMEING:
        /* all we have to do is check to see if we are there yet */
        if (at_home()) {
            abort_slew();
        } else if (millis() - homeing_timer > (1000 * DOME_FULL_ROTATION_TIME_SECS + 5000)) {
            /* unless we have taken too long to do it in which case enter error state */
            /* we allow for a full rotation plus 5 Seconds, all calculated in mS */
            abort_slew();
            state = STATE_ERROR;
        }
        break;

      /* ------------------------------- */
      case STATE_PARKING:
        /* all we have to do is check to see if we are there yet */
        if (at_park()) {
            abort_slew();
        } else if (millis() - parking_timer > (1000 * DOME_FULL_ROTATION_TIME_SECS + 5000)) {
            /* unless we have taken too long to do it in which case enter error state */
            /* we allow for a full rotation plus 5 Seconds, all calculated in mS */
            abort_slew();
            state = STATE_ERROR;
        }
        break;

      /* ------------------------------- */
      case STATE_SLEWING:
        /* we decide we are there when we are within 0.1 degrees */
        if (fabs(current_angle - goto_angle) < 0.1) {
            abort_slew();
        } else if (millis() - goto_timer > (1000 * DOME_FULL_ROTATION_TIME_SECS / 2 + 5000)) {
            /* unless we have taken too long to do it in which case enter error state */
            /* here we allow for half a rotation (worst case) plus the 5 seconds overlap */
            abort_slew();
            state = STATE_ERROR;
        }
        break;

      /* ------------------------------- */
      default: /* something wrong or state==STATE_ERROR */
        /* shutdown everything */
        motor_set(MOTOR_CTRL, MOTOR_OFF);
        led_set(LED_CW,       LED_OFF);
        led_set(LED_CCW,      LED_OFF);
        led_set(LED_READY,    LED_OFF);
        led_set(LED_HOME,     LED_OFF);
        led_set(LED_ERROR,    LED_ON);
        break;
    }
    
    /* if we are moving then flash the LED */
    if ((state != STATE_ERROR) && (state != STATE_STOPPED)) {
        /* flash LED every 0.5 seconds */
        if ((millis() % 1000) >= 500) {
            led_set(LED_READY, LED_ON);
        } else {
            led_set(LED_READY, LED_OFF);
        }
    }
    
    delay(BACKGROUND_LOOP_TIME);
}
