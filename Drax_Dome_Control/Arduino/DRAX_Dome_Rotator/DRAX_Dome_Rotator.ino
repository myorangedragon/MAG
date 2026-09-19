/* DRAX Dome rotator controller                     */
/* Copyright Heather Nickalls 2019,2020,2021,2022   */
/*                            2023,2024,2025,2026   */
/****************************************************/

/* todo
 * ----
 * get opto sensors working for position
 * EEPROM routines tested.
 * do cal
 * sort maths and calibration values
 * sort park position and detection
 */

/*-------------------------------------------------------------------------------------*/
/* revision history:                                                                   */
/*    1.0 - March 2020 Initial Code                                                    */
/*    1.1 - Aug   2022 New coding after COVID                                          */
/*    1.2 - Jan   2024 Adding Cal and Park (as opposed to home)                        */
/*    1.3 - Aug   2026 Tidy code and start testing                                     */
/*-------------------------------------------------------------------------------------*/
/* API Design:
   available ASCOM commands:

        AbortSlew_              : stop all motors
            0# - error
            1# - done
        Altitude_               : not implemented
        AtHome_                 : is the dome at the home position?
            0# - false
            1# - true
        AtPark_                 : is the dome at the park position?
            0# - false
            1# - true
        Azimuth_                : what is the current azimuth angle?
            xxx.x#              : xxx.x is current angle
        CanFindHome             : no code needed - ASCOM driver retuns true
        CanPark                 : no code needed - ASCOM driver retuns true
        CanSetAltitude          : not implemented - ASCOM driver returns false
        CanSetAzimuth           : no code needed - ASCOM driver retuns true
        CanSetPark              : no code needed - ASCOM driver retuns true
        CanSetShutter           : not implemented - ASCOM driver returns false
        CanSlave                : not implemented - ASCOM driver returns false
        CanSyncAzimuth          : not implemented - ASCOM driver returns false
        CloseShutter            : not implemented
        FindHome_               : go to the home position
            0#  - error
            1#  - started
        Park_                   : go to the park position
            0#  - error
            1#  - started
        SetPark_                : use the current angle (relative to North) as the new park angle
            0# - error
            1# - success
        ShutterStatus           : not implemented
        Slaved                  : not implemented
        SlewToAltitude          : not implemented
        SlewToAzimuth_xxx.x#    : move dome to xxx.x degrees      
            0# - error
            1# - started
        Slewing_                : is the dome moving?
            0# - false
            1# - true
        SyncToAzimuth           : not implemented
    
    Available custom commands:
    
        CALIBRATE_xxx.x         : xxx.x is the azimuth (in degrees) of the centre of the dome opening after a clockwise rotation
            0# - error
            1# - started
            2# - finished

   Hardware Design:
        We want a buzzer to sound when the dome is about to rotate
        LEDs for status:
            Error        : Red
            Ready        : Green
            Turn CCW     : Yellow
            Turn Cw      : Yellow
            In home      : Green
        drive is into an opto coupler:
            CCW/CW output: 5V=CW, 0=CCW, setup first before
            Motor drive  : 5V=motor on, 0v=motor off
        Buzzer=low power sounder and transitor drive? needs 2kHz output
   approx sizes:
        num teeth on rack            : 400
        num teeth on pinion          :  10
        num teeth on motor drive     :  20
        num positons per pinion turn :  10
        conclusion: use 32 bit storage for numbers                                                               */

/*-------------------------*/
/*----- INCLUDE FILES -----*/
/*-------------------------*/

#include <EEPROM.h>

/*-------------------------*/
/*-------- DEFINES --------*/
/*-------------------------*/

/* Set DEBUG if we are not using an ascom driver */
#define DEBUG
/* set NO_HARDWARE if the controller is not connected to the dome motors/sensors */
//#define NO_HARDWARE

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
#define LED_CW_PIN      2
#define LED_CCW_PIN     3
#define LED_READY_PIN   4
#define LED_ERROR_PIN   5
#define LED_HOME_PIN    6
#define   LED_ON           1
#define   LED_OFF          0

/* On the Driver board, we have opto couplers to control the Motor pins */
#define MOTOR_DIR_PIN   A0
#define   MOTOR_DIR_CW     1
#define   MOTOR_DIR_CCW    0
#define MOTOR_CTRL_PIN  A2
#define   MOTOR_ON         1
#define   MOTOR_OFF        0

/* the buzzer output pin */
/* this wants to be a pin we can set a freq on */
#define BUZZER_PIN      12
#define   BUZZER_ON        1
#define   BUZZER_OFF       0
/* the period of half a wavelength at the buzzer freq (2400Hz) */
#define BUZZER_2400_HZ  208

