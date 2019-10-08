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

routers_upper = ["NEWY", "WASH", "ATLA", "CHIC", "KANS", "HOUS", "SALT", "LOSA", "SEAT"]
routers_lower = ["newy", "wash", "atla", "chic", "kans", "hous", "salt", "losa", "seat"]

folder_location = "/home/cs551/candre97-cs551/lab2/configs/"

# named tuple like a struct to hold info about an interface
# need name, ip, cost 
# going to just use arrays
names = []
ips = []
costs = []

i = 0

for rtr in routers_upper: 

	print("configuring settings for %s" % rtr)
	file_location = folder_location + rtr + '/'
	print("opening file from location: ")
	print(file_location)
	del names[:]
	del ips[:]
	del costs[:]
	# network 4.0.0.0/8 area 0 for every router

	# open the files in read mode
	ospfd = open(file_location + "ospfd.conf.sav", "r")
	zebra = open(file_location + "zebra.conf.sav", "r")

	line = ospfd.readline()
	while len(line) > 0:
		line = ospfd.readline()
		#print(line)
		# if "interface" in line:
		# 	print("found interface")
		index = line.find("face")
		if(index > 0):
			index += 5
			name = line[index : len(line) - 1]
			#print("appending name %s " % name)
			names.append(name)
			#print(name)
			# next line has the link cost
			if name not in routers_lower:
				if name == "losa":
					print("error")
				else: 
					costs.append("0")
				#print("appending cost %i" % 0)
				continue
			line = ospfd.readline()
			index = line.find("cost ")
			if(index < 0):
				print(line)
				print("no cost found for this interface")
				continue
			costs.append(line[index+5 : len(line) - 1])
			#print(line[index+5 : len(line) - 1])

	#print("All done reading the ospfd file, time to read the zebra file")

	line = zebra.readline()
	while len(line) > 0:
		line = zebra.readline()
		if("interface lo" in line):
			if "losa" not in line:
				ips.append("0.0.0.0")
				continue
		index = line.find("address")
		if(index > 0):
			#print(index)
			index += 8
			address = line[index : len(line) - 1]
			#print(address)
			ips.append(address)

	#print("All Settings for Router %s: " % rtr)
	#for i in range(0,len(names) - 1):
		# print(names[i])
		# print(ips[i])
		# print(costs[i])

	child = pexpect.spawn('sudo ./go_to.sh ' + rtr)
	print('sudo ./go_to.sh ' + rtr)
	# start to configure the router in the CLI
	#print child.read()

	child.sendline("vtysh")
	print("vtysh")
	#print child.read()
	child.sendline("conf t")
	print("conf t")
	#print child.read()

	# make everything under 4.0.0.0/8 be in the area 0 network
	child.sendline("router ospf")
	child.sendline("network 4.0.0.0/8 area 0")
	child.sendline("network 4.0.0.0/16 area 0")
	child.sendline("exit")

	# configure the interfaces
	for i in range (0, len(names)) :
		if(names[i] == "lo"):
			continue
		command = "interface " + names[i]
		child.sendline(command)
		print(command)
		command = "ip address " + ips[i]
		child.sendline(command)
		print(command)
		if(names[i] == "host"):
			continue
		command = "ospf cost " + costs[i]
		child.sendline(command)
		print(command)
		child.sendline("exit")

	# get out of configuring the router and go back to lab 2 folder
	child.sendline("exit")
	child.sendline("write file")

	child.sendline("exit")
	child.sendline("exit")
	child.sendline("ls")
	print child.readline()
	child.close()

print("router configuration complete")

