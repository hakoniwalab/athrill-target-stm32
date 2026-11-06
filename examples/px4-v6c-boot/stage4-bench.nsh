param show SYS_AUTOSTART
logger status
param set THR_MDL_FAC 0
commander arm -f
sleep 1
commander disarm -f
usleep 200000
actuator_test -m 1 -v 0.1 -t 5 set
usleep 200000
actuator_test -m 2 -v 0.2 -t 5 set
usleep 200000
actuator_test -m 3 -v 0.3 -t 5 set
usleep 200000
actuator_test -m 4 -v 0.4 -t 5 set
usleep 200000
