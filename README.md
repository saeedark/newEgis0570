# egis0570 

Linux driver for Egis Technology Inc. ID 1c7a:0570 fingerprint scanner (LighTuning Technology Inc), it will work with fprint from freedesktop


Repository Content
------------------

* `Data/`: windows informatoin + driver + wireshark pcapng + Picture of sensor
* `Errors/`: Errors facing.
* `new one/`: converting driver for API 2
* `Old Driver/`: driver written by @indev29
* `Test Device/` : code for play :) (written by @indev29) 
 

Current state
-------------
memory leak is the issue. + sensor size is small + libfprint Algorithm sadly won't work at all.
but driver is working.


Possible Solutions
-------------
Improve libfprint Algorithm 

use this sensor as swipe_type

mozaiking ?

