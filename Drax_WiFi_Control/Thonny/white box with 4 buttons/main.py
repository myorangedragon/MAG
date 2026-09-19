# WiFi Motor Control Client 'Guide_Set_Tx_14.py'
# Currently has a 10mS delay at line 96 so ~100Hz
# Now tries indefinitely to reconnect to WLAN              
# Now multicast to ('224.3.29.70', 10000)
# Flashed to ESP32 6/2/21 ~13:45 

import sys
import time
try:
  import usocket as socket
except:
  import socket
import network
import esp
esp.osdebug(None)
import gc
gc.collect()
from machine import Pin
ap = network.WLAN(network.AP_IF)
ap.active(False)

#Define IOs
pns = Pin(16, Pin.IN, Pin.PULL_UP)# GPIO 16 is Pin 31, North/South SW (internal)
pb = Pin(17, Pin.IN, Pin.PULL_UP)# GPIO 17 is Pin 30, Set/Guide SW      (b,B)
#pc = Pin(19, Pin.IN, Pin.PULL_UP)# These two are subject to swapping:  (c,C)
#pd = Pin(22, Pin.IN, Pin.PULL_UP)# a mistake but stuck with it.        (d,D)
pe = Pin(18, Pin.IN, Pin.PULL_UP)# GPIO 18 is Pin 28, RA CW (Right) SW  (e,E)
pf = Pin(21, Pin.IN, Pin.PULL_UP)# GPIO 21 is Pin 25, RA CCW (Left) SW  (f,F)
pg = 0                           # Dummy to make up 8 chrs
pled = Pin(23, Pin.OUT, value=0)# GPIO 23 is Pin 21, LED drive

ssid = 'Drax'
password = 'RosaKlebb'
multicast_group = ('224.3.29.70', 10000) #new
########################################################################

# Set up the WLAN & socket
wlan = network.WLAN(network.STA_IF)
wlan.active(True)
#Create the datagram socket
s=socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
# Set the time-to-live for messages to 1
s.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
s.settimeout(0)# Set non blocking
s.bind(('', 10000))
#########################################################################
count = 0

while True:
    if not wlan.isconnected():
        print('connecting to network...')
        wlan.connect(ssid,password)
        while not wlan.isconnected():
            pled.value (1)
            time.sleep (0.2)
            pled.value (0)
            time.sleep (0.2)
            pass

###################################################################
    TxString='a'      # Start chr
    # Reverse DEC buttons for N/S hemisphere
    if pns.value() == 0:
        pc = Pin(22, Pin.IN, Pin.PULL_UP)
        pd = Pin(19, Pin.IN, Pin.PULL_UP)
    else:
        pc = Pin(19, Pin.IN, Pin.PULL_UP)
        pd = Pin(22, Pin.IN, Pin.PULL_UP)
    # create control word
    if pb.value() == 0:
        TxString = TxString + 'B'
    else:
        TxString = TxString + 'b'
    if pc.value() == 0:
        TxString = TxString + 'C'
    else:
        TxString = TxString + 'c'        
    if pd.value() == 0:
        TxString = TxString + 'D'
    else:
        TxString = TxString + 'd'
    if pe.value() == 0:
        TxString = TxString + 'E'
    else:
        TxString = TxString + 'e'
    if pf.value() == 0:
        TxString = TxString + 'F'
    else:
        TxString = TxString + 'f'
    if pg == 1:
        TxString = TxString + 'G'
    else:
        TxString = TxString + 'g'
    TxString = TxString + 'h'        # add end chr
    time.sleep (0.01) 
    data = bytes(TxString, 'utf-8')
    try:
        sent = s.sendto(data, multicast_group)
    except OSError:
        print ('send error')
        pass

    count = count + 1
    #print (count)
    if count > 100:
        print (data) # flashes internal LED to show running
        count = 0


print ('done')
s.close()
raise SystemExit()
