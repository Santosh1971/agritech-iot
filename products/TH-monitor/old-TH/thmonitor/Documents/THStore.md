# Storage File & Format
The file is `/data/TH.log`.
Each row is fixed 12 bytes in binary format, storing `{time, temp, humi}`.
The rows are stored as a circular queue in the file.
See `THStore.hpp` for more details.

# Storage Usage
As a default, storage is bypassed, and new rows are directly sent to server.
When not connected to server (or there are pending rows in storage),
new rows are appended to storage.
If that is beyond the limit, older rows are discarded from storage before adding
new rows.
When server connections are restored, then the stored rows are sent to server
one by one and removed from storage as sent.

# StoreLimit Configuration
`StoreLimit` (max #rows in the store) is configured in EEPROM and defaults to 100 rows.

## Changing StoreLimit Using `SetParameter` Command

`SetParameter` command can be used to set parameters such as `TempInterval` and `StoreLimit`.

```
{"payloadId"  : "SetParameter", 
"Token" : "4d350975-4d01-4f8d-82be-14a84edd952c",
"TempInterval" : 60,  // seconds
"StoreLimit" : 100,  // rows
"Date" : "7/1/2019 16:20:51",
"mac" : "5ccf7f3d79d7_110"}
```

When store limit is changed, any rows in the store are discarded.

> A `SetParameter` command can called with either or al of the parameters (such as `TempInterval` and `StoreLimit`. Those parameters present in the payload only are set as part of processing of the command.
