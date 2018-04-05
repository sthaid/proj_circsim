# OVERVIEW

This program simulates circuits that are comprised of resistors, capacitors, inductors, 
diodes, and AC or DC power supplies.

The eval_circuit_for_delta_t routine, in model.c, is the core of the simulation. This routine determines
the new circuit state (voltages and currents) after a short time interval (delta_t). Kirchoff's
current law, Ohms law, and the Current-Voltage relationship for capacitors and inductors are
used.

The eval_circuit_for_delta_t routine does the following
1. Loop over all nodes, and for each node determine an estimate for the next
voltage following the short delta_t interval. This estimate is computed by choosing a
voltage that satisfies Kirchoff's current law, using the values of the components
attached to this node and state of adjacent nodes as input.
2. Based on the voltages computed in step 1, determine the current passing through
each component, which is equivalent to the current at the node terminals that the 
component is attached to.
3. One might expect that the sum of the currents, for each node, calculated in 
step 2, would be 0. However, this is not the case because the next voltages
calculated in step 1 do not incorporate changes in the voltage of the adjacent 
nodes. So, step 3 will sum the current for each node, and if any node's total
current is significantly not zero then repeat from step 1. After a sufficient 
number of iterations all of the nodes currents will be nearly zero, 
in which case this routine has completed computing the next circuit state.

To evaluate the circuit for a long time interval (many delta_t), the 
eval_circuit_for_delta_t routine is called many times until the circuit has been
simulated for the desired interval.

# BUILD

I have developed this on Fedora26. In addition to the standard software development
packages the following are needed.
* SDL2-devel
* SDL2_ttf-devel
* SDL2_mixer-devel
* readline-devel

To build, run make, which should generate an executable file called 'model'

# TEST

The test directory contains many test circuits. For example, try:
* ./model test/rc1   &nbsp; &nbsp;  # simple RC circuit with T=1 
* ./model test/r3    &nbsp; &nbsp;  # almost infinite grid of 1 ohm resistors
* ./model test/ps3   &nbsp; &nbsp;  # 2x voltage multiplier 

See test/README for description of all test files.

# USAGE

model [<cmd_file>]

If <cmd_file> is supplied then commands are first read from this file. Subsequently
commands can be entered from the program's command prompt.

## Commands

```
set <param_name> <param_value>    : set parameter
show [<components|params|ground>] : prints the values of components, params, and ground

clear_all                         : clears the circuit and resets params
read <filename>                   : process commands from file
write <filename>                  : write the circuit to a file
add <type> <gl0> <gl1> [<value>]  : add a circuit component
                                    where type is power|resistor|capacitor|inductor|diode|wire
del <comp_str>                    : delete a circuit component
ground <gl>                       : specify location of ground

reset                             : reset the circuit back to time 0
run [<secs>]                      : evaluate circuit starting from time 0
stop                              : stop evaluating the circuit
cont [<secs>]                     : continue evaluating the circuit
step [<count>]                    : evaluate circuit for count delta_t steps

help                              : get help
printscreen                       : print screen to jpg file
```

## Parameters

```
run_t          : run time, for example: 100s or 50ms
delta_t        : delta_t interval, when set to 0 model will pick an appropriate delta_t
step_count     : used by the step command if the <count> is not supplied
dcpwr_ramp     : on|off - when on DC power will ramp up from 0 to V in 0.25 ms,
                 usually should be off
grid           : on|off - enables display of grid coordinates
current        : on|off - enables display of currents through components
voltage        : on|off - enables display of node voltages
component      : id|value|power - selects what is displayed for each component
intermediate   : on|off - enables display of the intermediate values calculated
                 by eval_circuit_for_delta_t prior to the circuit stabilizing
center         : specify the grid location of the display center; for example
                 'c2' or 'c2,500,500' where the latter is half way between c2 and d3
scale          : circuit size scale, 100 is smallest, 400 is largest
scope_mode     : trigger|continuous
scope_trigger  : set to 1 to trigger the scope
scope_span_t   : scope x time span
scope_a...h    : specifies what is displayed on each of the 8 scopes
                   <select>,<ymin>,<ymax>,<gl0>,<gl1>,<title>
                 examples:
                   set scope_a off
                   set scope_b voltage,-10V,10V,c2,d2,CAPACITOR-1
                   set scope_c current,0,50A,e3,e2,R1
```

## Display

There are 3 panes on the display. 
* control/status pane: displays the simulation time and state; if the circuit fails to
stabilize a red counter displays the number of occurrences
* circuit pane: the circuit can be panned and zoomed using the mouse left button and wheel
* scope pane: scroll using mouse wheel, select scope via mouse left button

Controls that can be clicked are displayed in light blue.

## Example - Entering an RC circuit 

```
$ ./model
set grid on              # show the grid locations
add power b1 c1 10v      # add power component
add wire b1 b2           # add wire
add resistor b2 c2 1k    # add resistor
add capacitor c2 d2 1mf  # add capacitor
add wire d2 d1           # add wire
add wire d1 c1           # add wire
ground c1                # set the ground location
set center c1,500,0      # center the circuit
set scale 300            # zoom in
set scope_a voltage,0,10v,b2,c2,RESISTOR   # define scope for voltage across resistor
set scope_b voltage,0,10v,c2,d2,CAPACITOR  # define scope for voltage across capacitor
set grid off             # stop showing the grid locations
run                      # run the simulation
```

