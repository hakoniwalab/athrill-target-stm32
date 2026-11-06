# Pixhawk 6C sensor calibration IDs. prepare.sh appends this board overlay to
# the shared x500 tune in docs/px4/x500-hakoniwa-params.sh.
param set CAL_ACC0_ID 4259850
param set CAL_ACC1_ID 2490378
param set CAL_GYRO0_ID 4325386
param set CAL_GYRO1_ID 2490378
param set CAL_MAG0_ID 396321
param set CAL_MAG0_PRIO 50
param set SENS_BOARD_X_OFF 0.000001
param save
