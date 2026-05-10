#---------------------------------------------------------------
# python code to interface the Drax Telescope ASCOM driver with
#   the DRAX wifi network. Executes on the SP32_DevKitC_V4
#   (which has an ESP32-WROOM-32D module on it
#---------------------------------------------------------------
from machine import Pin
from machine import UART
from time import sleep

# all messages that come in are of the form "COMMAND_"
# we respond to all messages with either the asnwer, or 0/1 (true or false) followed by '#'
#                objSerial.Speed = 115200
declination    = 12.356
decTarget      = 0
decSlewing     = False
rightAscension = 204
raTarget       = 0
raSlewing      = False
pulseGuiding   = True

#---------------------------------------------------------------
def sendReply(outMsg):
#---------------------------------------------------------------
# this function returns issues a reply to the ASCOM driver
#---------------------------------------------------------------
    uart.write('sending:' + outMsg + '#')

#---------------------------------------------------------------
def doAbortSlew():
#---------------------------------------------------------------
# this function stops slewing
#---------------------------------------------------------------
    if raSlewing:
        raSlewing = False
        # turn off the fast controls for RA
    if decSlewing:
        decSlewing = False
        # turn off the fast controls for Dec

#---------------------------------------------------------------
def cmdProcessing(message):
#---------------------------------------------------------------
# this is where we do the real work of interpreting the ASCOM commands
#---------------------------------------------------------------
    
    # ------------------ -----------------------------------------------
    # AbortSlew: stops any movement of the telescope
    if message.startswith('AbortSlew_'):
        # turn off motors if needed.
        doAbortSlew()
        sendReply('1')

    # ------------------ -----------------------------------------------
    # Declination: requests the current declination reading 
    elif message.startswith('Declination_'):
        sendReply(str(declination))
    
    # ------------------ -----------------------------------------------
    # Right Ascension: requests the current RA of the telescope
    elif message.startswith('RightAscension_'):
        sendReply(str(rightAscension))

    # ------------------ -----------------------------------------------
    # IsPulseGuiding: returns true (1) if currentl pulse guiding else 0
    elif message.startswith('IsPulseGuiding_'):
        if pulseGuiding:
            sendReply('1')
        else:
            sendReply('0')
             
    # ------------------ -----------------------------------------------
    # PulseGuide_direction_duration: start going in given direction for given time
    elif message.startswith('PulseGuide_'):
        subMsg = message.split('_');
        direction = subMsg[1]
        duration = subMsg[2]
        print(direction)
        print(duration)
        sendReply('1')
        
    # ------------------ -----------------------------------------------
    # SlewToCoordinates_RA_DEC: slew to given RA and Dec coordinates
    elif message.startswith('SlewToCoordinates_'):
        subMsg = message.split('_');
        ra = float(subMsg[1])
        dec = float(subMsg[2])
        print(ra)
        print(dec)
        # stop any slewing if it was going on
        doAbortSlewing()
        if ra != rightAscension: 
            # we need to move in RA
            raTarget = ra
            # decide which way are we going to go. Handle the wrap (24 to 0 to 24)
            raDiff = rightAscension - raTarget    
            if raDiff < 0.0:
                raDiff += 24.0
            if raDiff <= 12.0:
                raDirection = 1
                # turn on fast control for RA Increasing
            else:
                raDirection = -1
                # turn on fast control for RA Decreasing
            raSlewing = true
        if dec != declination:
            # we need to move in Dec. This is donw in two parts, first humans need to push it close
            # then the computer can finish the job
            decTarget = dec
            # light LED fo human input
            LED(LED_on)
            humanDecSlewing = true;
        # all is now in motion, so send OK messgae
        sendReply('1')
        
    # ------------------ -----------------------------------------------
    # Slewing: asks if we are currently slewing or not
    elif message.startswith('Slewing_'):
        if raSlewing or decSlewing:
            sendReply('1')
        else:
            sendReply('0')

#---------------------------------------------------------------
# Main Routine: here we decide if we are going into REPL or going
#  to process ASCOM commands
#---------------------------------------------------------------

# on power up we check to see if we are being told to go to REPL and allow
# editing of the code
return_to_REPL_pin = Pin(22, Pin.IN, Pin.PULL_UP)
humanDecSwitch - Pin(21, Pin.IN, Pin,PULL_UP)

if return_to_REPL_pin.value() == 0:
    # do nothing if we are told to go back to REPL
    print ('doing nothing - just return to REPL')
    print (' but first lets do some sims')
   
    message = "PulseGuide_10.2_24.6#"
    cmdProcessing(message)

else:
    # so, if we are not trying to edit the code, then we start the real application here
    # first off we take control of the UART pins for our purposes
    uart = UART(1, tx=1, rx=3, baudrate=115200)
    uart.init()
    uart.write('Starting Drax command processing\r\n')
    inMsg = bytes()
    inChar = bytes()
    # keep looking for messages all the time, and tracking the telescope
    while True:
        while uart.any():
            # if we have 1 or more characters come in then we read the first
            inChar = uart.read(1)
            # if it is an end of message symbol
            if inChar == b'#':
                uart.write('got ' + str(inMsg) + '\r\n')
                cmdProcessing(inMsg)
                inMsg = ''
            else:
                inMsg += inChar
                
        # now find the current position
        readCurrentRAandDec()
        
        # test for RA movement
        if raSlewing and raTarget == RightAscension:
            # we havereached our RA target so stop fast moving
            # note we may need to get close and then do fina adjust but not for now
            raSlewing = False
            
        # test for computer controlled Dec movement
        if decSlewing and decTarget == declination:
            # we havereached our dec target so stop fast moving
            # note we may need to get close and then do fina adjust but not for now
            decSlewing = False
            
        # test for Human controlled Dec movement
        if humanDecSlewing and humanDecSwitch.value() == 0:
            # out human has pressed the switch to say they have got it close
            # so now we can decide which way are we going to go (no wrap for Dec) to get it right
            if decTarget > dec:
                decDirection = 1
                # turn on fast control for Dec Increasing
            else:
                decDirection = -1
                # turn on fast control for Dec Decreasing
            decSlewing = true
            
