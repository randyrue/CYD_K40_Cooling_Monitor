ESP32 Cheap Yellow Device K40 Laser Cooling Monitor

The CYD is about a perfect solution for upgrading the monitoring of cooling for a K40 CO2 laser cutter. Water flow and temperature are measured and if they go out of bounds (too warm or flow too low) the laser output is disabled.

My defaults are temp <= 24C and flow >= 0.8L/m. Change them in the code if your needs vary.

Minimal components are needed:

CYD
https://www.amazon.com/dp/B0CLR7MQ91
This is the one I used. There are many slight variations. You might need to fiddle with your User_Setup.h if yours is different. For example, I had to dink around for a while to get some colors to make sense.

DS18B20 temperature probe
https://www.amazon.com/dp/B012C597T0
Needs a 4K7 pull-up resistor between +V and the data line. Specs call for connecting the resistor to 3.3V, which you could get from the CN1 connector if needed, but mine works fine connected to 5V. For mine I added it in the middle of the cable to the temp sensor as long as I was splicing wires and wrapping the splices in heat shrink. This simplifies what would otherwise be your only needed wiring across the ports of the CYD.

GR-R401 fluid flow counter
https://www.amazon.com/dp/B07RF57QF8

Relay Module
https://www.amazon.com/dp/B00LW15A4W

Connectors for the onboard jacks: you'll need a few more than the one supplied with the CYD
https://www.amazon.com/dp/B0G146WVC1

Small Speaker

Case (STL file and SCAD are included here)
https://www.thingiverse.com/thing:7330724
Case design is a remix with no holes for connectors and extra space around the edges for JST headers. I drilled holes through the front for the CYD mounting holes, and a large hole through the back for wires to pass through. I should have also included one hole for the USB C jack in case I want to reflash the thing without needing to remove it. I've included the STL and also the hacked SCAD file, mod it to your preference.

A few other things make installation and maintenance easier, like:

Connector pigtails for the sensors - Tip - Reverse the M/F pigtail connectors for the temp and flow sensor connections to avoid confusion when climbing under your bench to connect them. That is, put the M on the controller side and the F on the sensor side for one, and the F on the controller side and the M on the sensor side for the other.
https://www.amazon.com/dp/B071H5XCN5

3-Conductor Wire - I added about four feet of three conductor wire between the sensors and the connector pigtails, with tidy splices and heat shrink tubing and for the temp sensor, its pullup resistor in one of the junctions (see Temperature Probe note above).
https://www.amazon.com/dp/B08JTZKN4M

Heat Shrink Tubing

Double Sided Tape

Hookup Wire


Connections to the K40: You essentially need to tap into +5VDC, ground, and wire the relay module's "NO" (normally open) side into the wire to the "P" connection on your high voltage power supply. Easy Peasy.
