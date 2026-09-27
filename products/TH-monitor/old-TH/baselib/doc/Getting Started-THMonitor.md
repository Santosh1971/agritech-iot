
0. Download and Install Visual Code.
Install its PlatformIO IDE and C++ extensions.
See [Using PlatformIO][1], under section titled "Pre-requisites" for details.

1. Git Clone THMonitor2 to a new folder, say thmonitor2.
  This is the working folder.
  **Use below options for cloning**
  * URL: `https://bitbucket.org/asagrisystems/thmonitor2.git`
  * Folder: `-your folder-`
  * Recursive: `Y`   <--- needed to check out the submodules also
  * Branch: `Test32/V0.1`

  > If you already have thmonitor2 (in Arduino mode), you can use that after you safely commit any changes.  In that case, you need to `Fetch-Switch to Test32/V0.1-Pull` and then, do `Git Submodule Update`.

  > Test32 to indicate ESP32.
  In future, the folder will have both ESP8266 and ESP32 versions of the code.

2. Switch to `esp32/base` branch of the common services as follows:
  * Select `thmonitor2\lib\base` folder.  This has the common services.
  * `Git Switch` to `esp32/base` from that folder.

3. Open the project in PlatformIO IDE by double clicking on
   `thmonitor2.code-workspace` file in the `thmonitor2` folder.
  > This should open VS Code and the project.

  Do the below operations in the IDE.

4. `Ctrl-Shift-P` shows a menu of PlatformIO options. Select Build.
  > Ideally, all required libraries are automatically downloaded
  by PlatformIO as per project settings.

5. Similarly `Ctrl-Shift-P` Upload will upload the file into the device.
And, `Ctrl-Shift-P` Monitor can be used to access the Serial console.

  > All outputs are shown as subwindows of the IDE.
  > Takreem might be able to help on VS Code side.

You are all set. Please read [Using PlatformIO][1] for further details.

[1]: ./Using%20PlatformIO.md