/* the inputs from the posn sensor and the home sensor */
/* these have to be on interrupts to work properly  */
#define HOME_SENSOR_PIN  8

/* The position sensor is an optical sensor and a wheel */
#define POSN_INPUT_A_PIN 10
#define POSN_INPUT_B_PIN 11

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
#define STATE_NO_CAL  8

/* initialisatin state machine states */
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
double   cal_home_angle_deg;
double   cal_park_angle_deg;
uint32_t cal_counts_in_home;
uint32_t cal_counts_overrun;
uint32_t cal_pulse_counts = 0;


/* various angle stores we need */
double goto_angle_deg;
double new_home_angle_deg;  /* angle we are given by cal program for home sensor */
double current_angle_deg;

int32_t t_now_ms;

/* we have various timers that are used to check for error situations */
uint32_t init_timer_ms;
uint32_t homeing_timer_ms;
uint32_t parking_timer_ms;
uint32_t goto_timer_ms;
uint32_t cal_timer_ms;

/*-------------------------*/
/*------- FUNCTIONS -------*/
/*-------------------------*/

/*-------------------------------------------------------------------------------------*/
uint32_t eeprom_read32(uint8_t addr)
/*-------------------------------------------------------------------------------------*/
/*-------------------------------------------------------------------------------------*/
{
    uint32_t val;

    EEPROM.get(addr, val);
    return val;
}

/*-------------------------------------------------------------------------------------*/
void eeprom_write32(uint8_t addr, uint32_t val)
/*-------------------------------------------------------------------------------------*/
/*-------------------------------------------------------------------------------------*/
{
    EEPROM.put(addr, val); 
}

/*-------------------------------------------------------------------------------------*/
void led_set(uint8_t led_pin, uint8_t led_state)
/*-------------------------------------------------------------------------------------*/
/*-------------------------------------------------------------------------------------*/
{
    digitalWrite(led_pin, led_state);
}

/*-------------------------------------------------------------------------------------*/
void motor_set(uint8_t motor_pin, uint8_t motor_state)
/*-------------------------------------------------------------------------------------*/
/*-------------------------------------------------------------------------------------*/
{
    digitalWrite(motor_pin, motor_state);
}

/*-------------------------------------------------------------------------------------*/
void buzzer_sound(uint16_t buzz_time_ms)
/*-------------------------------------------------------------------------------------*/
/* buzz_time_ms: duration of the buzz in mS                                            */
/* could be done via an interrupt or PWM but no need yet                               */
/*-------------------------------------------------------------------------------------*/
{
    uint32_t buzz_start_ms;
    
    buzz_start_ms = millis();
    do
    {
      digitalWrite(BUZZER_PIN, BUZZER_ON);
      delayMicroseconds(BUZZER_2400_HZ);
      digitalWrite(BUZZER_PIN, BUZZER_OFF);
      delayMicroseconds(BUZZER_2400_HZ);
    }
    while (millis() < buzz_start_ms + buzz_time_ms);
}

/*-------------------------------------------------------------------------------------*/
bool at_home()
/*-------------------------------------------------------------------------------------*/
/* we need to debounce the magnetic switch                                             */
/*-------------------------------------------------------------------------------------*/
{
    bool result = false;
    static uint8_t debounce = 0;
  
    if (0 == digitalRead(HOME_SENSOR_PIN))
    {
        if (debounce >= 2) 
        {
            led_set(LED_HOME_PIN, LED_ON);
            result = true;
        }
        else
        {
            debounce++;
            led_set(LED_HOME_PIN, LED_OFF);
        }
    } else {
        debounce = 0;
        led_set(LED_HOME_PIN, LED_OFF);
    }

    return result;
}

/*-------------------------------------------------------------------------------------*/
bool at_park()
/*-------------------------------------------------------------------------------------*/
/* we need to debounce the magnetic switch                                             */
/*-------------------------------------------------------------------------------------*/
{
    bool parked;

    /* we decide we are there if we are within 0.1 degree */
    parked = (fabs(current_angle_deg - cal_park_angle_deg) < 0.1);
#ifdef NO_HARDWARE
    return false;
#else
    return parked;
#endif
}

