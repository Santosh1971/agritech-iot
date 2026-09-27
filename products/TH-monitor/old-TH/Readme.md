# Overview

## Contents
  1. baselib has the common code used in all controllers.
  2. thmonitor is specific to TH monitor.

## Building
1. open thmonitor workspace on VSCode.
2. run platformio clean.
3. run platformio build.

# General Fixes

## 2 local git repositories
Both `baselib` and `thmonitor` are local git repositories.
* You can keep them in your laptop only. Or, you can later link to github or
bitbucket later.
* History of changes I made are there in the git repository.

## baselib is to thmonitor as `symlink`

This means a couple of things:
* you can make changes to baselib, then rebuild THMonitor. It will rebuild without copying.
  - it was earlier a git reference. So, it used to do a git clone.

> `symlink` style might have some issues with Windows. If so, let me know.

## File name case inconsistencies

I built on linux where filenames are case sensitive.
In some cases, I renamed files. In others, I changed `#include`s.

## Build Flags

I add a `-I.` to the compiler. It could be specific to linux.
See in platformio.ini.

# Specific Fixes

## Library Call Changes

Ticker library call had an issue (version changes I guess). I changed the call.
LED flashing might get affected.
