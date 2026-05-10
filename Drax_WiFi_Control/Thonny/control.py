# DRAX Guide Set Control unit.py
# File renamed from 'Main' to above 29/12/20
# 
# 6/12/20 saved as v10 chnaged word len from 12 to 8 chrs
# 8/12/20 saved as v11 putting wlan routine into a def
# Strangly, socket can be defined before wlan is configured, so
# only has to be done once.
#
# Flashed to ESP32 23/12/20

import time
import machine
from machine import WDT
wdt = WDT(timeout=3000)  # enable it with a timeout of 3s
from machine import Pin
try:
  import usocket as socket
except:
  import socket
import network
import esp
esp.osdebug(None)
import gc
gc.collect()
print ('i am alive')
ssid = 'Drax'
password = 'RosaKlebb'
nic = network.WLAN(network.STA_IF) # enable station interface
nic.active(True)
count=0
count1=0
global s
ledsync = Pin(32, Pin.OUT, value=0)# GPIO 32 is Pin 7, LED drive
brake = Pin(16, Pin.OUT, value=0)# GPIO 16 is Pin 31, LED drive
guide_set = Pin(17, Pin.OUT, value=0)# GPIO 17 is Pin 30, LED drive
DEC_up = Pin(18, Pin.OUT, value=0)# GPIO 18 is Pin 28, LED drive
DEC_down = Pin(19, Pin.OUT, value=0)# GPIO 19 is Pin 27, LED drive
RA_en = Pin(21, Pin.OUT, value=1)# GPIO 21 is Pin 25, LED drive
RA_CW = Pin(22, Pin.OUT, value=0)# GPIO 22 is Pin 22, LED drive
RA_CCW = Pin(23, Pin.OUT, value=0)# GPIO 23 is Pin 21, LED drive

HOST = '192.168.1.32'    # Reserved for this server in router
PORT = 50001              # Arbitrary non-privileged port
s = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
s.bind((HOST, PORT))
s.settimeout(1.0)

def connect_wlan():
    if nic.isconnected():
        return
    # needs a try loop with flashing led in case router not on
    else:
        global data_OP
        data_OP=[0,0,0,0,0,0,0]
        print ('trying to connect to router')
        nic.connect(ssid,password)
        while not nic.isconnected():
            ledsync.value (1)
            time.sleep (0.2)
            ledsync.value (0)
            time.sleep (0.2)
            wdt.feed()
            pass

    time.sleep(0.5)
    print('Connection successful ?:',nic.isconnected())
    print(nic.ifconfig())
    return


def get_data():
    # traps rx error if remote or router not on
    # Checks response is 8 chrs long.
    # Checks SOM and EOM are 'a' & 'h' respectivly.
    # Returns 'X' if fail.
    time.sleep (0.00)
    try:
        w, addr1 =s.recvfrom(8)     # b'str'
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
    #if count > 2:
        #count =0
        #count1=count1+1
        #print (count1, data_OP[6], end='\r')


s.close()
print ('reached here')
raise SystemExit() #    print('Connected by', addr)