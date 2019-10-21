

import pexpect
import sys

routers_upper = ["NEWY", "WASH", "ATLA", "CHIC", "KANS", "HOUS", "SALT", "LOSA", "SEAT"]
routers_lower = ["newy", "wash", "atla", "chic", "kans", "hous", "salt", "losa", "seat"]

folder_location = "/home/cs551/candre97-cs551/lab2/configs_multiAS/"
router_addresses = ["101", "102", "103", "104", "105", "106", "107", "108", "109"]

i = 0

for rtr in routers_upper: 
	file_location = folder_location + rtr + '/'
	bgpd = open(file_location + "bgpd.conf.sav", "r")


	child = pexpect.spawn('sudo ./go_to.sh ' + rtr)

	child.sendline("vtysh")
	#print("vtysh")
	#print child.read()
	child.sendline("conf t")
	# iBGP Stuff 
	child.sendline("router bgp 4")

	# get the BGP router id from the bgpd.conf.sav file
	line = bgpd.readline()
	while len(line) > 0:
		line = bgpd.readline()
		index = line.find("bgp router-id")
		if index > 0:
			index += 14
			bgp_rtr_id = line[index : len(line) - 1]
			break

	command = "bgp router-id " + bgp_rtr_id
	child.sendline(command)

	child.sendline("network 4.0.0.0/8")
# loop through all router addresses -- CLI won't let you make a BGP connection with your own interface anyways
	for rtr_ip in router_addresses:
		command_base = "neighbor 4." + rtr_ip + ".0.2 "
		command = command_base + "remote-as 4"
		child.sendline(command)
		command = command_base + "update-source host"
		child.sendline(command)
		command = command_base + "next-hop-self"
		child.sendline(command)

	# implement special cases for NEWY and SEAT for eBGP sessions
	if(rtr == "NEWY"):
		child.sendline("neighbor 6.0.1.2 remote-as 6")
		child.sendline("neighbor 6.0.1.2 update-source host")
		child.sendline("neighbor 6.0.1.2 next-hop-self")
	elif(rtr == "SEAT"):
		child.sendline("neighbor 5.0.1.2 remote-as 5")
		child.sendline("neighbor 5.0.1.2 update-source host")
		child.sendline("neighbor 5.0.1.2 next-hop-self")

	child.sendline("exit")

	child.sendline("exit")
	child.sendline("write file")

	child.sendline("exit")
	child.sendline("exit")
	# child.sendline("ls")
	# print child.readline()
	child.close()