/*-------------------------------------------------------------------------------------*/
bool cal_check()
/*-------------------------------------------------------------------------------------*/
/* this checks to see if we have a valid calibration in EEPROM                         */
/* if not then we stop in the error state and do nothing until a cal is demanded       */
/* if we do have a cal then the values are read from the EEPROM and prepped for use    */
/*-------------------------------------------------------------------------------------*/
{
    uint32_t cal_check_value;
    bool result = false;
    
    /* now we read out the EEPROM Data */
    /* first, check that we have programmed the eeprom already */
    cal_check_value = eeprom_read32(EEPROM_CAL_CHECK);
    if (CAL_CHECK_NUMBER == cal_check_value)
    {
        /* if this test passes, we have already calibrated so we can go ahead and get all the numbers out of the EEPROM */
        cal_counts_overrun    = eeprom_read32(EEPROM_COUNTS_OVERRUN);
        cal_counts_in_home    = eeprom_read32(EEPROM_COUNTS_IN_HOME);
        cal_degrees_per_pulse = 360.0 / ((double)eeprom_read32(EEPROM_COUNTS_PER_360));
        cal_home_angle_deg    = ((double)eeprom_read32(EEPROM_HOME_ANGLE_X100)) / 100.0;
        cal_park_angle_deg    = ((double)eeprom_read32(EEPROM_PARK_ANGLE_X100)) / 100.0;
        result = true;
    }
    else
    {
        /* we get here if it is a virgin part with no cal */
    }

    return result;
}

/*-------------------------------------------------------------------------------------*/
void check_posn_sensors()
/*-------------------------------------------------------------------------------------*/
/* Here we process the 2 quadrature position encoder inputs                            */
/*     how fast are pulses coming in?                                                  */
/*     motor runs at 1400 rpm. gearbox ratio is 30:1, number of                        */
/*     pulses per turn of sensor wheel = 6                                             */
/*     so 1 / (1400 / 60 / 30 * 6) = 0.21 seconds per pulse                            */
/*     so we can poll the sensors and debounce them no problem (no interrupts)         */
/*-------------------------------------------------------------------------------------*/
{
    static uint8_t debounce_a = 0;
    static uint8_t debounce_b = 0;
    static uint8_t input_a = 0;
    static uint8_t input_b = 0;
    static uint8_t old_input_a = 0;
  
    /* record the current state of input A to detect transitions */
    old_input_a = input_a;
    
    /* now we debounce the 2 inputs */
    if (1 == digitalRead(POSN_INPUT_A_PIN))
    {
        if (debounce_a == 3) input_a = 1;
        else debounce_a++;
    }
    else
    {
        if (debounce_a == 0) input_a = 0;
        else debounce_a--;
    }
    if (1 == digitalRead(POSN_INPUT_B_PIN))
    {
        if (debounce_b == 3) input_b = 1;
        else debounce_b++;
    }
    else
    {
        if (debounce_b == 0) input_b = 0;
        else debounce_b--;
    }
    /* we are only interested in a rising edge on one (A) input. Then:      */
    /*   if encoder_b is low, then we are going CC so add one pulse angle   */
    /*   if encoder_b is high then we are going CCW so subtract pulse angle */
    if ((0 == old_input_a) && (1 == input_a))
    {
        if (1 == input_b)
        {
            current_angle_deg += cal_degrees_per_pulse;
            /* need to make sure we stay in 0 to 360 degrees */
            if (current_angle_deg >= 360.0) current_angle_deg -= 360.0;
        }
        else
        { /* we are going CCW */
            current_angle_deg -= cal_degrees_per_pulse;
            if (current_angle_deg < 0.0) current_angle_deg += 360.0;
        }
    }
#ifdef DEBUG
//    Serial.println(current_angle_deg);
#endif
}

/*-------------------------------------------------------------------------------------*/
void setup()
/*-------------------------------------------------------------------------------------*/
/* The standard Arduino Setup routine - Called once at the very start of the program   */
/*-------------------------------------------------------------------------------------*/
{
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
    t_now_ms = 0;

    /* now we set up all the IO and initialise it */
    /* first off, setup and sound the buzzer briefly */
    pinMode(BUZZER_PIN,        OUTPUT);
    buzzer_sound(200);

    /* next do the LEDs and do a cycle check on all of them for 0.5S */
    pinMode(LED_READY_PIN,     OUTPUT);
    pinMode(LED_CW_PIN,        OUTPUT);
    pinMode(LED_CCW_PIN,       OUTPUT);
    pinMode(LED_ERROR_PIN,     OUTPUT);
    pinMode(LED_HOME_PIN,      OUTPUT);
    led_set(LED_READY_PIN,     LED_ON);
    led_set(LED_CW_PIN,        LED_OFF);
    led_set(LED_CCW_PIN,       LED_OFF);
    led_set(LED_ERROR_PIN,     LED_OFF);
    led_set(LED_HOME_PIN,      LED_OFF);
    delay(500);
    led_set(LED_READY_PIN,     LED_OFF);
    led_set(LED_CCW_PIN,       LED_ON);
    delay(500);
    led_set(LED_CCW_PIN,       LED_OFF);
    led_set(LED_CW_PIN,        LED_ON);
    delay(500);
    led_set(LED_CW_PIN,        LED_OFF);
    led_set(LED_ERROR_PIN,     LED_ON);
    delay(500);
    led_set(LED_ERROR_PIN,     LED_OFF);
    led_set(LED_HOME_PIN,      LED_ON);
    delay(500);
    led_set(LED_HOME_PIN,      LED_OFF);

    pinMode(MOTOR_DIR_PIN,     OUTPUT);
    pinMode(MOTOR_CTRL_PIN,    OUTPUT);
    motor_set(MOTOR_DIR_PIN,   MOTOR_DIR_CW);
    motor_set(MOTOR_CTRL_PIN,  MOTOR_OFF);
    
    pinMode(HOME_SENSOR_PIN,   INPUT_PULLUP);
    
    pinMode(POSN_INPUT_A_PIN,  INPUT);
    pinMode(POSN_INPUT_B_PIN,  INPUT);

    if (!cal_check())
    {
#ifdef NO_HARDWARE
        state = STATE_INIT;
        init_state = INIT_INIT;
        led_set(LED_ERROR_PIN, LED_OFF);
#else
        state = STATE_NO_CAL;
        led_set(LED_ERROR_PIN, LED_OFF);
        /* note we need a reset to get out of the NO_CAL state */
#endif
    }
    else
    {
        state = STATE_INIT;
        init_state = INIT_INIT;
        led_set(LED_ERROR_PIN, LED_OFF);
    }
}

