# loads configurations for the hosts

import pexpect

routers_upper = ["NEWY", "WASH", "ATLA", "CHIC", "KANS", "HOUS", "SALT", "LOSA", "SEAT"]

host_ip = ""

folder_location = "/home/cs551/candre97-cs551/lab2/configs/"
child = pexpect.spawn ("/home/cs551/candre97-cs551/lab2/")

for rtr in routers_upper: 
	print("configuring settings for %s-host" % rtr)

	file_location = folder_location + rtr + '/'
	print("opening file from location: ")
	print(file_location)
	del ips[:]

	# open the file in read mode
	zebra = open(file_location + "zebra.conf.sav", "r")

	line = zebra.readline()

	while len(line) > 0:
		line = zebra.readline()
		if("interface host" in line):
			line = zebra.readline()
			index = line.find("address")
			if(index > 0):
				#print(index)
				index += 8
				host_ip = line[index : len(line) - 5]
				print(host_ip)
				break
			else: 
				print("ERROR: no ip for this host")

	command = "sudo ./go_to.sh " + rtr + "-host"

	# start to configure the router in the CLI
	child.sendline(command)

	# set the IP of the hosts interface towards the router
	command = "sudo ifconfig " + rtr.lower() + " " + host_ip + "1/24" + "up"
	child.sendline(command)

	# add the default gateway for a host to be its router
	command = "sudo route add default gw " + host_ip + "2 " + rtr.lower()
	child.sendline(command)

	child.sendline("exit")

print("host configuration complete")
child.close()