param show SYS_AUTOSTART
logger status
param set THR_MDL_FAC 0
commander arm -f
sleep 1
commander disarm -f
usleep 200000
actuator_test iterate-motors