/*-------------------------------------------------------------------------------------*/
void do_ccw()
/*-------------------------------------------------------------------------------------*/
/* Starts the motor driving Counter Clockwise (CCW) and sets the LEDS                  */
/*-------------------------------------------------------------------------------------*/
{
    led_set(LED_CW_PIN,       LED_OFF);
    led_set(LED_CCW_PIN,      LED_ON);
    motor_set(MOTOR_DIR_PIN,  MOTOR_DIR_CCW);
    motor_set(MOTOR_CTRL_PIN, MOTOR_ON);
}

/*-------------------------------------------------------------------------------------*/
void do_cw()
/*-------------------------------------------------------------------------------------*/
/* Starts the motor driving Clockwise (CW) and sets the LEDS                           */
/*-------------------------------------------------------------------------------------*/
{
    led_set(LED_CW_PIN,       LED_ON);
    led_set(LED_CCW_PIN,      LED_OFF);
    motor_set(MOTOR_DIR_PIN,  MOTOR_DIR_CW);
    motor_set(MOTOR_CTRL_PIN, MOTOR_ON);
}

/*-------------------------------------------------------------------------------------*/
bool do_init()
/*-------------------------------------------------------------------------------------*/
/* here we initialise the dome to a known state and position                           */
/* we do this by:                                                                      */
/*    run buzzer as a warning                                                          */
/*    if we are not in home, rotate CCW until we are in home                           */
/*    keep rotating CCW to get out of home and go on for 2 secs more                   */
/*    stop and rotate CW to go back into home and stop as soon as we are back in       */
/* at this point we will be at the CAL angle for home                                  */
/* return true when init has completed, otherwise false                                */
/*-------------------------------------------------------------------------------------*/
{
    bool result = false;

    switch (init_state)
    {
      /* ------------------------------- */
      case INIT_INIT:
        init_timer_ms = millis(); /* we need to time the buzzer */
        delay(200);
        buzzer_sound(500);
#ifdef NO_HARDWARE
        result = true;
#else
        init_state = INIT_BUZZED;
#endif
        break;
        
      /* ------------------------------- */
      case INIT_BUZZED:
        /* we have finished buzzing so we can get the dome into home position */
        init_timer_ms = millis();
        /* if we have started in home, then we go straight to GET_OUT_OF_HOME */
        if (at_home())
        {
            init_state = INIT_GET_OUT_OF_HOME;
        }
        else
        { /* otherwise we go CCW into home */
            init_state = INIT_GET_INTO_HOME;
        }
        /* either way we start going CCW */
        do_ccw();
        break;
        
      /* ------------------------------- */
      case INIT_GET_INTO_HOME:
        /* for safety we check if we have been going for more than a full rotation (based on time) */
        if (millis( ) - init_timer_ms >= DOME_FULL_ROTATION_TIME_SECS * 1000)
        {
            abort_slew();
            state = STATE_ERROR;
        }
        else
        {
            /* if all going ok, then we wait till we are in home */
            if (at_home())
            {
                init_timer_ms = millis();
                init_state = INIT_GET_OUT_OF_HOME;
            }
        }
        break;
        
      /* ------------------------------- */
      case INIT_GET_OUT_OF_HOME:
        if (millis() - init_timer_ms < 6000)
        {
            if (!at_home())
            {
                /* we have got out of home so now we go on for 2 seconds */
                init_timer_ms = millis();
                init_state = INIT_2_SECS;
            }
        }
        else
        { /* we have timed out, so all bets are off */
            abort_slew();
            state = STATE_ERROR;          
        }
        break;
        
      /* ------------------------------- */
      case INIT_2_SECS:
        if (millis() - init_timer_ms > 2000)
        {
            /* 2 secs is up so now we go back CW */
            init_timer_ms = millis();
            init_state = INIT_BACK_INTO_HOME;
            do_cw();
        }
        break;
        
      /* ------------------------------- */
      case INIT_BACK_INTO_HOME:
        /* we will go into an infinite loop waiting here but need a safety timeout of 6 seconds */
        while ((!at_home()) && (millis() - init_timer_ms < 6000)) {}
        /* as soon as we got back into home we stop */
        abort_slew();
        if (at_home())
        {
            result = true;
            /* we know where we are now, so setup the current angle for future ref */
            current_angle_deg = cal_home_angle_deg;
        }
        else
        {
            state = STATE_ERROR;
        }
        break;
        
      /* ------------------------------- */
      default:
        state = STATE_ERROR;   /* should never get here */
    }

    return result;
}

