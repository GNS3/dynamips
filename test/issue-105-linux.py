#!/usr/bin/env python3
# -*- coding: utf8 -*-
#
# test script for issue #105 "Ethernet switch crashes on JumboFrames+dot1q"
# @see https://github.com/GNS3/dynamips/issues/105
#
# assumes a linux OS and dynamips in ../build/stable

linux_setup = """
sudo ip link add vl0 type veth peer name vl1
sudo ip link add vr0 type veth peer name vr1
sudo ip l set up vl0 mtu 9000
sudo ip l set up vr0 mtu 9000

sudo ip netns add left
sudo ip netns add right
sudo ip link set dev vl1 netns left
sudo ip link set dev vr1 netns right
sudo ip netns exec left ip link set vl1 up mtu 9000
sudo ip netns exec right ip link set vr1 up mtu 9000

sudo ip netns exec left ip addr add 10.0.0.1/24 dev vl1
sudo ip netns exec right ip addr add 10.0.0.2/24 dev vr1
"""

linux_cleanup = """
sudo ip netns del right
sudo ip netns del left
"""

hypervisor = """
ethsw create sw1
nio create_gen_eth E10 vl0
nio create_udp  E11 10001 127.0.0.1 20001
ethsw add_nio sw1 E10
ethsw add_nio sw1 E11
ethsw set_access_port sw1 E10 10
ethsw set_dot1q_port sw1 E11 1

ethsw create sw2
nio create_gen_eth E20 vr0
nio create_udp  E21 20001 127.0.0.1 10001
ethsw add_nio sw2 E20
ethsw add_nio sw2 E21
ethsw set_access_port sw2 E20 10
ethsw set_dot1q_port sw2 E21 1
"""

# success: all pings work
# fail: buffer overflow in dynamips
linux_test = """
sudo ip netns exec left ping 10.0.0.2 -s 8000 -c 10
"""

import subprocess
import time

def sleep(secs=0.1):
	time.sleep(secs) # give some time to other processes

def each_line(code):
	for line in code.split("\n"):
		line = line.strip()
		if line == "":
			continue
		yield line

def run_script(script, prefix=""):
	for line in each_line(script):
		print(prefix, line)
		subprocess.run(line.split())
		sleep()

def dynamips_hypervisor():
	# stdin needs to be a PIPE to protect the script console
	dynamips = subprocess.Popen(
		["sudo", "./dynamips", "-H", "10000"],
		cwd="../build/stable",
		text=True,
		stdin=subprocess.PIPE,
		stderr=subprocess.STDOUT)
	sleep(1) # extra time for 1st time stuff
	return dynamips

def telnet_hypervisor():
	telnet = subprocess.Popen(
		["telnet", "127.0.0.1", "10000"],
		text=True,
		stdin=subprocess.PIPE,
		stderr=subprocess.STDOUT)
	def run_script(script, prefix=""):
		for line in each_line(script):
			print(prefix, line)
			telnet.stdin.write(line + "\n")
			telnet.stdin.flush()
			sleep()
	telnet.run_script = run_script
	sleep()
	return telnet


run_script(linux_setup, prefix="[SETUP]")
print("[DYNAMIPS.START]")
with dynamips_hypervisor() as dynamips:
	print("[TELNET.START]")
	with telnet_hypervisor() as telnet:
		telnet.run_script(hypervisor, prefix="[TELNET]")
	print("[TELNET.STOP]")
	run_script(linux_test, prefix="[TEST]")
	dynamips.terminate()
	sleep()
	dynamips.kill()
print("[DYNAMIPS.STOP]")
run_script(linux_cleanup, prefix="[CLEANUP]")
