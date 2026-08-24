"""Serial client for the test jig's controller (Arduino Nano/ESP8266) --
see docs/testing/TEST_JIG_SPEC.md section 4 for the protocol, and
jig_firmware/jig_controller.ino for the sketch that implements it.

The protocol and interface here are final; whether the calls actually
succeed depends on the physical jig existing and being wired up.
"""
import time

import serial


class JigController:
    def __init__(self, port: str, baud: int = 115200, timeout_s: float = 3.0):
        self.ser = serial.Serial(port, baud, timeout=timeout_s)
        time.sleep(2)  # allow the jig's own MCU to finish its boot/reset
        self.ser.reset_input_buffer()

    def close(self):
        self.ser.close()

    def _command(self, cmd: str) -> str:
        self.ser.write((cmd + "\n").encode("ascii"))
        response = self.ser.readline().decode("ascii", errors="replace").strip()
        return response

    def ping(self) -> bool:
        return self._command("PING") == "PONG"

    def pulse(self, count: int) -> bool:
        """Emit exactly `count` pulses on the flow-sim output. Blocks
        until the jig confirms it's done.
        """
        response = self._command(f"PULSE:{count}")
        return response == f"OK:{count}"

    def relay_state(self) -> bool:
        """True if the jig currently senses the DUT's relay output as
        closed/energized.
        """
        return self._command("RELAY?") == "RELAY:ON"

    def led_state(self, name: str) -> bool | None:
        """True/False if the jig has a photosensor for this LED
        populated, None if unsupported (see spec section 2.3 -- LED
        sensing is optional).
        """
        response = self._command(f"LED:{name}?")
        if response == f"LED:ON":
            return True
        if response == f"LED:OFF":
            return False
        return None


if __name__ == "__main__":
    import sys

    port_arg = sys.argv[1] if len(sys.argv) > 1 else "/dev/cu.usbmodem1101"
    jig = JigController(port_arg)
    print("PING:", "OK" if jig.ping() else "FAIL")
    print("Relay state:", "ON" if jig.relay_state() else "OFF")
    jig.close()
