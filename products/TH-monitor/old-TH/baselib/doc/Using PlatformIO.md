This provides essential details on moving over to PlatformIO from Arduino IDE.

# Introduction
## What is PlatformIO?
  1. PlatformIO is just a different development environment.
  Instead of using Arduino IDE, PlatformIO is used to build the same product.

  2. PlatformIO is implemented as an extension of Visual Code IDE.

  * Advantage is that a lot of Visual Code features are readily available.
    - such as C++ assistance, git integration etc.
  * Disadvantage is that it is a bit more complex than Arduino IDE,
    since the latter is simplified and focused on Arduino framework.
    - PlatformIO supports frameworks other than Arduino.
      + Quite popular. So, most boards that we need are supported in PlatformIO
        too.
    - Visual Code itself supports many other languages and platforms
    > This document will try to narrow the use of PlatformIO for develpment of
    > our products.

## Why Switch to PlatformIO?
  We are switching over to PlatformIO primarily because of the following
  reasons:

  1. Our products provide a variety of user functions (such as pump operations,
     climate/soil moisture control and temperature/humidity sensing).
     All the products require a library of services such as for platform
     connectivity (over MQTT via GSM or WiFi), OTA, RTC, configurability etc.
     PlatformIO (along with its integration of Visual Code and Git) allow for
     easy sharing of these services, and let both the services and products
     evolve separately.

     * As an example, in ESP32 version, THMonitor has 76 source code files.
     Of these, 56 are shared (nearly identically) with other products such as
     SMController.
     > In future, this can be extended for ESP8266 also.

  2. Configurability: PlatformIO has features for build time configurations
     which are very useful for our needs. Besides, the IDE supports specifying
     multiple such configurations and selecting from one as needed.
     These include:
     * Changes to pin configurations
     * Switching between GSM and WiFi
     * Compile time changes such as MQTT packet size limits
     * Debug flags (for GSM etc.)
     * Broker/ports, APN for GSM
  
  3. PlatformIO provides more advanced development and customization features.
  It has a rich set of CLI based operations, allowing for repeatability and
  possibilities for regression and testing.

  4. Visual Code is also used for platform side.
  It has integration with Git.
  The synergies can bring better practices in develoment in future.

# Purpose of This Document
As you will notice below, the project organizations for Arduino and PlatformIO are **not** mutually compatible.
Switching to PlatformIO is not that costly; but, switching back-and-forth, or
supporting both Arduino and PlatformIO versions will be counterproductive and
are to be avoided.

Theefore the purpose of the document is to make the team understand the
importance for the switch, and make them comfortable with the environment.

The expectation is that the switch to the environment will follow quickly enough, for future projects, starting with ESP32 ones.
Switch of other projects to this platform will be if and as needed.

## Key Differences from Arduino Version

### Structure

The contents are organized into separate git repositories:

  * baselib: a library of common services shared across all products.
     The library exists in its on git repository `https://bitbucket.org/sasyasystems/baselib.git`
     and are loaded as a separate library.
     The initial version has 56 files (for 28 basic functions).
  * product-specific: these are the present repository such as
  `https://bitbucket.org/sasyasystems/thmonitor2.git` (for THMonitor) and
  `https://bitbucket.org/sasyasystems/soilmoisture.git` (for SMController) with the following changes
    - common services are removed from these repositories;
    thus reducing them to around 20+ files instead of ~80 as it is at present
      - These are now mapped to `baselib` git repository and loaded from it as a as a `private library`.
    - the files are reorganized into PlatformIO defined folder organization, as
    detailed below.

The baselib contents will be managed as a separate project.
The changes to it will be propaged to all its client projects by using git's configuration management functions.

### Folder Organization

Below shows how the PlatformIO folder structure is adopted at present.

```
|--lib
|  |
|  |--base          --> library of common services shared across all products
|  |  |--doc        --> baselib documentation; other common stuff
|  |     |- Using PlatformIO.md       --> this document
|  |  |--src        --> the 56 common files appear in this folder
|  |     |- *.cpp, *.hpp, *.h
|--src                          --> product specific source code
   |- <product>.cpp         --> one main file containing setup/loop.
   |- *.cpp, *.hpp, *.h
|- platformIO.ini                --> configuration file
|- <product>.code-workspace     --> shortcut to open VS code workspace
|--include                      --> presently unused
|--test                         --> presently unused
|--.vscode                      --> folder used by VS code
|--.gitmodules, .gitignore      --> used by git
|--.pio                         --> internal PlatformIO folder; it generates compiled files here
   |- build
      |- gsm                    --> files generated for `env:gsm`
         |- firmware.bin        --> bin file to be used in `SystemUpgrade`
         |- firmware.elf        --> useful in debugging crash traces
      |- wifi                   --> files generated for each environment `env:wifi` etc.
      ...
   |- libdeps                   --> downloaded project libraries referred to in `platformIO.ini`
      |- gsm                    --> library files download for `env:gsm`
        |- baselib              --> shared base library
        |- TinyGSM              --> 3rd Party library
        |- EspSoftwareSerial    --> 3rd Party library
        ...
      |- wifi                   --> library files downloaded for each environment `env:wifi`
      ...
        
```