/*-------------------------------------------------------------------------------------*/
void abort_slew()
/*-------------------------------------------------------------------------------------*/
/* go into the stop state either from a command, or to reset for another state         */
/*-------------------------------------------------------------------------------------*/
{
    state = STATE_STOPPED;                /* Set state machine to STOPPED */
    motor_set(MOTOR_CTRL_PIN, MOTOR_OFF); /* turn off the motor */
                                          /* no need to worry about CW/CCW */
    led_set(LED_CW_PIN, LED_OFF);         /* reset the LEDs */
    led_set(LED_CCW_PIN,LED_OFF);
}

/*-------------------------------------------------------------------------------------*/
void cal_pulse_counter()
/*-------------------------------------------------------------------------------------*/
/*-------------------------------------------------------------------------------------*/
{
    static int old_input_a = 0;
    static int debounce_a = 0;
    int input_a;
    
    /* debounce the input a pin */
    if (1 == digitalRead(POSN_INPUT_A_PIN))
    {
        if (debounce_a == 3) input_a = 1;
        else debounce_a++;
    }
    else
    {
        if (debounce_a == 0) input_a = 0;
        else debounce_a--;
    }

    /* we are only interested in a rising edge on one (A) input     */
    if ((0 == old_input_a) && (1 == input_a))
    {
        cal_pulse_counts++;
    }
}

/*-------------------------------------------------------------------------------------*/
void do_cal()
/*-------------------------------------------------------------------------------------*/
/*  if we are already in home, turn CCW until we get back out                          */
/*  Now we know we are not in home turn CW. When we get into home and start counter.   */
/*  keep going. When we come out of home record the counter (number of counts in home) */
/*  keep going all the way round until we are back in home, record counts_per_360      */
/*  when next position sensor edge appears, turn off motor                             */
/*  record how many more pulses we get (overrrun)                                      */
/*-------------------------------------------------------------------------------------*/
{
    uint32_t new_counts_overrun;
    uint32_t new_counts_in_home;
    uint32_t new_counts_per_360;
    int wait;

#ifdef NO_HARDWARE
    delay(3000);
    Serial.print("2#"); /* send success */
    state = STATE_STOPPED;
    return;
#endif

    /* we want to be sure we have come into home in a CW direction */
    /* so, if we are in home we first get out of it CCW            */
#ifdef NO_HARDWARE
        do_ccw();
        delay(3000);
#else
#ifdef DEBUG
    Serial.println("Start Cal");
#endif
    if (at_home())
    {
#ifdef DEBUG
    Serial.println("we are at home");
#endif
        do_ccw();
        while (at_home()) {}
        abort_slew();
    }
    delay(1000); /* make sure it has come to a stop before reversing it */
#endif
    
    /* now we go into home CW - might be a long time if we are just out of it */
#ifdef NO_HARDWARE
        do_cw();
        delay(8000);
        abort_slew();
#else
#ifdef DEBUG
    Serial.println("now going ccw to get into home");
#endif
    do_cw();
    while (!at_home()) {};
#ifdef DEBUG
    Serial.println("in home so counting pulses in home");
#endif

    /* reset the pulse counter as we are about to count pulses for full rotation */
    cal_pulse_counts = 0;
    /* once we are in home, we count the number of pulses in home */
    while (at_home())
    {
        cal_pulse_counter();
    }
    new_counts_in_home = cal_pulse_counts;
#ifdef DEBUG
    Serial.println(new_counts_in_home);
    Serial.println("Out of home so counting pulses out of home");
#endif

    /* we don't reset the counter as we are still going round */
    /* next we keep going till we go all the way round 360, counting pulses all the time */
    while (!at_home())
    {
        cal_pulse_counter();
    };
    new_counts_per_360 = cal_pulse_counts;
#ifdef DEBUG
    Serial.println(new_counts_per_360);
    Serial.println("back in home so count overrun pulses");
#endif

    /* reset the pulse counter for the new measurement */
    cal_pulse_counts = 0;
    /* now we tell it to stop and count the number of overrun pulses */
    /* note we assume it will stop in less than 1 second             */
    abort_slew();
    for (wait = 0; wait < 1000; wait++)
    {
        cal_pulse_counter();
        delay(1);
    }    
    new_counts_overrun = cal_pulse_counts;

#ifdef DEBUG
    Serial.println(new_counts_overrun);
    Serial.println("Finished, so write to EEPROM");
#endif
    /* finally we save all the data to the EEPROM            */ 
    /* along with the home position we have been sent        */
    /* and a magic number so we know we have been calibrated */
    eeprom_write32(EEPROM_COUNTS_OVERRUN,  new_counts_overrun);
    eeprom_write32(EEPROM_COUNTS_IN_HOME,  new_counts_in_home);
    eeprom_write32(EEPROM_COUNTS_PER_360,  new_counts_per_360);
    eeprom_write32(EEPROM_HOME_ANGLE_X100, new_home_angle_deg);
    eeprom_write32(EEPROM_CAL_CHECK,       CAL_CHECK_NUMBER);
#endif    

    state = STATE_STOPPED;
    Serial.print("2#");  /* we have finished cal with SUCCESS */
}

