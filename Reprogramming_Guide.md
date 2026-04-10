Markdown
# Firmware Deployment Guide: XIAO nRF52840 Sense

This guide outlines the Standard Operating Procedure (SOP) for flashing firmware to the Seeed Studio XIAO nRF52840 Sense using the nRF Connect SDK (Zephyr) environment. It specifically addresses bypassing J-Link requirements using the **1200 baud bootloader hack**.

## 1. Prerequisites

### Global Tools
To avoid Python DLL conflicts with the nRF Connect toolchain, ensure `adafruit-nrfutil` is installed in your **global** Python environment (outside the `ncs` folder).

1. Open a standard Windows PowerShell (not the VS Code nRF terminal).
2. Install the utility:
   ```powershell
   pip install adafruit-nrfutil
Verify installation:

PowerShell
adafruit-nrfutil version
# Should return: adafruit-nrfutil version 0.5.3.post16
2. The Development Workflow
Step 1: Build & Package
After modifying your main.c or app.overlay in VS Code:

Run the Build task in the nRF Connect extension.

Open your standard PowerShell window and navigate to the project root.

Generate the DFU .zip package (required for serial flashing):

PowerShell
adafruit-nrfutil dfu genpkg --dev-type 0x0052 --application build/blinky/zephyr/zephyr.hex build/blinky/zephyr/zephyr.zip
Step 2: The "1200 Baud Hack" (Automated Flash)
To force the device into DFU mode without physically touching the reset button, use the --touch 1200 flag.

Note: Triggering the bootloader often causes Windows to reassign the COM port (e.g., COM6 shifts to COM5).

Run the automated flash command:

PowerShell
adafruit-nrfutil dfu serial -pkg build/blinky/zephyr/zephyr.zip -p COM[X] -b 115200 --touch 1200
If the command fails with Port Not Open, run nrfutil device list to find the new port and repeat the command without the --touch flag on the new port.

Step 3: Manual Recovery (Fail-safe)
If the software is unresponsive or the 1200 baud hack fails:

Double-click the tiny Reset button on the XIAO board quickly.

The LED will begin "breathing" (pulsing blue/green).

Identify the new COM port: nrfutil device list.

Flash directly:

PowerShell
adafruit-nrfutil dfu serial -pkg build/blinky/zephyr/zephyr.zip -p COM[New] -b 115200
3. Hardware Reference (AI Sensor/ADS1115)
For the 14-pad (7 pins per side) layout, use the following mapping for I2C sensor integration:

Sensor Pin	XIAO Location	Label	nRF52840 Pin
VDD	Right side, 3rd down	3V3	Power
GND	Right side, 2nd down	GND	Ground
SDA	Left side, 5th down	D4	P0.04
SCL	Left side, 6th down	D5	P0.05
I2C Address Configuration
Ensure the ADS1115 ADDR pin is tied to GND to set the I2C address to 0x48.

4. Troubleshooting
DLL Conflict: Ensure you are NOT running the flash command inside the VS Code terminal provided by the nRF extension. Use a clean, external PowerShell.

Timeout Error: Ensure the device is actually pulsing its LED. If not, the bootloader is not active.

Zip Not Found: Ensure you ran the genpkg command after every successful build.


---

### One final Architect's Tip:
When you upload this to GitHub, I recommend adding a `.vscode/tasks.json` file to the repo as well. That way, you can eventually map this whole "Gen Package + Touch + Flash" sequence to a single keyboard shortcut. 
