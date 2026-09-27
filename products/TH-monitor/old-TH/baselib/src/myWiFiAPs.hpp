#pragma once

#include <Arduino.h>

#include <functional>
#include <vector>

/**
 * Utility to efficiently store an ordered list of SSID/pwds
  */
class MyWiFiAPs {

  // internal data structures
  using AP = struct { String ssid, pwd; };
  using APs = std::vector<AP>;

  public:
    using OnChange = std::function<void(void)>; // called with updated area on change 
    MyWiFiAPs(char* area, const size_t len, OnChange onChange);

    enum Option {
      Top,  // highest
      Bottom, // lowest,
      Any
    };
    /**
     * add a new entry as per top/bottom/Any option
     *  - any existing entry is deleted
     * could removes old ones if it exceeds the area size if needed
     * @return false if there is no change
     */
    bool add(String nSsid, String nPwd, Option option=Option::Any);

    /**
     * remove specific ssid from the list
     * @return true; false if ssid wasnt in the list
     */
    bool remove(String ssid);

    /**
     * scan through each (ssid, pwd)
     * if any return true, return true
     * else return false
     */
    bool find(std::function<bool (String& ssid, String& pwd)> f);

    /**
     * #access points
     */
    int count();

    /**
     * debug dump
     */
    void dump();

  private:
    /**
     * call cb for each ssid/pwd, till end or cb returns true
     */
    void parse(std::function<bool (char* ssid, char* pwd)> cb);

    /**
     * build area; and call onChange
     */
    void saveChanges();

    /**
     * overwrite area/len by contents of input
     * @ return #rows accommodated; could be less than #rows in input
     */
    int buildArea(APs& input);

  char* area; /* a continguous area where the multi wifi values are saved */
  const size_t len;
  OnChange onChange;

  APs aps; // parsed AP information
};