/*-------------------------------------------------------------------------------------*/
void check_commands() {
/*-------------------------------------------------------------------------------------*/
/* Check for incominG ASCOM command (Note: we include all of them for completness)     */
/*-------------------------------------------------------------------------------------*/
    String cmd;
    String num_string;
    double mod_angle_deg;

    if (Serial.available() > 0)
    {
        cmd = Serial.readStringUntil('_');

        /* --------------------------------------------------------------------- */
        /* this is a special case and not a standard defined ASCOM command       */
        /* we will call this from the Qt calibration program                     */
        if (cmd == "CALIBRATE")
        {
            num_string = Serial.readStringUntil('#');
            new_home_angle_deg = num_string.toDouble();
            cal_timer_ms = millis();
            Serial.print("1#");  /* we have started calibration */
            buzzer_sound(50);
            state = STATE_CAL;
        }
        /* we only do real stuff if the Dome has been calibrated */
        else if (state != STATE_NO_CAL)
        {
            /* --------------------------------------------------------------------- */
            if (cmd == "AbortSlew")
            {
                /* shut down immediatly no matter what                               */
                /* if we haven't finished init, go into an error state afterwards    */
                /* so can't put the AbortSlew command outside of the if              */
                if (state == STATE_INIT)
                {
                    Serial.print("0#");
                    abort_slew();
                    state = STATE_ERROR;
                }
                else
                {
                    Serial.print("1#");
                    abort_slew();
                }
            }
            /* --------------------------------------------------------------------- */
            else if (cmd == "Altitude") {}     /* Altitude not implemented           */
            /* --------------------------------------------------------------------- */
            else if (cmd == "AtHome")
            {
                if (at_home()) Serial.print("1#");
                else           Serial.print("0#");
            }
            /* --------------------------------------------------------------------- */
            else if (cmd == "AtPark")
            {
                if (at_park()) Serial.print("1#");
                else           Serial.print("0#");
            }
            /* --------------------------------------------------------------------- */
            else if (cmd == "Azimuth")
            {
                Serial.print(current_angle_deg);
                Serial.print("#");
            }
            /* --------------------------------------------------------------------- */
            else if (cmd == "CanFindHome")    {}  /* CanFindHome no code needed      */
            /* --------------------------------------------------------------------- */
            else if (cmd == "CanPark")        {}  /* CanPark no code needed          */
            /* --------------------------------------------------------------------- */
            else if (cmd == "CanSetAltitude") {}  /* CanSetAltitude not implemented  */
            /* --------------------------------------------------------------------- */
            else if (cmd == "CanSetAzimuth")  {}  /* CanSetAzimuth no code needed    */
            /* --------------------------------------------------------------------- */
            else if (cmd == "CanSetPark")     {}  /* CanSetPark no code needed       */
            /* --------------------------------------------------------------------- */
            else if (cmd == "CanSetShutter")  {}  /* CanSetShutter not implemented   */
            /* --------------------------------------------------------------------- */
            else if (cmd == "CanSlave")       {}  /* CanSlave not implemented        */
            /* --------------------------------------------------------------------- */
            else if (cmd == "CanSyncAzimuth") {}  /* CanSyncAzimuth not implemented  */
            /* --------------------------------------------------------------------- */
            else if (cmd == "CloseShutter")   {}  /* CloseShutter not implemented    */
            /* --------------------------------------------------------------------- */
            else if (cmd == "FindHome")
            {
                /* ensure we are out of init and not in error */
                if ((state == STATE_INIT) || (state == STATE_ERROR))
                {
                    Serial.print("0#");
                }
                else
                {
                    /* first reply to say we got the message */
                    Serial.print("1#");
                    /* make sure we are stopped */
                    if (state != STATE_STOPPED) abort_slew();
                    /* if we are not already at home we need to command it */
                    if (!at_home())
                    {
                        state = STATE_HOMEING;
                        /* next we find which way to go and start turning */
                        mod_angle_deg = current_angle_deg - cal_home_angle_deg;
                        if (mod_angle_deg < 0) mod_angle_deg += 360.0;
                        if (mod_angle_deg < 180.0)
                        {
                            do_cw();
                        }
                        else
                        {
                            do_ccw();
                        }
                        /* For safety, record the time we started the park operation */
                        homeing_timer_ms = millis();
                    } /* if we are already in home then we do nothing */
                }
            /* --------------------------------------------------------------------- */
            }
            else if (cmd == "Park")           
            {
                /* ensure we are out of init and not in error */
                if ((state == STATE_INIT) || (state == STATE_ERROR))
                {
                    Serial.print("0#");
                }
                else
                {
                    /* first reply to say we got the message */
                    Serial.print("1#");
                    /* make sure we are stopped */
                    if (state != STATE_STOPPED) abort_slew();
                    /* if we are not already in park we need to command it */
                    if (!at_park())
                    {
                        state = STATE_PARKING;
                        /* next we find which way to go and start turning */
                        mod_angle_deg = current_angle_deg - cal_park_angle_deg;
                        if (mod_angle_deg < 0) mod_angle_deg += 360.0;
                        if (mod_angle_deg < 180.0)
                        {
                            do_cw();
                        }
                        else
                        {
                            do_ccw();
                        }
                        /* For safety, record the time we started the park operation */
                        parking_timer_ms = millis();
                    } /* if we are already in park then we do nothing */
                }
            /* --------------------------------------------------------------------- */
            }
            else if (cmd == "SetPark")
            {
                /* ensure we are out of init and not in error */
                if ((state == STATE_INIT) || (state == STATE_ERROR))
                {
                    Serial.print("0#");
                }
                else
                {
                    if (state != STATE_STOPPED)
                    {
                        abort_slew();
                        Serial.print("0#");
                    }
                    Serial.print("1#");
                    cal_park_angle_deg = current_angle_deg;
                    eeprom_write32(EEPROM_PARK_ANGLE_X100, (int)(cal_park_angle_deg * 100));    
                }
            }
            /* --------------------------------------------------------------------- */
            else if (cmd == "ShutterStatus")  {}   /* ShutterStatus not implemented  */
            /* --------------------------------------------------------------------- */
            else if (cmd == "Slaved")         {}   /* Slaved not implemented         */
            /* --------------------------------------------------------------------- */
            else if (cmd == "SlewToAltitude") {}   /* SlewToAltitude not implemented */
            /* --------------------------------------------------------------------- */
            else if (cmd == "SlewToAzimuth")
            {
                /* ensure we are out of init and not in error */
                if ((state == STATE_INIT) || (state == STATE_ERROR))
                {
                    Serial.print("0#");
                }
                else
                {
                    Serial.print("1#");
                    /* first we undo any previous commands */
                    if (state != STATE_STOPPED) abort_slew();
                    /* then we find which way to go */
                    num_string = Serial.readStringUntil('_');
                    goto_angle_deg = num_string.toDouble();
                    mod_angle_deg = current_angle_deg - goto_angle_deg;
                    if (mod_angle_deg < 0) mod_angle_deg += 360.0;
                    if (mod_angle_deg < 180.0)
                    {
                        do_cw();
                    }
                    else
                    {
                        do_ccw();
                    }
                    goto_timer_ms = millis();
                    state = STATE_SLEWING;
                }
            }
            /* --------------------------------------------------------------------- */
            else if (cmd == "Slewing")
            {
                if ((state == STATE_STOPPED) || (state == STATE_ERROR)) Serial.print("0#");
                else Serial.print("1#"); /* init, cal, slewing or homing */
            }
            /* --------------------------------------------------------------------- */
            else if (cmd == "SyncToAzimuth")  {}   /* SyncToAzimuth not implemented  */
        }
    }
}

