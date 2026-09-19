# DRAX Guide Set Control unit receiver 'Guide_Set_Rx_14.py
# Flashed 7/2/21 ~16:20
# 
# 

import time
import machine
from machine import WDT
wdt = WDT(timeout=10000)  # enable it with a timeout of 3s
from machine import Pin
try:
  import usocket as socket
except:
  import socket
import network
import struct
import sys
import esp
esp.osdebug(None)
import gc
gc.collect()
print ('i am alive')
ap = network.WLAN(network.AP_IF)
ap.active(False)

count=0
count1=0
global s
#ledloop = Pin(2, Pin.OUT, value=0)# pin 34, int led, loop ind
ledsync = Pin(32, Pin.OUT, value=0)# GPIO 32 is Pin 7, LED drive
brake = Pin(16, Pin.OUT, value=0)# GPIO 16 is Pin 31, LED drive
guide_set = Pin(17, Pin.OUT, value=0)# GPIO 17 is Pin 30, LED drive
DEC_up = Pin(18, Pin.OUT, value=0)# GPIO 18 is Pin 28, LED drive
DEC_down = Pin(19, Pin.OUT, value=0)# GPIO 19 is Pin 27, LED drive
RA_en = Pin(21, Pin.OUT, value=1)# GPIO 21 is Pin 25, LED drive
RA_CW = Pin(22, Pin.OUT, value=0)# GPIO 22 is Pin 22, LED drive
RA_CCW = Pin(23, Pin.OUT, value=0)# GPIO 23 is Pin 21, LED drive

ssid = 'Drax'
password = 'RosaKlebb'
multicast_group = '224.3.29.70'
server_address = ('', 10000)

# Set up the WLAN & socket
wlan = network.WLAN(network.STA_IF)
wlan.active(True)
wlan.connect ('ssid','password')

# Create the socket
sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
# Bind to the server address
sock.bind(server_address)
# Tell the operating system to add the socket to the multicast group
# on all interfaces.
# should be mreq=struct.pack('4sL', socket.inet_aton(multicast_group)\
#          , socket.INADDR_ANY)
# BUT, mPython doesn't have inet_aton or INADDR_ANY so:
group = (b'\xe0\x03\x1dF')# = socket.inet_aton(multicast_group) from PC
anyadd= (b'\x00\x00\x00\x00')# should be socket.INADDR_ANY
anyadd= 0 # = socket.INADDR_ANY from PC, because above didn't work
mreq = struct.pack('4sL', group, anyadd)
sock.setsockopt(socket.IPPROTO_IP, socket.IP_ADD_MEMBERSHIP, mreq)
sock.settimeout(0.5)# Set non blocking

#########################################################################

def connect_wlan():
    if wlan.isconnected():
        #print ('wlan connected')
        return
    # needs a try loop with flashing led in case router not on
    else:
        global data_OP
        data_OP=[0,0,0,0,0,0,0]
        print ('trying to connect to router')
        wlan.connect(ssid,password)
        while not wlan.isconnected():
            ledsync.value (1)
            time.sleep (0.2)
            ledsync.value (0)
            time.sleep (0.2)
            wdt.feed()
            pass

    time.sleep(0.5)
    print('Connection successful ?:',wlan.isconnected())
    print(wlan.ifconfig())
    return


def get_data():
    # traps rx error if remote or router not on
    # Checks response is 8 chrs long.
    # Checks SOM and EOM are 'a' & 'h' respectivly.
    # Returns 'X' if fail.
    time.sleep (0.01)
    try:
        w, addr1 =sock.recvfrom(8)     # b'str'
        #print (w)
    except:
        ledsync.value (1)
        time.sleep (0.2)
        ledsync.value (0)
        #time.sleep (0.1)
        w=b'X'
    if not len(w) == 8:
        w=b'X'
        return w
    if not w[0:1] == b'a':
        w=b'X'
        return w
    if not w[7:8] == b'h':
        w=b'X'
        return w 
    w=w[1:7]
    return w


def data_dec(y):
    # Returns 'X' if failed previous tst.
    # Checks that all 6 chrs are upper or lower case b to g.
    # Returns 'NBG' if fail.
    # Creates list according to u/l case of each chr.
    if y == b'X':
        y = [0,0,0,0,0,0,0]
        return y
    z=[0]*7
    for n in range (1,7):
        if (y[n-1])==(n+65):
            z[n-1]=1
            z[6]=1
            ledsync.value (1)
        elif (y[n-1])==(n+97):
            z[n-1]=0
            z[6]=1
            ledsync.value (1)
        else:
            z = [0,0,0,0,0,0,0]
            ledsync.value (0)
            return z
    return z

def switch_OPs():
    brake.value(data_OP[6])
    guide_set.value(data_OP[0])
    DEC_up.value(data_OP[1])
    DEC_down.value(data_OP[2])
    RA_CW.value(data_OP[3])
    RA_CCW.value(data_OP[4])
    return



while True:
    connect_wlan()
    data_raw = get_data() # now 8* b'str'
    data_OP=data_dec(data_raw)
    switch_OPs()
    count=count+1
    gc.collect()
    wdt.feed()
    print (data_OP,end='\r')
    #ledloop.value(not ledloop.value())
    if count > 100:
        count =0
        count1=count1+1
        #print (100,end='\r')#, count1, data_OP[6], end='\r')
        #print (data_OP,end='\r')


s.close()
print ('reached here')
raise SystemExit()


