# Lab 2B 
# loading configs for OSPF and BGP

# load_configs.py
# This file will load in the configs This script should:
'''
automate the loading of the configs for all the routers 
in Internet2. When you save the configs in Quagga using 
the write file Quagga saves the configs in files with the 
.sav extension. For example, for each router, it will save
configuration files named zebra.conf.sav and ospfd.conf.sav. Your script should read these files and load them automatically to each router appropriately. To do this, we give you two hints:
Look at the code for go_to.sh to understand how to run a 
command at a router.
Look at the manual for vtysh to understand how to pass 
commands to vtysh.
'''

import pexpect
import sys

routers_upper = ["NEWY", "WASH", "ATLA", "CHIC", "KANS", "HOUS", "SALT", "LOSA", "SEAT", "west", "east"]
routers_lower = ["newy", "wash", "atla", "chic", "kans", "hous", "salt", "losa", "seat", "west", "east"]

folder_location = "/home/cs551/candre97-cs551/lab2/configs_multiAS/"

# named tuple like a struct to hold info about an interface
# need name, ip, cost 
# going to just use arrays
names = []
ips = []
costs = []

router_addresses = ["101", "102", "103", "104", "105", "106", "107", "108", "109"]

i = 0

for rtr in routers_upper: 

	print("configuring settings for %s" % rtr)
	file_location = folder_location + rtr + '/'
	print("opening file from location: ")
	print(file_location)

	# open the files in read mode
	ospfd = open(file_location + "ospfd.conf.sav", "r")
	zebra = open(file_location + "zebra.conf.sav", "r")
	bgpd = open(file_location + "bgpd.conf.sav", "r")

	child = pexpect.spawn('sudo ./go_to.sh ' + rtr)

	# if(rtr == "east"):
	# 	child.sendline("ifconfig server1 6.0.2.1/24")
	# 	child.sendline("ifconfig server2 6.0.3.1/24")

	child.sendline("vtysh")
	child.sendline("conf t")

	line = zebra.readline()
	while(len(line) > 0):
		line = zebra.readline()
		if "!" in line:
			continue
		else:
			child.sendline(line)

	# if(rtr != "west" and rtr != "east"):
	line = ospfd.readline()
	while(len(line) > 0):
		line = ospfd.readline()
		if "!" in line:
			continue
		else:
			child.sendline(line)

	line = bgpd.readline()
	while(len(line) > 0):
		line = bgpd.readline()
		if "!" in line:
			continue
		else:
			child.sendline(line)

	child.sendline("exit")
	child.sendline("exit")
	child.sendline("write file")
	child.sendline("exit")
	child.sendline("exit")
	
	child.close()
	ospfd.close()
	bgpd.close()
	zebra.close()


child = pexpect.spawn('sudo ./go_to.sh ' + rtr)