#ifdef NO_HARDWARE
/*-------------------------------------------------------------------------------------*/
bool fake_timer(uint16_t millis)
/*-------------------------------------------------------------------------------------*/
/*-------------------------------------------------------------------------------------*/
{
    static uint16_t count;
    bool result = false;

    if (++count == millis)
    {
        count = 0;
        result = true;
    }

    return result;
}
#endif

#ifdef DEBUG
uint16_t debug_timer;
#endif

/*-------------------------------------------------------------------------------------*/
void loop()
/*-------------------------------------------------------------------------------------*/
/* the standard Arduino loop function - called repeatedly forever                      */
/*-------------------------------------------------------------------------------------*/
{
    check_commands();
    check_posn_sensors();

#ifdef DEBUG
    debug_timer++;
    if (debug_timer==1000)
    {
        debug_timer = 0;
        Serial.println(state);
        Serial.println(at_home());
        Serial.println(at_park());
    }
#endif

    switch (state) {
      /* ------------------------------- */
      case STATE_INIT:
        if (do_init()) state = STATE_STOPPED;
        /* otherwise we keep calling do_init until it completes */
        break;

      /* ------------------------------- */
      case STATE_STOPPED:
        /* check the home situation to keep the LED OK if we manually move the dome */
        at_home(); 
        /* make sure the ready LED is on */
        led_set(LED_READY_PIN, LED_ON);
        /* nothing to do in this state, just wait */
        break;

      /* ------------------------------- */
      case STATE_CAL:
        do_cal();
        break;

      /* ------------------------------- */
      case STATE_NO_CAL:
        /* if we have not been calibrated we just do nothing untilwe receive a CAL command */
        break;

      /* ------------------------------- */
      case STATE_HOMEING:
        /* all we have to do is check to see if we are there yet */
#ifdef NO_HARDWARE
        led_set(LED_READY_PIN,    LED_OFF);
        if (fake_timer(3000))
#else
        if (at_home())
#endif
        {
            abort_slew();
        }
        else if (millis() - homeing_timer_ms > (1000 * DOME_FULL_ROTATION_TIME_SECS + 5000))
        {
            /* unless we have taken too long to do it in which case enter error state */
            /* we allow for a full rotation plus 5 Seconds, all calculated in mS */
            abort_slew();
            state = STATE_ERROR;
        }
        break;

      /* ------------------------------- */
      case STATE_PARKING:
        /* all we have to do is check to see if we are there yet */
#ifdef NO_HARDWARE
        led_set(LED_READY_PIN,    LED_OFF);
        if (fake_timer(3000))
#else
        if (at_park())
#endif
        {
            abort_slew();
        }
        else if (millis() - parking_timer_ms > (1000 * DOME_FULL_ROTATION_TIME_SECS + 5000))
        {
            /* unless we have taken too long to do it in which case enter error state */
            /* we allow for a full rotation plus 5 Seconds, all calculated in mS */
            abort_slew();
            state = STATE_ERROR;
        }
        break;

      /* ------------------------------- */
      case STATE_SLEWING:
        /* we decide we are there when we are within 0.1 degrees */
#ifdef NO_HARDWARE
        led_set(LED_READY_PIN,    LED_OFF);
        if (fake_timer(3000))
#else
        if (fabs(current_angle_deg - goto_angle_deg) < 0.1)
#endif
        {
            abort_slew();
        }
        else if (millis() - goto_timer_ms > (1000 * DOME_FULL_ROTATION_TIME_SECS / 2 + 5000))
        {
            /* unless we have taken too long to do it in which case enter error state */
            /* here we allow for half a rotation (worst case) plus the 5 seconds overlap */
            abort_slew();
            state = STATE_ERROR;
        }
        break;

      /* ------------------------------- */
      default: /* something wrong or state==STATE_ERROR */
        /* shutdown everything */
        motor_set(MOTOR_CTRL_PIN, MOTOR_OFF);
        led_set(LED_CW_PIN,       LED_OFF);
        led_set(LED_CCW_PIN,      LED_OFF);
        led_set(LED_READY_PIN,    LED_OFF);
        led_set(LED_HOME_PIN,     LED_OFF);
        led_set(LED_ERROR_PIN,    LED_ON);
        break;
    }
    
    /* if we are moving then flash the LED */
    if ((state != STATE_ERROR) && (state != STATE_STOPPED))
    {
        /* flash LED every 0.5 seconds */
        if ((millis() % 1000) >= 500)
        {
            led_set(LED_READY_PIN, LED_ON);
        }
        else
        {
            led_set(LED_READY_PIN, LED_OFF);
        }
    }
    
    delay(BACKGROUND_LOOP_TIME);
}