### Key Contents

Key Contents (and how it's mapped from the older Arduino structure) are:

#### Common Library - `.pio/libdeps/<env>/baselib`

`.pio/libdeps/<env>/baselib` contains the shared library of common services.
The library exists in its on git repository `https://bitbucket.org/sasyasystems/baselib.git`
and are loaded as a `git submodule` in the project.

All common code for GSM/WiFi & MQTT connection setup & management, RTC, OTA and
utilities for LED indication, timers, EEPROM/SPIFFS storage etc. will be in this library.

#### Product Specific Code - `src`

  * `src` contains the product specific files, one file should have setup/loop

> PlatformIO doesnt impose any constraint on the name of the main file.
By convention, the file is called `main.cpp`.
I followed a convention of `product.cpp`, where `product` is the product name (such as `THMonitor`).

#### Configuration File - `platformIO.ini`
`platformIO.ini` is used to specify platform and board selection, build and
development configurations and process.

Below is an extract of this file for the THMonitor product, for ESP32 on the TTGO-T1 board and defining the libraries to be downloaded and used.
PlatformIO's `library manager` automatically downloads the dependent libraries mentioned in this file. No manual intervention is needed.

```ini
[env]
platform = espressif32
board = ttgo-t1
framework = arduino

lib_deps =
  # Using a library name; see below how using "baselib" simplifies this
  EspSoftwareSerial
  TinyGsm
  PubSubClient
  StreamDebugger
  ArduinoJson
  DHT sensor library for ESPx
```

Below is a representative set of build time configurations specified in the ini file.

```ini
[env:gsm]
build_flags =
  -DMQTT_MAX_PACKET_SIZE=512
  -DUSE_GSM
  ;-DDUMP_AT_COMMANDS
  -DGSM_APN=\"www\"
  -DMQTT_BROKER=\"mqtt.agrisensorsandcontrols.com\"
  -DPLATFORM_STORAGE_MARKER=54
```

> Platformio.ini allows specifying multiple configurations in the same file.
For e.g., we will soon add an `env:WiFi` section for WiFi build with the same project.
Ultimately, We should even have the same project reconfigured for ESP8266, as is the norm.

# Using PlatformIO

## Pre-requisites - BitBucket Credentials and Access
  1. Create and use your own credentials (Santosh Jha or Rupesh Kumar etc.)
  for accessing and modifying each project.

  2. The common libraries require to be included in platformio with below
     credentials.

  * email: sasyasystemspio@gmail.com
  * userid: sasyapio
  * password: platformio

> Nowadays, when cloning, it might ask you to use a generated password.
  This can be done from website, after logging in and navigating to `Your
  Profile & Settings -> Personal Settings -> App Passwords`
  (https://bitbucket.org/account/settings/app-passwords/).
  Save the generated password, since if you forget, the only option is to
  regenerate a new one (which you can do any number of times).

## Pre-requisities - Installation
  1. Download and install VS Code.
    * https://code.visualstudio.com/

  2. Set up PlatformIO extension of VS Code
    * Run VSCode -> Press `Ctrl-Shift-X` to select Extensions.
    * Search and install the below two extensions
      -   PlatformIO IDE (version 1.10.0 as on 26-Dec-2019)
      -   C/C++ (version 0.26.2 as on 26-Dec-2019)

> Visual Code supports a large collection of extensions. Ensure you choose the
> right ones.

## Setup and Opening of Projects

### Clone the git project
Clone the git project as usual.
For e.g., for the THMonitor2,

  * Git clone https://bitbucket.org/sasyasystems/thmonitor2.git to say
    thmonitor2 folder.
    Do the following operations in the thmonitor2 folder.
  * Git Fetch
  * Git Switch to the branch needed

### Open the project in IDE
Open the project by double-clicking on `thmonitor2.code-workspace` file in root folder.
This opens the Visual code with the project as the current workspace.

As described earlier, THMonitor2's board selection and library dependencies are already setup in platformIO.ini settings.
During first time run, PlatformIO should automatically download the dependent libraries from the network as per these settings, without manual intervention.
> So, during initial setup, ensure that you have a good network connectivity.

Now you are all set for development and build.

### Loading and Updating baselib

#### First Time
The git repository and branch to be used are mentioned as a custom library in `platformIO.ini` file.
For e.g.,

```ini
lib_deps =
  https://sasyapio@bitbucket.org/sasyasystems/baselib.git#TH-V0.1
  robtillaart/DHTNEW@^0.4.12
```

The right branch of the library is loaded automatically by `platformio`'s
library manager as part of the build process.

> The libraries used by `baselib` (such as `PubSubClient`, `ArduinoJson`,
> `WiFiManager` etc.) are not included in `platformio.ini` since their inclusion is done by `baselib` itself.

#### Updating

`Update project libraries` is one of the project tasks, as described in the
section `Key IDE Operations` below. This will automatically update 3rd party
libraries as well as `baselib`.

In case `platformIO.ini` is now referring to a different branch of `baselib`,
`Update project libraries` should take care of upgrading the `baselib` to the
new branch too.

> Building the project will also automatically trigger it,
if there are changes made to the dependencies.

## Key IDE Operations
This section describes key activities that are done once a project is open.

### Making Changes

The file explorer is shown in a navigation menu on the left.
A file shown can be edited within IDE and saved.
If a file is modified outside the IDE using another editor such as `EditPlus`,
Visual Code will capture those changes and update its buffers without any manual intervention.

### Build Tasks

All tasks are typically available as PlatformIO project tasks.
Project Task Explorer is located in the VSCode Activity Bar under the branded PlatformIO icon.
The tasks and how to access them are described under https://docs.platformio.org/en/latest/integration/ide/vscode.html#task-explorer.

The tasks include the below items, and a convenient combinations of these,
besides others:

  * Clean: remove all generated files
  * Build: build the project
  * Upload: upload to the connected device
  * Monitor: open console to monitor the device

The tasks may be invoked for each of the `env` sections defined in the `platformIO.ini` file.
For e.g., when two `env` sections are defined as `gsm` and `wifi`,
there are separate context menus for these tasks for each of `gsm` and `wifi`.
Besides, these tasks are defined at the top level, matching the `env` sections
listed under `default-envs` property in the `platformIO.ini` file.

The above tasks, as well as a few common tasks are also available, including:
  * Update project libraries: to update the dependent 3rd party libraries as
    well as `baselib` to the latest version

    > A host of other tasks are available by selecting `View -> Command Palette`
    > from the main menu.

### Troubleshooting

1. `platformio` generates all files in subfolders `.pio/<env>` where `<env>` is
one of `gsm`, `wifi` or custom environment in the `platformIO.ini` file.
In case any issue is faced, the process can be restarted by closing `VS Code`,
deleting `.pio/<env>` folder (and its contents) and then rebuilding the project.

2. `platformio` seems to have limitations in handling branch names with a '/' in them
For e.g. `https://bitbucket.org/sasyasystems/baselib.git#TH/V0.1` might not work correctly.
`https://bitbucket.org/sasyasystems/baselib.git#TH-V0.1` might.

3. `platformio` is unable to prompt for credentials to `baselib` when building a
   project, unless a userid is provided in the git URL.
   We have created a userid `sasyapio` exclusively for this.
   So, the URL gets modified to
`https://sasyapio@bitbucket.org/sasyasystems/baselib.git#TH-V0.1`.

4. `.pio\<env>\build\firmware.bin` contains the binary file to be uploaded to
   the device. This should be used for uploading `SystemUpgrade` operation.

### Other Notes
  * Monitor: shows commands for settings/changing the monitor terminal settings
    - platformIO.ini can be used to set the initial baud rate of the monitor
    - in general, VS code is being able to automatically choose the right COM port
    - You may enter serial input commands to the terminal console opened by the `Monitor` command, as described later.
  * Multiple windows are opened within IDE. Ensure that you are focused on the
  right window.

# References

The above is an essential subset of what is required to use this environment.
For additional details, please refer to:

* https://code.visualstudio.com/ for Visual code
* https://platformio.org/ for PlatformIO
* http://docs.platformio.org/en/latest/ide/vscode.html
* [Training materials and Additional information on ESP32 development using PlatformIO](https://github.coventry.ac.uk/302CEM-1819SEPJAN/TEACHING-MATERIALS/blob/master/archive/02%20Intro%20to%20the%20ESP32.md).

# Appendix

## Current State

### Development-wise...

  * Baselib is working with THMonitor2 and SMController, for GSM and WiFi on
    ESP32.
    - More testing, primarily related to occasional crashes on GSM only.

  * For WiFi, a WiFiManager (replacement for the ESP8266 version which works
  for both ESP8266 and ESP32), should be put in place.
    - Currently, the `w` command from the console can be used to set the access
      points.

### Other Refinements
These are desirable ones, to be done on demand.
  * myTimers.hpp should use Ticker than loop() as it is doing now
  * task.hpp similarly to be based on ESP32 tasks
  * GSM: to check SIM card readiness before anything else: Done in Arduino side
  * Platform: reset in case system remains unconnected for too long

### Future Development
  * Expand to other products, as per priority
    - ESP8266 versions

## Setup New Projects from Arduino

An example, to do it from scratch...

1. Import SMController Arduino project.
  * Choose the board prior to it
    - TTGO T1
  * Choose the folder containing ino file and press import.
    > This typically can take several minutes

2. Alternatively, since #2 above doesnt give control on resultant structure
and the location of the project,
create an empty project at a location of your choice,
and superimpose the Arduino project manually.

