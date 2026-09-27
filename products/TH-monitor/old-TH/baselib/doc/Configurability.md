# Requirements

1. Provide for dynamically changing elements such as broker names
   without having to rebuild.

2. Isolate hardware properties in one place, so it can be changed
   without affecting the rest of the code.

  * Pin layouts
  * Mapping pins to specific purpose: for e.g. LEDs and their use

# Implementation Considerations
Variables which can change dynamically will be stored in EEPROM.
They will have build-time defaults (aka `factory settings`)

> Alternative will be to support a file on SPIFFS, in say JSON format.
> Pros: Extensibility. Cons: Need for more space.

Some variables, on change, can require a device reset, to come into effect.

A few variables, which cannot change dynamically, will be only in build-time.
Example: Hardware pin assignments.
Basis is:

  * functional
  * implementation constraints

In all cases, a default value for the variables may be supplied from platformio.ini build_flags.

> Some of the below items are already implemented in the baselib module for
> ESP32. This will be extended to ESP8266 also.
> The aim is uniformity. Overall better in terms of portability, extensibility
> etc.

# Common Variables

Below are common across all products.

## Build Time Variables
Below variables will continue to be supported as build-time variables.
A change will require a rebuild.

### GSM mode vs WiFi mode
Switching between the two modes will require a re-build.
We will use the USE_GSM flag as in ESP32.

> There is a discussion on being in WiFi mode, with a dynamic switch to GSM in
case WiFi fails. That is out of scope of this document.

### Hardware Properties

#### Pin Layouts

A product/version will have the pin layout baked into the code at the time of building.
All pin layout will be in a single file, say `hardware.h`.

> Overrides to the defaults can be supplied as a build flag from platformio.ini.
In general, this is expected to be used as a temporary basis only.

> Suggested alternative:
> Pre-define multiple sets of layouts in hardware.h. Choose one from them using
> a build-time flag.

#### Pin Mappings

A few aspects, such as below, can also be changed.

  * assigning an LED for a purpose (blue for GSM etc.)

These mappings can be ideally changed dynamically. <-- a `desirable` requirement
only.

Initially, this will be a build flag. <-- implementation constraint

## Dynamic Variables
These should be dynamic variables.
Defaults (aka `factory settings`) will be supplied as build flags when possible.
Mechanisms to change current values and save them are provided as described
elsewhere.

### Connectivity Properties

#### GSM 
APN, userid and password

#### WiFi
(SSID, password) of current AP.
When MultiWiFi, multiple APs may be supported.

#### MQTT
Broker url, port and credentials

# Product Specific Variables

Most of these should be dynamic. Could be static depending on implementation
constraints.
These can be complex, and will usually involve a device reset.

Commonly, the variables will involve:

  * periodicity of activities
  * configuration of items being controlled

# Saving Dynamic Variables

Currently an EEPROM based memory map is used.
There are separate, contiguous areas for:

  * common variables, owned by the baselib
  * product specific variables, used by the product

This mechanism can be used here to cover all requirements.

> EEPROM will also need to store other data, as required by the
> product's business functions (such as SPIFFS store indices, sequence numbers
> etc.)

As requirements change, this will require a memory overwrite, thus resetting
all values to their `factory settings`.

# Changing Dynamic Variables

For further discussion...

## Console Based Changes

Console based commands are presently supported for many common variables.
They have the following constraints:

  * need to write specific code for every common variable
  * not available for all common variables
  * not available for product specific variables

Changes will often result in device reset.

## Platform Driven Changes

Setting connectivity variables (GSM/WiFi and MQTT)  should be done from console
at present. Or, it will require a WiFiManager-equivalent.

Once a connectivity is established, further changes could be initiated as MQTT
messages also.

The underlying implementation logic for executing commands should be the same,
regardless of if driven from console or platform (or WiFiManager).

# Design Specifics

## Changing Variables

### Command Structure

Specifically,

1. `GSM APN userid password`.
Forces reconnect.

2. `WiFi SSID password` to force connect to SSID.
Forces reconnect.

3. `WiFi Add/Remove/Set/List SSID password` to manage multi WiFis
Nothing specific.

4. `MQTT url port userid password` to set broker
Forces reconnect.

5. `Topic dev|test|prod` to set topic stage; actual topic patterns are hard wired to the product.
Forces reset.

### JSON payload structure

Equivalent JSON payload structure will be as follows:

1. 
```json
{
  "payloadId": "SetVar",
  "key": "GSM",
  "APN":, "userid":, "password":
}
```

2. 
```json
{
  "payloadId": "SetVar",
  "key": "WiFi",
   "SSID":, "password":,
}
```

3.
```json
{
  "payloadId": "SetVar",
  "key": "WiFi",
   "action": "Add/Remove/Set/List",
   "SSID":, "password":,
}
```

4.
```json
{
  "payloadId": "SetVar",
  "key": "MQTT",
   "url":, "port":, "userid":, "password":,
}
```

5.
```json
{
  "payloadId": "SetVar",
  "key": "Topic",
   "value": "dev|test|prod"
}
```
