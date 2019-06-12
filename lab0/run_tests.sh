#!/bin/sh
set -e
sudo python ../tester/generic_tester.py -d bus.xml
sudo python ../tester/generic_tester.py -d star.xml
sudo python ../tester/generic_tester.py -d circle.xml
