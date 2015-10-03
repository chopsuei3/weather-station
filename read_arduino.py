import serial
import time

ser = serial.Serial('/dev/ttyAMA0', 19200, timeout=1)
#print ser.name          # check which port was really used
ser.write("!")      # write a string
time.sleep(3)
line = ser.readline()   # read until newline
print line
ser.close()             # close port
