

router_addresses = ["101", "102", "103", "104", "105", "106", "107", "108", "109"]

i = 0

for rtr in routers_upper: 

# loop through all router addresses -- CLI won't let you make a BGP connection with your own interface anyways
	for rtr_ip in router_addresses:
		command_base = "neighbor 4." + rtr_ip + ".0.2 "
		command = command_base + "remote-as 4"
		child.sendline(command)
		command = command_base + "update-source host"
		child.sendline(command)
		command = command_base + "next-hop-self"
		child.sendline(command)