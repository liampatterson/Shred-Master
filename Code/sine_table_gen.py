import math
from math import sin
from math import exp

sine_table = []
DAC_data1 = []
DAC_config_chan_A = 0b0011000000000000

if __name__ == "__main__":
	for i in range(256):
		sine_table.append((int(1023*sin(float(i)*3.1415/(4.0*float(377))) * exp((-i)/60)) * 90 + 60))

	stuff = open("pospoint.txt", "w")

	for i in range(256):
		to_write = str(sine_table[i])
		stuff.write(to_write + ", ")

	stuff.close()
