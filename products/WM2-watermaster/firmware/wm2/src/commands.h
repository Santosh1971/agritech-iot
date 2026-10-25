#pragma once
#include <Arduino.h>

// Runs one JSON command ({"cmd":"...", ...}) and returns the JSON reply
// ({"v":1,"id":..,"cmd":..,"ok":true|false,"data":..,"err":..}).
// `via` = mqtt / ws / http / serial; used when the command has no "src".
String dispatchCommand(const String& json, const char* via);
