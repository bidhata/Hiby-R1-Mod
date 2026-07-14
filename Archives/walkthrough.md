# HiBy R1 Granular Tweaks Walkthrough

I have successfully applied all the requested granular system tweaks, diving deep into both the PMIC configuration and the filesystem initialization scripts!

## Changes Made

### 1. File System Mount Optimization ([mount_ubifs.sh](file:///c:/Users/me/Desktop/Projects/HibyR1/17/squashfs-root/usr/bin/mount_ubifs.sh))

I modified the system script responsible for mounting the user data partitions (UBIFS on top of NAND flash).
*   Replaced the `-o sync` flag with `-o noatime`. 
*   This allows the Linux kernel to properly cache reads/writes in RAM instead of forcefully blocking the CPU on every single flash write, dramatically improving system snappiness.
*   This also completely disables the useless "access time" metadata updates, saving the internal flash memory from tens of thousands of microscopic writes over its lifetime, increasing the longevity of the hardware itself.

```diff
-mount -o sync -t ubifs /dev/${ubi_name}_0 $mount_path
+mount -o noatime -t ubifs /dev/${ubi_name}_0 $mount_path
```

### 2. AXP2101 PMIC Settings ([axp2101.sh](file:///c:/Users/me/Desktop/Projects/HibyR1/17/squashfs-root/module_driver/axp2101.sh))

I modified the driver initialization parameters for the `axp2101.ko` kernel module:

*   **Charge Voltage Limit (`charge_voltage_limit=4350`)**: Reduced from 4400 (4.4V) to 4350 (4.35V). This strikes a balance between total capacity (playtime) and chemical degradation (long-term battery health).
*   **Charge Termination Current (`charge_term_current=50`)**: Reduced from 100mA to 50mA. This allows the battery to absorb a final "trickle" charge, resulting in a slightly more accurate 100% capacity before the PMIC cuts off charging.
*   **DCDC Efficiency Mode (`dcdc3_always_pwmmode=0`)**: Disabled forced PWM mode on DCDC3. This allows the voltage regulator to switch to PFM (Pulse Frequency Modulation) mode under low loads, saving power during standby or minimal CPU usage.

```diff
-insmod axp2101.ko i2c_bus_num=0 charge_voltage_limit=4400 charge_term_current=100 dcdc3_always_pwmmode=1 ...
+insmod axp2101.ko i2c_bus_num=0 charge_voltage_limit=4350 charge_term_current=50 dcdc3_always_pwmmode=0 ...
```

## Validation

These changes have been written to the extracted firmware filesystem. You can proceed to repack the squashfs root filesystem, build the firmware image, and flash it to the device!
