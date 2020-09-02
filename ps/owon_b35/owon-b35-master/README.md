# OWON-B35 (All models)
OWON B35 Mutltimeter data capture and display for Linux

Captures, converts, displays and stores output from Bluetooth (BLE) output of the OWON B35(T+) series multimeters for Linux.

# Requirements
gatttool needs to be installed and operational in linux for owon-cli to work.

# Setup

1) Build owoncli

 make


2) Find the multimeter;

 sudo hcitool lescan


3) Run owoncli with the multimeter address as the parameter ( and -t if you want the text file output as well )

 sudo ./owoncli -a 98:84:E3:CD:C0:E5 -t 

(by default, you'll likely have to run this under sudo because the gatttool won't seem to talk to BLE devices initially without being superuser/root)

The program will display in text the current meter display and also generate a text file called "owon.txt" which can be read in to programs like OpenBroadcaster so you can have a live on-screen-display of the multimeter.

# Usage
 ./owoncli  -a &lt;address&gt; [-t] [-o <filename>] [-d] [-q]
 
        -h: This help
        
        -a <address>: Set the address of the B35 meter, eg, -a 98:84:E3:CD:C0:E5
        
        -t: Generate a text file containing current meter data (default to owon.txt)
        
        -o <filename>: Set the filename for the meter data ( overrides 'owon.txt' )

		-l <log filename>: Log data to file <filename>		( seconds value units )
        
        -d: debug enabled
        
        -q: quiet output


        example: owoncli -a 98:84:E3:CD:C0:E5 -t -o obsdata.txt
